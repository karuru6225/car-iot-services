package info.karuru.cariot.auth

import android.app.Activity
import android.os.Bundle
import info.karuru.cariot.state.CarIotState
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.suspendCancellableCoroutine
import net.openid.appauth.AuthorizationException
import net.openid.appauth.AuthorizationResponse
import net.openid.appauth.AuthorizationService
import net.openid.appauth.TokenResponse

// AuthLoginFlowがAuthorizationService.performAuthorizationRequest()のcompletedIntentとして
// 指定するActivity。AuthorizationManagementActivityが認可フロー完了後にここへ結果を渡す
// （OAuthRedirectActivity→AuthorizationManagementActivity→ここ、という経路）。
// 認可コード→トークン交換までをここで完結させ、AuthStoreへ保存してからUIへ戻る。
class AuthResultActivity : Activity() {
  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)

    val response = AuthorizationResponse.fromIntent(intent)
    val exception = AuthorizationException.fromIntent(intent)
    if (response == null || exception != null) {
      finish()
      return
    }

    val authService = AuthorizationService(applicationContext)
    val authStore = AuthStore(applicationContext)

    CoroutineScope(Dispatchers.Main).launch {
      val tokenResponse = suspendCancellableCoroutine<TokenResponse?> { cont ->
        authService.performTokenRequest(response.createTokenExchangeRequest()) { resp, _ ->
          cont.resumeWith(Result.success(resp))
        }
      }
      if (tokenResponse != null) {
        val email = emailFromIdToken(tokenResponse.idToken)
        authStore.saveTokens(
            accessToken = tokenResponse.accessToken,
            idToken = tokenResponse.idToken,
            refreshToken = tokenResponse.refreshToken,
            expiresAtEpochMillis = tokenResponse.accessTokenExpirationTime,
            email = email,
        )
        CarIotState.setUserEmail(email)
      }
      authService.dispose()
      finish()
    }
  }
}
