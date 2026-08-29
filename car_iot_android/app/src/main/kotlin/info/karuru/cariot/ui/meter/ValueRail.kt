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
import info.karuru.cariot.ui.theme.InstrumentStyle
import info.karuru.cariot.ui.theme.LocalInstrumentStyle

// （AutoSizeValueText はこのファイル末尾に定義。計測値の表示を全画面で統一するため
// meter パッケージに置いている。）

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
  // GAUGEテーマではレールにも目盛りを刻み、円形ダイヤルと同じ「計器の目盛り」の語彙で
  // 揃える。FLATテーマでは軌道のヘアラインだけに留める。
  val analog = LocalInstrumentStyle.current == InstrumentStyle.ANALOG
  val scaleColor = MaterialTheme.colorScheme.onSurface
  Canvas(modifier = modifier.fillMaxWidth().height(if (analog) 16.dp else 10.dp)) {
    val y = size.height / 2f
    drawLine(
        color = trackColor,
        start = Offset(0f, y),
        end = Offset(size.width, y),
        strokeWidth = 1.dp.toPx(),
        cap = StrokeCap.Round,
    )
    if (analog) {
      // 目盛りの本数は幅に応じて決める。3列に並ぶ狭いタイルに20本刻むと線が詰まって
      // 「目盛り」ではなく塗り潰しの帯に見えてしまうため、最低間隔を確保できる本数まで
      // 落とす（主目盛りが等間隔に来るよう5の倍数に丸める）。
      val minSpacingPx = 7.dp.toPx()
      val total = (((size.width / minSpacingPx).toInt() / 5) * 5).coerceIn(5, 20)
      for (i in 0..total) {
        val isMajor = i % 5 == 0
        val x = size.width * (i.toFloat() / total)
        val len = if (isMajor) size.height * 0.34f else size.height * 0.18f
        drawLine(
            color = scaleColor,
            start = Offset(x, y - len),
            end = Offset(x, y + len),
            strokeWidth = (if (isMajor) 1.5f else 1f).dp.toPx(),
            cap = StrokeCap.Butt,
        )
      }
    }
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
