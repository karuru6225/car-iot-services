import 'package:flutter/material.dart';

import '../models/meter_slot.dart';
import '../models/obd_metric.dart';
import '../models/obd_reading.dart';
import '../services/meter_config_service.dart';
import '../theme/app_colors.dart';
import '../widgets/card_widgets.dart';
import '../widgets/meter_settings_sheet.dart';
import '../widgets/meter_tile.dart';

class MeterScreen extends StatefulWidget {
  final ObdReading? reading;
  final Map<ObdMetric, List<double>> history;

  const MeterScreen({super.key, required this.reading, required this.history});

  @override
  State<MeterScreen> createState() => _MeterScreenState();
}

class _MeterScreenState extends State<MeterScreen> {
  final _configService = MeterConfigService();
  List<MeterSlot> _slots = [];
  bool _loading = true;

  @override
  void initState() {
    super.initState();
    _configService.load().then((slots) {
      if (mounted) setState(() { _slots = slots; _loading = false; });
    });
  }

  Future<void> _openSettings() async {
    final updated = await showModalBottomSheet<List<MeterSlot>>(
      context: context,
      isScrollControlled: true,
      builder: (_) => MeterSettingsSheet(initial: _slots),
    );
    if (updated != null) {
      setState(() => _slots = updated);
      await _configService.save(updated);
    }
  }

  @override
  Widget build(BuildContext context) {
    if (_loading) {
      return const Center(child: CircularProgressIndicator());
    }
    return Column(
      children: [
        Padding(
          padding: const EdgeInsets.fromLTRB(12, 12, 12, 0),
          child: Align(
            alignment: Alignment.centerRight,
            child: actionButton('項目を編集', _openSettings, enabled: true, color: AppColors.primary),
          ),
        ),
        Expanded(
          child: _slots.isEmpty
              ? const Center(
                  child: Padding(
                    padding: EdgeInsets.all(24),
                    child: Text(
                      '表示する項目がありません。「項目を編集」から追加してください',
                      textAlign: TextAlign.center,
                      style: TextStyle(color: Colors.grey),
                    ),
                  ),
                )
              : GridView.builder(
                  padding: const EdgeInsets.all(12),
                  gridDelegate: const SliverGridDelegateWithFixedCrossAxisCount(
                    crossAxisCount: 2,
                    mainAxisSpacing: 12,
                    crossAxisSpacing: 12,
                    childAspectRatio: 1.1,
                  ),
                  itemCount: _slots.length,
                  itemBuilder: (context, i) {
                    final slot = _slots[i];
                    return MeterTile(
                      slot: slot,
                      reading: widget.reading,
                      history: widget.history[slot.metric] ?? const [],
                    );
                  },
                ),
        ),
      ],
    );
  }
}
