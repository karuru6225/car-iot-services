package info.karuru.cariot.ui.meter

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

// mobile/lib/widgets/meter_tile.dartの4種ゲージ移植。
//
// デザイン刷新(2026/08): Material3の CircularProgressIndicator /
// LinearProgressIndicator の使用をやめた。あれは「進捗」の表現で、太い塗り面が
// 数値より目立ってしまう。計測値に進捗という概念は無い（電圧は0%から100%へ
// 進行しない）ため、レンジ内の位置を細い線とティックだけで示す ValueRail に
// 統一し、視線が数値に行くようにしている。
//
// circular も同じ考えで、塗りつぶしの円弧ではなく細いアークのスイープにした。

private fun percentOf(value: Float, min: Float, max: Float): Float {
  val range = max - min
  if (range == 0f) return 0f
  return ((value - min) / range).coerceIn(0f, 1f)
}

// 値がまだ無いときは軌道だけ描く（fraction = null）。
private fun fractionOrNull(value: Float?, min: Float, max: Float): Float? =
    value?.let { percentOf(it, min, max) }

@Composable
fun CircularGauge(
    value: Float?,
    min: Float,
    max: Float,
    valueText: String,
    modifier: Modifier = Modifier,
) {
  val fraction = fractionOrNull(value, min, max)
  val trackColor = MaterialTheme.colorScheme.outline
  val tickColor = MaterialTheme.colorScheme.primary
  Column(modifier = modifier) {
    Text(valueText, style = MaterialTheme.typography.displaySmall)
    // 270度スイープの細いアーク。下方向に開いた自動車メーター状の弧にしている。
    Canvas(modifier = Modifier.padding(top = 8.dp).size(width = 96.dp, height = 30.dp)) {
      val stroke = Stroke(width = 2.dp.toPx(), cap = StrokeCap.Round)
      val arcSize = androidx.compose.ui.geometry.Size(size.width, size.width * 0.62f)
      drawArc(
          color = trackColor,
          startAngle = 160f,
          sweepAngle = 220f,
          useCenter = false,
          size = arcSize,
          style = stroke,
      )
      if (fraction != null) {
        drawArc(
            color = tickColor,
            startAngle = 160f + 220f * fraction - 1.5f,
            sweepAngle = 3f,
            useCenter = false,
            size = arcSize,
            style = Stroke(width = 5.dp.toPx(), cap = StrokeCap.Round),
        )
      }
    }
  }
}

@Composable
fun BarGauge(
    value: Float?,
    min: Float,
    max: Float,
    valueText: String,
    modifier: Modifier = Modifier,
) {
  Column(modifier = modifier.fillMaxWidth()) {
    Text(valueText, style = MaterialTheme.typography.displaySmall)
    ValueRail(fractionOrNull(value, min, max), modifier = Modifier.padding(top = 10.dp))
  }
}

@Composable
fun DigitalGauge(valueText: String, modifier: Modifier = Modifier) {
  Text(valueText, style = MaterialTheme.typography.displayMedium, modifier = modifier)
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
  val lineColor = MaterialTheme.colorScheme.primary
  Column(modifier = modifier.fillMaxWidth()) {
    Text(valueText, style = MaterialTheme.typography.displaySmall)
    if (history.size < 2) {
      ValueRail(null, modifier = Modifier.padding(top = 10.dp))
      return@Column
    }
    Canvas(modifier = Modifier.padding(top = 10.dp).fillMaxWidth().height(36.dp)) {
      val range = (max - min).takeIf { it != 0f } ?: 1f
      val stepX = size.width / (history.size - 1)
      val path = Path()
      history.forEachIndexed { index, v ->
        val normalized = ((v - min) / range).coerceIn(0f, 1f)
        val x = index * stepX
        val y = size.height * (1f - normalized)
        if (index == 0) path.moveTo(x, y) else path.lineTo(x, y)
      }
      drawPath(path, color = lineColor, style = Stroke(width = 1.5.dp.toPx(), cap = StrokeCap.Round))
    }
  }
}
