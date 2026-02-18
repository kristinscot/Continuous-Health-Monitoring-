package com.example.biobanddisplay

import android.content.Context
import android.graphics.Color
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.widget.Button
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import com.chaquo.python.PyObject
import com.chaquo.python.PyException
import com.chaquo.python.Python
import java.io.FileOutputStream

class GraphActivity : AppCompatActivity(), BleDataListener {

    private val TAG = "EMG_GRAPH_ACTIVITY"
    private lateinit var vrmsText: TextView
    private lateinit var muscleStatusText: TextView
    private lateinit var backButton: Button
    private val handler = Handler(Looper.getMainLooper())

    private val packetBuffer = mutableListOf<String>()
    private val PACKET_THRESHOLD = 10 
    private var lastPacketInActivation: String? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_graph)
        
        vrmsText = findViewById(R.id.vrms_text)
        muscleStatusText = findViewById(R.id.muscle_status_text)
        backButton = findViewById(R.id.back_button_emg)
        
        backButton.setOnClickListener {
            finish()
        }

        if (BleConnectionManager.gatt == null) {
            Toast.makeText(this, "Connection lost, returning to main screen.", Toast.LENGTH_LONG).show()
            finish()
            return
        }
    }

    override fun onResume() {
        super.onResume()
        BleConnectionManager.emgDataListener = this
    }

    override fun onPause() {
        super.onPause()
        if (BleConnectionManager.emgDataListener == this) {
            BleConnectionManager.emgDataListener = null
        }
    }

    override fun onDataReceived(data: String) {
        saveToFile(data)
        packetBuffer.add(data)
        if (packetBuffer.size >= PACKET_THRESHOLD) {
            processBufferedData()
        }
    }

    private fun processBufferedData() {
        try {
            val python = Python.getInstance()
            val analyzerModule = python.getModule("analyzer")
            
            val dataToProcess = mutableListOf<String>()
            lastPacketInActivation?.let { dataToProcess.add(it) }
            dataToProcess.addAll(packetBuffer)
            
            val combinedDataString = dataToProcess.joinToString(",")
            val result: PyObject = analyzerModule.callAttr("process_ble_data", combinedDataString)
            val resultMap = result.asMap()

            val vrms = resultMap[PyObject.fromJava("vrms")]?.toFloat() ?: 0f
            val isResting = resultMap[PyObject.fromJava("is_resting")]?.toBoolean() ?: true
            val endsInActivation = resultMap[PyObject.fromJava("ends_in_activation")]?.toBoolean() ?: false

            if (endsInActivation) {
                lastPacketInActivation = packetBuffer.lastOrNull()
            } else {
                lastPacketInActivation = null
            }
            packetBuffer.clear()

            handler.post {
                vrmsText.text = String.format("%.2f mV", vrms)
                muscleStatusText.text = if (isResting) "Resting" else "Active"
                muscleStatusText.setTextColor(if (isResting) Color.WHITE else Color.GREEN)
            }

        } catch (e: Exception) {
            Log.e(TAG, "Error processing buffer", e)
        }
    }

    private fun saveToFile(data: String) {
        try {
            val fileName = "emg_data_log.csv"
            val fileOutputStream: FileOutputStream = openFileOutput(fileName, Context.MODE_APPEND)
            fileOutputStream.write("$data\n".toByteArray())
            fileOutputStream.close()
        } catch (e: Exception) {
            Log.e(TAG, "Error saving data", e)
        }
    }
}
