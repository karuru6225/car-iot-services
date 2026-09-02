import 'package:flutter/material.dart';

import '../models/obd_metric.dart';
import '../models/obd_reading.dart';
import '../theme/app_colors.dart';
import 'card_widgets.dart';

class ObdCard extends StatelessWidget {
  final ObdReading? reading;
  const ObdCard({super.key, this.reading});

  @override
  Widget build(BuildContext context) {
    final r = reading;
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            cardLabel('OBD-II'),
            const SizedBox(height: 12),
            if (r == null)
              const Text('—', style: TextStyle(color: Colors.grey))
            else if (!r.valid)
              const Text('応答なし（IGN OFF または CAN 未接続）',
                  style: TextStyle(color: Colors.grey))
            else
              GridView.count(
                crossAxisCount: 2,
                shrinkWrap: true,
                physics: const NeverScrollableScrollPhysics(),
                childAspectRatio: 2.8,
                mainAxisSpacing: 8,
                crossAxisSpacing: 12,
                children: [
                  for (final metric in ObdMetric.values) _obdItem(metric, r),
                ],
              ),
          ],
        ),
      ),
    );
  }

  Widget _obdItem(ObdMetric metric, ObdReading r) {
    final meta = obdMetricMeta[metric]!;
    final value = meta.valueOf(r).toStringAsFixed(meta.decimals);
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(meta.label, style: const TextStyle(fontSize: 10, color: Colors.grey)),
        Text(
          meta.unit.isEmpty ? value : '$value ${meta.unit}',
          style: const TextStyle(
              fontSize: 14, fontWeight: FontWeight.w600, color: AppColors.primary),
        ),
      ],
    );
  }
}
