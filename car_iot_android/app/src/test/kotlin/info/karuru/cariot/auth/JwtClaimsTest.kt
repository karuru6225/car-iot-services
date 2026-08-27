package info.karuru.cariot.auth

import java.util.Base64
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

// テストリスト（mobile/lib/services/auth_service.dartの_emailFromIdToken()の移植。
// 署名検証はしない表示用途、API呼び出しの正当性検証はAPI Gateway側のJWT Authorizerが行う）
// - [ ] 3パート(header.payload.signature)でない文字列はnullを返す
// - [ ] 正常なJWTからemailクレームを取り出せる
// - [ ] emailクレームが無いJWTはnullを返す
// - [ ] nullを渡したらnullを返す
class JwtClaimsTest {
  private fun fakeJwt(payloadJson: String): String {
    val encode = { s: String -> Base64.getUrlEncoder().withoutPadding().encodeToString(s.toByteArray()) }
    return "${encode("{}")}.${encode(payloadJson)}.signature"
  }

  @Test
  fun `3パートでない文字列はnullを返す`() {
    assertNull(emailFromIdToken("not.a.valid.jwt.with.too.many.parts"))
  }

  @Test
  fun `正常なJWTからemailクレームを取り出せる`() {
    val jwt = fakeJwt("""{"email":"user@example.com","sub":"abc"}""")
    assertEquals("user@example.com", emailFromIdToken(jwt))
  }

  @Test
  fun `emailクレームが無いJWTはnullを返す`() {
    val jwt = fakeJwt("""{"sub":"abc"}""")
    assertNull(emailFromIdToken(jwt))
  }

  @Test
  fun `nullを渡したらnullを返す`() {
    assertNull(emailFromIdToken(null))
  }
}
