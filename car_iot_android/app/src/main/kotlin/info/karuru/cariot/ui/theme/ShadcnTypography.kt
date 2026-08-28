package info.karuru.cariot.ui.theme

import androidx.compose.material3.Typography
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.sp

// shadcn/ui のタイポグラフィ規約を Material3 の Typography にマッピングしたもの。
// 2テーマ(ライト/ダーク)で共通。shadcn の特徴は以下:
//   - 本文の既定が text-sm(14px)。Material3 の bodyLarge(16sp)より一段小さい
//   - ラベル・ボタンは font-medium(500)。太字を多用せず重さで階層を作る
//   - 見出しは tracking-tight(字間を詰める)
//   - 数値データは tabular-nums。Compose には直接の対応がないため、桁が揺れない
//     Monospace を数値表示ロール(displaySmall)に充てて同じ意図を満たす
val ShadcnTypography: Typography = Typography(
    // 計測値の大きな数値表示。等幅で桁位置が固定され、走行中に読んでも数字が踊らない。
    displaySmall = TextStyle(
        fontFamily = FontFamily.Monospace,
        fontWeight = FontWeight.Medium,
        fontSize = 30.sp,
        letterSpacing = (-0.5).sp,
    ),
    // カード見出し(shadcn の CardTitle: text-sm font-medium leading-none)
    titleMedium = TextStyle(
        fontFamily = FontFamily.SansSerif,
        fontWeight = FontWeight.Medium,
        fontSize = 14.sp,
        letterSpacing = (-0.2).sp,
    ),
    // 本文既定 text-sm
    bodyLarge = TextStyle(
        fontFamily = FontFamily.SansSerif,
        fontWeight = FontWeight.Normal,
        fontSize = 14.sp,
        letterSpacing = 0.sp,
    ),
    bodyMedium = TextStyle(
        fontFamily = FontFamily.SansSerif,
        fontWeight = FontWeight.Normal,
        fontSize = 14.sp,
        letterSpacing = 0.sp,
    ),
    // ボタンラベル(shadcn の Button: text-sm font-medium)
    labelLarge = TextStyle(
        fontFamily = FontFamily.SansSerif,
        fontWeight = FontWeight.Medium,
        fontSize = 14.sp,
        letterSpacing = 0.sp,
    ),
    // 補助ラベル(text-xs text-muted-foreground)
    labelSmall = TextStyle(
        fontFamily = FontFamily.SansSerif,
        fontWeight = FontWeight.Normal,
        fontSize = 12.sp,
        letterSpacing = 0.sp,
    ),
)
