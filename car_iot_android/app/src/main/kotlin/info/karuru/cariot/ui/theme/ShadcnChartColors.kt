package info.karuru.cariot.ui.theme

import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.graphics.Color

// shadcn/ui のデータ可視化用トークン(--chart-1 .. --chart-5)。
// Material3 の ColorScheme にはチャート用の枠が無いため CompositionLocal で配る。
//
// shadcn の配色は本体UIが彩度0の純グレースケールで、色を持つのは destructive と
// このチャート系だけ。ゲージ(円/バー/スパークライン)は「データ可視化」なので
// primary ではなくこちらを使うのが原典に忠実。
//
// OKLCH原典値からの変換(ShadcnLightColorScheme.kt のコメントに変換式):
//   light --chart-1: oklch(0.646 0.222 41.116)  -> #F54A00
//   dark  --chart-1: oklch(0.488 0.243 264.376) -> #1447E6
data class ShadcnChartColors(
    // ゲージの値部分に使う主色
    val chart1: Color,
    // ゲージの軌道(未到達部分)。背景と識別できる明度差を持たせる
    val track: Color,
)

val ShadcnLightChartColors = ShadcnChartColors(
    chart1 = Color(0xFFF54A00),
    track = Color(0xFFE5E5E5),
)

val ShadcnDarkChartColors = ShadcnChartColors(
    chart1 = Color(0xFF1447E6),
    track = Color(0xFF2E2E2E),
)

val LocalChartColors = staticCompositionLocalOf { ShadcnDarkChartColors }
