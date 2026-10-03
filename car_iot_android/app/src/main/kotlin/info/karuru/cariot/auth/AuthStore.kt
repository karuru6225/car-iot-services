package info.karuru.cariot.auth

import android.content.Context
import info.karuru.cariot.AppConfig
import kotlinx.coroutines.suspendCancellableCoroutine
import net.openid.appauth.AuthorizationService
import net.openid.appauth.GrantTypeValues
import net.openid.appauth.TokenRequest

private const val KEY_ACCESS_TOKEN = "access_token"
private const val KEY_ID_TOKEN = "id_token"
private const val KEY_REFRESH_TOKEN = "refresh_token"
private const val KEY_EXPIRES_AT = "expires_at"
private const val KEY_EMAIL = "email"

// アクセストークンの読み書き・リフレッシュを担当する。Contextさえあれば呼べるため
// Activity不要（CarIotForegroundServiceからのアップロード時にも使う想定、Phase5）。
// mobile/lib/services/auth_service.dartのAuthServiceに相当。
class AuthStore(private val context: Context) {
  private val secureStore = SecureStore(context)

  fun saveTokens(
      accessToken: String?,
      idToken: String?,
      refreshToken: String?,
      expiresAtEpochMillis: Long?,
      email: String?,
  ) {
    accessToken?.let { secureStore.putString(KEY_ACCESS_TOKEN, it) }
    idToken?.let { secureStore.putString(KEY_ID_TOKEN, it) }
    refreshToken?.let { secureStore.putString(KEY_REFRESH_TOKEN, it) }
    expiresAtEpochMillis?.let { secureStore.putString(KEY_EXPIRES_AT, it.toString()) }
    email?.let { secureStore.putString(KEY_EMAIL, it) }
  }

  fun getEmail(): String? = secureStore.getString(KEY_EMAIL)

  // 起動時のセッション復元判定用（mobile版のtryRestoreSession()相当）
  fun hasRefreshToken(): Boolean = secureStore.getString(KEY_REFRESH_TOKEN) != null

  fun clear() = secureStore.clear()

  // 期限内ならキャッシュ済みaccess_tokenをそのまま返し、切れていればrefresh_tokenで
  // リフレッシュしてから返す。
  suspend fun getValidAccessToken(): AccessTokenResult {
    val cachedToken = secureStore.getString(KEY_ACCESS_TOKEN)
    val expiresAt = secureStore.getString(KEY_EXPIRES_AT)?.toLongOrNull()
    if (cachedToken != null && expiresAt != null && isStillValid(expiresAt, System.currentTimeMillis())) {
      return AccessTokenResult.Valid(cachedToken)
    }
    return refresh()
  }

  private suspend fun refresh(): AccessTokenResult {
    val refreshToken = secureStore.getString(KEY_REFRESH_TOKEN) ?: return AccessTokenResult.SessionExpired
    val config = fetchCognitoServiceConfig() ?: return AccessTokenResult.Unavailable
    val request = TokenRequest.Builder(config, AppConfig.COGNITO_CLIENT_ID)
        .setGrantType(GrantTypeValues.REFRESH_TOKEN)
        .setRefreshToken(refreshToken)
        .setScopes("openid", "email", "profile")
        .build()

    val authService = AuthorizationService(context)
    return try {
      suspendCancellableCoroutine { cont ->
        authService.performTokenRequest(request) { response, ex ->
          val accessToken = response?.accessToken
          if (accessToken != null) {
            saveTokens(
                accessToken = accessToken,
                idToken = response.idToken,
                refreshToken = response.refreshToken ?: refreshToken,
                expiresAtEpochMillis = response.accessTokenExpirationTime,
                email = getEmail(),
            )
            cont.resumeWith(Result.success(AccessTokenResult.Valid(accessToken)))
          } else if (isSessionExpiredError(ex?.error)) {
            // refresh_token自体が失効している。再ログインしない限り回復しない
            cont.resumeWith(Result.success(AccessTokenResult.SessionExpired))
          } else {
            // 通信エラー等の一時的な失敗。refresh_tokenはまだ使える可能性がある
            cont.resumeWith(Result.success(AccessTokenResult.Unavailable))
          }
        }
      }
    } finally {
      authService.dispose()
    }
  }
}
