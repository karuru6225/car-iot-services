package info.karuru.cariot.ui.meter

import androidx.compose.foundation.text.BasicText
import androidx.compose.foundation.text.TextAutoSize
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.unit.TextUnit
import androidx.compose.ui.unit.sp

// 計測値を「枠に収まるまで自動で縮小して1行で出す」表示。
//
// 端末のフォントサイズ設定を大きくすると、固定サイズの数値はタイル幅を超える。
// maxLines=1 の既定の overflow は Clip なので、そのままだと末尾の桁が黙って
// 切り落とされる——"13.812" が "13.81" になり、省略記号も出ないため
// 「別の正しそうな値」に見えてしまう。計測値の表示としては単なる崩れではなく
// 誤読を生むので、切るのではなく縮めて全桁を残す。
//
// WCAG 1.4.4 (Resize text) は文字を200%まで拡大できることを求めるが、
// 幅の決まったタイルでは拡大に応じて縮小・リフローするのが現実的な満たし方になる。
// 下限(minFontSize)を置いて、縮みすぎて読めなくなるのも防ぐ。
@Composable
fun AutoSizeValueText(
    text: String,
    style: TextStyle,
    color: Color,
    modifier: Modifier = Modifier,
    minFontSize: TextUnit = 12.sp,
) {
  val maxFontSize = style.fontSize.takeIf { it != TextUnit.Unspecified } ?: 26.sp
  BasicText(
      text = text,
      modifier = modifier,
      style = style.copy(color = color),
      maxLines = 1,
      autoSize = TextAutoSize.StepBased(
          minFontSize = minFontSize,
          maxFontSize = maxFontSize,
          stepSize = 0.5.sp,
      ),
  )
}
