import 'package:flutter/material.dart';

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
                  _obdItem('RPM', '${r.rpm}', 'rpm'),
                  _obdItem('速度', '${r.speedKmh}', 'km/h'),
                  _obdItem('負荷', '${r.loadPct}', '%'),
                  _obdItem('MAP', '${r.mapKpa}', 'kPa'),
                  _obdItem('大気圧', '${r.baroKpa}', 'kPa'),
                  _obdItem('ブースト', '${r.boostKpa}', 'kPa'),
                  _obdItem('スロットル', '${r.throttlePct}', '%'),
                  _obdItem('点火時期', r.timingDeg.toStringAsFixed(1), '°BTDC'),
                  _obdItem('ECU電圧', r.ecuVoltage.toStringAsFixed(2), 'V'),
                  _obdItem('MAF', r.mafGs.toStringAsFixed(2), 'g/s'),
                  _obdItem('水温', '${r.coolantC}', '°C'),
                  _obdItem('燃費推算', r.fuelRateLph.toStringAsFixed(2), 'L/h'),
                  _obdItem('短期燃調', r.stftPct.toStringAsFixed(1), '%'),
                  _obdItem('長期燃調', r.ltftPct.toStringAsFixed(1), '%'),
                  _obdItem('O2 B1S2電圧', r.o2B1s2V.toStringAsFixed(2), 'V'),
                  _obdItem('O2 B1S2燃調', r.o2B1s2TrimPct.toStringAsFixed(1), '%'),
                  _obdItem('稼働時間', '${r.engineRunTimeSec}', '秒'),
                  _obdItem('MIL点灯距離', '${r.milDistanceKm}', 'km'),
                  _obdItem('O2 WBratio', r.o2S1Ratio.toStringAsFixed(3), ''),
                  _obdItem('O2 WB電圧', r.o2S1Voltage.toStringAsFixed(2), 'V'),
                  _obdItem('エバパージ', '${r.evapPurgePct}', '%'),
                  _obdItem('暖機回数', '${r.warmupsSinceCleared}', '回'),
                  _obdItem('消去後距離', '${r.distanceSinceClearedKm}', 'km'),
                  _obdItem('触媒温度', r.catalystTempC.toStringAsFixed(0), '°C'),
                  _obdItem('絶対負荷', r.absoluteLoadPct.toStringAsFixed(1), '%'),
                  _obdItem('目標AFR', r.commandedAfr.toStringAsFixed(2), ''),
                  _obdItem('スロットルB', '${r.throttleBPct}', '%'),
                  _obdItem('アクセルD', '${r.accelPedalDPct}', '%'),
                  _obdItem('アクセルE', '${r.accelPedalEPct}', '%'),
                  _obdItem('燃料種別', '${r.fuelType}', ''),
                  _obdItem('副O2短期', r.secO2TrimStPct.toStringAsFixed(1), '%'),
                  _obdItem('副O2長期', r.secO2TrimLtPct.toStringAsFixed(1), '%'),
                ],
              ),
          ],
        ),
      ),
    );
  }

  Widget _obdItem(String label, String value, String unit) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(label, style: const TextStyle(fontSize: 10, color: Colors.grey)),
        Text(
          unit.isEmpty ? value : '$value $unit',
          style: const TextStyle(
              fontSize: 14, fontWeight: FontWeight.w600, color: AppColors.primary),
        ),
      ],
    );
  }
}
