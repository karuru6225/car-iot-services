package info.karuru.cariot.ui.theme

import androidx.compose.material3.ColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.ui.graphics.Color

// shadcn/ui の neutral ベーステーマ(ライト)を Material3 の ColorScheme にマッピングしたもの。
// https://ui.shadcn.com/docs/theming の :root 定義が出典。
//
// shadcn の配色体系の要点:
//   - 全ロールが彩度0の純グレースケール。有彩色は destructive(赤)ただ1つ
//   - primary は「アクセント色」ではなく前景色に近いニアブラック。塗りボタンは黒地に白文字
//   - card と background を色で分けず、境界は border(#E5E5E5) の細線で示す
//
// OKLCH表記の原典値からの変換: 彩度0のグレーは Oklab の L と線形sRGB値 v の間に
// L = cbrt(v) が成り立つため v = L^3 を求め、sRGBガンマを適用して8bit値を得ている。
//   oklch(1     0 0) -> #FFFFFF   oklch(0.985 0 0) -> #FAFAFA
//   oklch(0.97  0 0) -> #F5F5F5   oklch(0.922 0 0) -> #E5E5E5
//   oklch(0.708 0 0) -> #A1A1A1   oklch(0.556 0 0) -> #737373
//   oklch(0.205 0 0) -> #171717   oklch(0.145 0 0) -> #0A0A0A
//   oklch(0.577 0.245 27.325) -> #E7000B (destructive)
val ShadcnLightColorScheme: ColorScheme = lightColorScheme(
    // --primary / --primary-foreground
    primary = Color(0xFF171717),
    onPrimary = Color(0xFFFAFAFA),
    primaryContainer = Color(0xFFF5F5F5),
    onPrimaryContainer = Color(0xFF171717),
    // --secondary / --secondary-foreground（セカンダリボタンの下地）
    secondary = Color(0xFF737373),
    onSecondary = Color(0xFFFAFAFA),
    secondaryContainer = Color(0xFFF5F5F5),
    onSecondaryContainer = Color(0xFF171717),
    // --accent / --accent-foreground（neutralテーマでは secondary と同値）
    tertiary = Color(0xFF737373),
    onTertiary = Color(0xFFFAFAFA),
    tertiaryContainer = Color(0xFFF5F5F5),
    onTertiaryContainer = Color(0xFF171717),
    // --background / --foreground
    background = Color(0xFFFFFFFF),
    onBackground = Color(0xFF0A0A0A),
    // --card / --card-foreground
    surface = Color(0xFFFFFFFF),
    onSurface = Color(0xFF0A0A0A),
    // --muted / --muted-foreground
    surfaceVariant = Color(0xFFF5F5F5),
    onSurfaceVariant = Color(0xFF737373),
    surfaceContainerLowest = Color(0xFFFFFFFF),
    surfaceContainerLow = Color(0xFFFFFFFF),
    surfaceContainer = Color(0xFFFAFAFA),
    surfaceContainerHigh = Color(0xFFF5F5F5),
    surfaceContainerHighest = Color(0xFFF5F5F5),
    inverseSurface = Color(0xFF171717),
    inverseOnSurface = Color(0xFFFAFAFA),
    inversePrimary = Color(0xFFE5E5E5),
    // --destructive
    error = Color(0xFFE7000B),
    onError = Color(0xFFFAFAFA),
    errorContainer = Color(0xFFFFE2E2),
    onErrorContainer = Color(0xFF82181A),
    // --border / --input / --ring
    outline = Color(0xFFA1A1A1),
    outlineVariant = Color(0xFFE5E5E5),
    scrim = Color(0xFF000000),
)
