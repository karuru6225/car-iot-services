package info.karuru.cariot.auth

import android.net.Uri
import info.karuru.cariot.AppConfig
import kotlinx.coroutines.suspendCancellableCoroutine
import net.openid.appauth.AuthorizationServiceConfiguration

// Cognito User Pool自体のissuer URL（Hosted UIドメインではない）からOIDC discoveryを取得する。
// mobile/lib/services/auth_service.dartの「AppAuthのissuerにHosted UIドメインを使うと404になる」
// 注意点と同じ（discoveryドキュメントはUser Pool自体のissuer URLにある）。
private val ISSUER_URI: Uri = Uri.parse(
    "https://cognito-idp.${AppConfig.AWS_REGION}.amazonaws.com/${AppConfig.COGNITO_USER_POOL_ID}",
)

suspend fun fetchCognitoServiceConfig(): AuthorizationServiceConfiguration? {
  return suspendCancellableCoroutine { cont ->
    AuthorizationServiceConfiguration.fetchFromIssuer(ISSUER_URI) { config, _ ->
      cont.resumeWith(Result.success(config))
    }
  }
}
