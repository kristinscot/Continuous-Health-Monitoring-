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
import java.util.Locale

class RealTimeActivity : AppCompatActivity() {

    private val TAG = "REAL_TIME_ACTIVITY"
    private lateinit var ppgChart: LineChart
    private lateinit var emgChart: LineChart
    private lateinit var emgMetricsText: TextView
    private lateinit var backButton: Button
    private val handler = Handler(Looper.getMainLooper())

    private val emgListener = object : BleDataListener {
        override fun onDataReceived(data: String) {
            processEmgData(data)
        }
    }

    private val ppgListener = object : BleDataListener {
        override fun onDataReceived(data: String) {
            processPpgData(data)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_real_time)

        ppgChart = findViewById(R.id.ppg_chart_realtime)
        emgChart = findViewById(R.id.emg_chart_realtime)
        emgMetricsText = findViewById(R.id.emg_metrics_realtime)
        backButton = findViewById(R.id.back_button_realtime)

        backButton.setOnClickListener { finish() }

        setupChart(ppgChart, "PPG Data", Color.GREEN)
        setupChart(emgChart, "EMG Data", Color.CYAN)

        if (BleConnectionManager.gatt == null) {
            Toast.makeText(this, "Device not connected", Toast.LENGTH_SHORT).show()
            finish()
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
            val result = analyzer.callAttr("process_ppg_data", data).asMap()
            val points = result[com.chaquo.python.PyObject.fromJava("points")]?.toJava(FloatArray::class.java) ?: floatArrayOf()

            handler.post {
                for (p in points) {
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
        chart.setVisibleXRangeMaximum(100f)
        chart.moveViewToX(data.entryCount.toFloat())
    }
}
