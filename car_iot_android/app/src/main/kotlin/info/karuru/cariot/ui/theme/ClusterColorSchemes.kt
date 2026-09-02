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
    onSurfaceVariant = Color(0xFF838EA0),
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
    outline = Color(0xFF5C6E8C),
    outlineVariant = Color(0xFF161D27),
    scrim = Color(0xFF000000),
)

// 3つ目のテーマ。古典的なアナログ計器の語彙——黒い文字盤・白い刻印・赤い針——をそのまま採る。
//
// NIGHT も暗色だが、あちらは「暖色アンバーで照らされたガラス越しの表示」で、
// こちらは「白で刻印された文字盤に赤い針が乗っている」。地色も NIGHT の青黒(#06080C)に対し
// 中性の黒にして、ガラスではなく成形樹脂のパネルらしい質感に寄せている。
// 配色以上に差が出るのは形の方で、GAUGE だけ InstrumentStyle.ANALOG になり
// 主目盛り・副目盛り・目盛りの数字・針を持つダイヤルとして描かれる。
//
// primary は針の赤。アクセント色は図形(針・ティック)と塗りボタンに限定し、文字には
// 使わない(WCAGの文字コントラストを満たせないのと、実際の計器も刻印は白で色が乗るのは針だけ)。
val GaugePanelColorScheme: ColorScheme = darkColorScheme(
    primary = Color(0xFFDF2E25),
    onPrimary = Color(0xFFFFFFFF),
    primaryContainer = Color(0xFF3A0F0C),
    onPrimaryContainer = Color(0xFFFFC9C4),
    secondary = Color(0xFF83838A),
    onSecondary = Color(0xFF0E0E10),
    secondaryContainer = Color(0xFF26262A),
    onSecondaryContainer = Color(0xFFEDEDE8),
    tertiary = Color(0xFF83838A),
    onTertiary = Color(0xFF0E0E10),
    tertiaryContainer = Color(0xFF26262A),
    onTertiaryContainer = Color(0xFFEDEDE8),
    background = Color(0xFF0E0E10),
    onBackground = Color(0xFFEDEDE8),
    surface = Color(0xFF17171A),
    onSurface = Color(0xFFEDEDE8),
    surfaceVariant = Color(0xFF26262A),
    // 目盛りの刻印に使う色。文字盤の白より一段落とし、数字が主張しすぎないようにする。
    onSurfaceVariant = Color(0xFF91919A),
    surfaceContainerLowest = Color(0xFF08080A),
    surfaceContainerLow = Color(0xFF121214),
    surfaceContainer = Color(0xFF17171A),
    surfaceContainerHigh = Color(0xFF1E1E22),
    surfaceContainerHighest = Color(0xFF26262A),
    inverseSurface = Color(0xFFEDEDE8),
    inverseOnSurface = Color(0xFF17171A),
    inversePrimary = Color(0xFFE0342B),
    error = Color(0xFFFF7A6E),
    onError = Color(0xFF1A0300),
    errorContainer = Color(0xFF3A0F0C),
    onErrorContainer = Color(0xFFFFC9C4),
    // ベゼル・目盛りの基準線。金属リングを思わせる中間グレー。
    outline = Color(0xFF74747F),
    outlineVariant = Color(0xFF212126),
    scrim = Color(0xFF000000),
)

val ClusterDayColorScheme: ColorScheme = lightColorScheme(
    primary = Color(0xFFA96500),
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
    onSurfaceVariant = Color(0xFF545F6D),
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
    // ValueRail の軌道線に使う色。明るい地の上で 1dp のヘアラインを引くため、
    // 薄いグレー(#C8D0DA)では沈んで見えなかったので濃い灰色まで落としている。
    // outlineVariant(カードの枠)は薄いままにして、線の主張は計器側だけに寄せる。
    outline = Color(0xFF6B7480),
    outlineVariant = Color(0xFFDDE3EA),
    scrim = Color(0xFF000000),
)
