import 'obd_reading.dart';

// メーター画面での可視化形式。
enum GaugeStyle {
  circular('サーキュラー'),
  digital('デジタル数値'),
  bar('バー'),
  sparkline('ミニグラフ');

  final String label;
  const GaugeStyle(this.label);
}

// ObdReadingのts/valid/atfTempValidを除く全35フィールドに対応する識別子
// （末尾のfuelEconomyKmLのみ、speedKmh/fuelRateLphから算出する派生値）。
// 宣言順はobd_card.dartの表示順と完全に一致させること（ObdMetric.valuesを
// そのままグリッド順として使うため、順序を変えると既存OBDタブの見た目が変わる）。
enum ObdMetric {
  rpm,
  speedKmh,
  loadPct,
  mapKpa,
  baroKpa,
  boostKpa,
  throttlePct,
  timingDeg,
  ecuVoltage,
  mafGs,
  coolantC,
  fuelRateLph,
  stftPct,
  ltftPct,
  o2B1s2V,
  o2B1s2TrimPct,
  engineRunTimeSec,
  milDistanceKm,
  o2S1Ratio,
  o2S1Voltage,
  evapPurgePct,
  warmupsSinceCleared,
  distanceSinceClearedKm,
  catalystTempC,
  absoluteLoadPct,
  commandedAfr,
  throttleBPct,
  accelPedalDPct,
  accelPedalEPct,
  fuelType,
  secO2TrimStPct,
  secO2TrimLtPct,
  iatC,
  iat2C,
  atfTempC,
  fuelEconomyKmL,
}

// 各ObdMetricの表示・ゲージ描画に必要なメタ情報。
// label/unit/decimalsはobd_card.dartの表記と一字一句一致させている。
// min/maxはOBD規格上の理論上限ではなく実車での実用レンジ（暫定値、後日調整前提）。
class ObdMetricMeta {
  final String label;
  final String unit;
  final int decimals;
  final double min;
  final double max;
  final double Function(ObdReading r) valueOf;

  const ObdMetricMeta({
    required this.label,
    required this.unit,
    required this.decimals,
    required this.min,
    required this.max,
    required this.valueOf,
  });
}

// クロージャを含むためconstマップにはできない（トップレベルfinalで1度だけ構築される）。
final Map<ObdMetric, ObdMetricMeta> obdMetricMeta = {
  ObdMetric.rpm: ObdMetricMeta(
    label: 'RPM', unit: 'rpm', decimals: 0,
    min: 0, max: 7000,
    valueOf: (r) => r.rpm.toDouble(),
  ),
  ObdMetric.speedKmh: ObdMetricMeta(
    label: '速度', unit: 'km/h', decimals: 0,
    min: 0, max: 180,
    valueOf: (r) => r.speedKmh.toDouble(),
  ),
  ObdMetric.loadPct: ObdMetricMeta(
    label: '負荷', unit: '%', decimals: 0,
    min: 0, max: 100,
    valueOf: (r) => r.loadPct.toDouble(),
  ),
  ObdMetric.mapKpa: ObdMetricMeta(
    label: 'MAP', unit: 'kPa', decimals: 0,
    min: 0, max: 150,
    valueOf: (r) => r.mapKpa.toDouble(),
  ),
  ObdMetric.baroKpa: ObdMetricMeta(
    label: '大気圧', unit: 'kPa', decimals: 0,
    min: 80, max: 110,
    valueOf: (r) => r.baroKpa.toDouble(),
  ),
  ObdMetric.boostKpa: ObdMetricMeta(
    label: 'ブースト', unit: 'kPa', decimals: 0,
    min: -100, max: 150,
    valueOf: (r) => r.boostKpa.toDouble(),
  ),
  ObdMetric.throttlePct: ObdMetricMeta(
    label: 'スロットル', unit: '%', decimals: 0,
    min: 0, max: 100,
    valueOf: (r) => r.throttlePct.toDouble(),
  ),
  ObdMetric.timingDeg: ObdMetricMeta(
    label: '点火時期', unit: '°BTDC', decimals: 1,
    min: -20, max: 50,
    valueOf: (r) => r.timingDeg,
  ),
  ObdMetric.ecuVoltage: ObdMetricMeta(
    label: 'ECU電圧', unit: 'V', decimals: 2,
    min: 10, max: 16,
    valueOf: (r) => r.ecuVoltage,
  ),
  ObdMetric.mafGs: ObdMetricMeta(
    label: 'MAF', unit: 'g/s', decimals: 2,
    min: 0, max: 100,
    valueOf: (r) => r.mafGs,
  ),
  ObdMetric.coolantC: ObdMetricMeta(
    label: '水温', unit: '°C', decimals: 0,
    min: -20, max: 120,
    valueOf: (r) => r.coolantC.toDouble(),
  ),
  ObdMetric.fuelRateLph: ObdMetricMeta(
    label: '燃費推算', unit: 'L/h', decimals: 2,
    min: 0, max: 30,
    valueOf: (r) => r.fuelRateLph,
  ),
  ObdMetric.stftPct: ObdMetricMeta(
    label: '短期燃調', unit: '%', decimals: 1,
    min: -25, max: 25,
    valueOf: (r) => r.stftPct,
  ),
  ObdMetric.ltftPct: ObdMetricMeta(
    label: '長期燃調', unit: '%', decimals: 1,
    min: -25, max: 25,
    valueOf: (r) => r.ltftPct,
  ),
  ObdMetric.o2B1s2V: ObdMetricMeta(
    label: 'O2 B1S2電圧', unit: 'V', decimals: 2,
    min: 0, max: 1.5,
    valueOf: (r) => r.o2B1s2V,
  ),
  ObdMetric.o2B1s2TrimPct: ObdMetricMeta(
    label: 'O2 B1S2燃調', unit: '%', decimals: 1,
    min: -100, max: 100,
    valueOf: (r) => r.o2B1s2TrimPct,
  ),
  ObdMetric.engineRunTimeSec: ObdMetricMeta(
    label: '稼働時間', unit: '秒', decimals: 0,
    min: 0, max: 7200,
    valueOf: (r) => r.engineRunTimeSec.toDouble(),
  ),
  ObdMetric.milDistanceKm: ObdMetricMeta(
    label: 'MIL点灯距離', unit: 'km', decimals: 0,
    min: 0, max: 500,
    valueOf: (r) => r.milDistanceKm.toDouble(),
  ),
  ObdMetric.o2S1Ratio: ObdMetricMeta(
    label: 'O2 WBratio', unit: '', decimals: 3,
    min: 0, max: 2,
    valueOf: (r) => r.o2S1Ratio,
  ),
  ObdMetric.o2S1Voltage: ObdMetricMeta(
    label: 'O2 WB電圧', unit: 'V', decimals: 2,
    min: 0, max: 5,
    valueOf: (r) => r.o2S1Voltage,
  ),
  ObdMetric.evapPurgePct: ObdMetricMeta(
    label: 'エバパージ', unit: '%', decimals: 0,
    min: 0, max: 100,
    valueOf: (r) => r.evapPurgePct.toDouble(),
  ),
  ObdMetric.warmupsSinceCleared: ObdMetricMeta(
    label: '暖機回数', unit: '回', decimals: 0,
    min: 0, max: 255,
    valueOf: (r) => r.warmupsSinceCleared.toDouble(),
  ),
  ObdMetric.distanceSinceClearedKm: ObdMetricMeta(
    label: '消去後距離', unit: 'km', decimals: 0,
    min: 0, max: 2000,
    valueOf: (r) => r.distanceSinceClearedKm.toDouble(),
  ),
  ObdMetric.catalystTempC: ObdMetricMeta(
    label: '触媒温度', unit: '°C', decimals: 0,
    min: 0, max: 900,
    valueOf: (r) => r.catalystTempC,
  ),
  ObdMetric.absoluteLoadPct: ObdMetricMeta(
    label: '絶対負荷', unit: '%', decimals: 1,
    min: 0, max: 150,
    valueOf: (r) => r.absoluteLoadPct,
  ),
  ObdMetric.commandedAfr: ObdMetricMeta(
    label: '目標AFR', unit: '', decimals: 2,
    min: 0, max: 2,
    valueOf: (r) => r.commandedAfr,
  ),
  ObdMetric.throttleBPct: ObdMetricMeta(
    label: 'スロットルB', unit: '%', decimals: 0,
    min: 0, max: 100,
    valueOf: (r) => r.throttleBPct.toDouble(),
  ),
  ObdMetric.accelPedalDPct: ObdMetricMeta(
    label: 'アクセルD', unit: '%', decimals: 0,
    min: 0, max: 100,
    valueOf: (r) => r.accelPedalDPct.toDouble(),
  ),
  ObdMetric.accelPedalEPct: ObdMetricMeta(
    label: 'アクセルE', unit: '%', decimals: 0,
    min: 0, max: 100,
    valueOf: (r) => r.accelPedalEPct.toDouble(),
  ),
  ObdMetric.fuelType: ObdMetricMeta(
    label: '燃料種別', unit: '', decimals: 0,
    min: 0, max: 25,
    valueOf: (r) => r.fuelType.toDouble(),
  ),
  ObdMetric.secO2TrimStPct: ObdMetricMeta(
    label: '副O2短期', unit: '%', decimals: 1,
    min: -100, max: 100,
    valueOf: (r) => r.secO2TrimStPct,
  ),
  ObdMetric.secO2TrimLtPct: ObdMetricMeta(
    label: '副O2長期', unit: '%', decimals: 1,
    min: -100, max: 100,
    valueOf: (r) => r.secO2TrimLtPct,
  ),
  ObdMetric.iatC: ObdMetricMeta(
    label: '吸気温1(仮)', unit: '°C', decimals: 0,
    min: -20, max: 120,
    valueOf: (r) => r.iatC.toDouble(),
  ),
  ObdMetric.iat2C: ObdMetricMeta(
    label: '吸気温2(仮)', unit: '°C', decimals: 0,
    min: -20, max: 120,
    valueOf: (r) => r.iat2C.toDouble(),
  ),
  ObdMetric.atfTempC: ObdMetricMeta(
    label: 'ATF油温(仮)', unit: '°C', decimals: 0,
    min: -20, max: 150,
    valueOf: (r) => r.atfTempC.toDouble(),
  ),
  // speedKmh/fuelRateLphからの算出値（km/h ÷ L/h = km/L）。fuelRateLphが
  // ほぼ0（アイドル・減速時の燃料カット等）だと発散するため、停車中は0、
  // 走行中の燃料カットはmax値に丸める。
  ObdMetric.fuelEconomyKmL: ObdMetricMeta(
    label: '瞬間燃費', unit: 'km/L', decimals: 1,
    min: 0, max: 40,
    valueOf: (r) => r.fuelRateLph > 0.05
        ? (r.speedKmh / r.fuelRateLph).clamp(0.0, 40.0)
        : (r.speedKmh > 0 ? 40.0 : 0.0),
  ),
};

// メーター画面の初期タイル構成（保存済み設定が無い場合のデフォルト）。
const defaultMeterMetrics = [
  (ObdMetric.rpm, GaugeStyle.circular),
  (ObdMetric.speedKmh, GaugeStyle.digital),
  (ObdMetric.coolantC, GaugeStyle.bar),
  (ObdMetric.ecuVoltage, GaugeStyle.sparkline),
];
