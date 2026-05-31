import 'dart:async';
import 'dart:io';
import 'dart:typed_data';

import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:permission_handler/permission_handler.dart';

// 計測サービス
const _kMeasService  = 'f3a8b2c1-d4e5-4f6a-7b8c-9d0e1f2a3b4c';
const _kVoltMainChar = 'f3a8b2c2-d4e5-4f6a-7b8c-9d0e1f2a3b4c';  // float32, V
const _kCurrChar     = 'f3a8b2c3-d4e5-4f6a-7b8c-9d0e1f2a3b4c';  // float32, A
const _kPwrChar      = 'f3a8b2c4-d4e5-4f6a-7b8c-9d0e1f2a3b4c';  // float32, W
const _kVoltSubChar  = 'f3a8b2c5-d4e5-4f6a-7b8c-9d0e1f2a3b4c';  // float32, V

// 設定サービス（ペアリング認証必要）
const _kCfgService      = 'f3a8b2d1-d4e5-4f6a-7b8c-9d0e1f2a3b4c';
const _kAhOffsetChar    = 'f3a8b2d2-d4e5-4f6a-7b8c-9d0e1f2a3b4c';  // int32, Ah
const _kChgTimeoutChar  = 'f3a8b2d3-d4e5-4f6a-7b8c-9d0e1f2a3b4c';  // uint32, 分
const _kChgStartVChar   = 'f3a8b2d4-d4e5-4f6a-7b8c-9d0e1f2a3b4c';  // float32, V
const _kChgStopVChar    = 'f3a8b2d5-d4e5-4f6a-7b8c-9d0e1f2a3b4c';  // float32, V

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
  double? _vMain, _curr, _pwr, _vSub;

  // 設定値
  int?    _ahOffset;
  int?    _chgTimeout;
  double? _chgStartV;
  double? _chgStopV;

  // Characteristic 参照
  BluetoothCharacteristic? _cAhOffset;
  BluetoothCharacteristic? _cChgTimeout;
  BluetoothCharacteristic? _cChgStartV;
  BluetoothCharacteristic? _cChgStopV;

  // テキスト入力コントローラ
  final _ctrlAhOffset   = TextEditingController();
  final _ctrlChgTimeout = TextEditingController();
  final _ctrlChgStartV  = TextEditingController();
  final _ctrlChgStopV   = TextEditingController();

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

    // ボンディング後に OS が自動接続済みの場合はスキャンを迂回（失敗時はスキャンへ自動フォールバック）
    final already = FlutterBluePlus.connectedDevices
        .where((d) => d.platformName.startsWith('car-iot-'))
        .toList();

    if (already.isNotEmpty) {
      try {
        _device = already.first;
        _deviceName = _device!.platformName;
        setState(() { _state = _ConnState.connecting; _deviceName = _deviceName; });
        _addLog('OS 接続済みデバイスを使用: $_deviceName', _LogType.sys);
        await _discoverAndSubscribe();
        return;
      } catch (_) {
        // 切断直後で stale だった場合はスキャンへフォールバック
        _addLog('OS 接続経由失敗 → スキャンへ切り替え', _LogType.sys);
        _cleanup();
      }
    }

    // 通常スキャン
    try {
      setState(() => _state = _ConnState.scanning);
      _addLog('スキャン開始...', _LogType.sys);

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
      await _discoverAndSubscribe();
    } catch (e) {
      _addLog('エラー: $e', _LogType.err);
      await FlutterBluePlus.stopScan();
      _cleanup();
    }
  }

  Future<void> _discoverAndSubscribe() async {
    final services = await _device!.discoverServices();

    // 計測サービス: Notify 購読
    final measSvc = _findService(services, _kMeasService);
    if (measSvc != null) {
      for (final (key, uuid) in [
        ('vMain', _kVoltMainChar),
        ('curr',  _kCurrChar),
        ('pwr',   _kPwrChar),
        ('vSub',  _kVoltSubChar),
      ]) {
        final c = _findChar(measSvc, uuid);
        if (c != null) {
          await c.setNotifyValue(true);
          _notifySubs.add(c.onValueReceived.listen((v) {
            final val = ByteData.sublistView(Uint8List.fromList(v))
                .getFloat32(0, Endian.little);
            setState(() {
              switch (key) {
                case 'vMain': _vMain = val;
                case 'curr':  _curr  = val;
                case 'pwr':   _pwr   = val;
                case 'vSub':  _vSub  = val;
              }
            });
          }));
        }
      }
      _addLog('計測サービス: Notify 開始', _LogType.sys);
    }

    // 設定サービス: Characteristic 取得 + 初期値 Read
    final cfgSvc = _findService(services, _kCfgService);
    if (cfgSvc != null) {
      _cAhOffset   = _findChar(cfgSvc, _kAhOffsetChar);
      _cChgTimeout = _findChar(cfgSvc, _kChgTimeoutChar);
      _cChgStartV  = _findChar(cfgSvc, _kChgStartVChar);
      _cChgStopV   = _findChar(cfgSvc, _kChgStopVChar);
      await _readConfig();
      _addLog('設定サービス: 初期値読み込み完了', _LogType.sys);
    } else {
      _addLog('設定サービスなし（未ペアリング）', _LogType.sys);
    }

    _connSub = _device!.connectionState.listen((s) {
      if (s == BluetoothConnectionState.disconnected) {
        _addLog('切断されました', _LogType.sys);
        _cleanup();
      }
    });

    setState(() => _state = _ConnState.connected);
    _addLog('接続完了', _LogType.sys);
  }

  // ---------- 設定 Read ----------

  Future<void> _readConfig() async {
    Future<void> readInt32(BluetoothCharacteristic? c, String label, String unit,
        void Function(int) onVal) async {
      if (c == null) return;
      try {
        final v = await c.read();
        final val = ByteData.sublistView(Uint8List.fromList(v)).getInt32(0, Endian.little);
        onVal(val);
        _addLog('$label: $val $unit', _LogType.rx);
      } catch (e) {
        _addLog('$label 読み込みエラー: $e', _LogType.err);
      }
    }

    Future<void> readUint32(BluetoothCharacteristic? c, String label, String unit,
        void Function(int) onVal) async {
      if (c == null) return;
      try {
        final v = await c.read();
        final val = ByteData.sublistView(Uint8List.fromList(v)).getUint32(0, Endian.little);
        onVal(val);
        _addLog('$label: $val $unit', _LogType.rx);
      } catch (e) {
        _addLog('$label 読み込みエラー: $e', _LogType.err);
      }
    }

    Future<void> readFloat(BluetoothCharacteristic? c, String label, String unit,
        void Function(double) onVal) async {
      if (c == null) return;
      try {
        final v = await c.read();
        final val = ByteData.sublistView(Uint8List.fromList(v)).getFloat32(0, Endian.little);
        onVal(val);
        _addLog('$label: ${val.toStringAsFixed(2)} $unit', _LogType.rx);
      } catch (e) {
        _addLog('$label 読み込みエラー: $e', _LogType.err);
      }
    }

    await readInt32(_cAhOffset, 'Ah オフセット', 'Ah',
        (v) { setState(() { _ahOffset = v; _ctrlAhOffset.text = v.toString(); }); });
    await readUint32(_cChgTimeout, '充電タイムアウト', '分',
        (v) { setState(() { _chgTimeout = v; _ctrlChgTimeout.text = v.toString(); }); });
    await readFloat(_cChgStartV, '充電開始電圧', 'V',
        (v) { setState(() { _chgStartV = v; _ctrlChgStartV.text = v.toStringAsFixed(2); }); });
    await readFloat(_cChgStopV, '充電停止電圧', 'V',
        (v) { setState(() { _chgStopV = v; _ctrlChgStopV.text = v.toStringAsFixed(2); }); });
  }

  // ---------- 設定 Write ----------

  Future<void> _writeInt32(BluetoothCharacteristic? c, String label,
      String text, String unit, void Function(int) onSuccess) async {
    if (c == null) return;
    final val = int.tryParse(text.trim());
    if (val == null) return;
    try {
      final bytes = ByteData(4)..setInt32(0, val, Endian.little);
      await c.write(bytes.buffer.asUint8List());
      onSuccess(val);
      _addLog('→ $label: $val $unit', _LogType.tx);
    } catch (e) {
      _addLog('$label 書き込みエラー: $e', _LogType.err);
    }
  }

  Future<void> _writeUint32(BluetoothCharacteristic? c, String label,
      String text, String unit, void Function(int) onSuccess) async {
    if (c == null) return;
    final val = int.tryParse(text.trim());
    if (val == null || val < 0) return;
    try {
      final bytes = ByteData(4)..setUint32(0, val, Endian.little);
      await c.write(bytes.buffer.asUint8List());
      onSuccess(val);
      _addLog('→ $label: $val $unit', _LogType.tx);
    } catch (e) {
      _addLog('$label 書き込みエラー: $e', _LogType.err);
    }
  }

  Future<void> _writeFloat(BluetoothCharacteristic? c, String label,
      String text, String unit, void Function(double) onSuccess) async {
    if (c == null) return;
    final val = double.tryParse(text.trim());
    if (val == null) return;
    try {
      final bytes = ByteData(4)..setFloat32(0, val, Endian.little);
      await c.write(bytes.buffer.asUint8List());
      onSuccess(val);
      _addLog('→ $label: ${val.toStringAsFixed(2)} $unit', _LogType.tx);
    } catch (e) {
      _addLog('$label 書き込みエラー: $e', _LogType.err);
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
    _cAhOffset = _cChgTimeout = _cChgStartV = _cChgStopV = null;
    setState(() {
      _state = _ConnState.disconnected;
      _deviceName = '';
      _vMain = _curr = _pwr = _vSub = null;
      _ahOffset = _chgTimeout = null;
      _chgStartV = _chgStopV = null;
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
      return null;
    }
  }

  @override
  void dispose() {
    for (final sub in _notifySubs) { sub.cancel(); }
    _connSub?.cancel();
    _logScroll.dispose();
    _ctrlAhOffset.dispose();
    _ctrlChgTimeout.dispose();
    _ctrlChgStartV.dispose();
    _ctrlChgStopV.dispose();
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
          _MeasCard(vMain: _vMain, curr: _curr, pwr: _pwr, vSub: _vSub),
          const SizedBox(height: 12),
          _ConfigCard(
            enabled: isConn,
            ahOffset:   _ahOffset,
            chgTimeout: _chgTimeout,
            chgStartV:  _chgStartV,
            chgStopV:   _chgStopV,
            ctrlAhOffset:   _ctrlAhOffset,
            ctrlChgTimeout: _ctrlChgTimeout,
            ctrlChgStartV:  _ctrlChgStartV,
            ctrlChgStopV:   _ctrlChgStopV,
            onWriteAhOffset:   () => _writeInt32(_cAhOffset, 'Ah オフセット',
                _ctrlAhOffset.text, 'Ah', (v) => setState(() => _ahOffset = v)),
            onWriteChgTimeout: () => _writeUint32(_cChgTimeout, '充電タイムアウト',
                _ctrlChgTimeout.text, '分', (v) => setState(() => _chgTimeout = v)),
            onWriteChgStartV:  () => _writeFloat(_cChgStartV, '充電開始電圧',
                _ctrlChgStartV.text, 'V', (v) => setState(() => _chgStartV = v)),
            onWriteChgStopV:   () => _writeFloat(_cChgStopV, '充電停止電圧',
                _ctrlChgStopV.text, 'V', (v) => setState(() => _chgStopV = v)),
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
  final double? vMain, curr, pwr, vSub;
  const _MeasCard({this.vMain, this.curr, this.pwr, this.vSub});

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

// ---------- 設定カード ----------

class _ConfigCard extends StatelessWidget {
  final bool enabled;
  final int?    ahOffset, chgTimeout;
  final double? chgStartV, chgStopV;
  final TextEditingController ctrlAhOffset, ctrlChgTimeout, ctrlChgStartV, ctrlChgStopV;
  final VoidCallback onWriteAhOffset, onWriteChgTimeout, onWriteChgStartV, onWriteChgStopV;
  final VoidCallback onRead;

  const _ConfigCard({
    required this.enabled,
    required this.ahOffset,
    required this.chgTimeout,
    required this.chgStartV,
    required this.chgStopV,
    required this.ctrlAhOffset,
    required this.ctrlChgTimeout,
    required this.ctrlChgStartV,
    required this.ctrlChgStopV,
    required this.onWriteAhOffset,
    required this.onWriteChgTimeout,
    required this.onWriteChgStartV,
    required this.onWriteChgStopV,
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
                _cardLabel('設定（要ペアリング）'),
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
              label: 'Ah オフセット', unit: 'Ah',
              current: ahOffset?.toString(),
              ctrl: ctrlAhOffset,
              keyboardType: const TextInputType.numberWithOptions(signed: true),
              enabled: enabled,
              onWrite: onWriteAhOffset,
            ),
            const SizedBox(height: 14),
            _cfgRow(
              label: '充電タイムアウト', unit: '分',
              current: chgTimeout?.toString(),
              ctrl: ctrlChgTimeout,
              keyboardType: TextInputType.number,
              enabled: enabled,
              onWrite: onWriteChgTimeout,
            ),
            const SizedBox(height: 14),
            _cfgRow(
              label: '充電開始電圧', unit: 'V',
              current: chgStartV?.toStringAsFixed(2),
              ctrl: ctrlChgStartV,
              keyboardType: const TextInputType.numberWithOptions(decimal: true),
              enabled: enabled,
              onWrite: onWriteChgStartV,
            ),
            const SizedBox(height: 14),
            _cfgRow(
              label: '充電停止電圧', unit: 'V',
              current: chgStopV?.toStringAsFixed(2),
              ctrl: ctrlChgStopV,
              keyboardType: const TextInputType.numberWithOptions(decimal: true),
              enabled: enabled,
              onWrite: onWriteChgStopV,
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
                  contentPadding: EdgeInsets.symmetric(horizontal: 12, vertical: 10),
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
