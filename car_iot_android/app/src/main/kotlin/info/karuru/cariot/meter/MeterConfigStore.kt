package info.karuru.cariot.meter

import android.content.Context
import info.karuru.cariot.obd.defaultMeterMetrics

private const val PREFS_NAME = "meter_prefs"
private const val KEY_SLOTS = "meter_slots_v1"

// メーター画面のタイル構成の永続化。mobile/lib/services/meter_config_service.dartの移植。
// JSON変換自体はMeterSlotJson(TDD済み)に任せ、ここはSharedPreferencesへの読み書きに徹する。
class MeterConfigStore(context: Context) {
  private val prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

  // キー未設定・デコード失敗・空リストのいずれもdefaultMeterMetricsにフォールバックする
  // （mobile/lib/services/meter_config_service.dartのload()と同じ挙動）。
  fun load(): List<MeterSlot> {
    val raw = prefs.getString(KEY_SLOTS, null) ?: return defaultSlots()
    val slots = MeterSlotJson.decode(raw) ?: return defaultSlots()
    return slots.ifEmpty { defaultSlots() }
  }

  fun save(slots: List<MeterSlot>) {
    prefs.edit().putString(KEY_SLOTS, MeterSlotJson.encode(slots)).apply()
  }

  private fun defaultSlots(): List<MeterSlot> = defaultMeterMetrics.map { (metric, style) ->
    MeterSlot(metric, style)
  }
}
