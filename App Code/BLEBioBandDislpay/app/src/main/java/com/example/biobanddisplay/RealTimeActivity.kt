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
import com.chaquo.python.Python
import com.github.mikephil.charting.charts.LineChart
import com.github.mikephil.charting.data.Entry
import com.github.mikephil.charting.data.LineData
import com.github.mikephil.charting.data.LineDataSet
import java.io.File
import java.util.Locale

class RealTimeActivity : AppCompatActivity() {

    private val TAG = "REAL_TIME_ACTIVITY"
    private lateinit var ppgChart: LineChart
    private lateinit var emgChart: LineChart
    private lateinit var emgMetricsText: TextView
    private lateinit var ppgMetricsText: TextView
    private lateinit var backButton: Button
    private val handler = Handler(Looper.getMainLooper())

    private val emgListener = object : BleDataListener {
        override fun onDataReceived(data: String) {
            processEmgData(data)
        }
    }

    private val ppgListener = object : BleDataListener {
        override fun onDataReceived(data: String) {
            // Live data arrived, but we also refresh from the latest CSV state if desired
            processPpgData(data)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_real_time)

        ppgChart = findViewById(R.id.ppg_chart_realtime)
        emgChart = findViewById(R.id.emg_chart_realtime)
        emgMetricsText = findViewById(R.id.emg_metrics_realtime)
        ppgMetricsText = findViewById(R.id.ppg_metrics_realtime)
        backButton = findViewById(R.id.back_button_realtime)

        backButton.setOnClickListener { finish() }

        setupChart(ppgChart, "PPG Data", Color.GREEN)
        setupChart(emgChart, "EMG Data", Color.CYAN)

        // 1. Copy the bundled CSV to internal storage so Python can read it
        copyCsvToInternalStorage()

        // 2. Perform an initial load of the CSV data to populate the graph immediately
        processPpgData("INITIAL_LOAD")

        if (BleConnectionManager.gatt == null) {
            Toast.makeText(this, "Device not connected. Showing stored data.", Toast.LENGTH_SHORT).show()
        }
    }

    private fun copyCsvToInternalStorage() {
        try {
            val fileName = "ppg_filtered_data.csv"
            val targetFile = File(filesDir, fileName)
            
            // Chaquopy bundles the 'python' folder. We can find the file using the Python instance.
            val python = Python.getInstance()
            val sys = python.getModule("sys")
            val pyPath = sys["path"]?.asList() ?: emptyList()
            
            var sourceFile: File? = null
            for (path in pyPath) {
                val potential = File(path.toString(), fileName)
                if (potential.exists()) {
                    sourceFile = potential
                    break
                }
            }

            if (sourceFile != null) {
                sourceFile.inputStream().use { input ->
                    targetFile.outputStream().use { output ->
                        input.copyTo(output)
                    }
                }
                Log.d(TAG, "CSV copied successfully to: ${targetFile.absolutePath}")
            } else {
                Log.e(TAG, "Could not find bundled CSV: $fileName")
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error in copyCsvToInternalStorage", e)
        }
    }

    override fun onResume() {
        super.onResume()
        BleConnectionManager.emgDataListener = emgListener
        BleConnectionManager.ppgDataListener = ppgListener
    }

    override fun onPause() {
        super.onPause()
        if (BleConnectionManager.emgDataListener == emgListener) BleConnectionManager.emgDataListener = null
        if (BleConnectionManager.ppgDataListener == ppgListener) BleConnectionManager.ppgDataListener = null
    }

    private fun processEmgData(data: String) {
        try {
            val python = Python.getInstance()
            val analyzer = python.getModule("analyzer")
            val result = analyzer.callAttr("process_ble_data", data).asMap()
            
            val voltages = result[com.chaquo.python.PyObject.fromJava("voltages")]?.toJava(FloatArray::class.java) ?: floatArrayOf()
            val vrms = result[com.chaquo.python.PyObject.fromJava("vrms")]?.toFloat() ?: 0f
            val tdmf = result[com.chaquo.python.PyObject.fromJava("tdmf")]?.toFloat() ?: 0f
            val isResting = result[com.chaquo.python.PyObject.fromJava("is_resting")]?.toBoolean() ?: true
            
            handler.post {
                emgMetricsText.text = String.format(Locale.US, "Vrms: %.2f mV | TDMF: %.0f Hz", vrms, tdmf)
                emgMetricsText.setTextColor(if (isResting) Color.parseColor("#BDC3C7") else Color.GREEN)
                
                for (v in voltages) {
                    addEntry(emgChart, v)
                }
            }
        } catch (e: Exception) {
            Log.e(TAG, "EMG error", e)
        }
    }

    private fun processPpgData(data: String) {
        try {
            val python = Python.getInstance()
            val analyzer = python.getModule("ppg_analyzer")
            
            val csvFile = File(filesDir, "ppg_filtered_data.csv")
            val csvPath = csvFile.absolutePath
            
            // Call Python to process the CSV file
            val result = analyzer.callAttr("process_ppg_data", data, csvPath).asMap()
            
            val pointsRed = result[com.chaquo.python.PyObject.fromJava("points_red")]?.toJava(FloatArray::class.java) ?: floatArrayOf()
            val bpm = result[com.chaquo.python.PyObject.fromJava("bpm")]?.toFloat() ?: 0f
            val spo2 = result[com.chaquo.python.PyObject.fromJava("SpO2")]?.toFloat() ?: 0f

            handler.post {
                ppgMetricsText.text = String.format(Locale.US, "BPM: %.0f | SpO2: %.1f%%", bpm, spo2)
                
                // Clear and plot all points from the CSV
                if (data == "INITIAL_LOAD") {
                    ppgChart.data?.clearValues()
                }
                
                for (p in pointsRed) {
                    addEntry(ppgChart, p)
                }
            }
        } catch (e: Exception) {
            Log.e(TAG, "PPG error", e)
        }
    }

    private fun setupChart(chart: LineChart, label: String, color: Int) {
        chart.description.isEnabled = false
        chart.setNoDataText("Waiting for data...")
        chart.setNoDataTextColor(Color.WHITE)
        chart.xAxis.textColor = Color.WHITE
        chart.axisLeft.textColor = Color.WHITE
        chart.axisRight.isEnabled = false
        chart.legend.textColor = Color.WHITE
        chart.data = LineData()
    }

    private fun addEntry(chart: LineChart, value: Float) {
        val data = chart.data ?: return
        var set = data.getDataSetByIndex(0)
        if (set == null) {
            set = LineDataSet(null, "Data").apply {
                this.color = if (chart.id == R.id.ppg_chart_realtime) Color.GREEN else Color.CYAN
                setDrawCircles(false)
                lineWidth = 2f
                setDrawValues(false)
                mode = if (chart.id == R.id.ppg_chart_realtime) LineDataSet.Mode.CUBIC_BEZIER else LineDataSet.Mode.LINEAR
            }
            data.addDataSet(set)
        }
        data.addEntry(Entry(set.entryCount.toFloat(), value), 0)
        data.notifyDataChanged()
        chart.notifyDataSetChanged()
        
        // Show more points for PPG if it's the initial load from CSV
        val maxRange = if (chart.id == R.id.ppg_chart_realtime) 1000f else 100f
        chart.setVisibleXRangeMaximum(maxRange)
        chart.moveViewToX(data.entryCount.toFloat())
    }
}
