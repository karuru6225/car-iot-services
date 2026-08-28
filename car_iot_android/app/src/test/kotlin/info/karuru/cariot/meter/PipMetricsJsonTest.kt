package info.karuru.cariot.meter

import info.karuru.cariot.obd.ObdMetric
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class PipMetricsJsonTest {
  @Test
  fun `単一項目をエンコードしてデコードすると元に戻る`() {
    val metrics = listOf(ObdMetric.RPM)
    val decoded = PipMetricsJson.decode(PipMetricsJson.encode(metrics))
    assertEquals(metrics, decoded)
  }

  @Test
  fun `複数項目をエンコードしてデコードすると順序も含めて元に戻る`() {
    val metrics = listOf(ObdMetric.RPM, ObdMetric.SPEED_KMH, ObdMetric.COOLANT_C)
    val decoded = PipMetricsJson.decode(PipMetricsJson.encode(metrics))
    assertEquals(metrics, decoded)
  }

  @Test
  fun `空リストをエンコードしてデコードすると空リストのまま`() {
    val decoded = PipMetricsJson.decode(PipMetricsJson.encode(emptyList()))
    assertEquals(emptyList<ObdMetric>(), decoded)
  }

  @Test
  fun `不正なJSON文字列はnullを返す`() {
    assertNull(PipMetricsJson.decode("not valid json"))
  }

  @Test
  fun `存在しない項目名は無視して残りをデコードする`() {
    val decoded = PipMetricsJson.decode("""["RPM","NO_SUCH_METRIC","SPEED_KMH"]""")
    assertEquals(listOf(ObdMetric.RPM, ObdMetric.SPEED_KMH), decoded)
  }
}
