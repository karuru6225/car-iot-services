import 'package:flutter/material.dart';

import 'card_widgets.dart';

class MeasCard extends StatelessWidget {
  final double? vMain, curr, pwr, vSub;
  const MeasCard({super.key, this.vMain, this.curr, this.pwr, this.vSub});

  @override
  Widget build(BuildContext context) {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            cardLabel('計測値'),
            const SizedBox(height: 12),
            GridView.count(
              crossAxisCount: 2,
              shrinkWrap: true,
              physics: const NeverScrollableScrollPhysics(),
              childAspectRatio: 2.2,
              mainAxisSpacing: 12,
              crossAxisSpacing: 12,
              children: [
                _dataItem('メイン電圧', vMain, 'V', 3),
                _dataItem('電流',       curr,  'A', 3),
                _dataItem('電力',       pwr,   'W', 2),
                _dataItem('サブ電圧',   vSub,  'V', 3),
              ],
            ),
          ],
        ),
      ),
    );
  }

  Widget _dataItem(String label, double? value, String unit, int digits) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(label, style: const TextStyle(fontSize: 11, color: Colors.grey)),
        Row(
          crossAxisAlignment: CrossAxisAlignment.baseline,
          textBaseline: TextBaseline.alphabetic,
          children: [
            Text(
              value?.toStringAsFixed(digits) ?? '—',
              style: TextStyle(
                fontSize: 22,
                fontWeight: FontWeight.w600,
                color: value != null ? const Color(0xFF4F8EF7) : Colors.grey,
              ),
            ),
            if (value != null) ...[
              const SizedBox(width: 3),
              Text(unit, style: const TextStyle(fontSize: 12, color: Colors.grey)),
            ],
          ],
        ),
      ],
    );
  }
}
