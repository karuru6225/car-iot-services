package info.karuru.cariot.ui.theme

import androidx.compose.material3.Typography
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.sp

// このデザインの核。計器盤は「極小のラベル」と「巨大な数値」の落差で出来ている。
// これまでの版はラベル11sp・数値22〜30spと差が小さく、結果として
// 「数値も出る設定アプリ」に見えていた。ここを最大の差に開く。
//
//   ラベル 10sp / letterSpacing +2.5sp（うんと小さく、うんと字間を空ける）
//   数値   60sp / letterSpacing -2sp  （うんと大きく、字間を詰める）
//
// 数値は等幅かつ細字。桁が変動しても位置が動かず、大きくしても圧迫感が出ない
// （アナログ計器の数字は太くない。太字は警告表示にだけ使う）。
val ClusterTypography: Typography = Typography(
    // ヒーロー数値（バッテリータブのメイン電圧など、その画面の主役ひとつ）
    displayLarge = TextStyle(
        fontFamily = FontFamily.Monospace,
        fontWeight = FontWeight.Light,
        fontSize = 60.sp,
        letterSpacing = (-2).sp,
    ),
    // 副次の大きい数値
    displayMedium = TextStyle(
        fontFamily = FontFamily.Monospace,
        fontWeight = FontWeight.Light,
        fontSize = 38.sp,
        letterSpacing = (-1).sp,
    ),
    // タイル内の数値
    displaySmall = TextStyle(
        fontFamily = FontFamily.Monospace,
        fontWeight = FontWeight.Normal,
        fontSize = 26.sp,
        letterSpacing = (-0.5).sp,
    ),
    // セクション見出し。字間を大きく開けた小さな見出しで、計器のパネル区画名に似せる
    titleMedium = TextStyle(
        fontFamily = FontFamily.SansSerif,
        fontWeight = FontWeight.Medium,
        fontSize = 11.sp,
        letterSpacing = 3.sp,
    ),
    // 計測項目名。極小・広字間で数値の引き立て役に徹する
    labelSmall = TextStyle(
        fontFamily = FontFamily.SansSerif,
        fontWeight = FontWeight.Medium,
        fontSize = 10.sp,
        letterSpacing = 2.5.sp,
    ),
    labelLarge = TextStyle(
        fontFamily = FontFamily.SansSerif,
        fontWeight = FontWeight.Medium,
        fontSize = 13.sp,
        letterSpacing = 0.5.sp,
    ),
    bodyLarge = TextStyle(
        fontFamily = FontFamily.SansSerif,
        fontWeight = FontWeight.Normal,
        fontSize = 14.sp,
        letterSpacing = 0.sp,
    ),
    bodyMedium = TextStyle(
        fontFamily = FontFamily.SansSerif,
        fontWeight = FontWeight.Normal,
        fontSize = 13.sp,
        letterSpacing = 0.sp,
    ),
)
