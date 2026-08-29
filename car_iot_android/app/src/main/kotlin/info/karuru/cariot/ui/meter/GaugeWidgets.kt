package info.karuru.cariot.ui.meter

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import info.karuru.cariot.ui.theme.InstrumentStyle
import info.karuru.cariot.ui.theme.LocalInstrumentStyle

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
    unit: String,
    decimals: Int = 0,
    modifier: Modifier = Modifier,
) {
  // GAUGEテーマでは針と目盛りを持つアナログ計器として描く。数値はダイヤルの下に小さく
  // 添えるだけにして、主役を文字盤に譲る。
  if (LocalInstrumentStyle.current == InstrumentStyle.ANALOG) {
    Column(modifier = modifier) {
      AnalogDial(value = value, min = min, max = max, decimals = decimals)
      ValueText(valueText, unit, fontSize = 18.sp)
    }
    return
  }

  val fraction = fractionOrNull(value, min, max)
  val trackColor = MaterialTheme.colorScheme.outline
  val tickColor = MaterialTheme.colorScheme.primary
  Column(modifier = modifier) {
    ValueText(valueText, unit)
    // 上半分だけの180度スイープ。円弧の外接ボックスは正方形(幅×幅)で、その上半分が
    // 収まる高さのCanvasに描く。ここが合っていないと弧の下側が切れて、
    // ゲージではなくただの曲線に見えてしまう。
    val arcWidth = 96.dp
    Canvas(
        modifier = Modifier
            .padding(top = 10.dp)
            .size(width = arcWidth, height = arcWidth / 2 + 4.dp),
    ) {
      val box = androidx.compose.ui.geometry.Size(size.width, size.width)
      drawArc(
          color = trackColor,
          startAngle = 180f,
          sweepAngle = 180f,
          useCenter = false,
          size = box,
          style = Stroke(width = 2.dp.toPx(), cap = StrokeCap.Round),
      )
      if (fraction != null) {
        drawArc(
            color = tickColor,
            startAngle = 180f + 180f * fraction - 1.5f,
            sweepAngle = 3f,
            useCenter = false,
            size = box,
            style = Stroke(width = 6.dp.toPx(), cap = StrokeCap.Round),
        )
      }
    }
  }
}

// 数値と単位の関係は全タブで共通にする（数値は大きく、単位は極小でくすませる）。
// 単位まで同じ大きさで出すと、せっかく作った「ラベル/数値」の落差が崩れる。
@Composable
private fun ValueText(valueText: String, unit: String, fontSize: androidx.compose.ui.unit.TextUnit = 26.sp) {
  Row(verticalAlignment = Alignment.Bottom) {
    Text(
        valueText,
        style = MaterialTheme.typography.displaySmall.copy(fontSize = fontSize),
        maxLines = 1,
    )
    if (unit.isNotEmpty()) {
      Text(
          unit,
          style = MaterialTheme.typography.labelSmall,
          color = MaterialTheme.colorScheme.onSurfaceVariant,
          modifier = Modifier.padding(start = 6.dp, bottom = 4.dp),
      )
    }
  }
}

@Composable
fun BarGauge(
    value: Float?,
    min: Float,
    max: Float,
    valueText: String,
    unit: String,
    modifier: Modifier = Modifier,
) {
  Column(modifier = modifier.fillMaxWidth()) {
    ValueText(valueText, unit)
    ValueRail(fractionOrNull(value, min, max), modifier = Modifier.padding(top = 10.dp))
  }
}

// レンジを持たない項目向け。数値だけを大きく出す。
@Composable
fun DigitalGauge(valueText: String, unit: String, modifier: Modifier = Modifier) {
  Column(modifier = modifier) {
    ValueText(valueText, unit, fontSize = 34.sp)
  }
}

// 履歴2点未満は折れ線が描けないため数値表示にフォールバックする
// （mobile/lib/widgets/meter_tile.dartと同じ）。
@Composable
fun SparklineGauge(
    history: List<Float>,
    min: Float,
    max: Float,
    valueText: String,
    unit: String,
    modifier: Modifier = Modifier,
) {
  val lineColor = MaterialTheme.colorScheme.primary
  Column(modifier = modifier.fillMaxWidth()) {
    ValueText(valueText, unit)
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
