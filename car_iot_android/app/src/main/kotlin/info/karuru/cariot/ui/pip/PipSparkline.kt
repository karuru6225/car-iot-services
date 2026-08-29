package info.karuru.cariot.ui.pip

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.unit.dp

// PiPウィンドウ内のスパークライン。
//
// 高さは呼び出し側が modifier で決める。ゲージを含むPiPではウィンドウの高さが
// アナログダイヤルに合わせて確保されるため、グラフだけのセルは下が余る。
// weight で余りを吸わせて、その分グラフを大きく描けるようにしている。
//
// 縦軸をデータ自身の最小/最大に合わせる考え方はメータータブ版と同じ
// （固定レンジだと変動が数%しか使われず平坦な直線になるため）。
// 履歴が2点未満のときは軌道だけの水平線にして、「データがまだ無い」ことを示す。
@Composable
fun PipSparkline(
    history: List<Float>,
    metricMin: Float,
    metricMax: Float,
    modifier: Modifier = Modifier,
) {
  val lineColor = MaterialTheme.colorScheme.primary
  val trackColor = MaterialTheme.colorScheme.outline

  Canvas(modifier = modifier.fillMaxWidth()) {
    val inset = 1.5.dp.toPx()
    if (history.size < 2) {
      val y = size.height / 2f
      drawLine(
          color = trackColor,
          start = Offset(0f, y),
          end = Offset(size.width, y),
          strokeWidth = 1.dp.toPx(),
          cap = StrokeCap.Round,
      )
      return@Canvas
    }

    // 値がほぼ一定のときにセンサーノイズが画面いっぱいの山脈に化けないよう、
    // メーターレンジの2%を下限の振れ幅として確保する（メータータブ版と同じ歯止め。
    // ここだけ緩いと、同じ項目でもPiPのほうが激しく振れて見える）。
    val dataMin = history.min()
    val dataMax = history.max()
    val minSpan = (metricMax - metricMin) * 0.02f
    val span = maxOf(dataMax - dataMin, minSpan).takeIf { it > 0f } ?: 1f
    val center = (dataMin + dataMax) / 2f
    val lo = center - span / 2f
    val usable = size.height - inset * 2f
    val stepX = size.width / (history.size - 1)

    val path = Path()
    history.forEachIndexed { index, v ->
      val normalized = ((v - lo) / span).coerceIn(0f, 1f)
      val x = index * stepX
      val y = inset + usable * (1f - normalized)
      if (index == 0) path.moveTo(x, y) else path.lineTo(x, y)
    }
    drawPath(path, color = lineColor, style = Stroke(width = 1.5.dp.toPx(), cap = StrokeCap.Round))
  }
}
