package info.karuru.cariot.ui.theme

// ユーザーが切り替え可能な2種類のテーマ。shadcn/ui の neutral ベーステーマを
// Material3 に移植したもので、LIGHT=:root 定義、DARK=.dark 定義に対応する。
// 配色・タイポ・角丸の詳細は Shadcn*ColorScheme.kt / ShadcnTypography.kt /
// ShadcnShapes.kt を参照。
enum class AppTheme(val label: String) {
  LIGHT("ライト"),
  DARK("ダーク"),
}
