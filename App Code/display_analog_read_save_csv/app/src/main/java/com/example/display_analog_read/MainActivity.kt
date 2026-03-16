package com.example.display_analog_read
import android.content.Intent
import android.net.Uri
import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.os.Bundle
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
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
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import com.example.display_analog_read.ui.theme.Display_analog_readTheme
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import kotlin.math.max
import kotlin.math.roundToInt

class MainActivity : ComponentActivity() {

    private lateinit var ble: BleClient

    private val permissionsLauncher =
        registerForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) { _ ->
            // User can tap Scan again after granting permissions
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
            this,
            Manifest.permission.BLUETOOTH_SCAN
        ) == PackageManager.PERMISSION_GRANTED

        val connOk = ContextCompat.checkSelfPermission(
            this,
            Manifest.permission.BLUETOOTH_CONNECT
        ) == PackageManager.PERMISSION_GRANTED

        return scanOk && connOk
    }
}

@Composable
private fun BleScreen(
    ble: BleClient,
    requestPerms: () -> Unit
) {
    val context = LocalContext.current

    val state by ble.state.collectAsState()
    val devices by ble.devices.collectAsState()
    val latestMv by ble.latestMv.collectAsState()

    val history = remember { mutableStateListOf<Int>() }
    val maxPoints = 200

    var lastSavedPath by remember { mutableStateOf<String?>(null) }
    var recordedCount by remember { mutableIntStateOf(0) }

    LaunchedEffect(latestMv) {
        val v = latestMv ?: return@LaunchedEffect
        history.add(v)
        if (history.size > maxPoints) {
            history.removeAt(0)
        }
        recordedCount = ble.getRecordedSamplesSnapshot().size
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .statusBarsPadding()
            .verticalScroll(rememberScrollState())
            .padding(16.dp)
    ) {
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

            Button(
                onClick = {
                    val samples = ble.getRecordedSamplesSnapshot()
                    if (samples.isEmpty()) {
                        Toast.makeText(context, "No data to save yet", Toast.LENGTH_SHORT).show()
                    } else {
                        val result = saveSamplesToCsv(context, samples)
                        result.onSuccess { file ->
                            lastSavedPath = file.absolutePath
                            Toast.makeText(
                                context,
                                "Saved CSV: ${file.name}",
                                Toast.LENGTH_LONG
                            ).show()
                        }.onFailure { e ->
                            Toast.makeText(
                                context,
                                "Save failed: ${e.message}",
                                Toast.LENGTH_LONG
                            ).show()
                        }
                    }
                }
            ) {
                Text("Save CSV", maxLines = 1)
            }

            Button(
                onClick = {
                    ble.clearRecordedSamples()
                    history.clear()
                    recordedCount = 0
                    Toast.makeText(context, "Recorded samples cleared", Toast.LENGTH_SHORT).show()
                }
            ) {
                Text("Clear recorded", maxLines = 1)
            }

            Button(
                onClick = {
                    val filePath = lastSavedPath
                    if (filePath == null) {
                        Toast.makeText(context, "No CSV saved yet", Toast.LENGTH_SHORT).show()
                    } else {
                        Toast.makeText(context, filePath, Toast.LENGTH_LONG).show()
                    }
                }
            ) {
                Text("Show CSV Path")
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
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(180.dp),
                    yMin = 0,
                    yMax = 3000
                )
            }
        }

        Spacer(Modifier.height(12.dp))

        Card(modifier = Modifier.fillMaxWidth()) {
            Column(modifier = Modifier.padding(12.dp)) {
                Text("CSV Recorder", style = MaterialTheme.typography.titleMedium)
                Spacer(Modifier.height(6.dp))
                Text("Recorded samples: $recordedCount")
                Text("Last saved: ${lastSavedPath ?: "—"}")
            }
        }

        Spacer(Modifier.height(12.dp))
        Text("Tap a device to connect:", style = MaterialTheme.typography.titleMedium)
        Spacer(Modifier.height(6.dp))

        Column(
            modifier = Modifier.fillMaxWidth(),
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            devices.forEach { d ->
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

private fun saveSamplesToCsv(
    context: Context,
    samples: List<Int>
): Result<File> {
    return runCatching {
        val timestamp = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US).format(Date())
        val dir = context.getExternalFilesDir(null)
            ?: throw IllegalStateException("External files directory not available")

        val file = File(dir, "ble_data_$timestamp.csv")

        val csvText = buildString {
            appendLine("index,value")
            samples.forEachIndexed { index, value ->
                append(index)
                append(',')
                append(value)
                appendLine()
            }
        }

        file.writeText(csvText)
        file
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

        drawLine(
            color = Color.Gray,
            start = Offset(plotLeft, plotTop),
            end = Offset(plotLeft, plotBottom),
            strokeWidth = 2f
        )

        drawLine(
            color = Color.Gray,
            start = Offset(plotLeft, plotBottom),
            end = Offset(plotRight, plotBottom),
            strokeWidth = 2f
        )

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

            drawLine(
                color = Color.Gray,
                start = Offset(plotLeft - tickLen, yy),
                end = Offset(plotLeft, yy),
                strokeWidth = 2f
            )

            drawContext.canvas.nativeCanvas.drawText(
                value.toString(),
                2f,
                yy + paint.textSize / 3f,
                paint
            )
        }

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