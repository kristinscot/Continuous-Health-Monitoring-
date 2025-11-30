package com.example.biobanddisplay

import android.Manifest
import android.annotation.SuppressLint
import android.bluetooth.*
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Color
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.widget.TextView
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import com.chaquo.python.Python
import com.chaquo.python.android.AndroidPlatform
import com.github.mikephil.charting.charts.LineChart
import com.github.mikephil.charting.components.Description
import com.github.mikephil.charting.data.Entry
import com.github.mikephil.charting.data.LineData
import com.github.mikephil.charting.data.LineDataSet
import java.util.UUID

class MainActivity : AppCompatActivity() {

    private val TAG = "BLE_CONNECT_APP"

    // Bluetooth setup
    private val bluetoothManager: BluetoothManager by lazy {
        getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
    }
    private val bluetoothAdapter: BluetoothAdapter by lazy {
        bluetoothManager.adapter
    }

    // Scanning variables
    private var scanning = false
    private val handler = Handler(Looper.getMainLooper())
    private val SCAN_PERIOD: Long = 10000 // 10 seconds

    // GATT connection
    private var bluetoothGatt: BluetoothGatt? = null
    private val DEVICE_NAME = "CEBB7EAB1A91" // *UI for the nRF52840*

    // --- UI and Chart Variables ---
    private lateinit var lineChart: LineChart
    private lateinit var statusText: TextView

    // Request permissions launcher for modern Android
    private val requestPermissionLauncher =
        registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { permissions ->
            val allGranted = permissions.all { it.value }
            if (allGranted) {
                Log.d(TAG, "Bluetooth permissions granted.")
                checkAndEnableBluetooth()
            } else {
                Toast.makeText(this, "Permissions are required for BLE functionality.", Toast.LENGTH_LONG).show()
            }
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        // --- Initialize UI and Chart ---
        statusText = findViewById(R.id.status_text)
        lineChart = findViewById(R.id.line_chart)
        setupChart()

        // --- Initialize Python ---
        if (!Python.isStarted()) {
            Python.start(AndroidPlatform(this))
        }

        // --- Start BLE process ---
        if (!packageManager.hasSystemFeature(PackageManager.FEATURE_BLUETOOTH_LE)) {
            Toast.makeText(this, "BLE not supported on this device.", Toast.LENGTH_LONG).show()
            finish()
            return
        }

        checkPermissionsAndStartScan()
    }

    // --- Permission Handling ---
    private fun checkPermissionsAndStartScan() {
        val permissions = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            arrayOf(
                Manifest.permission.BLUETOOTH_SCAN,
                Manifest.permission.BLUETOOTH_CONNECT,
                Manifest.permission.ACCESS_FINE_LOCATION
            )
        } else {
            arrayOf(
                Manifest.permission.ACCESS_FINE_LOCATION
            )
        }

        if (permissions.all { ActivityCompat.checkSelfPermission(this, it) == PackageManager.PERMISSION_GRANTED }) {
            checkAndEnableBluetooth()
        } else {
            requestPermissionLauncher.launch(permissions)
        }
    }

    private fun checkAndEnableBluetooth() {
        if (!bluetoothAdapter.isEnabled) {
            val enableBtIntent = Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE)
            if (ActivityCompat.checkSelfPermission(
                    this,
                    Manifest.permission.BLUETOOTH_CONNECT
                ) != PackageManager.PERMISSION_GRANTED && Build.VERSION.SDK_INT >= Build.VERSION_CODES.S
            ) {
                // This case is handled by checkPermissionsAndStartScan, but as a fallback.
                Toast.makeText(this, "Bluetooth Connect permission not granted.", Toast.LENGTH_SHORT).show()
                return
            }
            startActivity(enableBtIntent) // User will be prompted to enable Bluetooth
            // We can't scan immediately, as we need to wait for the user's choice.
            // A more robust solution would use a ActivityResultLauncher for this intent as well.
        } else {
            startBleScan()
        }
    }

    // --- BLE Scanning ---
    @SuppressLint("MissingPermission")
    private fun startBleScan() {
        if (scanning) return

        scanning = true
        Log.i(TAG, "Starting BLE scan...")
        handler.post {
            statusText.text = "Scanning for $DEVICE_NAME..."
            Toast.makeText(this, "Scanning for $DEVICE_NAME...", Toast.LENGTH_SHORT).show()
        }

        handler.postDelayed({
            if (scanning) stopBleScan()
        }, SCAN_PERIOD)

        bluetoothAdapter.bluetoothLeScanner.startScan(leScanCallback)
    }

    @SuppressLint("MissingPermission")
    private fun stopBleScan() {
        if (!scanning) return
        scanning = false
        Log.i(TAG, "Stopping BLE scan.")
        bluetoothAdapter.bluetoothLeScanner.stopScan(leScanCallback)
    }

    // Callback for scan results
    private val leScanCallback: ScanCallback = object : ScanCallback() {
        @SuppressLint("MissingPermission")
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            super.onScanResult(callbackType, result)
            if (result.device.name == DEVICE_NAME) {
                Log.i(TAG, "Found device: ${result.device.name} @ ${result.device.address}")
                stopBleScan()
                connectToDevice(result.device)
            }
        }

        override fun onScanFailed(errorCode: Int) {
            super.onScanFailed(errorCode)
            Log.e(TAG, "BLE Scan failed with code $errorCode")
            scanning = false
        }
    }

    // --- Connection and GATT ---
    @SuppressLint("MissingPermission")
    private fun connectToDevice(device: BluetoothDevice) {
        Log.i(TAG, "Connecting to ${device.address}...")
        handler.post { statusText.text = "Connecting..." }
        bluetoothGatt = device.connectGatt(this, false, gattCallback)
    }

    private val gattCallback = object : BluetoothGattCallback() {
        @SuppressLint("MissingPermission")
        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            val deviceAddress = gatt.device.address

            if (status == BluetoothGatt.GATT_SUCCESS) {
                if (newState == BluetoothProfile.STATE_CONNECTED) {
                    Log.i(TAG, "Successfully connected to $deviceAddress")
                    handler.post {
                        statusText.text = "Connected! Discovering services..."
                        gatt.discoverServices()
                    }
                } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                    Log.w(TAG, "Successfully disconnected from $deviceAddress")
                    gatt.close()
                    bluetoothGatt = null
                    handler.post {
                        statusText.text = "Disconnected. Scanning again..."
                        Toast.makeText(this@MainActivity, "Disconnected.", Toast.LENGTH_SHORT).show()
                        startBleScan() // Optional: Automatically restart scanning
                    }
                }
            } else {
                Log.w(TAG, "Error $status encountered for $deviceAddress! Disconnecting.")
                gatt.close()
                bluetoothGatt = null
                handler.post {
                    statusText.text = "Connection failed. Retrying..."
                    Toast.makeText(this@MainActivity, "Connection failed: $status", Toast.LENGTH_LONG).show()
                    startBleScan() // Optional: Automatically restart scanning
                }
            }
        }

        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                Log.i(TAG, "Services discovered: ${gatt.services.size}")
                handler.post { statusText.text = "Device connected." }

                // TODO: Find your specific service and characteristic and enable notifications
                // For example, if using Nordic UART Service (NUS)
                val service = gatt.getService(SERVICE_UUID)
                service?.getCharacteristic(WRITE_CHARACTERISTIC_UUID)?.let {
                    // This is usually the RX characteristic from the device's perspective
                    enableNotifications(gatt, it)
                }

            } else {
                Log.w(TAG, "onServicesDiscovered received error: $status")
            }
        }

        override fun onCharacteristicChanged(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic) {
            val rawDataString = characteristic.value.toString(Charsets.UTF_8)
            Log.i(TAG, "Received notification: $rawDataString from ${characteristic.uuid}")

            try {
                val python = Python.getInstance()
                val analyzerModule = python.getModule("analyzer")
                val processedDataPyObject = analyzerModule.callAttr("process_ble_data", rawDataString)
                val processedData: List<Float> = processedDataPyObject.toJava(List::class.java) as List<Float>

                Log.i(TAG, "Processed data from Python: $processedData")

                handler.post {
                    for (point in processedData) {
                        addChartEntry(point)
                    }
                }
            } catch (e: Exception) {
                Log.e(TAG, "Error calling Python or updating chart", e)
            }
        }

        override fun onCharacteristicWrite(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic, status: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                Log.i(TAG, "Characteristic ${characteristic.uuid} written successfully.")
            } else {
                Log.e(TAG, "Characteristic write failed: $status")
            }
        }
    }

    // --- Charting Functions ---
    private fun setupChart() {
        lineChart.description = Description().apply { text = "Sensor Data" }
        lineChart.setNoDataText("Waiting for data...")
        lineChart.data = LineData()
        lineChart.invalidate() // refresh
    }

    private fun addChartEntry(value: Float) {
        val data = lineChart.data ?: return

        var set = data.getDataSetByIndex(0)
        if (set == null) {
            set = createDataSet()
            data.addDataSet(set)
        }

        data.addEntry(Entry(set.entryCount.toFloat(), value), 0)
        data.notifyDataChanged()
        lineChart.notifyDataSetChanged()
        lineChart.setVisibleXRangeMaximum(100f) // Show 100 points at a time
        lineChart.moveViewToX(data.entryCount.toFloat())
    }

    private fun createDataSet(): LineDataSet {
        return LineDataSet(null, "Real-time Data").apply {
            axisDependency = com.github.mikephil.charting.components.YAxis.AxisDependency.LEFT
            color = Color.CYAN
            setCircleColor(Color.WHITE)
            lineWidth = 2f
            circleRadius = 3f
            setDrawValues(false) // Don't draw the numeric value on each point
            mode = LineDataSet.Mode.CUBIC_BEZIER
        }
    }

    // --- Helper Functions for BLE ---

    @SuppressLint("MissingPermission")
    fun enableNotifications(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic) {
        val cccdUuid = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")
        val descriptor = characteristic.getDescriptor(cccdUuid)
        if (descriptor == null) {
            Log.e(TAG, "Could not find CCCD descriptor for characteristic: ${characteristic.uuid}")
            return
        }

        gatt.setCharacteristicNotification(characteristic, true)

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            gatt.writeDescriptor(descriptor, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)
        } else {
            descriptor.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
            gatt.writeDescriptor(descriptor)
        }
        Log.i(TAG, "Enabled notifications for ${characteristic.uuid}")
    }

    @SuppressLint("MissingPermission")
    fun writeCharacteristic(gatt: BluetoothGatt, data: ByteArray) {
        if (!hasPermissions(Manifest.permission.BLUETOOTH_CONNECT)) {
            Log.e(TAG, "BLUETOOTH_CONNECT permission not granted for writing.")
            return
        }

        val service = gatt.getService(SERVICE_UUID)
        val characteristic = service?.getCharacteristic(WRITE_CHARACTERISTIC_UUID)
        if (characteristic == null) {
            Log.e(TAG, "Characteristic not found for writing")
            return
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            gatt.writeCharacteristic(characteristic, data, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT)
        } else {
            characteristic.value = data
            gatt.writeCharacteristic(characteristic)
        }
    }

    @SuppressLint("MissingPermission")
    override fun onDestroy() {
        super.onDestroy()
        bluetoothGatt?.close()
        bluetoothGatt = null
    }

    private fun hasPermissions(vararg permissions: String): Boolean = permissions.all {
        ActivityCompat.checkSelfPermission(this, it) == PackageManager.PERMISSION_GRANTED
    }

    // --- GATT UUID Constants (Update to match your nRF52840 firmware!) ---
    private val SERVICE_UUID = UUID.fromString("6E400001-B5A3-F393-E0A9-E50E24DCCA9E")
    // This is the characteristic the APP WRITES TO.
    private val WRITE_CHARACTERISTIC_UUID = UUID.fromString("6E400002-B5A3-F393-E0A9-E50E24DCCA9E")
    // This is the characteristic the APP RECEIVES NOTIFICATIONS FROM.
    private val NOTIFY_CHARACTERISTIC_UUID = UUID.fromString("6E400003-B5A3-F393-E0A9-E50E24DCCA9E")
}
