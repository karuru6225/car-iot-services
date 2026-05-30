import 'dart:async';
import 'dart:io';
import 'dart:typed_data';

import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:permission_handler/permission_handler.dart';

// カスタム計測サービス
const _kMeasService = 'f3a8b2c1-d4e5-4f6a-7b8c-9d0e1f2a3b4c';
const _kVoltChar    = 'f3a8b2c2-d4e5-4f6a-7b8c-9d0e1f2a3b4c';  // float32, V
const _kCurrentChar = 'f3a8b2c3-d4e5-4f6a-7b8c-9d0e1f2a3b4c';  // float32, A
const _kPowerChar   = 'f3a8b2c4-d4e5-4f6a-7b8c-9d0e1f2a3b4c';  // float32, W
const _kTempChar    = 'f3a8b2c5-d4e5-4f6a-7b8c-9d0e1f2a3b4c';  // float32, °C

// カスタム設定サービス
const _kCfgService   = 'f3a8b2d1-d4e5-4f6a-7b8c-9d0e1f2a3b4c';
const _kLowVoltChar  = 'f3a8b2d2-d4e5-4f6a-7b8c-9d0e1f2a3b4c';  // float32, V
const _kSleepSecChar = 'f3a8b2d3-d4e5-4f6a-7b8c-9d0e1f2a3b4c';  // uint32, sec

void main() => runApp(const _App());

class _App extends StatelessWidget {
  const _App();

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'BLE ダッシュボード',
      debugShowCheckedModeBanner: false,
      theme: ThemeData(
        colorScheme: const ColorScheme.dark(
          primary: Color(0xFF4F8EF7),
          surface: Color(0xFF16213E),
        ),
        scaffoldBackgroundColor: const Color(0xFF1A1A2E),
        cardColor: const Color(0xFF16213E),
        useMaterial3: true,
      ),
      home: const BleHome(),
    );
  }
}

enum _ConnState { disconnected, scanning, connecting, connected }

enum _LogType { sys, rx, tx, err }

class _LogEntry {
  final DateTime time;
  final String msg;
  final _LogType type;
  _LogEntry(this.msg, this.type) : time = DateTime.now();
}

class BleHome extends StatefulWidget {
  const BleHome({super.key});

  @override
  State<BleHome> createState() => _BleHomeState();
}

class _BleHomeState extends State<BleHome> {
  _ConnState _state = _ConnState.disconnected;
  String _deviceName = '';

  // 計測値
  final Map<String, double?> _meas = {
    'voltage': null, 'current': null, 'power': null, 'temp': null,
  };

  // 設定値
  double?  _lowVolt;
  int?     _sleepSec;
  BluetoothCharacteristic? _lowVoltChar;
  BluetoothCharacteristic? _sleepSecChar;
  final _lowVoltCtrl  = TextEditingController();
  final _sleepSecCtrl = TextEditingController();

  final List<_LogEntry> _log = [];
  final _logScroll = ScrollController();

  BluetoothDevice? _device;
  final List<StreamSubscription> _notifySubs = [];
  StreamSubscription? _connSub;

  // ---------- ログ ----------

  void _addLog(String msg, _LogType type) {
    setState(() {
      _log.add(_LogEntry(msg, type));
      if (_log.length > 300) _log.removeAt(0);
    });
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (_logScroll.hasClients) {
        _logScroll.jumpTo(_logScroll.position.maxScrollExtent);
      }
    });
  }

  // ---------- パーミッション ----------

  Future<bool> _requestPermissions() async {
    if (!Platform.isAndroid) return true;
    final statuses = await [
      Permission.bluetoothScan,
      Permission.bluetoothConnect,
    ].request();
    return statuses.values.every((s) => s.isGranted);
  }

  // ---------- 接続 ----------

  Future<void> _connect() async {
    if (!await _requestPermissions()) {
      _addLog('Bluetooth 権限が必要です', _LogType.err);
      return;
    }

    setState(() => _state = _ConnState.scanning);
    _addLog('スキャン開始...', _LogType.sys);

    try {
      final found = Completer<BluetoothDevice>();
      final scanSub = FlutterBluePlus.onScanResults.listen((results) {
        if (results.isNotEmpty && !found.isCompleted) {
          _addLog('発見: ${results.first.device.platformName}', _LogType.sys);
          found.complete(results.first.device);
        }
      });

      await FlutterBluePlus.startScan(
        withServices: [Guid(_kMeasService)],
        timeout: const Duration(seconds: 10),
      );

      _device = await found.future.timeout(
        const Duration(seconds: 12),
        onTimeout: () => throw TimeoutException('デバイスが見つかりません (10s)'),
      );
      scanSub.cancel();
      await FlutterBluePlus.stopScan();

      setState(() {
        _state = _ConnState.connecting;
        _deviceName = _device!.platformName;
      });
      _addLog('接続中: $_deviceName', _LogType.sys);

      await _device!.connect(timeout: const Duration(seconds: 10));
      final services = await _device!.discoverServices();

      // 計測サービス: Notify 購読
      final measSvc = _findService(services, _kMeasService);
      if (measSvc != null) {
        for (final (key, uuid) in [
          ('voltage', _kVoltChar),
          ('current', _kCurrentChar),
          ('power',   _kPowerChar),
          ('temp',    _kTempChar),
        ]) {
          final c = _findChar(measSvc, uuid);
          if (c != null) {
            await c.setNotifyValue(true);
            _notifySubs.add(c.onValueReceived.listen((v) {
              final val = ByteData.sublistView(Uint8List.fromList(v))
                  .getFloat32(0, Endian.little);
              setState(() => _meas[key] = val);
            }));
          }
        }
        _addLog('計測サービス: Notify 開始', _LogType.sys);
      }

      // 設定サービス: Characteristic 取得 + 初期値 Read
      final cfgSvc = _findService(services, _kCfgService);
      if (cfgSvc != null) {
        _lowVoltChar  = _findChar(cfgSvc, _kLowVoltChar);
        _sleepSecChar = _findChar(cfgSvc, _kSleepSecChar);
        await _readConfig();
        _addLog('設定サービス: 初期値読み込み完了', _LogType.sys);
      }

      _connSub = _device!.connectionState.listen((s) {
        if (s == BluetoothConnectionState.disconnected) {
          _addLog('切断されました', _LogType.sys);
          _cleanup();
        }
      });

      setState(() => _state = _ConnState.connected);
      _addLog('接続完了', _LogType.sys);
    } catch (e) {
      _addLog('エラー: $e', _LogType.err);
      await FlutterBluePlus.stopScan();
      _cleanup();
    }
  }

  // ---------- 設定 Read ----------

  Future<void> _readConfig() async {
    try {
      if (_lowVoltChar != null) {
        final v = await _lowVoltChar!.read();
        final val = ByteData.sublistView(Uint8List.fromList(v))
            .getFloat32(0, Endian.little);
        setState(() => _lowVolt = val);
        _lowVoltCtrl.text = val.toStringAsFixed(2);
        _addLog('低電圧閾値: ${val.toStringAsFixed(2)} V', _LogType.rx);
      }
      if (_sleepSecChar != null) {
        final v = await _sleepSecChar!.read();
        final val = ByteData.sublistView(Uint8List.fromList(v))
            .getUint32(0, Endian.little);
        setState(() => _sleepSec = val);
        _sleepSecCtrl.text = val.toString();
        _addLog('スリープ間隔: $val 秒', _LogType.rx);
      }
    } catch (e) {
      _addLog('設定読み込みエラー: $e', _LogType.err);
    }
  }

  // ---------- 設定 Write ----------

  Future<void> _writeLowVolt() async {
    final text = _lowVoltCtrl.text.trim();
    final val  = double.tryParse(text);
    if (val == null || _lowVoltChar == null) return;
    try {
      final bytes = ByteData(4)..setFloat32(0, val, Endian.little);
      await _lowVoltChar!.write(bytes.buffer.asUint8List());
      setState(() => _lowVolt = val);
      _addLog('→ 低電圧閾値: ${val.toStringAsFixed(2)} V', _LogType.tx);
    } catch (e) {
      _addLog('書き込みエラー: $e', _LogType.err);
    }
  }

  Future<void> _writeSleepSec() async {
    final text = _sleepSecCtrl.text.trim();
    final val  = int.tryParse(text);
    if (val == null || val <= 0 || _sleepSecChar == null) return;
    try {
      final bytes = ByteData(4)..setUint32(0, val, Endian.little);
      await _sleepSecChar!.write(bytes.buffer.asUint8List());
      setState(() => _sleepSec = val);
      _addLog('→ スリープ間隔: $val 秒', _LogType.tx);
    } catch (e) {
      _addLog('書き込みエラー: $e', _LogType.err);
    }
  }

  // ---------- 切断 ----------

  Future<void> _disconnect() async {
    await _device?.disconnect();
    _addLog('手動切断', _LogType.sys);
    _cleanup();
  }

  void _cleanup() {
    for (final sub in _notifySubs) { sub.cancel(); }
    _notifySubs.clear();
    _connSub?.cancel();
    _connSub = null;
    _lowVoltChar  = null;
    _sleepSecChar = null;
    setState(() {
      _state = _ConnState.disconnected;
      _deviceName = '';
      _meas.updateAll((_, __) => null);
      _lowVolt  = null;
      _sleepSec = null;
    });
  }

  // ---------- UUID 検索 ----------

  BluetoothService? _findService(List<BluetoothService> services, String uuid) {
    try {
      return services.firstWhere(
        (s) => s.serviceUuid.toString().toLowerCase() == uuid.toLowerCase(),
      );
    } catch (_) {
      _addLog('サービスが見つかりません: $uuid', _LogType.err);
      return null;
    }
  }

  BluetoothCharacteristic? _findChar(BluetoothService svc, String uuid) {
    try {
      return svc.characteristics.firstWhere(
        (c) => c.characteristicUuid.toString().toLowerCase() == uuid.toLowerCase(),
      );
    } catch (_) {
      _addLog('Characteristic が見つかりません: $uuid', _LogType.err);
      return null;
    }
  }

  @override
  void dispose() {
    for (final sub in _notifySubs) { sub.cancel(); }
    _connSub?.cancel();
    _logScroll.dispose();
    _lowVoltCtrl.dispose();
    _sleepSecCtrl.dispose();
    super.dispose();
  }

  // ---------- UI ----------

  @override
  Widget build(BuildContext context) {
    final isConn = _state == _ConnState.connected;
    return Scaffold(
      appBar: AppBar(
        title: const Text('BLE ダッシュボード'),
        backgroundColor: const Color(0xFF16213E),
        foregroundColor: const Color(0xFF4F8EF7),
      ),
      body: ListView(
        padding: const EdgeInsets.all(16),
        children: [
          _ConnCard(
            state: _state,
            deviceName: _deviceName,
            onConnect: _connect,
            onDisconnect: _disconnect,
          ),
          const SizedBox(height: 12),
          _MeasCard(data: _meas),
          const SizedBox(height: 12),
          _ConfigCard(
            enabled: isConn,
            lowVolt: _lowVolt,
            sleepSec: _sleepSec,
            lowVoltCtrl: _lowVoltCtrl,
            sleepSecCtrl: _sleepSecCtrl,
            onWriteLowVolt: _writeLowVolt,
            onWriteSleepSec: _writeSleepSec,
            onRead: _readConfig,
          ),
          const SizedBox(height: 12),
          _LogCard(log: _log, scroll: _logScroll),
          const SizedBox(height: 24),
        ],
      ),
    );
  }
}

// ---------- 接続カード ----------

class _ConnCard extends StatelessWidget {
  final _ConnState state;
  final String deviceName;
  final VoidCallback onConnect;
  final VoidCallback onDisconnect;

  const _ConnCard({
    required this.state,
    required this.deviceName,
    required this.onConnect,
    required this.onDisconnect,
  });

  @override
  Widget build(BuildContext context) {
    final (dotColor, label) = switch (state) {
      _ConnState.disconnected => (const Color(0xFFE74C3C), '未接続'),
      _ConnState.scanning     => (const Color(0xFFF39C12), 'スキャン中...'),
      _ConnState.connecting   => (const Color(0xFFF39C12), '接続中...'),
      _ConnState.connected    => (const Color(0xFF2ECC71), '接続済み'),
    };
    final isBusy = state == _ConnState.scanning || state == _ConnState.connecting;
    final isConn = state == _ConnState.connected;

    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            _cardLabel('接続'),
            const SizedBox(height: 12),
            Row(
              children: [
                Container(
                  width: 12, height: 12,
                  decoration: BoxDecoration(color: dotColor, shape: BoxShape.circle),
                ),
                const SizedBox(width: 10),
                Text(label),
                if (deviceName.isNotEmpty) ...[
                  const Spacer(),
                  Text(deviceName,
                      style: const TextStyle(color: Colors.grey, fontSize: 12)),
                ],
              ],
            ),
            const SizedBox(height: 12),
            Row(
              children: [
                _btn('接続', onConnect,
                    enabled: !isConn && !isBusy,
                    color: const Color(0xFF4F8EF7)),
                const SizedBox(width: 10),
                _btn('切断', onDisconnect,
                    enabled: isConn,
                    color: const Color(0xFFC0392B)),
              ],
            ),
          ],
        ),
      ),
    );
  }
}

// ---------- 計測カード ----------

class _MeasCard extends StatelessWidget {
  final Map<String, double?> data;
  const _MeasCard({required this.data});

  @override
  Widget build(BuildContext context) {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            _cardLabel('計測値'),
            const SizedBox(height: 12),
            GridView.count(
              crossAxisCount: 2,
              shrinkWrap: true,
              physics: const NeverScrollableScrollPhysics(),
              childAspectRatio: 2.2,
              mainAxisSpacing: 12,
              crossAxisSpacing: 12,
              children: [
                _dataItem('電圧', data['voltage'], 'V',  3),
                _dataItem('電流', data['current'], 'A',  3),
                _dataItem('電力', data['power'],   'W',  2),
                _dataItem('温度', data['temp'],    '°C', 2),
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

// ---------- 設定カード ----------

class _ConfigCard extends StatelessWidget {
  final bool enabled;
  final double? lowVolt;
  final int? sleepSec;
  final TextEditingController lowVoltCtrl;
  final TextEditingController sleepSecCtrl;
  final VoidCallback onWriteLowVolt;
  final VoidCallback onWriteSleepSec;
  final VoidCallback onRead;

  const _ConfigCard({
    required this.enabled,
    required this.lowVolt,
    required this.sleepSec,
    required this.lowVoltCtrl,
    required this.sleepSecCtrl,
    required this.onWriteLowVolt,
    required this.onWriteSleepSec,
    required this.onRead,
  });

  @override
  Widget build(BuildContext context) {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                _cardLabel('設定'),
                const Spacer(),
                TextButton(
                  onPressed: enabled ? onRead : null,
                  child: const Text('再読込',
                      style: TextStyle(fontSize: 12, color: Color(0xFF4F8EF7))),
                ),
              ],
            ),
            const SizedBox(height: 12),
            _cfgRow(
              label: '低電圧アラート閾値',
              unit: 'V',
              current: lowVolt?.toStringAsFixed(2),
              ctrl: lowVoltCtrl,
              keyboardType: const TextInputType.numberWithOptions(decimal: true),
              enabled: enabled,
              onWrite: onWriteLowVolt,
            ),
            const SizedBox(height: 16),
            _cfgRow(
              label: 'スリープ間隔',
              unit: '秒',
              current: sleepSec?.toString(),
              ctrl: sleepSecCtrl,
              keyboardType: TextInputType.number,
              enabled: enabled,
              onWrite: onWriteSleepSec,
            ),
          ],
        ),
      ),
    );
  }

  Widget _cfgRow({
    required String label,
    required String unit,
    required String? current,
    required TextEditingController ctrl,
    required TextInputType keyboardType,
    required bool enabled,
    required VoidCallback onWrite,
  }) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Row(
          children: [
            Text(label, style: const TextStyle(fontSize: 13)),
            const SizedBox(width: 6),
            Text('($unit)', style: const TextStyle(fontSize: 11, color: Colors.grey)),
            const Spacer(),
            Text(
              current != null ? '現在値: $current $unit' : '—',
              style: const TextStyle(fontSize: 11, color: Color(0xFF4F8EF7)),
            ),
          ],
        ),
        const SizedBox(height: 8),
        Row(
          children: [
            Expanded(
              child: TextField(
                controller: ctrl,
                enabled: enabled,
                keyboardType: keyboardType,
                decoration: const InputDecoration(
                  border: OutlineInputBorder(),
                  contentPadding:
                      EdgeInsets.symmetric(horizontal: 12, vertical: 10),
                  isDense: true,
                ),
                onSubmitted: enabled ? (_) => onWrite() : null,
              ),
            ),
            const SizedBox(width: 8),
            _btn('書込み', onWrite,
                enabled: enabled, color: const Color(0xFF4F8EF7)),
          ],
        ),
      ],
    );
  }
}

// ---------- ログカード ----------

class _LogCard extends StatelessWidget {
  final List<_LogEntry> log;
  final ScrollController scroll;
  const _LogCard({required this.log, required this.scroll});

  @override
  Widget build(BuildContext context) {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            _cardLabel('ログ'),
            const SizedBox(height: 8),
            Container(
              height: 200,
              decoration: BoxDecoration(
                color: const Color(0xFF0F1A2E),
                borderRadius: BorderRadius.circular(6),
              ),
              child: ListView.builder(
                controller: scroll,
                padding: const EdgeInsets.all(8),
                itemCount: log.length,
                itemBuilder: (_, i) {
                  final e = log[i];
                  final ts =
                      '${e.time.hour.toString().padLeft(2, '0')}:'
                      '${e.time.minute.toString().padLeft(2, '0')}:'
                      '${e.time.second.toString().padLeft(2, '0')}';
                  final msgColor = switch (e.type) {
                    _LogType.rx  => const Color(0xFF2ECC71),
                    _LogType.tx  => const Color(0xFFF39C12),
                    _LogType.err => const Color(0xFFE74C3C),
                    _LogType.sys => const Color(0xFF888888),
                  };
                  return RichText(
                    text: TextSpan(
                      style: const TextStyle(
                          fontFamily: 'monospace', fontSize: 12, height: 1.6),
                      children: [
                        TextSpan(text: '$ts  ',
                            style: const TextStyle(color: Color(0xFF4A6A8A))),
                        TextSpan(text: e.msg,
                            style: TextStyle(color: msgColor)),
                      ],
                    ),
                  );
                },
              ),
            ),
          ],
        ),
      ),
    );
  }
}

// ---------- ヘルパー ----------

Widget _cardLabel(String text) => Text(
      text,
      style: const TextStyle(
          fontSize: 11,
          color: Colors.grey,
          letterSpacing: 1,
          fontWeight: FontWeight.w600),
    );

Widget _btn(String label, VoidCallback onTap,
    {required bool enabled, required Color color}) {
  return ElevatedButton(
    onPressed: enabled ? onTap : null,
    style: ElevatedButton.styleFrom(
      backgroundColor: color,
      disabledBackgroundColor: const Color(0xFF2A4060),
      minimumSize: const Size(72, 44),
    ),
    child: Text(label, style: const TextStyle(color: Colors.white)),
  );
}
