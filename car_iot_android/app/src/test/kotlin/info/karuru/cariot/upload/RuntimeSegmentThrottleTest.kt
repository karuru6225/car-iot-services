package info.karuru.cariot.upload

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class RuntimeSegmentThrottleTest {
  @Test
  fun `空リストなら稼働時間は0`() {
    val result = RuntimeSegmentThrottle.overlappingRuntimeMs(emptyList(), windowStart = 0, now = 1000)
    assertEquals(0L, result)
  }

  @Test
  fun `ウィンドウ内に完全に収まる完了済みセグメントはその長さがそのまま計上される`() {
    val segments = listOf(RuntimeSegment(startTs = 100, endTs = 300))
    val result = RuntimeSegmentThrottle.overlappingRuntimeMs(segments, windowStart = 0, now = 1000)
    assertEquals(200L, result)
  }

  @Test
  fun `windowStartより前から始まるセグメントはウィンドウ内部分だけ計上される`() {
    val segments = listOf(RuntimeSegment(startTs = -500, endTs = 300))
    val result = RuntimeSegmentThrottle.overlappingRuntimeMs(segments, windowStart = 0, now = 1000)
    assertEquals(300L, result)
  }

  @Test
  fun `endTsがnull(実行中)のセグメントはnowまで稼働しているとみなす`() {
    val segments = listOf(RuntimeSegment(startTs = 400, endTs = null))
    val result = RuntimeSegmentThrottle.overlappingRuntimeMs(segments, windowStart = 0, now = 1000)
    assertEquals(600L, result)
  }

  @Test
  fun `複数セグメントは合計される`() {
    val segments = listOf(
        RuntimeSegment(startTs = 0, endTs = 100),
        RuntimeSegment(startTs = 200, endTs = 350),
    )
    val result = RuntimeSegmentThrottle.overlappingRuntimeMs(segments, windowStart = 0, now = 1000)
    assertEquals(250L, result)
  }

  @Test
  fun `アップロード側稼働がBLE側稼働の25パーセント以上ならスキップする`() {
    assertTrue(RuntimeSegmentThrottle.shouldSkipUpload(uploadRuntimeMs = 250, foregroundRuntimeMs = 1000))
  }

  @Test
  fun `アップロード側稼働が25パーセント未満ならスキップしない`() {
    assertFalse(RuntimeSegmentThrottle.shouldSkipUpload(uploadRuntimeMs = 100, foregroundRuntimeMs = 1000))
  }
}
