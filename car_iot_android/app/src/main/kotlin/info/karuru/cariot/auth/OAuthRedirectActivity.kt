package info.karuru.cariot.auth

import android.app.Activity
import android.os.Bundle
import net.openid.appauth.AuthorizationManagementActivity

// info.karuru.cariotmobile://oauthredirect を受けるActivity（AndroidManifestのintent-filter参照）。
// AppAuthの設計上、このActivityは受け取ったリダイレクトURIをAuthorizationManagementActivityへ
// 転送するだけの薄い中継役に徹する必要がある（net.openid.appauth.RedirectUriReceiverActivityの
// 実装と同じ）。ここで直接AuthorizationResponse.fromIntent()を呼んでも正しく解析できず、
// 実機で「Custom Tabsが一瞬開いてすぐ閉じる」症状として発覚した。
// 実際のトークン交換・CarIotStateへの保存はAuthResultActivityが行う。
class OAuthRedirectActivity : Activity() {
  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)
    startActivity(AuthorizationManagementActivity.createResponseHandlingIntent(this, intent.data))
    finish()
  }
}
