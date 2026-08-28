package info.karuru.cariot.meter

import android.content.Context
import info.karuru.cariot.obd.ObdMetric
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json

// PiP(ピクチャーインピクチャー)表示に使う項目名のJSON変換ロジック。
// ObdMetric自体は@Serializableではないため、名前(String)の配列として保存する
// （項目名変更・削除への耐性のため、存在しない名前はデコード時に無視する）。
object PipMetricsJson {
  private val json = Json { ignoreUnknownKeys = true }

  fun encode(metrics: List<ObdMetric>): String = json.encodeToString(metrics.map { it.name })

  // 壊れたJSONはnullを返す（呼び出し側でデフォルト値へのフォールバックを判断する）。
  fun decode(raw: String): List<ObdMetric>? {
    return try {
      val names: List<String> = json.decodeFromString(raw)
      names.mapNotNull { name -> ObdMetric.entries.find { it.name == name } }
    } catch (e: Exception) {
      null
    }
  }
}

private const val PREFS_NAME = "pip_prefs"
private const val KEY_METRICS = "pip_metrics_v1"

private val defaultPipMetrics = listOf(ObdMetric.RPM, ObdMetric.SPEED_KMH, ObdMetric.COOLANT_C)

// PiP表示項目の永続化。JSON変換自体はPipMetricsJson(TDD済み)に任せ、
// ここはSharedPreferencesへの読み書きに徹する（MeterConfigStore.ktと同じ設計）。
class PipConfigStore(context: Context) {
  private val prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

  // キー未設定・デコード失敗・空リストのいずれもdefaultPipMetricsにフォールバックする。
  fun load(): List<ObdMetric> {
    val raw = prefs.getString(KEY_METRICS, null) ?: return defaultPipMetrics
    val metrics = PipMetricsJson.decode(raw) ?: return defaultPipMetrics
    return metrics.ifEmpty { defaultPipMetrics }
  }

  fun save(metrics: List<ObdMetric>) {
    prefs.edit().putString(KEY_METRICS, PipMetricsJson.encode(metrics)).apply()
  }
}
