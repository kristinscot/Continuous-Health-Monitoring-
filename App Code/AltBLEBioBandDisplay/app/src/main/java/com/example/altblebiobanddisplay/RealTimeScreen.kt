package com.example.biobanddisplay

import android.content.Context
import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.launch
import java.io.File
import java.util.Locale

/**
 * Replaces Kristen's RealTimeActivity.
 *
 * Shows two live charts (PPG + EMG) and metric summaries.
 * Both are driven from the single BLE millivolt stream.
 */
@Composable
fun RealTimeScreen(
    ble: BleClient,
    context: Context,
    onBack: () -> Unit
) {
    val latestMv by ble.latestMv.collectAsState()
    val scope = rememberCoroutineScope()

    // EMG state
    val emgHistory = remember { mutableStateListOf<Int>() }
    val emgBuffer = remember { mutableListOf<Int>() }
    var emgMetrics by remember { mutableStateOf("Vrms: — | TDMF: —") }
    var emgColor by remember { mutableStateOf(Color.Gray) }

    // PPG state
    val ppgHistory = remember { mutableStateListOf<Int>() }
    var ppgMetrics by remember { mutableStateOf("BPM: — | SpO₂: —") }

    val maxPoints = 300

    // Load PPG CSV on first composition
    LaunchedEffect(Unit) {
        val csvPath = File(context.filesDir, "ppg_filtered_data.csv").absolutePath
        val result = PythonBridge.processPpg("INITIAL_LOAD", csvPath)
        ppgMetrics = String.format(Locale.US, "BPM: %.0f | SpO₂: %.1f%%", result.bpm, result.spo2)
        for (p in result.pointsRed) {
            ppgHistory.add(p.toInt())
            if (ppgHistory.size > maxPoints) ppgHistory.removeAt(0)
        }
    }

    // React to live BLE data
    LaunchedEffect(latestMv) {
        val mv = latestMv ?: return@LaunchedEffect

        // EMG chart
        emgHistory.add(mv)
        if (emgHistory.size > maxPoints) emgHistory.removeAt(0)

        emgBuffer.add(mv)
        if (emgBuffer.size >= 10) {
            val csv = emgBuffer.joinToString(",")
            emgBuffer.clear()
            scope.launch {
                val r = PythonBridge.processEmg(csv)
                emgMetrics = String.format(Locale.US, "Vrms: %.2f mV | TDMF: %.0f Hz", r.vrms, r.tdmf)
                emgColor = if (r.isResting) Color.Gray else Color.Green
            }
        }

        // PPG chart (append live samples)
        ppgHistory.add(mv)
        if (ppgHistory.size > maxPoints) ppgHistory.removeAt(0)
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .statusBarsPadding()
            .padding(16.dp)
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Text("Real-Time", style = MaterialTheme.typography.headlineSmall)
            Button(onClick = onBack) { Text("Back") }
        }

        Spacer(Modifier.height(8.dp))

        // PPG section
        Card(modifier = Modifier.fillMaxWidth()) {
            Column(Modifier.padding(12.dp)) {
                Text("PPG", style = MaterialTheme.typography.titleMedium)
                Text(ppgMetrics, color = Color.Green)
                Spacer(Modifier.height(4.dp))
                LineChart(
                    samples = ppgHistory,
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(160.dp)
                )
            }
        }

        Spacer(Modifier.height(12.dp))

        // EMG section
        Card(modifier = Modifier.fillMaxWidth()) {
            Column(Modifier.padding(12.dp)) {
                Text("EMG", style = MaterialTheme.typography.titleMedium)
                Text(emgMetrics, color = emgColor)
                Spacer(Modifier.height(4.dp))
                LineChart(
                    samples = emgHistory,
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(160.dp)
                )
            }
        }
    }
}
