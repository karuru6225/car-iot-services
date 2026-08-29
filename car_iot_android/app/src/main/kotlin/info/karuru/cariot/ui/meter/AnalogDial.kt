package info.karuru.cariot.ui.meter

import android.graphics.Paint
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.nativeCanvas
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlin.math.cos
import kotlin.math.roundToInt
import kotlin.math.sin

// GAUGE テーマの核。針と目盛りを持つアナログ計器を描く。
//
// 「計器に見えるかどうか」を決めるのは針ではなく目盛りの作り込み。円弧に棒を1本
// 生やしただけでは、円い形のプログレスバーにしか見えない。そのため以下を全部描く:
//   ・主目盛り(長い線) と 副目盛り(短い線) の階層
//   ・主目盛りに対応する実際の数字
//   ・下側に開いた240度スイープ(実車の計器は一周しない。この欠けが計器らしさを作る)
//   ・針の根元のハブ
//
// 意図的に入れていないもの:
//   ・レッドゾーン … タコメーターらしさは出るが、電圧や水温では「上限側＝危険」という
//     意味を持たない。意味のない装飾になるため描かない
//   ・クローム風グラデーション/ガラスの映り込み/針の影 … 質感の演出であって情報ではない

// 実車の計器に倣い、真下を避けて左下から右下へ240度回す。
// Compose の drawArc は3時方向が0度、時計回りが正。
private const val START_ANGLE = 150f
private const val SWEEP_ANGLE = 240f

// 主目盛りの区間数。境界の数は MAJOR_DIVISIONS + 1 本になる。
private const val MAJOR_DIVISIONS = 4

// 主目盛りの間に入れる副目盛りの数。
private const val MINOR_PER_MAJOR = 4

// 目盛りに刻む数字をキリのいい値にするためのスケール。
//
// 実測レンジをそのまま4等分すると 0/1750/3500/5250/7000 のような半端な数字が並び、
// 「計器の文字盤」ではなく「プログレスバーに数字を振ったもの」に見える。実車の計器は
// 必ず 0/2000/4000/6000/8000 のようなキリのいい刻みを持ち、そのぶん上限がレンジより
// 広いこともある（7000rpmの車に 0-8000 の文字盤が付くのと同じ）。
// ここでも刻み幅を 1/2/2.5/5×10^n に丸め、上限をその倍数まで広げる。
private data class NiceScale(val min: Float, val max: Float)

private fun niceScaleFor(min: Float, max: Float): NiceScale {
  val range = max - min
  if (range <= 0f) return NiceScale(min, min + 1f)

  val rough = range / MAJOR_DIVISIONS
  val magnitude = Math.pow(10.0, kotlin.math.floor(kotlin.math.log10(rough.toDouble()))).toFloat()
  val normalized = rough / magnitude
  val niceNormalized = when {
    normalized <= 1f -> 1f
    normalized <= 2f -> 2f
    normalized <= 2.5f -> 2.5f
    normalized <= 5f -> 5f
    else -> 10f
  }
  val step = niceNormalized * magnitude

  val niceMin = kotlin.math.floor(min / step) * step
  var niceMax = niceMin + step * MAJOR_DIVISIONS
  // 丸めの結果で実レンジを覆えない場合だけ1目盛り広げる。
  while (niceMax < max) niceMax += step
  return NiceScale(niceMin, niceMax)
}

@Composable
fun AnalogDial(
    value: Float?,
    min: Float,
    max: Float,
    decimals: Int,
    modifier: Modifier = Modifier,
) {
  val faceColor = MaterialTheme.colorScheme.surfaceContainerHighest
  val bezelColor = MaterialTheme.colorScheme.outline
  val markColor = MaterialTheme.colorScheme.onSurface
  val numeralColor = MaterialTheme.colorScheme.onSurfaceVariant
  val needleColor = MaterialTheme.colorScheme.primary

  val density = LocalDensity.current
  // 目盛りの数字用。Canvas に直接描くので android.graphics.Paint を使う。
  // 毎フレーム生成しないよう remember しておく。
  //
  // 文字サイズは必ず sp で指定する。dp で指定すると端末のフォントサイズ設定を無視し、
  // 他の文字だけ大きくなって目盛りの数字が取り残される（WCAG 1.4.4 Resize text）。
  // ただし文字盤は直径が決まっているので、際限なく大きくすると目盛りを突き破る。
  // 半径の 16% を上限として頭打ちにする。
  val numeralBaseSizePx = with(density) { 9.sp.toPx() }
  val numeralPaint = remember(numeralColor) {
    Paint().apply {
      color = numeralColor.toArgb()
      textAlign = Paint.Align.CENTER
      isAntiAlias = true
      typeface = android.graphics.Typeface.MONOSPACE
    }
  }

  // 文字盤に刻む値はキリのいいスケールに丸める。針の位置もこのスケール基準にするため、
  // 実レンジより広い文字盤になることがある（実車の計器と同じ挙動）。
  val scale = remember(min, max) { niceScaleFor(min, max) }

  Canvas(modifier = modifier.fillMaxWidth().height(124.dp)) {
    val radius = minOf(size.width, size.height * 1.12f) / 2f * 0.90f
    // 毎描画で基準値から計算し直す。前回の値を minOf で潰すと縮小が累積して戻らなくなる。
    numeralPaint.textSize = minOf(numeralBaseSizePx, radius * 0.16f)
    // 下が欠けた図形なので、見た目の重心を合わせるためやや上寄りに中心を置く。
    val center = Offset(size.width / 2f, size.height * 0.52f)

    // 文字盤（針や目盛りが乗る面）
    drawCircle(color = faceColor, radius = radius, center = center)
    // ベゼル（外周のリング）
    drawCircle(
        color = bezelColor,
        radius = radius,
        center = center,
        style = Stroke(width = 1.5.dp.toPx()),
    )

    val totalTicks = MAJOR_DIVISIONS * MINOR_PER_MAJOR
    val majorLen = radius * 0.17f
    val minorLen = radius * 0.09f

    for (i in 0..totalTicks) {
      val isMajor = i % MINOR_PER_MAJOR == 0
      val angleDeg = START_ANGLE + SWEEP_ANGLE * (i.toFloat() / totalTicks)
      val rad = Math.toRadians(angleDeg.toDouble())
      val cosA = cos(rad).toFloat()
      val sinA = sin(rad).toFloat()

      val outer = radius * 0.94f
      val inner = outer - (if (isMajor) majorLen else minorLen)
      drawLine(
          color = markColor,
          start = Offset(center.x + cosA * inner, center.y + sinA * inner),
          end = Offset(center.x + cosA * outer, center.y + sinA * outer),
          strokeWidth = (if (isMajor) 2f else 1f).dp.toPx(),
          cap = StrokeCap.Butt,
      )

      // 主目盛りにだけ実際の値を刻む。これがあるかないかで計器に見えるかが決まる。
      if (isMajor) {
        val t = i.toFloat() / totalTicks
        val scaleValue = scale.min + (scale.max - scale.min) * t
        val label = if (decimals == 0 || kotlin.math.abs(scale.max - scale.min) >= 100f) {
          scaleValue.roundToInt().toString()
        } else {
          "%.1f".format(scaleValue)
        }
        val textR = radius * 0.62f
        // drawText のy座標はベースラインなので、文字の高さ分だけ下げて視覚的に中央へ寄せる。
        val baselineFix = numeralPaint.textSize * 0.36f
        drawContext.canvas.nativeCanvas.drawText(
            label,
            center.x + cosA * textR,
            center.y + sinA * textR + baselineFix,
            numeralPaint,
        )
      }
    }

    // 針。値が無いときは描かず、文字盤と目盛りだけを残す（計器としては
    // 「電源が入っていて針が振れていない」ではなく「信号が来ていない」状態なので、
    // ゼロを指す針を描くと嘘になる）。
    if (value != null) {
      val t = ((value - scale.min) / (scale.max - scale.min)).coerceIn(0f, 1f)
      val angleDeg = START_ANGLE + SWEEP_ANGLE * t
      val rad = Math.toRadians(angleDeg.toDouble())
      val cosA = cos(rad).toFloat()
      val sinA = sin(rad).toFloat()
      // 針の向きに直交する方向。根元の幅を作るのに使う。
      val perpX = -sinA
      val perpY = cosA

      val needleLen = radius * 0.76f
      // 根元を中心より少し手前から始めると、実際の計器の針の付き方に近くなる。
      val tailLen = radius * 0.13f
      val halfWidth = 2.2.dp.toPx()

      // 先細りの針。棒を1本引くのではなく、根元が太く先端が尖った三角形にする。
      // 実物の針はこの形で、線1本との差が「計器らしさ」にそのまま出る。
      val needle = Path().apply {
        moveTo(center.x + cosA * needleLen, center.y + sinA * needleLen)
        lineTo(center.x + perpX * halfWidth, center.y + perpY * halfWidth)
        lineTo(center.x - cosA * tailLen, center.y - sinA * tailLen)
        lineTo(center.x - perpX * halfWidth, center.y - perpY * halfWidth)
        close()
      }
      drawPath(needle, color = needleColor)

      // 針の回転軸。
      drawCircle(color = needleColor, radius = 4.dp.toPx(), center = center)
      drawCircle(color = faceColor, radius = 1.5.dp.toPx(), center = center)
    }
  }
}
