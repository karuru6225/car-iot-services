package info.karuru.cariot.ui.theme

import androidx.compose.material3.ColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.ui.graphics.Color

// ミニマル・モノクロームテーマ。白基調に黄緑のワンポイントカラーのみ、装飾は最小限。
//
// 配色見直し(2026/08): lightColorScheme()はsurfaceContainer*/secondaryContainer等を
// 指定しないとMaterial3のベースライン紫にフォールバックする。これによりCard背景や
// 選択中のFilterChip/NavigationBarのインジケータが意図せず紫がかっていたため、
// 全ロールをグレースケール系統で明示的に埋めた。secondaryContainerも紫ではなく
// 中立なグレーにすることで、黄緑は「接続」ボタン等ごく一部だけに絞る
// "ワンポイント"という設計意図をUIの選択状態にも一貫させている。
val MinimalColorScheme: ColorScheme = lightColorScheme(
    primary = Color(0xFFC6FF00),
    onPrimary = Color(0xFF111318),
    primaryContainer = Color(0xFFE8FFAD),
    onPrimaryContainer = Color(0xFF1B2200),
    secondary = Color(0xFF5C5C5C),
    onSecondary = Color(0xFFFFFFFF),
    secondaryContainer = Color(0xFFECECEC),
    onSecondaryContainer = Color(0xFF111318),
    tertiary = Color(0xFF5C5C5C),
    onTertiary = Color(0xFFFFFFFF),
    tertiaryContainer = Color(0xFFECECEC),
    onTertiaryContainer = Color(0xFF111318),
    background = Color(0xFFFFFFFF),
    onBackground = Color(0xFF111318),
    surface = Color(0xFFF5F5F5),
    onSurface = Color(0xFF111318),
    surfaceVariant = Color(0xFFE8E8E8),
    onSurfaceVariant = Color(0xFF5C5C5C),
    surfaceContainerLowest = Color(0xFFFFFFFF),
    surfaceContainerLow = Color(0xFFF2F2F2),
    surfaceContainer = Color(0xFFEFEFEF),
    surfaceContainerHigh = Color(0xFFE5E5E5),
    surfaceContainerHighest = Color(0xFFDCDCDC),
    inverseSurface = Color(0xFF2B2B2B),
    inverseOnSurface = Color(0xFFF5F5F5),
    inversePrimary = Color(0xFFC6FF00),
    error = Color(0xFFCC0000),
    onError = Color(0xFFFFFFFF),
    outline = Color(0xFFD0D0D0),
    outlineVariant = Color(0xFFE5E5E5),
    scrim = Color(0xFF000000),
)
