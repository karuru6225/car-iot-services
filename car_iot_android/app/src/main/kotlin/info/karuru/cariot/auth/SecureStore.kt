package info.karuru.cariot.auth

import android.content.Context
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Base64
import java.security.KeyStore
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

private const val KEYSTORE_PROVIDER = "AndroidKeyStore"
private const val KEY_ALIAS = "car_iot_auth_key"
private const val TRANSFORMATION = "AES/GCM/NoPadding"
private const val GCM_TAG_LENGTH_BITS = 128
private const val PREFS_NAME = "auth_secure_prefs"

// Android Keystore(AES/GCM)で値を暗号化してSharedPreferencesに保存する薄いラッパー。
// androidx.security:security-cryptoは2025年7月に全API非推奨化された（Google公式が
// Keystore直接利用への移行を推奨）ため使わず、自前実装する（docs/car_iot_android_plan.md参照）。
class SecureStore(context: Context) {
  private val prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)

  private fun getOrCreateKey(): SecretKey {
    val keyStore = KeyStore.getInstance(KEYSTORE_PROVIDER).apply { load(null) }
    (keyStore.getKey(KEY_ALIAS, null) as? SecretKey)?.let { return it }

    val keyGenerator = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, KEYSTORE_PROVIDER)
    val spec = KeyGenParameterSpec.Builder(
        KEY_ALIAS,
        KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT,
    )
        .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
        .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
        .build()
    keyGenerator.init(spec)
    return keyGenerator.generateKey()
  }

  fun putString(key: String, value: String) {
    val cipher = Cipher.getInstance(TRANSFORMATION).apply {
      init(Cipher.ENCRYPT_MODE, getOrCreateKey())
    }
    val encrypted = cipher.doFinal(value.toByteArray(Charsets.UTF_8))
    prefs.edit()
        .putString("$key.iv", Base64.encodeToString(cipher.iv, Base64.NO_WRAP))
        .putString("$key.data", Base64.encodeToString(encrypted, Base64.NO_WRAP))
        .apply()
  }

  fun getString(key: String): String? {
    val ivStr = prefs.getString("$key.iv", null) ?: return null
    val dataStr = prefs.getString("$key.data", null) ?: return null
    return try {
      val cipher = Cipher.getInstance(TRANSFORMATION).apply {
        val iv = Base64.decode(ivStr, Base64.NO_WRAP)
        init(Cipher.DECRYPT_MODE, getOrCreateKey(), GCMParameterSpec(GCM_TAG_LENGTH_BITS, iv))
      }
      String(cipher.doFinal(Base64.decode(dataStr, Base64.NO_WRAP)), Charsets.UTF_8)
    } catch (e: Exception) {
      null // 鍵ローテーション・データ破損等。呼び出し側は「保存されていない」として扱う
    }
  }

  fun clear() {
    prefs.edit().clear().apply()
  }
}
