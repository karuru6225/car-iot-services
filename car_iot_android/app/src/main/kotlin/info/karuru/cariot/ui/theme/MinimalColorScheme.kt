package info.karuru.cariot.ui.theme

import androidx.compose.material3.ColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.ui.graphics.Color

// ミニマル・モノクロームテーマ。白基調に黄緑のワンポイントカラーのみ、装飾は最小限。
val MinimalColorScheme: ColorScheme = lightColorScheme(
    primary = Color(0xFFC6FF00),
    onPrimary = Color(0xFF111318),
    primaryContainer = Color(0xFFE8FFAD),
    onPrimaryContainer = Color(0xFF1B2200),
    secondary = Color(0xFF5C5C5C),
    onSecondary = Color(0xFFFFFFFF),
    background = Color(0xFFFFFFFF),
    onBackground = Color(0xFF111318),
    surface = Color(0xFFF5F5F5),
    onSurface = Color(0xFF111318),
    surfaceVariant = Color(0xFFE8E8E8),
    onSurfaceVariant = Color(0xFF5C5C5C),
    error = Color(0xFFCC0000),
    onError = Color(0xFFFFFFFF),
    outline = Color(0xFFD0D0D0),
)
