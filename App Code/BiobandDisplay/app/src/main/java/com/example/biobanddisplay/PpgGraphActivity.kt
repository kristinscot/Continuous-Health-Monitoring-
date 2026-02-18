package com.example.biobanddisplay

import android.graphics.Color
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.widget.Button
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import com.chaquo.python.PyException
import com.chaquo.python.Python

class PpgGraphActivity : AppCompatActivity(), BleDataListener {

    private val TAG = "PPG_GRAPH_ACTIVITY"
    private lateinit var heartRateText: TextView
    private lateinit var bloodFlowText: TextView
    private lateinit var backButton: Button
    private val handler = Handler(Looper.getMainLooper())

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_ppg_graph)
        
        heartRateText = findViewById(R.id.heart_rate_text)
        bloodFlowText = findViewById(R.id.blood_flow_text)
        backButton = findViewById(R.id.back_button_ppg)

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
        BleConnectionManager.ppgDataListener = this
    }

    override fun onPause() {
        super.onPause()
        if (BleConnectionManager.ppgDataListener == this) {
            BleConnectionManager.ppgDataListener = null
        }
    }

    override fun onDataReceived(data: String) {
        try {
            val python = Python.getInstance()
            val analyzerModule = python.getModule("ppg_analyzer")
            val result = analyzerModule.callAttr("process_ppg_data", data)
            val resultMap = result.asMap()

            val heartRate = resultMap[com.chaquo.python.PyObject.fromJava("heart_rate")]?.toInt() ?: 0
            val bloodFlow = resultMap[com.chaquo.python.PyObject.fromJava("blood_flow")]?.toInt() ?: 0

            handler.post {
                if (heartRate > 0) heartRateText.text = "$heartRate BPM"
                if (bloodFlow > 0) bloodFlowText.text = "$bloodFlow %"
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error processing PPG data", e)
        }
    }
}
