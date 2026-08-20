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

    final slots = _decode(raw);
    return (slots == null || slots.isEmpty) ? _defaultSlots() : slots;
  }

  Future<void> save(List<MeterSlot> slots) async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setString(_key, _encode(slots));
  }

  // ファイルエクスポート用。save()と同じJSON形式（他端末・他バージョンとの互換のため
  // フォーマットを分けない）。
  String exportToJson(List<MeterSlot> slots) => _encode(slots);

  // ファイルインポート用。壊れたJSON・enum値が読めない等はnullを返す（呼び出し側で
  // エラー表示に使う）。load()と違いデフォルト設定へのフォールバックはしない——
  // インポート操作でユーザーが意図せずデフォルトに戻されると気づきにくいため。
  List<MeterSlot>? importFromJson(String raw) => _decode(raw);

  String _encode(List<MeterSlot> slots) =>
      jsonEncode(slots.map((s) => s.toJson()).toList());

  // 保存形式が壊れている・enum値が読めない等はnullを返す。
  List<MeterSlot>? _decode(String raw) {
    try {
      final decoded = jsonDecode(raw) as List;
      return decoded
          .map((e) => MeterSlot.fromJson(e as Map<String, dynamic>))
          .whereType<MeterSlot>()
          .toList();
    } catch (_) {
      return null;
    }
  }

  List<MeterSlot> _defaultSlots() => [
        for (final (metric, style) in defaultMeterMetrics)
          MeterSlot(metric: metric, style: style),
      ];
}
