package info.karuru.cariot.ui.meter

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

// mobile/lib/widgets/meter_tile.dartの4種ゲージ移植。追加ライブラリ不要の既定方針どおり、
// circular/barはMaterial3標準Widget、digitalはText、sparklineはCanvas自作
// （docs/car_iot_android_plan.md「UIコンポーネント（ゲージ4種）」参照）。

private fun percentOf(value: Float, min: Float, max: Float): Float {
  val range = max - min
  if (range == 0f) return 0f
  return ((value - min) / range).coerceIn(0f, 1f)
}

@Composable
fun CircularGauge(value: Float, min: Float, max: Float, valueText: String, modifier: Modifier = Modifier) {
  val pct = percentOf(value, min, max)
  Box(contentAlignment = Alignment.Center, modifier = modifier.size(84.dp)) {
    CircularProgressIndicator(
        progress = { pct },
        modifier = Modifier.fillMaxSize(),
        strokeWidth = 8.dp,
    )
    Text(valueText, fontSize = 13.sp, fontWeight = FontWeight.Bold)
  }
}

@Composable
fun BarGauge(value: Float, min: Float, max: Float, valueText: String, modifier: Modifier = Modifier) {
  val pct = percentOf(value, min, max)
  Column(modifier = modifier.fillMaxWidth()) {
    LinearProgressIndicator(
        progress = { pct },
        modifier = Modifier
            .fillMaxWidth()
            .height(16.dp)
            .clip(RoundedCornerShape(4.dp)),
    )
    Text(valueText, fontSize = 13.sp, modifier = Modifier.padding(top = 4.dp))
  }
}

@Composable
fun DigitalGauge(valueText: String, modifier: Modifier = Modifier) {
  Text(valueText, fontSize = 22.sp, fontWeight = FontWeight.Bold, modifier = modifier)
}

// 履歴2点未満は折れ線が描けないため数値表示にフォールバックする
// （mobile/lib/widgets/meter_tile.dartと同じ）。
@Composable
fun SparklineGauge(
    history: List<Float>,
    min: Float,
    max: Float,
    valueText: String,
    modifier: Modifier = Modifier,
) {
  if (history.size < 2) {
    Text(valueText, fontSize = 22.sp, fontWeight = FontWeight.Bold, modifier = modifier)
    return
  }
  val lineColor = MaterialTheme.colorScheme.primary
  Canvas(modifier = modifier.fillMaxWidth().height(50.dp)) {
    val range = (max - min).takeIf { it != 0f } ?: 1f
    val stepX = size.width / (history.size - 1)
    val path = Path()
    history.forEachIndexed { index, v ->
      val normalized = ((v - min) / range).coerceIn(0f, 1f)
      val x = index * stepX
      val y = size.height * (1f - normalized)
      if (index == 0) path.moveTo(x, y) else path.lineTo(x, y)
    }
    drawPath(path, color = lineColor, style = Stroke(width = 3f))
  }
}

