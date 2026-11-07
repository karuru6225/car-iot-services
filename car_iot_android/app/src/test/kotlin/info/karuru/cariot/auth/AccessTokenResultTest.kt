package info.karuru.cariot.auth

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class AccessTokenResultTest {
  @Test
  fun `invalid_grantはセッション失効とみなす`() {
    assertTrue(isSessionExpiredError("invalid_grant"))
  }

  @Test
  fun `それ以外のエラーやエラー無しはセッション失効とみなさない`() {
    assertFalse(isSessionExpiredError("invalid_request"))
    assertFalse(isSessionExpiredError(null))
  }
}
