package com.example.display_analog_read

import android.annotation.SuppressLint
import android.bluetooth.*
import android.bluetooth.le.*
import android.content.Context
import android.os.Build
import android.os.ParcelUuid
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import java.util.UUID

data class BleDevice(
    val name: String,
    val address: String,
    val rssi: Int
)

sealed class BleState {
    data object Idle : BleState()
    data object Scanning : BleState()
    data class ScanError(val reason: String) : BleState()
    data class Connecting(val address: String) : BleState()
    data class Connected(val address: String) : BleState()
    data class Disconnected(val address: String) : BleState()
    data class Error(val message: String) : BleState()
}

class BleClient(private val context: Context) {

    companion object {
        const val TARGET_NAME = "ADC_DONGLE"

        val SERVICE_UUID: UUID = UUID.fromString("fedcba98-7654-3210-fedc-ba9876543210")
        val CHAR_UUID: UUID = UUID.fromString("fedcba98-7654-3210-fedc-ba9876543211")
        val CCCD_UUID: UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
    }

    private val bluetoothManager =
        context.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
    private val adapter: BluetoothAdapter? = bluetoothManager.adapter
    private val scanner: BluetoothLeScanner? get() = adapter?.bluetoothLeScanner

    private var scanning = false
    private var gatt: BluetoothGatt? = null

    private val _state = MutableStateFlow<BleState>(BleState.Idle)
    val state: StateFlow<BleState> = _state

    private val _devices = MutableStateFlow<List<BleDevice>>(emptyList())
    val devices: StateFlow<List<BleDevice>> = _devices

    private val _latestMv = MutableStateFlow<Int?>(null)
    val latestMv: StateFlow<Int?> = _latestMv

    private val scanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            val dev = result.device
            val name = result.scanRecord?.deviceName ?: dev.name ?: "Unknown"
            if (name != TARGET_NAME) return

            val entry = BleDevice(name = name, address = dev.address, rssi = result.rssi)
            val current = _devices.value.toMutableList()

            val idx = current.indexOfFirst { it.address == entry.address }
            if (idx >= 0) current[idx] = entry else current.add(entry)

            // Sort strongest first
            current.sortByDescending { it.rssi }
            _devices.value = current
        }

        override fun onScanFailed(errorCode: Int) {
            scanning = false
            _state.value = BleState.ScanError("Scan failed: $errorCode")
        }
    }

    @SuppressLint("MissingPermission")
    fun startScan() {
        if (scanning) return

        val sc = scanner ?: run {
            _state.value = BleState.Error("BluetoothLeScanner unavailable")
            return
        }

        _devices.value = emptyList()
        _state.value = BleState.Scanning

        val filters = listOf(
            // Filter by advertised service UUID (works if you include it in advertising);
            // safe to keep even if not present—name filter below is what catches it.
            ScanFilter.Builder()
                .setDeviceName(TARGET_NAME)
                .build()
        )

        val settings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
            .build()

        scanning = true
        sc.startScan(filters, settings, scanCallback)
    }

    @SuppressLint("MissingPermission")
    fun stopScan() {
        if (!scanning) return
        scanner?.stopScan(scanCallback)
        scanning = false
        _state.value = BleState.Idle
    }

    @SuppressLint("MissingPermission")
    fun connect(address: String) {
        stopScan()

        val dev = adapter?.getRemoteDevice(address) ?: run {
            _state.value = BleState.Error("Device not found for address $address")
            return
        }

        _state.value = BleState.Connecting(address)
        _latestMv.value = null

        gatt?.close()
        gatt = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            dev.connectGatt(context, false, gattCallback, BluetoothDevice.TRANSPORT_LE)
        } else {
            dev.connectGatt(context, false, gattCallback)
        }
    }

    @SuppressLint("MissingPermission")
    fun disconnect() {
        val current = gatt
        if (current != null) {
            current.disconnect()
            current.close()
        }
        gatt = null
        _state.value = BleState.Idle
    }

    private val gattCallback = object : BluetoothGattCallback() {

        @SuppressLint("MissingPermission")
        override fun onConnectionStateChange(g: BluetoothGatt, status: Int, newState: Int) {
            val addr = g.device.address

            if (status != BluetoothGatt.GATT_SUCCESS) {
                _state.value = BleState.Error("GATT error status=$status")
                g.close()
                return
            }

            when (newState) {
                BluetoothProfile.STATE_CONNECTED -> {
                    _state.value = BleState.Connected(addr)
                    g.discoverServices()
                }
                BluetoothProfile.STATE_DISCONNECTED -> {
                    _state.value = BleState.Disconnected(addr)
                    g.close()
                }
            }
        }

        @SuppressLint("MissingPermission")
        override fun onServicesDiscovered(g: BluetoothGatt, status: Int) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                _state.value = BleState.Error("Service discovery failed: $status")
                return
            }

            val svc = g.getService(SERVICE_UUID)
            if (svc == null) {
                _state.value = BleState.Error("Service not found: $SERVICE_UUID")
                return
            }

            val ch = svc.getCharacteristic(CHAR_UUID)
            if (ch == null) {
                _state.value = BleState.Error("Characteristic not found: $CHAR_UUID")
                return
            }

            // Enable notifications
            val ok = g.setCharacteristicNotification(ch, true)
            if (!ok) {
                _state.value = BleState.Error("setCharacteristicNotification returned false")
                return
            }

            val cccd = ch.getDescriptor(CCCD_UUID)
            if (cccd == null) {
                _state.value = BleState.Error("CCCD descriptor not found (0x2902)")
                return
            }

            cccd.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
            val wrote = g.writeDescriptor(cccd)
            if (!wrote) {
                _state.value = BleState.Error("writeDescriptor(CCCD) failed to start")
                return
            }
        }

        // For Android 13+ (API 33): this overload may be used
        override fun onCharacteristicChanged(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            value: ByteArray
        ) {
            if (characteristic.uuid != CHAR_UUID) return
            decodeAndPublish(value)
        }

        // Back-compat: some devices still call deprecated version
        @Deprecated("Deprecated in Android 13")
        override fun onCharacteristicChanged(
            gatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic
        ) {
            if (characteristic.uuid != CHAR_UUID) return
            decodeAndPublish(characteristic.value ?: return)
        }

        private fun decodeAndPublish(bytes: ByteArray) {
            // Zephyr sends int16_t in little-endian (2 bytes)
            if (bytes.size < 2) return
            val lo = bytes[0].toInt() and 0xFF
            val hi = bytes[1].toInt() and 0xFF
            val u16 = lo or (hi shl 8)

            // Convert to signed 16-bit
            val s16 = if (u16 and 0x8000 != 0) u16 - 0x10000 else u16
            _latestMv.value = s16
        }
    }
}
