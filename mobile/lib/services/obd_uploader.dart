import 'dart:async';
import 'dart:convert';

import 'package:http/http.dart' as http;

import '../config.dart';
import '../models/obd_reading.dart';
import 'auth_service.dart';

// OBD-IIデータをメモリ上にバッファし、件数上限／時間経過／BLE切断（呼び出し側からflush()を
// 呼ぶ）のいずれか早い方でAWSへバッチアップロードする。アプリが落ちたらバッファは失われる
// （永続化しない）。
class ObdUploader {
  ObdUploader({
    required AuthService auth,
    this.onLog,
  }) : _auth = auth;

  static const _apiEndpoint = AppConfig.apiEndpoint;

  static const _maxBatchSize = 100;
  static const _flushInterval = Duration(minutes: 5);
  static const _maxBufferHardCap = 6000; // 異常系（長時間送信失敗が続いた場合）の保険

  final AuthService _auth;
  void Function(String msg)? onLog;

  // 接続成功時に呼び出し側（ble_home_screen.dart）が設定する。バッファと同じ場所で保持し、
  // 切断後もflush()が正しいdevice_idで再送できるようにする（切断してもクリアしない）。
  String deviceId = '';

  final _buffer = <ObdReading>[];
  Timer? _flushTimer;
  bool _uploading = false;

  void start() {
    _flushTimer ??= Timer.periodic(_flushInterval, (_) => flush());
  }

  void add(ObdReading reading) {
    if (!reading.valid) return; // IGN OFF等の無応答データはアップロード対象外
    _buffer.add(reading);
    if (_buffer.length >= _maxBatchSize) {
      flush();
    } else if (_buffer.length > _maxBufferHardCap) {
      _buffer.removeRange(0, _buffer.length - _maxBufferHardCap); // 古いものから間引く
    }
  }

  Future<void> flush() async {
    if (_buffer.isEmpty || _uploading) return;
    _uploading = true;
    final batch = List<ObdReading>.from(_buffer);
    try {
      final token = await _auth.accessToken;
      if (token == null) {
        onLog?.call('OBDアップロード未送信: 未ログイン');
        return;
      }

      final resp = await http
          .post(
            Uri.parse('$_apiEndpoint/obd'),
            headers: {
              'Authorization': 'Bearer $token',
              'Content-Type': 'application/json',
            },
            body: jsonEncode({
              'device_id': deviceId,
              'readings': batch.map(_toJson).toList(),
            }),
          )
          .timeout(const Duration(seconds: 15));

      if (resp.statusCode == 200) {
        // flush中に追加された分を誤って消さないよう、送信済み件数分だけ先頭から除去する
        _buffer.removeRange(0, batch.length);
        onLog?.call('OBDアップロード成功: ${batch.length}件');
      } else {
        onLog?.call('OBDアップロード失敗 (${resp.statusCode})、次回リトライ');
      }
    } catch (e) {
      onLog?.call('OBDアップロードエラー: $e、次回リトライ');
    } finally {
      _uploading = false;
    }
  }

  Map<String, dynamic> _toJson(ObdReading r) => {
        'ts': r.ts,
        'rpm': r.rpm,
        'speedKmh': r.speedKmh,
        'loadPct': r.loadPct,
        'mapKpa': r.mapKpa,
        'baroKpa': r.baroKpa,
        'boostKpa': r.boostKpa,
        'throttlePct': r.throttlePct,
        'timingDeg': r.timingDeg,
        'ecuVoltage': r.ecuVoltage,
        'mafGs': r.mafGs,
        'coolantC': r.coolantC,
        'fuelRateLph': r.fuelRateLph,
        'stftPct': r.stftPct,
        'ltftPct': r.ltftPct,
        'o2B1s2V': r.o2B1s2V,
        'o2B1s2TrimPct': r.o2B1s2TrimPct,
        'engineRunTimeSec': r.engineRunTimeSec,
        'milDistanceKm': r.milDistanceKm,
        'o2S1Ratio': r.o2S1Ratio,
        'o2S1Voltage': r.o2S1Voltage,
        'evapPurgePct': r.evapPurgePct,
        'warmupsSinceCleared': r.warmupsSinceCleared,
        'distanceSinceClearedKm': r.distanceSinceClearedKm,
        'catalystTempC': r.catalystTempC,
        'absoluteLoadPct': r.absoluteLoadPct,
        'commandedAfr': r.commandedAfr,
        'throttleBPct': r.throttleBPct,
        'accelPedalDPct': r.accelPedalDPct,
        'accelPedalEPct': r.accelPedalEPct,
        'fuelType': r.fuelType,
        'secO2TrimStPct': r.secO2TrimStPct,
        'secO2TrimLtPct': r.secO2TrimLtPct,
        'valid': r.valid,
      };

  void dispose() {
    _flushTimer?.cancel();
    _flushTimer = null;
  }
}
