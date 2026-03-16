package com.example.biobanddisplay

import android.Manifest
import android.content.pm.PackageManager
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch

/**
 * Single-Activity Compose app.
 * Navigation is handled with a simple enum + `when` block — no Navigation library needed.
 */
class MainActivity : ComponentActivity() {

    lateinit var ble: BleClient
        private set

    private val permissionsLauncher =
        registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { _ -> }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        ble = BleClient(this)

        setContent {
            MaterialTheme {
                val scope = rememberCoroutineScope()

                // Start Python on first composition
                LaunchedEffect(Unit) {
                    scope.launch(Dispatchers.IO) {
                        PythonBridge.ensureStarted(this@MainActivity)
                        PythonBridge.copyBundledCsv(this@MainActivity)
                    }
                }

                Surface(modifier = Modifier.fillMaxSize()) {
                    AppRoot(
                        ble = ble,
                        context = this,
                        requestPerms = { requestBlePermissions() }
                    )
                }
            }
        }
    }

    private fun requestBlePermissions() {
        permissionsLauncher.launch(
            arrayOf(
                Manifest.permission.BLUETOOTH_SCAN,
                Manifest.permission.BLUETOOTH_CONNECT,
                Manifest.permission.ACCESS_FINE_LOCATION
            )
        )
    }
}

// ──────────────────────── Navigation ────────────────────────

enum class Screen {
    Home, EmgGraph, PpgGraph, SweatGraph, TestGraph, RealTime, Journal
}

@Composable
fun AppRoot(
    ble: BleClient,
    context: android.content.Context,
    requestPerms: () -> Unit
) {
    var currentScreen by remember { mutableStateOf(Screen.Home) }

    when (currentScreen) {
        Screen.Home       -> HomeScreen(ble, requestPerms) { currentScreen = it }
        Screen.EmgGraph   -> EmgScreen(ble)   { currentScreen = Screen.Home }
        Screen.PpgGraph   -> PpgScreen(context){ currentScreen = Screen.Home }
        Screen.SweatGraph -> SweatScreen(ble)  { currentScreen = Screen.Home }
        Screen.TestGraph  -> TestScreen()      { currentScreen = Screen.Home }
        Screen.RealTime   -> RealTimeScreen(ble, context) { currentScreen = Screen.Home }
        Screen.Journal    -> JournalScreen()   { currentScreen = Screen.Home }
    }
}

// ──────────────────────── Home / Connect ────────────────────────

@Composable
fun HomeScreen(
    ble: BleClient,
    requestPerms: () -> Unit,
    navigate: (Screen) -> Unit
) {
    val state by ble.state.collectAsState()
    val devices by ble.devices.collectAsState()
    val isConnected = state is BleState.Connected

    Column(
        modifier = Modifier
            .fillMaxSize()
            .statusBarsPadding()
            .padding(16.dp)
    ) {
        Text("Bioband Display", style = MaterialTheme.typography.headlineSmall)
        Spacer(Modifier.height(4.dp))
        Text("State: ${stateLabel(state)}", style = MaterialTheme.typography.bodyMedium)

        Spacer(Modifier.height(12.dp))

        // ── BLE controls ──
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Button(onClick = requestPerms) { Text("Permissions") }
            Button(onClick = { ble.startScan() }) { Text("Scan") }
            Button(onClick = { ble.stopScan() }) { Text("Stop") }
            if (isConnected) {
                Button(onClick = { ble.disconnect() }) { Text("Disconnect") }
            }
        }

        // ── Device list (visible while scanning / idle) ──
        if (!isConnected && devices.isNotEmpty()) {
            Spacer(Modifier.height(12.dp))
            Text("Tap a device to connect:", style = MaterialTheme.typography.titleMedium)
            Spacer(Modifier.height(6.dp))
            devices.forEach { d ->
                Card(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(vertical = 4.dp),
                    onClick = { ble.connect(d.address) }
                ) {
                    Column(Modifier.padding(12.dp)) {
                        Text(d.name, style = MaterialTheme.typography.titleMedium)
                        Text("${d.address}  •  ${d.rssi} dBm")
                    }
                }
            }
        }

        // ── Navigation buttons (visible when connected) ──
        if (isConnected) {
            Spacer(Modifier.height(20.dp))
            Text("Choose a view:", style = MaterialTheme.typography.titleMedium)
            Spacer(Modifier.height(8.dp))

            val buttons = listOf(
                "EMG Graph"   to Screen.EmgGraph,
                "PPG Data"    to Screen.PpgGraph,
                "Sweat Graph" to Screen.SweatGraph,
                "Test / CSV"  to Screen.TestGraph,
                "Real-Time"   to Screen.RealTime,
                "Journal"     to Screen.Journal,
            )
            buttons.forEach { (label, screen) ->
                Button(
                    onClick = { navigate(screen) },
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(vertical = 4.dp)
                ) {
                    Text(label)
                }
            }
        }
    }
}

private fun stateLabel(s: BleState): String = when (s) {
    is BleState.Idle         -> "Idle"
    is BleState.Scanning     -> "Scanning…"
    is BleState.ScanError    -> "Scan error: ${s.reason}"
    is BleState.Connecting   -> "Connecting…"
    is BleState.Connected    -> "Connected (${s.address})"
    is BleState.Disconnected -> "Disconnected"
    is BleState.Error        -> "Error: ${s.message}"
}
