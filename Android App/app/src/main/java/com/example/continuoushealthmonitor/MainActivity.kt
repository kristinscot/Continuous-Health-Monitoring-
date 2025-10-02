package com.example.continuoushealthmonitor

import android.Manifest
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothManager
import android.bluetooth.le.BluetoothLeScanner
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanResult
import android.content.ActivityNotFoundException
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.example.continuoushealthmonitor.ui.theme.ContinuousHealthMonitorTheme
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothProfile
import android.util.Log


data class UiDevice(
    val name: String,
    val address: String,
    val rssi: Int
)

class MainActivity : ComponentActivity() {

    private val bluetoothAdapter: BluetoothAdapter? by lazy {
        (getSystemService(BLUETOOTH_SERVICE) as BluetoothManager).adapter
    }
    private val leScanner: BluetoothLeScanner? get() = bluetoothAdapter?.bluetoothLeScanner

    // runtime state backing fields (observed by Compose)
    private val devices = mutableStateListOf<UiDevice>()
    private var isScanning by mutableStateOf(false)
    private var showGoToSettings by mutableStateOf(false)
    private var currentGatt: BluetoothGatt? = null
    private var connectionStatus by mutableStateOf("Idle")
    private val TAG = "BLE"

    // ---- Permissions (Android 12+) ----
    private val blePerms31Plus = arrayOf(
        Manifest.permission.BLUETOOTH_SCAN,
        Manifest.permission.BLUETOOTH_CONNECT
        // Manifest.permission.BLUETOOTH_ADVERTISE // if you advertise
    )

    private val requestPermsLauncher =
        registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { results ->
            val allGranted = results.values.all { it }
            if (!allGranted) {
                showGoToSettings = true
            } else {
                ensureBluetoothEnabledOrProceed()
            }
        }

    private val requestEnableBtLauncher =
        registerForActivityResult(ActivityResultContracts.StartActivityForResult()) {
            if (isBluetoothEnabled()) {
                // Ready to scan
            }
        }

    // ---- Scan callback ----
    private val scanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            val d = result.device ?: return
            val name = d.name ?: result.scanRecord?.deviceName ?: "Unknown"
            val address = d.address ?: return
            val rssi = result.rssi

            // Dedup by MAC, update RSSI/name
            val idx = devices.indexOfFirst { it.address == address }
            if (idx >= 0) {
                val old = devices[idx]
                devices[idx] = old.copy(name = if (name.isNotBlank()) name else old.name, rssi = rssi)
            } else {
                devices.add(UiDevice(name, address, rssi))
            }
        }

        override fun onBatchScanResults(results: MutableList<ScanResult>) {
            results.forEach { onScanResult(0, it) } // 0 is fine here
        }


        override fun onScanFailed(errorCode: Int) {
            // You could surface this to the UI if you want
            stopBleScan()
        }
    }
    private val gattCallback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(gatt: BluetoothGatt, status: Int, newState: Int) {
            Log.d(TAG, "onConnectionStateChange: status=$status, state=$newState")
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                connectionStatus = "Connected → discovering services…"
                if (hasBlePerms()) gatt.discoverServices() else {
                    connectionStatus = "Missing BLUETOOTH_CONNECT"
                }
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                connectionStatus = "Disconnected"
                currentGatt?.close()
                currentGatt = null
            }
        }

        override fun onServicesDiscovered(gatt: BluetoothGatt, status: Int) {
            val services = gatt.services
            connectionStatus = "Services discovered: ${services?.size ?: 0}"

            services?.forEach { service ->
                Log.d(TAG, "Service: ${service.uuid}")
                service.characteristics.forEach { char ->
                    Log.d(TAG, "  Characteristic: ${char.uuid}, props=${char.properties}")
                }
            }
        }
    }
    private fun connect(address: String) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S && !hasBlePerms()) {
            connectionStatus = "Requesting permissions…"
            ensureBleReady()
            return
        }
        if (!isBluetoothEnabled()) {
            connectionStatus = "Enabling Bluetooth…"
            ensureBluetoothEnabledOrProceed()
            return
        }

        val device = bluetoothAdapter?.getRemoteDevice(address)
        if (device == null) {
            connectionStatus = "Device not found"
            return
        }

        connectionStatus = "Connecting to $address…"
        currentGatt?.close()
        // Use 3-arg overload (simpler), Android will pick LE
        currentGatt = device.connectGatt(this, /*autoConnect*/ false, gattCallback)
    }

    private fun disconnect() {
        currentGatt?.let {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S && !hasBlePerms()) {
                // If user revoked permission mid-session, just best-effort close
                it.close()
            } else {
                it.disconnect()
                it.close()
            }
        }
        currentGatt = null
        connectionStatus = "Disconnected"
    }

    private fun hasBlePerms(): Boolean {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            checkSelfPermission(android.Manifest.permission.BLUETOOTH_SCAN) ==
                    android.content.pm.PackageManager.PERMISSION_GRANTED &&
                    checkSelfPermission(android.Manifest.permission.BLUETOOTH_CONNECT) ==
                    android.content.pm.PackageManager.PERMISSION_GRANTED
        } else true
    }
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        Log.v("BLE", "App started (verbose)")
        Log.d("BLE", "App started (debug)")
        Log.i("BLE", "App started (info)")
        Log.w("BLE", "App started (warn)")
        Log.e("BLE", "App started (error)")

        setContent {
            ContinuousHealthMonitorTheme {
                Scaffold { padding ->
                    ScannerScreen(
                        devices = devices,
                        isScanning = isScanning,
                        connectionStatus = connectionStatus,
                        onStart = { startBleScan() },
                        onStop = { stopBleScan() },
                        onClick = { d ->
                            Log.d("BLE", "Clicked ${d.name} ${d.address}")   // safe here
                            stopBleScan()
                            connect(d.address)
                        },
                        modifier = Modifier.padding(padding)
                    )


                    if (showGoToSettings) {
                        DeniedDialog(
                            onOpenSettings = {
                                openAppSettings()
                                showGoToSettings = false
                            },
                            onDismiss = { showGoToSettings = false }
                        )
                    }
                }

            }
        }
    }


    override fun onStart() {
        super.onStart()
        // Ask for permissions early so "Start Scan" works immediately
        ensureBleReady()
    }

    override fun onStop() {
        super.onStop()
        stopBleScan()
        currentGatt?.disconnect()
        currentGatt?.close()
        currentGatt = null
    }

    // ---------- Permission + Bluetooth enable flow ----------

    private fun ensureBleReady() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            val missing = blePerms31Plus.filter {
                checkSelfPermission(it) != android.content.pm.PackageManager.PERMISSION_GRANTED
            }
            if (missing.isNotEmpty()) {
                requestPermsLauncher.launch(missing.toTypedArray())
                return
            }
        }
        ensureBluetoothEnabledOrProceed()
    }

    private fun ensureBluetoothEnabledOrProceed() {
        if (!isBluetoothEnabled()) {
            val intent = Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE)
            requestEnableBtLauncher.launch(intent)
        }
    }

    private fun isBluetoothEnabled(): Boolean = bluetoothAdapter?.isEnabled == true

    // ---------- Scan control ----------

    private fun startBleScan() {
        if (isScanning) return
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            // re-check perms just in case
            val missing = blePerms31Plus.any {
                checkSelfPermission(it) != android.content.pm.PackageManager.PERMISSION_GRANTED
            }
            if (missing) {
                ensureBleReady()
                return
            }
        }
        if (!isBluetoothEnabled()) {
            ensureBluetoothEnabledOrProceed()
            return
        }

        devices.clear()
        // Example: scan with no filters to discover everything.
        // You can add ScanFilter for a specific service UUID if desired.
        leScanner?.startScan(scanCallback)
        isScanning = true
    }

    private fun stopBleScan() {
        if (!isScanning) return
        leScanner?.stopScan(scanCallback)
        isScanning = false
    }

    private fun openAppSettings() {
        try {
            val uri = Uri.fromParts("package", packageName, null)
            startActivity(Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS, uri))
        } catch (_: ActivityNotFoundException) {
            startActivity(Intent(Settings.ACTION_MANAGE_APPLICATIONS_SETTINGS))
        }
    }
}

// ---------- UI ----------

@Composable
private fun DeviceList(
    devices: List<UiDevice>,
    onClick: (UiDevice) -> Unit,
    modifier: Modifier
) {
    if (devices.isEmpty()) {
        Text("No devices found yet.", style = MaterialTheme.typography.bodyMedium)
        return
    }
    LazyColumn(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        items(devices) { d ->
            ElevatedCard(
                modifier = Modifier
                    .fillMaxWidth()
                    .clickable { onClick(d) }
            ) {
                Column(modifier = Modifier.padding(12.dp)) {
                    Text(
                        d.name.ifBlank { "Unknown" },
                        style = MaterialTheme.typography.titleMedium,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )
                    Text(d.address, style = MaterialTheme.typography.bodySmall)
                    Text("RSSI: ${d.rssi} dBm", style = MaterialTheme.typography.bodySmall)
                }
            }
        }
    }
}

@Composable
private fun DeniedDialog(
    onOpenSettings: () -> Unit,
    onDismiss: () -> Unit
) {
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Permissions needed") },
        text = { Text("Nearby devices permission is required to scan and connect.") },
        confirmButton = { TextButton(onClick = onOpenSettings) { Text("Open Settings") } },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Cancel") } }
    )
}
@Composable
fun ScannerScreen(
    devices: List<UiDevice>,
    isScanning: Boolean,
    connectionStatus: String,
    onStart: () -> Unit,
    onStop: () -> Unit,
    onClick: (UiDevice) -> Unit,
    modifier: Modifier = Modifier
) {
    Column(
        modifier = modifier
            .fillMaxSize()
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        Text("BLE Scanner", style = MaterialTheme.typography.headlineSmall)

        Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            Button(onClick = onStart, enabled = !isScanning) { Text("Start Scan") }
            OutlinedButton(onClick = onStop, enabled = isScanning) { Text("Stop Scan") }
        }

        // 🔹 Message pinned at the top
        Surface(tonalElevation = 2.dp, shape = MaterialTheme.shapes.medium) {
            Text(
                connectionStatus,
                modifier = Modifier.padding(horizontal = 12.dp, vertical = 8.dp),
                style = MaterialTheme.typography.bodyMedium
            )
        }

        Divider()

        // 🔹 Only the list scrolls (sorted by RSSI)
        DeviceList(
            devices = devices.sortedByDescending { it.rssi },
            onClick = onClick,
            modifier = Modifier.weight(1f)
        )
    }
}
