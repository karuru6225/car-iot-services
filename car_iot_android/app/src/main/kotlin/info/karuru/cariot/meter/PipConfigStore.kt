package info.karuru.cariot.meter

import android.content.Context
import info.karuru.cariot.obd.GaugeStyle
import info.karuru.cariot.obd.ObdMetric

private const val PREFS_NAME = "pip_prefs"

// v1 は表示項目(ObdMetric)の配列だけを持っていた。ゲージ種別も選べるようにしたため
// MeterSlot の配列へ移行し、キーを変えている。旧キーは読まずデフォルトへフォールバック
// させる（PiPの表示項目という軽い設定で、移行コードを持つ価値がないため）。
private const val KEY_SLOTS = "pip_slots_v2"

// 既定はテキスト表示3項目。PiPウィンドウは小さく、初期状態で読めることを優先する。
private val defaultPipSlots = listOf(
    MeterSlot(ObdMetric.RPM, GaugeStyle.DIGITAL),
    MeterSlot(ObdMetric.SPEED_KMH, GaugeStyle.DIGITAL),
    MeterSlot(ObdMetric.COOLANT_C, GaugeStyle.DIGITAL),
)

// PiP表示項目の永続化。JSON変換はメータータブと同じ MeterSlotJson(TDD済み)を共用する。
// ここはSharedPreferencesへの読み書きに徹する（MeterConfigStore.ktと同じ設計）。
class PipConfigStore(context: Context) {
  private val prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

  // キー未設定・デコード失敗・空リストのいずれも既定値にフォールバックする。
  fun load(): List<MeterSlot> {
    val raw = prefs.getString(KEY_SLOTS, null) ?: return defaultPipSlots
    val slots = MeterSlotJson.decode(raw) ?: return defaultPipSlots
    return slots.ifEmpty { defaultPipSlots }
  }

  fun save(slots: List<MeterSlot>) {
    prefs.edit().putString(KEY_SLOTS, MeterSlotJson.encode(slots)).apply()
  }
}
