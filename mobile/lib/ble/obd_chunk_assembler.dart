import 'dart:typed_data';

import '../models/obd_reading.dart';

// ESP32側 device/ble_peripheral.cpp の notifyObd() と対になる受信処理。
// [seq:1][total:1][payload] 形式のチャンクを seq==0 から集め、total個揃ったら結合してパースする。
// 総数が食い違う（取りこぼし等）場合は今回の seq==0 から集め直す（次サイクルで自然に復帰する想定）。
class ObdChunkAssembler {
  final Map<int, Uint8List> _chunks = {};
  int? _total;

  // チャンクを1つ追加する。全チャンクが揃ったらパースして返す。途中なら null。
  ObdReading? add(List<int> raw) {
    if (raw.length < 2) return null;
    final seq = raw[0];
    final total = raw[1];
    final payload = Uint8List.fromList(raw.sublist(2));

    if (seq == 0 || _total != total) {
      _chunks.clear();
      _total = total;
    }
    _chunks[seq] = payload;

    if (_chunks.length != total) return null;

    final builder = BytesBuilder();
    for (var i = 0; i < total; i++) {
      final chunk = _chunks[i];
      if (chunk == null) return null; // 途中が欠けていれば揃うまで待つ
      builder.add(chunk);
    }
    _chunks.clear();
    return ObdReading.fromBytes(builder.toBytes());
  }

  void reset() {
    _chunks.clear();
    _total = null;
  }
}
