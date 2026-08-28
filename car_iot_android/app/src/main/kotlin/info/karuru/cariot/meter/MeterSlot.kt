package info.karuru.cariot.meter

import info.karuru.cariot.obd.GaugeStyle
import info.karuru.cariot.obd.ObdMetric
import kotlinx.serialization.Serializable
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json

// メーター画面の1タイル（表示項目＋ゲージ種別）。mobile/lib/models/meter_slot.dart相当。
@Serializable
data class MeterSlot(val metric: ObdMetric, val style: GaugeStyle)

// MeterConfigStore(永続化)・インポート/エクスポートの両方から使うJSON変換ロジック。
// 永続化層から切り離すことでテスト容易にする（upload/ObdUploader.ktのbuildRequestBodyと
// 同じ設計判断、Phase8後半）。
object MeterSlotJson {
  private val json = Json { ignoreUnknownKeys = true }

  fun encode(slots: List<MeterSlot>): String = json.encodeToString(slots)

  // 壊れたJSONはnullを返す（呼び出し側でデフォルト値へのフォールバックを判断する）。
  fun decode(raw: String): List<MeterSlot>? {
    return try {
      json.decodeFromString(raw)
    } catch (e: Exception) {
      null
    }
  }
}
