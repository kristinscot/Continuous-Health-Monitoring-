package com.example.biobanddisplay

import android.content.Context
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import java.text.SimpleDateFormat
import java.util.*

/**
 * Replaces Kristen's JournalActivity.
 * Simple form that saves soreness + tiredness to SharedPreferences.
 */
@Composable
fun JournalScreen(
    onBack: () -> Unit
) {
    val context = LocalContext.current
    val today = remember {
        SimpleDateFormat("yyyy-MM-dd", Locale.getDefault()).format(Date())
    }

    var date by remember { mutableStateOf(today) }
    var soreness by remember { mutableStateOf("") }
    var tiredness by remember { mutableStateOf("") }
    var message by remember { mutableStateOf<String?>(null) }

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
            Text("Journal", style = MaterialTheme.typography.headlineSmall)
            Button(onClick = onBack) { Text("Back") }
        }

        Spacer(Modifier.height(16.dp))

        OutlinedTextField(
            value = date,
            onValueChange = { date = it },
            label = { Text("Date (yyyy-MM-dd)") },
            modifier = Modifier.fillMaxWidth()
        )

        Spacer(Modifier.height(8.dp))

        OutlinedTextField(
            value = soreness,
            onValueChange = { soreness = it },
            label = { Text("Soreness (1–10)") },
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
            modifier = Modifier.fillMaxWidth()
        )

        Spacer(Modifier.height(8.dp))

        OutlinedTextField(
            value = tiredness,
            onValueChange = { tiredness = it },
            label = { Text("Tiredness (1–10)") },
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
            modifier = Modifier.fillMaxWidth()
        )

        Spacer(Modifier.height(16.dp))

        Button(
            onClick = {
                val s = soreness.toIntOrNull()
                val t = tiredness.toIntOrNull()

                if (date.isBlank() || s == null || t == null) {
                    message = "Please fill in all fields."
                    return@Button
                }
                if (s !in 1..10 || t !in 1..10) {
                    message = "Values must be between 1 and 10."
                    return@Button
                }

                val prefs = context.getSharedPreferences("BiobandJournal", Context.MODE_PRIVATE)
                prefs.edit()
                    .putString("${date}_soreness", soreness)
                    .putString("${date}_tiredness", tiredness)
                    .apply()

                message = "Saved entry for $date"
                soreness = ""
                tiredness = ""
            },
            modifier = Modifier.fillMaxWidth()
        ) {
            Text("Save Entry")
        }

        message?.let {
            Spacer(Modifier.height(8.dp))
            Text(it, color = MaterialTheme.colorScheme.primary)
        }
    }
}
