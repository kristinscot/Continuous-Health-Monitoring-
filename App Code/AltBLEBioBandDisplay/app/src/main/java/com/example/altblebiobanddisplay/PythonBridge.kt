package com.example.biobanddisplay

import android.content.Context
import android.util.Log
import com.chaquo.python.PyObject
import com.chaquo.python.Python
import com.chaquo.python.android.AndroidPlatform
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File

/**
 * Thin wrapper around Kristen's Chaquopy Python modules.
 *
 * Call [ensureStarted] once at app startup (from a background thread / coroutine).
 * Then use the individual process* functions from any coroutine scope.
 *
 * All heavy Python work is dispatched to [Dispatchers.IO].
 */
object PythonBridge {

    private const val TAG = "PythonBridge"

    // ── Initialization ──────────────────────────────────────────────

    @Volatile
    var isReady = false
        private set

    /** Must be called once before any other function. Safe to call repeatedly. */
    fun ensureStarted(context: Context) {
        if (isReady) return
        synchronized(this) {
            if (!Python.isStarted()) {
                Python.start(AndroidPlatform(context))
                Log.d(TAG, "Python started.")
            }
            isReady = true
        }
    }

    private val py: Python get() = Python.getInstance()

    // ── CSV helpers ─────────────────────────────────────────────────

    /**
     * Copies the bundled ppg_filtered_data.csv from Chaquopy's Python path
     * into the app's internal storage so Python scripts can read/write it.
     * Returns the destination [File].
     */
    fun copyBundledCsv(context: Context, fileName: String = "ppg_filtered_data.csv"): File {
        val target = File(context.filesDir, fileName)
        if (target.exists()) return target          // already copied

        val sys = py.getModule("sys")
        val paths = sys["path"]?.asList() ?: emptyList()
        for (p in paths) {
            val candidate = File(p.toString(), fileName)
            if (candidate.exists()) {
                candidate.inputStream().use { inp ->
                    target.outputStream().use { out -> inp.copyTo(out) }
                }
                Log.d(TAG, "Copied $fileName → ${target.absolutePath}")
                return target
            }
        }
        Log.w(TAG, "Bundled CSV '$fileName' not found in Python path.")
        return target   // empty file – callers should handle gracefully
    }

    // ── EMG (analyzer.py) ───────────────────────────────────────────

    data class EmgResult(
        val voltages: FloatArray = floatArrayOf(),
        val vrms: Float = 0f,
        val tdmf: Float = 0f,
        val isResting: Boolean = true,
        val endsInActivation: Boolean = false
    )

    /**
     * Calls `analyzer.process_ble_data(csv_string)`.
     * [dataString] is a comma-separated series of millivolt readings.
     */
    suspend fun processEmg(dataString: String): EmgResult = withContext(Dispatchers.IO) {
        try {
            val mod = py.getModule("analyzer")
            val result = mod.callAttr("process_ble_data", dataString).asMap()

            EmgResult(
                voltages        = result.pyGet("voltages")?.toJava(FloatArray::class.java) ?: floatArrayOf(),
                vrms             = result.pyGet("vrms")?.toFloat() ?: 0f,
                tdmf             = result.pyGet("tdmf")?.toFloat() ?: 0f,
                isResting        = result.pyGet("is_resting")?.toBoolean() ?: true,
                endsInActivation = result.pyGet("ends_in_activation")?.toBoolean() ?: false,
            )
        } catch (e: Exception) {
            Log.e(TAG, "processEmg error", e)
            EmgResult()
        }
    }

    // ── PPG (ppg_analyzer.py) ───────────────────────────────────────

    data class PpgResult(
        val bpm: Float = 0f,
        val spo2: Float = 0f,
        val artStiffness: Float = 0f,
        val breathingRate: Float = 0f,
        val pointsRed: FloatArray = floatArrayOf()
    )

    /**
     * Calls `ppg_analyzer.process_ppg_data(data, csv_path)`.
     */
    suspend fun processPpg(dataString: String, csvPath: String): PpgResult = withContext(Dispatchers.IO) {
        try {
            val mod = py.getModule("ppg_analyzer")
            val result = mod.callAttr("process_ppg_data", dataString, csvPath).asMap()

            PpgResult(
                bpm           = result.pyGet("bpm")?.toFloat() ?: 0f,
                spo2          = result.pyGet("SpO2")?.toFloat() ?: 0f,
                artStiffness  = result.pyGet("artStiffness")?.toFloat() ?: 0f,
                breathingRate = result.pyGet("breathingRate")?.toFloat() ?: 0f,
                pointsRed     = result.pyGet("points_red")?.toJava(FloatArray::class.java) ?: floatArrayOf(),
            )
        } catch (e: Exception) {
            Log.e(TAG, "processPpg error", e)
            PpgResult()
        }
    }

    // ── Sweat (sweat_analyzer.py) ───────────────────────────────────

    /**
     * Calls `sweat_analyzer.process_sweat_data(data)`.
     * Returns a list of processed float values.
     */
    suspend fun processSweat(dataString: String): List<Float> = withContext(Dispatchers.IO) {
        try {
            val mod = py.getModule("sweat_analyzer")
            @Suppress("UNCHECKED_CAST")
            mod.callAttr("process_sweat_data", dataString)
                .toJava(List::class.java) as List<Float>
        } catch (e: Exception) {
            Log.e(TAG, "processSweat error", e)
            emptyList()
        }
    }

    // ── Test / CSV (test_analyzer.py) ───────────────────────────────

    /**
     * Calls `test_analyzer.process_test_data(raw_string)`.
     */
    suspend fun processTest(dataString: String): List<Float> = withContext(Dispatchers.IO) {
        try {
            val mod = py.getModule("test_analyzer")
            mod.callAttr("process_test_data", dataString)
                .asList().map { it.toFloat() }
        } catch (e: Exception) {
            Log.e(TAG, "processTest error", e)
            emptyList()
        }
    }

    /**
     * Calls `test_analyzer.get_csv_data()`.
     * Returns rows of floats (each inner list = one CSV row).
     */
    suspend fun getCsvData(): List<List<Float>> = withContext(Dispatchers.IO) {
        try {
            val mod = py.getModule("test_analyzer")
            mod.callAttr("get_csv_data").asList().map { row ->
                row.asList().map { it.toFloat() }
            }
        } catch (e: Exception) {
            Log.e(TAG, "getCsvData error", e)
            emptyList()
        }
    }

    // ── Utility ─────────────────────────────────────────────────────

    /** Helper to get a value from a Python dict using a Java-wrapped key. */
    private fun Map<PyObject, PyObject>.pyGet(key: String): PyObject? =
        this[PyObject.fromJava(key)]
}
