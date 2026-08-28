package info.karuru.cariot.ui.theme

import android.content.Context

private const val PREFS_NAME = "theme_prefs"
private const val KEY_THEME = "app_theme_v1"

// テーマ選択の永続化。meter/MeterConfigStore.ktと同じSharedPreferencesパターン。
// 旧テーマ(RACING/MINIMAL)を保存済みの端末では valueOf が例外を投げるが、
// catch 節が既定値へフォールバックするため移行処理は不要。
class ThemeStore(context: Context) {
  private val prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

  fun load(): AppTheme {
    val raw = prefs.getString(KEY_THEME, null) ?: return AppTheme.DARK
    return try {
      AppTheme.valueOf(raw)
    } catch (e: IllegalArgumentException) {
      AppTheme.DARK
    }
  }

  fun save(theme: AppTheme) {
    prefs.edit().putString(KEY_THEME, theme.name).apply()
  }
}
