package info.karuru.cariot.ui.theme

// ユーザーが切り替え可能な2種類のテーマ。どちらも車載インストルメントクラスターの
// 語彙に基づく配色で、NIGHT=夜間のメーター照明、DAY=日中の計器パネルに対応する。
// 詳細は ClusterColorSchemes.kt を参照。
enum class AppTheme(val label: String) {
  NIGHT("ナイト"),
  DAY("デイ"),
  // 針と目盛りのあるアナログ計器として描くテーマ。NIGHT/DAYとの違いは配色だけでなく
  // 表示形式そのもの（InstrumentStyle.ANALOG）にある。
  GAUGE("メーター"),
}
