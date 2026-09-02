package info.karuru.cariot.ui.theme

import androidx.compose.runtime.staticCompositionLocalOf

// テーマが「配色」だけでなく「計器の描かれ方」も切り替えるための指定。
//
// NIGHT/DAY は数値を主役にした平面的な表示（細いレールとアークだけで位置を示す）。
// GAUGE は針と目盛りのあるアナログダイヤルとして描く。
//
// メーター設定でユーザーが選ぶゲージ種別(CIRCULAR/DIGITAL/BAR/SPARKLINE)は据え置きで、
// 「CIRCULARやBARがどう描かれるか」だけがテーマ側で変わる。種別の選択が無意味に
// ならないようにするための切り分け。
enum class InstrumentStyle {
  // 平面表示。数値が主役で、レンジ内の位置はヘアライン＋ティックだけで示す。
  FLAT,

  // アナログ計器表示。主目盛り・副目盛り・目盛りの数字・針・ハブを描く。
  ANALOG,
}

val LocalInstrumentStyle = staticCompositionLocalOf { InstrumentStyle.FLAT }
