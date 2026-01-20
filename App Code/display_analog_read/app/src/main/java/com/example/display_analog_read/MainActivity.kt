package com.example.display_analog_read

import android.Manifest
import android.content.pm.PackageManager
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.nativeCanvas
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import com.example.display_analog_read.ui.theme.Display_analog_readTheme
import kotlin.math.max
import kotlin.math.roundToInt
import androidx.compose.foundation.layout.FlowRow


class MainActivity : ComponentActivity() {

    private lateinit var ble: BleClient

    private val permissionsLauncher =
        registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { _ ->
            // We’ll just try scanning again when user taps Scan
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        ble = BleClient(this)

        setContent {
            Display_analog_readTheme {
                Surface(modifier = Modifier.fillMaxSize()) {
                    BleScreen(
                        ble = ble,
                        requestPerms = { requestBlePermissions() }
                    )
                }
            }
        }
    }

    private fun requestBlePermissions() {
        val perms = arrayOf(
            Manifest.permission.BLUETOOTH_SCAN,
            Manifest.permission.BLUETOOTH_CONNECT
        )
        permissionsLauncher.launch(perms)
    }

    private fun hasBlePermissions(): Boolean {
        val scanOk = ContextCompat.checkSelfPermission(
            this, Manifest.permission.BLUETOOTH_SCAN
        ) == PackageManager.PERMISSION_GRANTED

        val connOk = ContextCompat.checkSelfPermission(
            this, Manifest.permission.BLUETOOTH_CONNECT
        ) == PackageManager.PERMISSION_GRANTED

        return scanOk && connOk
    }
}

@Composable
private fun BleScreen(
    ble: BleClient,
    requestPerms: () -> Unit
) {
    val state by ble.state.collectAsState()
    val devices by ble.devices.collectAsState()
    val latestMv by ble.latestMv.collectAsState()
    val history = remember { mutableStateListOf<Int>() }
    val maxPoints = 200

    LaunchedEffect(latestMv) {
        val v = latestMv ?: return@LaunchedEffect
        history.add(v)
        if (history.size > maxPoints) {
            history.removeAt(0)
        }
    }

    var permsGranted by remember { mutableStateOf(false) }

    // Simple “check permission” UI logic:
    LaunchedEffect(Unit) {
        // We can’t check permissions from Composable directly without context;
        // so we just show a button and let scan attempt fail gracefully if not granted.
        permsGranted = true
    }

    Column(modifier = Modifier.fillMaxSize().statusBarsPadding().padding(16.dp)) {
        Text("ADC_DONGLE BLE Viewer", style = MaterialTheme.typography.headlineSmall)
        Spacer(Modifier.height(8.dp))

        Text("State: ${state::class.simpleName}")
        Spacer(Modifier.height(8.dp))

        FlowRow(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            Button(onClick = { requestPerms() }) {
                Text("Request BLE perms", maxLines = 1)
            }

            Button(onClick = { ble.startScan() }) {
                Text("Scan", maxLines = 1)
            }

            Button(onClick = { ble.stopScan() }) {
                Text("Stop", maxLines = 1)
            }
        }


        Spacer(Modifier.height(12.dp))

        Card(modifier = Modifier.fillMaxWidth()) {
            Column(modifier = Modifier.padding(12.dp)) {
                Text("Analog Voltage (mV):", style = MaterialTheme.typography.titleMedium)
                Text(
                    text = latestMv?.toString() ?: "—",
                    style = MaterialTheme.typography.displaySmall
                )
            }
        }

        Spacer(Modifier.height(12.dp))

        Card(modifier = Modifier.fillMaxWidth()) {
            Column(modifier = Modifier.padding(12.dp)) {
                Text("History", style = MaterialTheme.typography.titleMedium)
                Spacer(Modifier.height(8.dp))
                LineChart(
                    samples = history,
                    modifier = Modifier.fillMaxWidth().height(180.dp),
                    yMin = 0, yMax=3000
                )
            }
        }

        Spacer(Modifier.height(12.dp))
        Text("Tap a device to connect:", style = MaterialTheme.typography.titleMedium)
        Spacer(Modifier.height(6.dp))

        LazyColumn(
            modifier = Modifier.fillMaxWidth(),
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            items(devices) { d ->
                Card(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clickable { ble.connect(d.address) }
                ) {
                    Column(modifier = Modifier.padding(12.dp)) {
                        Text(d.name, style = MaterialTheme.typography.titleMedium)
                        Text("MAC: ${d.address}")
                        Text("RSSI: ${d.rssi} dBm")
                    }
                }
            }
        }
    }
}

@Composable
fun LineChart(
    samples: List<Int>,
    modifier: Modifier = Modifier,
    yMin: Int? = null,
    yMax: Int? = null,
    ticks: Int = 4
) {
    if (samples.isEmpty()) return

    val density = LocalDensity.current

    // Space reserved for Y-axis labels
    val labelPaddingDp = 44.dp
    val tickLengthDp = 6.dp
    val bottomPaddingDp = 8.dp
    val topPaddingDp = 8.dp

    Canvas(modifier = modifier) {
        val labelPad = with(density) { labelPaddingDp.toPx() }
        val tickLen = with(density) { tickLengthDp.toPx() }
        val topPad = with(density) { topPaddingDp.toPx() }
        val bottomPad = with(density) { bottomPaddingDp.toPx() }

        val w = size.width
        val h = size.height

        val plotLeft = labelPad
        val plotRight = w
        val plotTop = topPad
        val plotBottom = h - bottomPad

        val minY = yMin ?: (samples.minOrNull() ?: 0)
        val maxY = yMax ?: (samples.maxOrNull() ?: (minY + 1))
        val range = max(1, maxY - minY)

        fun x(i: Int): Float {
            if (samples.size == 1) return plotLeft
            val t = i.toFloat() / (samples.size - 1)
            return plotLeft + t * (plotRight - plotLeft)
        }

        fun y(v: Int): Float {
            val t = (v - minY).toFloat() / range.toFloat()
            return plotBottom - t * (plotBottom - plotTop)
        }

        // ---- Axes ----
        // Y axis line
        drawLine(
            color = Color.Gray,
            start = Offset(plotLeft, plotTop),
            end = Offset(plotLeft, plotBottom),
            strokeWidth = 2f
        )

        // X axis baseline (optional)
        drawLine(
            color = Color.Gray,
            start = Offset(plotLeft, plotBottom),
            end = Offset(plotRight, plotBottom),
            strokeWidth = 2f
        )

        // ---- Y ticks + labels ----
        val tickCount = max(2, ticks)
        val step = range.toFloat() / (tickCount - 1)

        val paint = android.graphics.Paint().apply {
            isAntiAlias = true
            textSize = with(density) { 12.dp.toPx() }
            color = android.graphics.Color.LTGRAY
        }

        for (i in 0 until tickCount) {
            val value = (minY + i * step).roundToInt()
            val yy = y(value)

            // tick mark
            drawLine(
                color = Color.Gray,
                start = Offset(plotLeft - tickLen, yy),
                end = Offset(plotLeft, yy),
                strokeWidth = 2f
            )

            // label text (left of axis)
            drawContext.canvas.nativeCanvas.drawText(
                value.toString(),
                2f,                 // small left margin
                yy + paint.textSize / 3f,  // vertical centering tweak
                paint
            )
        }

        // ---- Line path ----
        val path = Path()
        path.moveTo(x(0), y(samples[0]))
        for (i in 1 until samples.size) {
            path.lineTo(x(i), y(samples[i]))
        }

        drawPath(
            path = path,
            color = Color.Green,
            style = Stroke(width = 4f)
        )
    }
}