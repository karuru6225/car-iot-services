package info.karuru.cariot.ui.theme

import androidx.compose.material3.ColorScheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.ui.graphics.Color

// レーシング/モータースポーツ風テーマ。マット黒の背景にレーシングレッドのアクセント。
val RacingColorScheme: ColorScheme = darkColorScheme(
    primary = Color(0xFFE10600),
    onPrimary = Color(0xFFFFFFFF),
    primaryContainer = Color(0xFF3D0000),
    onPrimaryContainer = Color(0xFFFFB4A8),
    secondary = Color(0xFFB0B0B0),
    onSecondary = Color(0xFF000000),
    background = Color(0xFF121212),
    onBackground = Color(0xFFF5F5F5),
    surface = Color(0xFF1C1C1C),
    onSurface = Color(0xFFF5F5F5),
    surfaceVariant = Color(0xFF2A2A2A),
    onSurfaceVariant = Color(0xFFB0B0B0),
    error = Color(0xFFFF6259),
    onError = Color(0xFF000000),
    outline = Color(0xFF4A4A4A),
)
