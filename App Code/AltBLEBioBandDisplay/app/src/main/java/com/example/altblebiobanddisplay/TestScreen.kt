package com.example.biobanddisplay

import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.launch

/**
 * Replaces Kristen's TestGraphActivity.
 *
 * Shows a "Load CSV" button that calls test_analyzer.get_csv_data(),
 * then plots the first two columns (ECG + Blood Pressure) from the CSV.
 */
@Composable
fun TestScreen(
    onBack: () -> Unit
) {
    val scope = rememberCoroutineScope()

    val series1 = remember { mutableStateListOf<Int>() }   // e.g. ECG
    val series2 = remember { mutableStateListOf<Int>() }   // e.g. Blood Pressure
    var loading by remember { mutableStateOf(false) }

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
            Text("Test / CSV", style = MaterialTheme.typography.headlineSmall)
            Button(onClick = onBack) { Text("Back") }
        }

        Spacer(Modifier.height(12.dp))

        Button(
            onClick = {
                loading = true
                scope.launch {
                    val rows = PythonBridge.getCsvData()
                    series1.clear()
                    series2.clear()
                    for (row in rows) {
                        // row[0] = x value (time), row[1] = ch1, row[2] = ch2
                        if (row.size > 1) series1.add(row[1].toInt())
                        if (row.size > 2) series2.add(row[2].toInt())
                    }
                    loading = false
                }
            },
            enabled = !loading,
            modifier = Modifier.fillMaxWidth()
        ) {
            Text(if (loading) "Loading…" else "Load CSV Data")
        }

        if (loading) {
            Spacer(Modifier.height(8.dp))
            LinearProgressIndicator(modifier = Modifier.fillMaxWidth())
        }

        if (series1.isNotEmpty()) {
            Spacer(Modifier.height(12.dp))
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(Modifier.padding(12.dp)) {
                    Text("ECG (column 1)", style = MaterialTheme.typography.titleMedium)
                    Spacer(Modifier.height(8.dp))
                    LineChart(
                        samples = series1,
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(180.dp)
                    )
                }
            }
        }

        if (series2.isNotEmpty()) {
            Spacer(Modifier.height(12.dp))
            Card(modifier = Modifier.fillMaxWidth()) {
                Column(Modifier.padding(12.dp)) {
                    Text("Blood Pressure (column 2)", style = MaterialTheme.typography.titleMedium)
                    Spacer(Modifier.height(8.dp))
                    LineChart(
                        samples = series2,
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(180.dp)
                    )
                }
            }
        }
    }
}
