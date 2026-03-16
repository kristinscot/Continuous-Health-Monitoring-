package com.example.biobanddisplay

import android.content.Context
import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.launch
import java.io.File
import java.util.Locale

/**
 * Replaces Kristen's PpgGraphActivity.
 *
 * On first composition, loads stored CSV data via ppg_analyzer.
 * Also refreshes when new BLE data could be appended to the CSV.
 */
@Composable
fun PpgScreen(
    context: Context,
    onBack: () -> Unit
) {
    val scope = rememberCoroutineScope()

    var bpm by remember { mutableFloatStateOf(0f) }
    var spo2 by remember { mutableFloatStateOf(0f) }
    var artStiffness by remember { mutableFloatStateOf(0f) }
    var breathingRate by remember { mutableFloatStateOf(0f) }
    var loading by remember { mutableStateOf(true) }

    // Load from CSV on first composition
    LaunchedEffect(Unit) {
        val csvPath = File(context.filesDir, "ppg_filtered_data.csv").absolutePath
        val result = PythonBridge.processPpg("INITIAL_LOAD", csvPath)
        bpm = result.bpm
        spo2 = result.spo2
        artStiffness = result.artStiffness
        breathingRate = result.breathingRate
        loading = false
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
            Text("PPG Data", style = MaterialTheme.typography.headlineSmall)
            Button(onClick = onBack) { Text("Back") }
        }

        Spacer(Modifier.height(16.dp))

        if (loading) {
            CircularProgressIndicator()
        } else {
            MetricCard("Heart Rate",       String.format(Locale.US, "%.0f BPM", bpm))
            MetricCard("SpO₂",             String.format(Locale.US, "%.1f %%", spo2))
            MetricCard("Art. Stiffness",   String.format(Locale.US, "%.2f m/s", artStiffness))
            MetricCard("Breathing Rate",   String.format(Locale.US, "%.0f BrPM", breathingRate))
        }
    }
}

@Composable
private fun MetricCard(label: String, value: String) {
    Card(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 4.dp)
    ) {
        Row(
            modifier = Modifier.padding(16.dp),
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Text(label, style = MaterialTheme.typography.titleMedium)
            Text(value, style = MaterialTheme.typography.headlineMedium)
        }
    }
}
