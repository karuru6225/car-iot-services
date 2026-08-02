import 'dart:convert';

import 'package:shared_preferences/shared_preferences.dart';

import '../models/meter_slot.dart';
import '../models/obd_metric.dart';

// メーター画面のタイル構成（項目×表示形式のリスト）を端末に永続化する。
class MeterConfigService {
  static const _key = 'meter_slots_v1';

  Future<List<MeterSlot>> load() async {
    final prefs = await SharedPreferences.getInstance();
    final raw = prefs.getString(_key);
    if (raw == null) return _defaultSlots();

    try {
      final decoded = jsonDecode(raw) as List;
      final slots = decoded
          .map((e) => MeterSlot.fromJson(e as Map<String, dynamic>))
          .whereType<MeterSlot>()
          .toList();
      return slots.isEmpty ? _defaultSlots() : slots;
    } catch (_) {
      // 保存形式が壊れている・enum値が読めない等はデフォルトへフォールバック
      return _defaultSlots();
    }
  }

  Future<void> save(List<MeterSlot> slots) async {
    final prefs = await SharedPreferences.getInstance();
    final raw = jsonEncode(slots.map((s) => s.toJson()).toList());
    await prefs.setString(_key, raw);
  }

  List<MeterSlot> _defaultSlots() => [
        for (final (metric, style) in defaultMeterMetrics)
          MeterSlot(metric: metric, style: style),
      ];
}
