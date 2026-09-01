package info.karuru.cariot.obd

// メーター画面での可視化形式（Phase8後半で使用予定、現時点では定義のみ）。
enum class GaugeStyle(val label: String) {
  CIRCULAR("サーキュラー"),
  DIGITAL("デジタル数値"),
  BAR("バー"),
  SPARKLINE("ミニグラフ"),
}

// ObdReadingのts/valid/atfTempValid/validMaskを除く全フィールドに対応する識別子
// （末尾のFUEL_ECONOMY_KM_Lのみ、speedKmh/fuelRateLphから算出する派生値）。
// 宣言順はOBDタブの表示順と完全に一致させること（entries をそのままグリッド順として使うため、
// 順序を変えると既存OBDタブの見た目が変わる。mobile/lib/models/obd_metric.dartと同じ順序）。
enum class ObdMetric {
  RPM,
  SPEED_KMH,
  LOAD_PCT,
  MAP_KPA,
  BARO_KPA,
  BOOST_KPA,
  THROTTLE_PCT,
  TIMING_DEG,
  ECU_VOLTAGE,
  MAF_GS,
  COOLANT_C,
  FUEL_RATE_LPH,
  STFT_PCT,
  LTFT_PCT,
  O2_B1S2_V,
  O2_B1S2_TRIM_PCT,
  ENGINE_RUN_TIME_SEC,
  MIL_DISTANCE_KM,
  O2_S1_RATIO,
  O2_S1_VOLTAGE,
  EVAP_PURGE_PCT,
  WARMUPS_SINCE_CLEARED,
  DISTANCE_SINCE_CLEARED_KM,
  CATALYST_TEMP_C,
  ABSOLUTE_LOAD_PCT,
  COMMANDED_AFR,
  THROTTLE_B_PCT,
  ACCEL_PEDAL_D_PCT,
  ACCEL_PEDAL_E_PCT,
  FUEL_TYPE,
  SEC_O2_TRIM_ST_PCT,
  SEC_O2_TRIM_LT_PCT,
  IAT_C,
  IAT2_C,
  ATF_TEMP_C,
  FUEL_ECONOMY_KM_L,
}

// 各ObdMetricの表示・ゲージ描画に必要なメタ情報。
// label/unit/decimalsはOBDタブの表記と一字一句一致させている。
// min/maxはOBD規格上の理論上限ではなく実車での実用レンジ（暫定値、後日調整前提）。
data class ObdMetricMeta(
    val label: String,
    val unit: String,
    val decimals: Int,
    val min: Float,
    val max: Float,
    val valueOf: (ObdReading) -> Float,
)

// mobile/lib/models/obd_metric.dartのobdMetricMetaを1:1移植。
val obdMetricMeta: Map<ObdMetric, ObdMetricMeta> = mapOf(
    ObdMetric.RPM to ObdMetricMeta(
        label = "RPM", unit = "rpm", decimals = 0,
        min = 0f, max = 7000f,
        valueOf = { it.rpm.toFloat() },
    ),
    ObdMetric.SPEED_KMH to ObdMetricMeta(
        label = "速度", unit = "km/h", decimals = 0,
        min = 0f, max = 180f,
        valueOf = { it.speedKmh.toFloat() },
    ),
    ObdMetric.LOAD_PCT to ObdMetricMeta(
        label = "負荷", unit = "%", decimals = 0,
        min = 0f, max = 100f,
        valueOf = { it.loadPct.toFloat() },
    ),
    ObdMetric.MAP_KPA to ObdMetricMeta(
        label = "MAP", unit = "kPa", decimals = 0,
        min = 0f, max = 150f,
        valueOf = { it.mapKpa.toFloat() },
    ),
    ObdMetric.BARO_KPA to ObdMetricMeta(
        label = "大気圧", unit = "kPa", decimals = 0,
        min = 80f, max = 110f,
        valueOf = { it.baroKpa.toFloat() },
    ),
    ObdMetric.BOOST_KPA to ObdMetricMeta(
        label = "ブースト", unit = "kPa", decimals = 0,
        min = -100f, max = 150f,
        valueOf = { it.boostKpa.toFloat() },
    ),
    ObdMetric.THROTTLE_PCT to ObdMetricMeta(
        label = "スロットル", unit = "%", decimals = 0,
        min = 0f, max = 100f,
        valueOf = { it.throttlePct.toFloat() },
    ),
    ObdMetric.TIMING_DEG to ObdMetricMeta(
        label = "点火時期", unit = "°BTDC", decimals = 1,
        min = -20f, max = 50f,
        valueOf = { it.timingDeg },
    ),
    ObdMetric.ECU_VOLTAGE to ObdMetricMeta(
        label = "ECU電圧", unit = "V", decimals = 2,
        min = 10f, max = 16f,
        valueOf = { it.ecuVoltage },
    ),
    ObdMetric.MAF_GS to ObdMetricMeta(
        label = "MAF", unit = "g/s", decimals = 2,
        min = 0f, max = 100f,
        valueOf = { it.mafGs },
    ),
    ObdMetric.COOLANT_C to ObdMetricMeta(
        label = "水温", unit = "°C", decimals = 0,
        min = -20f, max = 120f,
        valueOf = { it.coolantC.toFloat() },
    ),
    ObdMetric.FUEL_RATE_LPH to ObdMetricMeta(
        label = "燃費推算", unit = "L/h", decimals = 2,
        min = 0f, max = 30f,
        valueOf = { it.fuelRateLph },
    ),
    ObdMetric.STFT_PCT to ObdMetricMeta(
        label = "短期燃調", unit = "%", decimals = 1,
        min = -25f, max = 25f,
        valueOf = { it.stftPct },
    ),
    ObdMetric.LTFT_PCT to ObdMetricMeta(
        label = "長期燃調", unit = "%", decimals = 1,
        min = -25f, max = 25f,
        valueOf = { it.ltftPct },
    ),
    ObdMetric.O2_B1S2_V to ObdMetricMeta(
        label = "O2 B1S2電圧", unit = "V", decimals = 2,
        min = 0f, max = 1.5f,
        valueOf = { it.o2B1s2V },
    ),
    ObdMetric.O2_B1S2_TRIM_PCT to ObdMetricMeta(
        label = "O2 B1S2燃調", unit = "%", decimals = 1,
        min = -100f, max = 100f,
        valueOf = { it.o2B1s2TrimPct },
    ),
    ObdMetric.ENGINE_RUN_TIME_SEC to ObdMetricMeta(
        label = "稼働時間", unit = "秒", decimals = 0,
        min = 0f, max = 7200f,
        valueOf = { it.engineRunTimeSec.toFloat() },
    ),
    ObdMetric.MIL_DISTANCE_KM to ObdMetricMeta(
        label = "MIL点灯距離", unit = "km", decimals = 0,
        min = 0f, max = 500f,
        valueOf = { it.milDistanceKm.toFloat() },
    ),
    ObdMetric.O2_S1_RATIO to ObdMetricMeta(
        label = "O2 WBratio", unit = "", decimals = 3,
        min = 0f, max = 2f,
        valueOf = { it.o2S1Ratio },
    ),
    ObdMetric.O2_S1_VOLTAGE to ObdMetricMeta(
        label = "O2 WB電圧", unit = "V", decimals = 2,
        min = 0f, max = 5f,
        valueOf = { it.o2S1Voltage },
    ),
    ObdMetric.EVAP_PURGE_PCT to ObdMetricMeta(
        label = "エバパージ", unit = "%", decimals = 0,
        min = 0f, max = 100f,
        valueOf = { it.evapPurgePct.toFloat() },
    ),
    ObdMetric.WARMUPS_SINCE_CLEARED to ObdMetricMeta(
        label = "暖機回数", unit = "回", decimals = 0,
        min = 0f, max = 255f,
        valueOf = { it.warmupsSinceCleared.toFloat() },
    ),
    ObdMetric.DISTANCE_SINCE_CLEARED_KM to ObdMetricMeta(
        label = "消去後距離", unit = "km", decimals = 0,
        min = 0f, max = 2000f,
        valueOf = { it.distanceSinceClearedKm.toFloat() },
    ),
    ObdMetric.CATALYST_TEMP_C to ObdMetricMeta(
        label = "触媒温度", unit = "°C", decimals = 0,
        min = 0f, max = 900f,
        valueOf = { it.catalystTempC },
    ),
    ObdMetric.ABSOLUTE_LOAD_PCT to ObdMetricMeta(
        label = "絶対負荷", unit = "%", decimals = 1,
        min = 0f, max = 150f,
        valueOf = { it.absoluteLoadPct },
    ),
    ObdMetric.COMMANDED_AFR to ObdMetricMeta(
        label = "目標AFR", unit = "", decimals = 2,
        min = 0f, max = 2f,
        valueOf = { it.commandedAfr },
    ),
    ObdMetric.THROTTLE_B_PCT to ObdMetricMeta(
        label = "スロットルB", unit = "%", decimals = 0,
        min = 0f, max = 100f,
        valueOf = { it.throttleBPct.toFloat() },
    ),
    ObdMetric.ACCEL_PEDAL_D_PCT to ObdMetricMeta(
        label = "アクセルD", unit = "%", decimals = 0,
        min = 0f, max = 100f,
        valueOf = { it.accelPedalDPct.toFloat() },
    ),
    ObdMetric.ACCEL_PEDAL_E_PCT to ObdMetricMeta(
        label = "アクセルE", unit = "%", decimals = 0,
        min = 0f, max = 100f,
        valueOf = { it.accelPedalEPct.toFloat() },
    ),
    ObdMetric.FUEL_TYPE to ObdMetricMeta(
        label = "燃料種別", unit = "", decimals = 0,
        min = 0f, max = 25f,
        valueOf = { it.fuelType.toFloat() },
    ),
    ObdMetric.SEC_O2_TRIM_ST_PCT to ObdMetricMeta(
        label = "副O2短期", unit = "%", decimals = 1,
        min = -100f, max = 100f,
        valueOf = { it.secO2TrimStPct },
    ),
    ObdMetric.SEC_O2_TRIM_LT_PCT to ObdMetricMeta(
        label = "副O2長期", unit = "%", decimals = 1,
        min = -100f, max = 100f,
        valueOf = { it.secO2TrimLtPct },
    ),
    ObdMetric.IAT_C to ObdMetricMeta(
        label = "吸気温1(仮)", unit = "°C", decimals = 0,
        min = -20f, max = 120f,
        valueOf = { it.iatC.toFloat() },
    ),
    ObdMetric.IAT2_C to ObdMetricMeta(
        label = "吸気温2(仮)", unit = "°C", decimals = 0,
        min = -20f, max = 120f,
        valueOf = { it.iat2C.toFloat() },
    ),
    ObdMetric.ATF_TEMP_C to ObdMetricMeta(
        label = "ATF油温(仮)", unit = "°C", decimals = 0,
        min = -20f, max = 150f,
        valueOf = { it.atfTempC.toFloat() },
    ),
    // speedKmh/fuelRateLphからの算出値（km/h ÷ L/h = km/L）。fuelRateLphがほぼ0
    // （アイドル・減速時の燃料カット等）だと発散するため、停車中は0、走行中の燃料カットは
    // max値に丸める（mobile/lib/models/obd_metric.dartと同じガード条件）。
    ObdMetric.FUEL_ECONOMY_KM_L to ObdMetricMeta(
        label = "瞬間燃費", unit = "km/L", decimals = 1,
        min = 0f, max = 40f,
        valueOf = { r ->
          if (r.fuelRateLph > 0.05f) {
            (r.speedKmh / r.fuelRateLph).coerceIn(0f, 40f)
          } else if (r.speedKmh > 0) {
            40f
          } else {
            0f
          }
        },
    ),
)

// メーター画面の初期タイル構成（保存済み設定が無い場合のデフォルト、
// mobile/lib/models/obd_metric.dartのdefaultMeterMetricsと同じ）。
val defaultMeterMetrics: List<Pair<ObdMetric, GaugeStyle>> = listOf(
    ObdMetric.RPM to GaugeStyle.CIRCULAR,
    ObdMetric.SPEED_KMH to GaugeStyle.DIGITAL,
    ObdMetric.COOLANT_C to GaugeStyle.BAR,
    ObdMetric.ECU_VOLTAGE to GaugeStyle.SPARKLINE,
)
