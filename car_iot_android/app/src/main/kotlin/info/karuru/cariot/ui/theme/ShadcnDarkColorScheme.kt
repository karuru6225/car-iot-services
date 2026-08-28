package info.karuru.cariot.ui.theme

import androidx.compose.material3.ColorScheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.ui.graphics.Color

// shadcn/ui の neutral ベーステーマ(ダーク)を Material3 の ColorScheme にマッピングしたもの。
// https://ui.shadcn.com/docs/theming の .dark 定義が出典。変換方法は
// ShadcnLightColorScheme.kt のコメント参照。
//
// ダークで特徴的なのは primary がニアホワイト(#E5E5E5)になる点。塗りボタンは
// 白地に黒文字になり、暗い画面の中で最も明るい面が主アクションを指す。
// border/input は白の10%/15%アルファで、下地の明度に応じて自然に馴染むようにしている
// (shadcn原典が oklch(1 0 0 / 10%) というアルファ指定を使っているのをそのまま踏襲)。
val ShadcnDarkColorScheme: ColorScheme = darkColorScheme(
    // --primary / --primary-foreground
    primary = Color(0xFFE5E5E5),
    onPrimary = Color(0xFF171717),
    primaryContainer = Color(0xFF262626),
    onPrimaryContainer = Color(0xFFFAFAFA),
    // --secondary / --secondary-foreground
    secondary = Color(0xFFA1A1A1),
    onSecondary = Color(0xFF171717),
    secondaryContainer = Color(0xFF262626),
    onSecondaryContainer = Color(0xFFFAFAFA),
    // --accent / --accent-foreground（neutralテーマでは secondary と同値）
    tertiary = Color(0xFFA1A1A1),
    onTertiary = Color(0xFF171717),
    tertiaryContainer = Color(0xFF262626),
    onTertiaryContainer = Color(0xFFFAFAFA),
    // --background / --foreground
    background = Color(0xFF0A0A0A),
    onBackground = Color(0xFFFAFAFA),
    // --card / --card-foreground
    surface = Color(0xFF171717),
    onSurface = Color(0xFFFAFAFA),
    // --muted / --muted-foreground
    surfaceVariant = Color(0xFF262626),
    onSurfaceVariant = Color(0xFFA1A1A1),
    surfaceContainerLowest = Color(0xFF0A0A0A),
    surfaceContainerLow = Color(0xFF141414),
    surfaceContainer = Color(0xFF171717),
    surfaceContainerHigh = Color(0xFF1F1F1F),
    surfaceContainerHighest = Color(0xFF262626),
    inverseSurface = Color(0xFFFAFAFA),
    inverseOnSurface = Color(0xFF171717),
    inversePrimary = Color(0xFF171717),
    // --destructive
    error = Color(0xFFFF6467),
    onError = Color(0xFF171717),
    errorContainer = Color(0xFF82181A),
    onErrorContainer = Color(0xFFFFC9C9),
    // --border / --input / --ring
    outline = Color(0x26FFFFFF),
    outlineVariant = Color(0x1AFFFFFF),
    scrim = Color(0xFF000000),
)
