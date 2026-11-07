package info.karuru.cariot.upload

import android.util.Log
import info.karuru.cariot.AppConfig
import info.karuru.cariot.auth.AccessTokenResult
import info.karuru.cariot.auth.AuthStore
import info.karuru.cariot.db.PendingObdReading
import info.karuru.cariot.db.PendingObdReadingDao
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody
import java.io.IOException
import java.util.concurrent.TimeUnit

private const val TAG = "ObdUploader"
private const val MAX_BATCH_SIZE = 100

// mobile/lib/services/obd_uploader.dartの_maxBufferHardCap相当（送信失敗が続いた場合の保険）。
// db/PendingObdReadingDao.insertAndTrim()の呼び出し側(CarIotForegroundService)が使う。
const val PENDING_READING_HARD_CAP = 6000

// uploadOnce()の結果。SENTのときだけ呼び出し側は続けて次のバッチを送る。
// 失敗時に続行すると同じバッチを待ち時間なしで再送し続け、dataSync型の6時間上限まで
// 止まらなくなるため、失敗は次回のアップロード起動（5分ごと）に任せて打ち切る。
enum class UploadResult {
  SENT,
  EMPTY,
  FAILED,
  // refresh_token失効・未ログイン・401。再ログインしない限り送れない
  AUTH_REQUIRED,
}

@Serializable
private data class ObdUploadRequest(
    @SerialName("device_id") val deviceId: String,
    val readings: List<PendingObdReading>,
)

// Room(PendingObdReadingDao)から未送信バッチを取り出しAWSへPOSTする。
// CarIotUploadService(dataSync型)から呼ばれる想定（Phase5、docs/car_iot_android_plan.md）。
// mobile/lib/services/obd_uploader.dartと同じAPIフォーマット（infra/lambda_src/obd_ingest/index.py）。
class ObdUploader(private val dao: PendingObdReadingDao, private val authStore: AuthStore) {
  companion object {
    // lat/lonがnullの場合キー自体を出力しない（Lambda側はキーが無ければGPS未取得として扱う）。
    private val json = Json { explicitNulls = false }

    // API送信用JSONの組み立て（純粋ロジック、TDD対象）。
    fun buildRequestBody(deviceId: String, readings: List<PendingObdReading>): String {
      return json.encodeToString(ObdUploadRequest(deviceId, readings))
    }

    // HTTPステータス→結果の分類（純粋ロジック、TDD対象）。
    // 401はJWT Authorizerがトークンを拒否した場合。403はLambda側の権限チェック由来で
    // 再ログインしても直らない可能性があるため、AUTH_REQUIREDには含めない。
    fun classifyResponse(code: Int): UploadResult = when {
      code in 200..299 -> UploadResult.SENT
      code == 401 -> UploadResult.AUTH_REQUIRED
      else -> UploadResult.FAILED
    }
  }

  // docs/car_iot_android_plan.md: dataSync型の6時間累積上限に対する安全マージンとして
  // タイムアウトはFlutter版(15秒)より短くし、失敗は次回再送に任せて早めに諦める。
  private val client = OkHttpClient.Builder()
      .connectTimeout(5, TimeUnit.SECONDS)
      .readTimeout(8, TimeUnit.SECONDS)
      .writeTimeout(8, TimeUnit.SECONDS)
      .build()

  // 1バッチ分を送信する。
  suspend fun uploadOnce(deviceId: String): UploadResult {
    val batch = dao.nextBatch(MAX_BATCH_SIZE)
    if (batch.isEmpty()) return UploadResult.EMPTY

    val token = when (val t = authStore.getValidAccessToken()) {
      is AccessTokenResult.Valid -> t.token
      AccessTokenResult.SessionExpired -> {
        Log.w(TAG, "OBDアップロード未送信: 未ログインまたはセッション失効")
        return UploadResult.AUTH_REQUIRED
      }
      AccessTokenResult.Unavailable -> {
        Log.w(TAG, "OBDアップロード未送信: トークンを取得できない（通信エラー等）、次回リトライ")
        return UploadResult.FAILED
      }
    }

    val body = buildRequestBody(deviceId, batch).toRequestBody("application/json".toMediaType())
    val request = Request.Builder()
        .url("${AppConfig.API_ENDPOINT}/obd")
        .addHeader("Authorization", "Bearer $token")
        .post(body)
        .build()

    return withContext(Dispatchers.IO) {
      try {
        client.newCall(request).execute().use { response ->
          val result = classifyResponse(response.code)
          if (result == UploadResult.SENT) {
            dao.deleteByIds(batch.map { it.id })
            Log.i(TAG, "OBDアップロード成功: ${batch.size}件")
          } else {
            Log.w(TAG, "OBDアップロード失敗 (${response.code})、次回リトライ")
          }
          result
        }
      } catch (e: IOException) {
        Log.w(TAG, "OBDアップロードエラー: ${e.message}、次回リトライ")
        UploadResult.FAILED
      }
    }
  }
}
