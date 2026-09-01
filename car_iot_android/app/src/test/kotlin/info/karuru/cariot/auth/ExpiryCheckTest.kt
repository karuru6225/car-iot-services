package info.karuru.cariot.auth

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

// テストリスト（mobile/lib/services/auth_service.dartのaccessToken getter、期限1分前
// マージンの判定ロジックの移植）
// - [ ] 期限まで十分猶予がある場合はtrueを返す
// - [ ] 期限まで1分未満はfalseを返す（更新すべき）
// - [ ] 期限を過ぎていたらfalseを返す
class ExpiryCheckTest {
  @Test
  fun `期限まで十分猶予がある場合はtrue`() {
    val now = 1_000_000L
    val expiresAt = now + 10 * 60_000 // 10分後
    assertTrue(isStillValid(expiresAt, now))
  }

  @Test
  fun `期限まで1分未満はfalse`() {
    val now = 1_000_000L
    val expiresAt = now + 30_000 // 30秒後
    assertFalse(isStillValid(expiresAt, now))
  }

  @Test
  fun `期限を過ぎていたらfalse`() {
    val now = 1_000_000L
    val expiresAt = now - 1
    assertFalse(isStillValid(expiresAt, now))
  }
}
