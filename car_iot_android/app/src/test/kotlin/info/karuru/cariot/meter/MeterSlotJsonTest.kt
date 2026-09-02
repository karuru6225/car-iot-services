package info.karuru.cariot.meter

import info.karuru.cariot.obd.GaugeStyle
import info.karuru.cariot.obd.ObdMetric
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class MeterSlotJsonTest {
  @Test
  fun `単一スロットをエンコードしてデコードすると元に戻る`() {
    val slots = listOf(MeterSlot(ObdMetric.RPM, GaugeStyle.CIRCULAR))
    val decoded = MeterSlotJson.decode(MeterSlotJson.encode(slots))
    assertEquals(slots, decoded)
  }

  @Test
  fun `複数スロットをエンコードしてデコードすると順序も含めて元に戻る`() {
    val slots = listOf(
        MeterSlot(ObdMetric.RPM, GaugeStyle.CIRCULAR),
        MeterSlot(ObdMetric.SPEED_KMH, GaugeStyle.DIGITAL),
        MeterSlot(ObdMetric.COOLANT_C, GaugeStyle.BAR),
    )
    val decoded = MeterSlotJson.decode(MeterSlotJson.encode(slots))
    assertEquals(slots, decoded)
  }

  @Test
  fun `空リストをエンコードしてデコードすると空リストのまま`() {
    val decoded = MeterSlotJson.decode(MeterSlotJson.encode(emptyList()))
    assertEquals(emptyList<MeterSlot>(), decoded)
  }

  @Test
  fun `不正なJSON文字列はnullを返す`() {
    assertNull(MeterSlotJson.decode("not valid json"))
  }
}
