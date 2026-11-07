package info.karuru.cariot.auth

// AuthStore.getValidAccessToken()の結果。失敗を「再ログインが必要」と「一時的に取れない」に
// 分けるのは、前者だけをユーザーに知らせて再ログインを促すため（後者は次回リトライで回復する）。
sealed interface AccessTokenResult {
  data class Valid(val token: String) : AccessTokenResult

  // 未ログイン、またはrefresh_tokenが失効している
  data object SessionExpired : AccessTokenResult

  // Cognitoへの通信エラー等
  data object Unavailable : AccessTokenResult
}

// トークンエンドポイントのOAuthエラーコード(RFC 6749 5.2)のうち、refresh_tokenの失効・
// 取り消しを表すもの。CognitoはRefreshTokenValidityを過ぎたrefresh_tokenにinvalid_grantを返す。
fun isSessionExpiredError(oauthError: String?): Boolean = oauthError == "invalid_grant"
