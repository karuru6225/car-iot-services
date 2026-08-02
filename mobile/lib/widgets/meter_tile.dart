import 'package:fl_chart/fl_chart.dart';
import 'package:flutter/material.dart';
import 'package:percent_indicator/percent_indicator.dart';

import '../models/meter_slot.dart';
import '../models/obd_metric.dart';
import '../models/obd_reading.dart';
import '../theme/app_colors.dart';
import 'card_widgets.dart';

// 1タイル分（項目×表示形式）を描画するウィジェット。
class MeterTile extends StatelessWidget {
  final MeterSlot slot;
  final ObdReading? reading;
  final List<double> history; // sparkline用の直近履歴（呼び出し側がリングバッファを渡す）

  const MeterTile({
    super.key,
    required this.slot,
    required this.reading,
    required this.history,
  });

  @override
  Widget build(BuildContext context) {
    final meta = obdMetricMeta[slot.metric]!;
    final r = reading;
    final valid = r != null && r.valid;
    final raw = valid ? meta.valueOf(r) : null;

    return RepaintBoundary(
      child: Card(
        child: Padding(
          padding: const EdgeInsets.all(12),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            mainAxisSize: MainAxisSize.min,
            children: [
              cardLabel(meta.label),
              const SizedBox(height: 8),
              Expanded(child: Center(child: _buildGauge(meta, raw))),
            ],
          ),
        ),
      ),
    );
  }

  Widget _buildGauge(ObdMetricMeta meta, double? raw) {
    if (slot.style == GaugeStyle.sparkline) {
      return _buildSparkline(meta);
    }
    if (raw == null) {
      return const Text('—', style: TextStyle(color: Colors.grey));
    }

    final valueText = '${raw.toStringAsFixed(meta.decimals)}${meta.unit.isEmpty ? '' : ' ${meta.unit}'}';

    switch (slot.style) {
      case GaugeStyle.circular:
        final pct = ((raw - meta.min) / (meta.max - meta.min)).clamp(0.0, 1.0);
        return CircularPercentIndicator(
          radius: 42,
          lineWidth: 8,
          percent: pct,
          center: Text(
            valueText,
            textAlign: TextAlign.center,
            style: const TextStyle(fontSize: 12, color: AppColors.primary),
          ),
          progressColor: AppColors.primary,
          backgroundColor: AppColors.disabled,
          circularStrokeCap: CircularStrokeCap.round,
        );
      case GaugeStyle.bar:
        final pct = ((raw - meta.min) / (meta.max - meta.min)).clamp(0.0, 1.0);
        return LinearPercentIndicator(
          percent: pct,
          lineHeight: 16,
          center: Text(valueText, style: const TextStyle(fontSize: 10)),
          progressColor: AppColors.primary,
          backgroundColor: AppColors.disabled,
          barRadius: const Radius.circular(4),
          padding: EdgeInsets.zero,
        );
      case GaugeStyle.digital:
        return Text(
          valueText,
          style: const TextStyle(
              fontSize: 22, fontWeight: FontWeight.bold, color: AppColors.primary),
        );
      case GaugeStyle.sparkline:
        return _buildSparkline(meta); // switchの網羅性のため。実際は冒頭で処理済み
    }
  }

  Widget _buildSparkline(ObdMetricMeta meta) {
    if (history.length < 2) {
      final text = history.isEmpty ? '—' : history.last.toStringAsFixed(meta.decimals);
      return Text(text, style: const TextStyle(color: Colors.grey));
    }
    return Column(
      mainAxisSize: MainAxisSize.min,
      children: [
        SizedBox(
          height: 40,
          child: LineChart(
            LineChartData(
              gridData: const FlGridData(show: false),
              titlesData: const FlTitlesData(show: false),
              borderData: FlBorderData(show: false),
              lineTouchData: const LineTouchData(enabled: false),
              minY: meta.min,
              maxY: meta.max,
              lineBarsData: [
                LineChartBarData(
                  spots: [
                    for (var i = 0; i < history.length; i++) FlSpot(i.toDouble(), history[i]),
                  ],
                  isCurved: true,
                  color: AppColors.primary,
                  barWidth: 2,
                  dotData: const FlDotData(show: false),
                  belowBarData: BarAreaData(
                    show: true,
                    color: AppColors.primary.withValues(alpha: 0.15),
                  ),
                ),
              ],
            ),
          ),
        ),
        Text(
          '${history.last.toStringAsFixed(meta.decimals)}${meta.unit.isEmpty ? '' : ' ${meta.unit}'}',
          style: const TextStyle(fontSize: 12),
        ),
      ],
    );
  }
}
