package info.karuru.cariot.auth

import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.net.Uri
import info.karuru.cariot.AppConfig
import info.karuru.cariot.MainActivity
import net.openid.appauth.AuthorizationRequest
import net.openid.appauth.AuthorizationService
import net.openid.appauth.ResponseTypeValues

// Cognito App Client（infra/mobile_auth.tf）のcallback_urlsと完全一致させること。
// URIスキームはRFC3986でアンダースコア不可のためapplicationId（info.karuru.cariot）とは別文字列
// （mobile/android版と同じスキームを使い回す、docs/car_iot_android_plan.md参照）。
val OAUTH_REDIRECT_URI: Uri = Uri.parse("info.karuru.cariotmobile://oauthredirect")

// サインイン開始（Custom Tabsを開くための認可リクエスト送出）を担当。Activity起点でのみ使う
// （AuthStoreと違いContextだけでは完結しない、ブラウザUIを開くため）。
// mobile/lib/services/auth_service.dartのsignIn()に相当。
//
// AppAuthのPendingIntentベースのAPI(performAuthorizationRequest)を使う。認可フロー完了時は
// OAuthRedirectActivity→AuthorizationManagementActivityを経由してAuthResultActivityが
// 起動される（そちらでトークン交換・CarIotStateへの保存を行う）。
class AuthLoginFlow(private val context: Context) {
  suspend fun startSignIn() {
    val config = fetchCognitoServiceConfig() ?: return
    val request = AuthorizationRequest.Builder(
        config,
        AppConfig.COGNITO_CLIENT_ID,
        ResponseTypeValues.CODE,
        OAUTH_REDIRECT_URI,
    )
        .setScopes("openid", "email", "profile")
        // Google選択画面を省略してGoogleフェデレーテッドログインへ直行する（mobile版と同じ）
        .setAdditionalParameters(mapOf("identity_provider" to "Google"))
        .build()

    val authService = AuthorizationService(context)
    val completedIntent = PendingIntent.getActivity(
        context,
        0,
        Intent(context, AuthResultActivity::class.java),
        PendingIntent.FLAG_MUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
    )
    // キャンセル時（ユーザーがブラウザを閉じた等）はMainActivityへそのまま戻る
    val canceledIntent = PendingIntent.getActivity(
        context,
        0,
        Intent(context, MainActivity::class.java),
        PendingIntent.FLAG_MUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
    )
    authService.performAuthorizationRequest(request, completedIntent, canceledIntent)
  }
}
