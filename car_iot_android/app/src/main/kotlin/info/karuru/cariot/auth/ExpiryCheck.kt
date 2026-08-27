package info.karuru.cariot.auth

// mobile/lib/services/auth_service.dartのaccessToken getterと同じ判定（期限1分前
// マージンを取る）。まだ有効ならtrue、期限切れ間近・過ぎていればfalse（リフレッシュすべき）。
private const val EXPIRY_MARGIN_MS = 60_000L

fun isStillValid(expiresAtEpochMillis: Long, nowEpochMillis: Long): Boolean {
  return expiresAtEpochMillis - nowEpochMillis > EXPIRY_MARGIN_MS
}
