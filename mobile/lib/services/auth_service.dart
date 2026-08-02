import 'dart:convert';

import 'package:flutter_appauth/flutter_appauth.dart';
import 'package:flutter_secure_storage/flutter_secure_storage.dart';

import '../config.dart';

// Cognito Hosted UI（Googleフェデレーテッドログイン）経由の認証。
class AuthService {
  static const _clientId = AppConfig.cognitoClientId;
  // AppAuthのOIDC discovery(.well-known/openid-configuration)取得先。Hosted UIドメインではなく
  // User Pool自体のissuer URLを使う（Hosted UIドメイン側にはdiscoveryドキュメントが無く404になる）。
  static const _issuer =
      'https://cognito-idp.${AppConfig.awsRegion}.amazonaws.com/${AppConfig.cognitoUserPoolId}';
  // Cognito App Client の callback_urls・AndroidのmanifestPlaceholdersと完全一致させること
  // （URIスキームはRFC3986でアンダースコア不可のためapplicationIdとは別文字列）
  static const _redirectUri = 'info.karuru.cariotmobile://oauthredirect';
  static const _scopes = ['openid', 'email', 'profile'];

  static const _kAccessToken = 'obd_access_token';
  static const _kIdToken = 'obd_id_token';
  static const _kRefreshToken = 'obd_refresh_token';
  static const _kExpiresAt = 'obd_expires_at';
  static const _kEmail = 'obd_user_email';

  final _appAuth = const FlutterAppAuth();
  final _storage = const FlutterSecureStorage();

  // 起動時に保存済みリフレッシュトークンの有無からログイン状態を復元する。
  // 戻り値はログイン中のメールアドレス（表示用）、未ログインならnull。
  Future<String?> tryRestoreSession() async {
    final refreshToken = await _storage.read(key: _kRefreshToken);
    if (refreshToken == null) return null;
    return _storage.read(key: _kEmail);
  }

  // Cognito Hosted UIをidentity_provider=Google付きで開き、Google選択画面を省略してログインする。
  // ユーザーがブラウザをキャンセルした場合等は例外を投げるので、呼び出し側でnullとして扱えるよう吸収する。
  Future<String?> signIn() async {
    try {
      final result = await _appAuth.authorizeAndExchangeCode(
        AuthorizationTokenRequest(
          _clientId,
          _redirectUri,
          issuer: _issuer,
          scopes: _scopes,
          additionalParameters: const {'identity_provider': 'Google'},
        ),
      );

      final email = _emailFromIdToken(result.idToken);
      await _persist(
        accessToken: result.accessToken,
        idToken: result.idToken,
        refreshToken: result.refreshToken,
        expiresAt: result.accessTokenExpirationDateTime,
        email: email,
      );
      return email;
    } catch (_) {
      return null;
    }
  }

  Future<void> signOut() => _storage.deleteAll();

  // API呼び出し用のaccess_token。期限切れ間近ならrefresh_tokenで自動更新してから返す。
  // 未ログイン・更新失敗時はnull（呼び出し側は再ログインを促す）。
  Future<String?> get accessToken async {
    final token = await _storage.read(key: _kAccessToken);
    if (token == null) return null;

    final expiresAtStr = await _storage.read(key: _kExpiresAt);
    final expiresAt = expiresAtStr != null ? DateTime.tryParse(expiresAtStr) : null;
    final stillValid = expiresAt != null &&
        expiresAt.isAfter(DateTime.now().add(const Duration(minutes: 1)));
    if (stillValid) return token;

    return _refresh();
  }

  Future<String?> _refresh() async {
    final refreshToken = await _storage.read(key: _kRefreshToken);
    if (refreshToken == null) return null;

    try {
      final result = await _appAuth.token(
        TokenRequest(
          _clientId,
          _redirectUri,
          issuer: _issuer,
          refreshToken: refreshToken,
          grantType: 'refresh_token',
          scopes: _scopes,
        ),
      );
      await _persist(
        accessToken: result.accessToken,
        idToken: result.idToken,
        refreshToken: result.refreshToken ?? refreshToken,
        expiresAt: result.accessTokenExpirationDateTime,
        email: await _storage.read(key: _kEmail),
      );
      return result.accessToken;
    } catch (_) {
      // リフレッシュトークン自体が失効している場合など。呼び出し側で再ログインへ誘導する。
      return null;
    }
  }

  Future<void> _persist({
    required String? accessToken,
    required String? idToken,
    required String? refreshToken,
    required DateTime? expiresAt,
    required String? email,
  }) async {
    if (accessToken != null) await _storage.write(key: _kAccessToken, value: accessToken);
    if (idToken != null) await _storage.write(key: _kIdToken, value: idToken);
    if (refreshToken != null) await _storage.write(key: _kRefreshToken, value: refreshToken);
    if (expiresAt != null) {
      await _storage.write(key: _kExpiresAt, value: expiresAt.toIso8601String());
    }
    if (email != null) await _storage.write(key: _kEmail, value: email);
  }

  // IDトークン(JWT)のpayloadからemailクレームのみ取り出す（表示用途、署名検証はしない。
  // API呼び出しの正当性検証はAPI Gateway側のJWT Authorizerが行うため問題ない）。
  String? _emailFromIdToken(String? idToken) {
    if (idToken == null) return null;
    final parts = idToken.split('.');
    if (parts.length != 3) return null;
    try {
      final payload = utf8.decode(base64Url.decode(base64Url.normalize(parts[1])));
      final claims = jsonDecode(payload) as Map<String, dynamic>;
      return claims['email'] as String?;
    } catch (_) {
      return null;
    }
  }
}
