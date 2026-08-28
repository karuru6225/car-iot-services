package info.karuru.cariot.ui.meter

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.unit.dp

// このデザインの署名要素。数値の下に敷く極細のレールで、レンジ内のどこに今の値が
// あるかだけを示す。
//
// Material3 の LinearProgressIndicator は太く塗りつぶされた「進捗」の表現で、
// 計測値には意味が合わない（電圧に進捗は無い）。計器のスイープに倣い、
//   ・軌道は 1dp のヘアライン
//   ・現在値は 2dp の短いティック
// だけで示す。塗り面積を持たないぶん、巨大な数値の邪魔をしない。
//
// 値が無いとき(reading==null)は tick を描かず軌道だけ残し、「レンジはあるが
// まだ値が来ていない」ことを示す。
@Composable
fun ValueRail(
    fraction: Float?,
    modifier: Modifier = Modifier,
) {
  val trackColor = MaterialTheme.colorScheme.outline
  val tickColor = MaterialTheme.colorScheme.primary
  Canvas(modifier = modifier.fillMaxWidth().height(10.dp)) {
    val y = size.height / 2f
    drawLine(
        color = trackColor,
        start = Offset(0f, y),
        end = Offset(size.width, y),
        strokeWidth = 1.dp.toPx(),
        cap = StrokeCap.Round,
    )
    if (fraction != null) {
      val x = (size.width * fraction.coerceIn(0f, 1f))
      drawLine(
          color = tickColor,
          start = Offset(x, 0f),
          end = Offset(x, size.height),
          strokeWidth = 2.dp.toPx(),
          cap = StrokeCap.Round,
      )
    }
  }
}
