package info.karuru.cariot.ui.theme

import androidx.compose.material3.ColorScheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.ui.graphics.Color

// 車載インストルメントクラスター(計器盤)の語彙を借りた2つの配色。
//
// なぜこの色か:
//   アクセントに「暖色アンバー」を選んでいるのは、夜間の自動車メーターの照明が
//   実際にこの色域だから(VDO/Smiths以来のアナログ計器のバックライト、現在も
//   多くの車種が踏襲している)。走行中に見る画面という被写体そのものの語彙であり、
//   かつ「黒背景＋鮮やかなライムグリーン/バーミリオン1色」という、いま生成される
//   UIが揃って落ちる配色を避けられる。
//   NIGHT の地色を純黒(#000)ではなく青みを含んだ #06080C にしているのも計器由来で、
//   クラスターはガラス越しに見えるぶん実機より冷たく沈んで見えるため。
//
//   DAY はその昼側。紙のようなクリーム系ではなく、青寄りの明るいグレーにして
//   「日中でも読める計器パネル」に寄せた。アクセントは同じアンバーを暗く落とした
//   #B26A00 で、2テーマが同じ製品だと分かる連続性を持たせている。

val ClusterNightColorScheme: ColorScheme = darkColorScheme(
    primary = Color(0xFFFFB23F),
    onPrimary = Color(0xFF1A1200),
    primaryContainer = Color(0xFF2A1E08),
    onPrimaryContainer = Color(0xFFFFD9A0),
    secondary = Color(0xFF8B97A8),
    onSecondary = Color(0xFF06080C),
    secondaryContainer = Color(0xFF1C2430),
    onSecondaryContainer = Color(0xFFE8EDF4),
    tertiary = Color(0xFF8B97A8),
    onTertiary = Color(0xFF06080C),
    tertiaryContainer = Color(0xFF1C2430),
    onTertiaryContainer = Color(0xFFE8EDF4),
    background = Color(0xFF06080C),
    onBackground = Color(0xFFE8EDF4),
    surface = Color(0xFF0E1218),
    onSurface = Color(0xFFE8EDF4),
    surfaceVariant = Color(0xFF1C2430),
    onSurfaceVariant = Color(0xFF6B7789),
    surfaceContainerLowest = Color(0xFF04060A),
    surfaceContainerLow = Color(0xFF0A0E14),
    surfaceContainer = Color(0xFF0E1218),
    surfaceContainerHigh = Color(0xFF121821),
    surfaceContainerHighest = Color(0xFF18202B),
    inverseSurface = Color(0xFFE8EDF4),
    inverseOnSurface = Color(0xFF0E1218),
    inversePrimary = Color(0xFFB26A00),
    error = Color(0xFFFF5C4D),
    onError = Color(0xFF1A0300),
    errorContainer = Color(0xFF3D0F0A),
    onErrorContainer = Color(0xFFFFB4A8),
    outline = Color(0xFF232C3A),
    outlineVariant = Color(0xFF161D27),
    scrim = Color(0xFF000000),
)

val ClusterDayColorScheme: ColorScheme = lightColorScheme(
    primary = Color(0xFFB26A00),
    onPrimary = Color(0xFFFFFFFF),
    primaryContainer = Color(0xFFFFE3BC),
    onPrimaryContainer = Color(0xFF2A1900),
    secondary = Color(0xFF5B6675),
    onSecondary = Color(0xFFFFFFFF),
    secondaryContainer = Color(0xFFDDE3EA),
    onSecondaryContainer = Color(0xFF0E1218),
    tertiary = Color(0xFF5B6675),
    onTertiary = Color(0xFFFFFFFF),
    tertiaryContainer = Color(0xFFDDE3EA),
    onTertiaryContainer = Color(0xFF0E1218),
    background = Color(0xFFEEF1F4),
    onBackground = Color(0xFF0E1218),
    surface = Color(0xFFF8FAFB),
    onSurface = Color(0xFF0E1218),
    surfaceVariant = Color(0xFFDDE3EA),
    onSurfaceVariant = Color(0xFF5B6675),
    surfaceContainerLowest = Color(0xFFFFFFFF),
    surfaceContainerLow = Color(0xFFF8FAFB),
    surfaceContainer = Color(0xFFE7EBF0),
    surfaceContainerHigh = Color(0xFFE0E5EC),
    surfaceContainerHighest = Color(0xFFD8DEE6),
    inverseSurface = Color(0xFF0E1218),
    inverseOnSurface = Color(0xFFEEF1F4),
    inversePrimary = Color(0xFFFFB23F),
    error = Color(0xFFC3281C),
    onError = Color(0xFFFFFFFF),
    errorContainer = Color(0xFFFFDAD5),
    onErrorContainer = Color(0xFF410100),
    outline = Color(0xFFC8D0DA),
    outlineVariant = Color(0xFFDDE3EA),
    scrim = Color(0xFF000000),
)
