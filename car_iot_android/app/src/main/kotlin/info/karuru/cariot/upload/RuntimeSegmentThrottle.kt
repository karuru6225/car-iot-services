package info.karuru.cariot.upload

// db.ServiceRuntimeSegmentへの依存を切り離した純粋データ（テスト容易性のため、
// ble/ObdChunkAssemblerがObdReadingへの依存を切り離しているのと同じ設計判断）。
data class RuntimeSegment(val startTs: Long, val endTs: Long?)

// CarIotUploadService(dataSync型)の6時間累積上限に対する安全マージンとして、
// アップロード側の稼働時間がBLE接続側の稼働時間に対して既に一定割合を超えていたら
// 今回のアップロードを見送るための純粋ロジック（docs/car_iot_android_plan.md）。
object RuntimeSegmentThrottle {
  private const val SKIP_THRESHOLD_RATIO = 0.25

  // [windowStart, now]の範囲内でセグメントが占める時間の合計(ms)を返す。
  // endTs=null（Serviceが実行中、または強制終了されendTs更新前に終わった）はnowまで
  // 稼働しているとみなし、安全側に倒す。
  fun overlappingRuntimeMs(segments: List<RuntimeSegment>, windowStart: Long, now: Long): Long {
    return segments.sumOf { seg ->
      val start = maxOf(seg.startTs, windowStart)
      val end = minOf(seg.endTs ?: now, now)
      maxOf(end - start, 0L)
    }
  }

  fun shouldSkipUpload(uploadRuntimeMs: Long, foregroundRuntimeMs: Long): Boolean {
    return uploadRuntimeMs >= (foregroundRuntimeMs * SKIP_THRESHOLD_RATIO).toLong()
  }
}
