package info.karuru.cariot.auth

import java.util.Base64
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.jsonPrimitive

// IDトークン(JWT)のpayloadからemailクレームのみ取り出す（表示用途、署名検証はしない。
// API呼び出しの正当性検証はAPI Gateway側のJWT Authorizerが行うため問題ない）。
// mobile/lib/services/auth_service.dartの_emailFromIdToken()の移植。
// org.json.JSONObjectはAndroid Unit Test環境でstub化され動かないためkotlinx.serializationを使う。
fun emailFromIdToken(idToken: String?): String? {
  if (idToken == null) return null
  val parts = idToken.split(".")
  if (parts.size != 3) return null
  return try {
    val payload = String(Base64.getUrlDecoder().decode(parts[1]))
    val claims = Json.parseToJsonElement(payload) as? JsonObject ?: return null
    claims["email"]?.jsonPrimitive?.content
  } catch (e: Exception) {
    null
  }
}
