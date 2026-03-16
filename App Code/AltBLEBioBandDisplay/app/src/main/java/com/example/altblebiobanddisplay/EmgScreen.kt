package com.example.biobanddisplay

import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.launch
import java.util.Locale

/**
 * Replaces Kristen's GraphActivity.
 *
 * Buffers incoming millivolt samples, then sends them as a comma-separated
 * string to `analyzer.process_ble_data()` every [PACKET_THRESHOLD] samples.
 */
@Composable
fun EmgScreen(
    ble: BleClient,
    onBack: () -> Unit
) {
    val latestMv by ble.latestMv.collectAsState()
    val scope = rememberCoroutineScope()

    // Buffer & metrics state
    val buffer = remember { mutableListOf<Int>() }
    val PACKET_THRESHOLD = 10

    var vrms by remember { mutableFloatStateOf(0f) }
    var tdmf by remember { mutableFloatStateOf(0f) }
    var isResting by remember { mutableStateOf(true) }

    // Chart history
    val history = remember { mutableStateListOf<Int>() }
    val maxPoints = 300

    // React to each new sample
    LaunchedEffect(latestMv) {
        val mv = latestMv ?: return@LaunchedEffect

        // Keep chart history
        history.add(mv)
        if (history.size > maxPoints) history.removeAt(0)

        // Buffer for Python
        buffer.add(mv)
        if (buffer.size >= PACKET_THRESHOLD) {
            val csv = buffer.joinToString(",")
            buffer.clear()

            scope.launch {
                val result = PythonBridge.processEmg(csv)
                vrms = result.vrms
                tdmf = result.tdmf
                isResting = result.isResting
            }
        }
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
            Text("EMG Graph", style = MaterialTheme.typography.headlineSmall)
            Button(onClick = onBack) { Text("Back") }
        }

        Spacer(Modifier.height(12.dp))

        // Metrics cards
        Card(modifier = Modifier.fillMaxWidth()) {
            Column(Modifier.padding(12.dp)) {
                Text("Vrms: ${String.format(Locale.US, "%.2f mV", vrms)}")
                Text("TDMF: ${String.format(Locale.US, "%.0f Hz", tdmf)}")
                Text(
                    text = if (isResting) "Resting" else "Active",
                    color = if (isResting) Color.Gray else Color.Green
                )
            }
        }

        Spacer(Modifier.height(12.dp))

        Card(modifier = Modifier.fillMaxWidth()) {
            Column(Modifier.padding(12.dp)) {
                Text("Live EMG", style = MaterialTheme.typography.titleMedium)
                Spacer(Modifier.height(8.dp))
                LineChart(
                    samples = history,
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(220.dp)
                )
            }
        }
    }
}
