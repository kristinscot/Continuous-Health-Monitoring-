package com.example.biobanddisplay

import androidx.compose.foundation.Canvas
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.nativeCanvas
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.dp
import kotlin.math.max
import kotlin.math.roundToInt

/**
 * Lightweight line chart drawn directly on a Compose [Canvas].
 * No external charting library needed.
 */
@Composable
fun LineChart(
    samples: List<Int>,
    modifier: Modifier = Modifier,
    lineColor: Color = Color.Green,
    yMin: Int? = null,
    yMax: Int? = null,
    ticks: Int = 4
) {
    if (samples.isEmpty()) return

    val density = LocalDensity.current
    val labelPaddingDp = 44.dp
    val tickLengthDp = 6.dp
    val bottomPaddingDp = 8.dp
    val topPaddingDp = 8.dp

    Canvas(modifier = modifier) {
        val labelPad  = with(density) { labelPaddingDp.toPx() }
        val tickLen   = with(density) { tickLengthDp.toPx() }
        val topPad    = with(density) { topPaddingDp.toPx() }
        val bottomPad = with(density) { bottomPaddingDp.toPx() }

        val w = size.width
        val h = size.height

        val plotLeft   = labelPad
        val plotRight  = w
        val plotTop    = topPad
        val plotBottom = h - bottomPad

        val minY  = yMin ?: (samples.minOrNull() ?: 0)
        val maxY  = yMax ?: (samples.maxOrNull() ?: (minY + 1))
        val range = max(1, maxY - minY)

        fun x(i: Int): Float {
            if (samples.size == 1) return plotLeft
            val t = i.toFloat() / (samples.size - 1)
            return plotLeft + t * (plotRight - plotLeft)
        }

        fun y(v: Int): Float {
            val t = (v - minY).toFloat() / range.toFloat()
            return plotBottom - t * (plotBottom - plotTop)
        }

        // Axes
        drawLine(Color.Gray, Offset(plotLeft, plotTop), Offset(plotLeft, plotBottom), 2f)
        drawLine(Color.Gray, Offset(plotLeft, plotBottom), Offset(plotRight, plotBottom), 2f)

        // Y ticks + labels
        val tickCount = max(2, ticks)
        val step = range.toFloat() / (tickCount - 1)
        val paint = android.graphics.Paint().apply {
            isAntiAlias = true
            textSize = with(density) { 12.dp.toPx() }
            color = android.graphics.Color.LTGRAY
        }
        for (i in 0 until tickCount) {
            val value = (minY + i * step).roundToInt()
            val yy = y(value)
            drawLine(Color.Gray, Offset(plotLeft - tickLen, yy), Offset(plotLeft, yy), 2f)
            drawContext.canvas.nativeCanvas.drawText(
                value.toString(), 2f, yy + paint.textSize / 3f, paint
            )
        }

        // Line path
        val path = Path()
        path.moveTo(x(0), y(samples[0]))
        for (i in 1 until samples.size) {
            path.lineTo(x(i), y(samples[i]))
        }
        drawPath(path, lineColor, style = Stroke(width = 4f))
    }
}
