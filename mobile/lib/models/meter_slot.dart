import 'obd_metric.dart';

// メーター画面の1タイル分の設定（どの項目をどの形式で表示するか）。
// 同一metricを複数タイルで選ぶことも許可する（禁止ロジックは持たせない）。
class MeterSlot {
  final ObdMetric metric;
  final GaugeStyle style;

  const MeterSlot({required this.metric, required this.style});

  Map<String, String> toJson() => {'metric': metric.name, 'style': style.name};

  // 対応するenum値が見つからない場合（将来のリネーム等）はnullを返す。
  // 呼び出し側で読み捨てられるようにし、アプリをクラッシュさせない。
  static MeterSlot? fromJson(Map<String, dynamic> json) {
    ObdMetric? metric;
    for (final m in ObdMetric.values) {
      if (m.name == json['metric']) {
        metric = m;
        break;
      }
    }
    GaugeStyle? style;
    for (final s in GaugeStyle.values) {
      if (s.name == json['style']) {
        style = s;
        break;
      }
    }
    if (metric == null || style == null) return null;
    return MeterSlot(metric: metric, style: style);
  }
}
