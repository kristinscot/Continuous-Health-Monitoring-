package com.example.biobanddisplay

import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.launch

/**
 * Replaces Kristen's SweatGraphActivity.
 */
@Composable
fun SweatScreen(
    ble: BleClient,
    onBack: () -> Unit
) {
    val latestMv by ble.latestMv.collectAsState()
    val scope = rememberCoroutineScope()

    val history = remember { mutableStateListOf<Int>() }
    val maxPoints = 300

    LaunchedEffect(latestMv) {
        val mv = latestMv ?: return@LaunchedEffect

        // Send each sample to Python for processing
        scope.launch {
            val processed = PythonBridge.processSweat(mv.toString())
            for (p in processed) {
                history.add(p.toInt())
                if (history.size > maxPoints) history.removeAt(0)
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
            Text("Sweat Graph", style = MaterialTheme.typography.headlineSmall)
            Button(onClick = onBack) { Text("Back") }
        }

        Spacer(Modifier.height(12.dp))

        Card(modifier = Modifier.fillMaxWidth()) {
            Column(Modifier.padding(12.dp)) {
                Text("Live Sweat Data", style = MaterialTheme.typography.titleMedium)
                Spacer(Modifier.height(8.dp))
                LineChart(
                    samples = history,
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(250.dp)
                )
            }
        }
    }
}
