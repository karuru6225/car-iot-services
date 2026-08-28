package info.karuru.cariot.ui.theme

import androidx.compose.material3.ColorScheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.ui.graphics.Color

// レーシング/モータースポーツ風テーマ。マット黒の背景にレーシングレッドのアクセント。
//
// 配色見直し(2026/08): darkColorScheme()はsurfaceContainer*/secondaryContainer等を
// 指定しないとMaterial3のベースライン紫にフォールバックする。これによりCard背景や
// 選択中のFilterChip/NavigationBarのインジケータが意図せず紫がかっていたため、
// 全ロールを黒×レッドの系統色で明示的に埋めた。secondaryContainerは彩度を落とした
// レッド系にし、プライマリの赤1色に視線が集中するよう選択状態は控えめにしている。
val RacingColorScheme: ColorScheme = darkColorScheme(
    primary = Color(0xFFE10600),
    onPrimary = Color(0xFFFFFFFF),
    primaryContainer = Color(0xFF3D0000),
    onPrimaryContainer = Color(0xFFFFB4A8),
    secondary = Color(0xFFB0B0B0),
    onSecondary = Color(0xFF000000),
    secondaryContainer = Color(0xFF2A1414),
    onSecondaryContainer = Color(0xFFFFB4A8),
    tertiary = Color(0xFFB0B0B0),
    onTertiary = Color(0xFF000000),
    tertiaryContainer = Color(0xFF2A1414),
    onTertiaryContainer = Color(0xFFFFB4A8),
    background = Color(0xFF121212),
    onBackground = Color(0xFFF5F5F5),
    surface = Color(0xFF1C1C1C),
    onSurface = Color(0xFFF5F5F5),
    surfaceVariant = Color(0xFF2A2A2A),
    onSurfaceVariant = Color(0xFFB0B0B0),
    surfaceContainerLowest = Color(0xFF0A0A0A),
    surfaceContainerLow = Color(0xFF1A1A1A),
    surfaceContainer = Color(0xFF202020),
    surfaceContainerHigh = Color(0xFF262626),
    surfaceContainerHighest = Color(0xFF303030),
    inverseSurface = Color(0xFFF5F5F5),
    inverseOnSurface = Color(0xFF1C1C1C),
    inversePrimary = Color(0xFFE10600),
    error = Color(0xFFFF6259),
    onError = Color(0xFF000000),
    outline = Color(0xFF4A4A4A),
    outlineVariant = Color(0xFF333333),
    scrim = Color(0xFF000000),
)
