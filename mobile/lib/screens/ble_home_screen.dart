import 'dart:async';
import 'dart:io';
import 'dart:typed_data';

import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:permission_handler/permission_handler.dart';
import 'package:wakelock_plus/wakelock_plus.dart';

import '../ble/ble_constants.dart';
import '../ble/obd_chunk_assembler.dart';
import '../models/conn_state.dart';
import '../models/log_entry.dart';
import '../models/obd_reading.dart';
import '../services/auth_service.dart';
import '../services/obd_uploader.dart';
import '../theme/app_colors.dart';
import '../widgets/auth_card.dart';
import '../widgets/conn_card.dart';
import '../widgets/debug_toggle_card.dart';
import '../widgets/log_card.dart';
import '../widgets/meas_card.dart';
import '../widgets/obd_card.dart';

class BleHome extends StatefulWidget {
  const BleHome({super.key});

  @override
  State<BleHome> createState() => _BleHomeState();
}

class _BleHomeState extends State<BleHome> {
  ConnState _state = ConnState.disconnected;
  String _deviceName = '';

  // 計測値
  double? _vMain, _curr, _pwr, _vSub;

  // OBD-II（チャンク分割で受信・再構成）
  ObdReading? _obdReading;
  final _obdAssembler = ObdChunkAssembler();

  // デバッグモード（接続画面のトグルでON時のみログカードを表示）
  bool _debugMode = false;
  int _tabIndex = 0;

  final List<LogEntry> _log = [];
  final _logScroll = ScrollController();

  BluetoothDevice? _device;
  bool _userDisconnected = false;
  bool _connectCancelled = false;
  final List<StreamSubscription> _notifySubs = [];
  StreamSubscription? _connSub;

  // OBDデータのAWS送信（認証・バッファアップロード）
  final _auth = AuthService();
  late final ObdUploader _uploader;
  String? _userEmail;
  bool _authBusy = false;

  @override
  void initState() {
    super.initState();
    _uploader = ObdUploader(auth: _auth, deviceIdProvider: () => _deviceName);
    _uploader.onLog = (m) => _addLog(m, LogType.sys);
    _uploader.start();
    _auth.tryRestoreSession().then((email) {
      if (mounted) setState(() => _userEmail = email);
    });
  }

  // ---------- 認証 ----------

  Future<void> _signIn() async {
    setState(() => _authBusy = true);
    final email = await _auth.signIn();
    if (!mounted) return;
    setState(() {
      _userEmail = email;
      _authBusy = false;
    });
    _addLog(email != null ? 'ログイン: $email' : 'ログインに失敗しました',
        email != null ? LogType.sys : LogType.err);
  }

  Future<void> _signOut() async {
    await _auth.signOut();
    if (!mounted) return;
    setState(() => _userEmail = null);
    _addLog('ログアウトしました', LogType.sys);
  }

  // ---------- ログ ----------

  void _addLog(String msg, LogType type) {
    setState(() {
      _log.add(LogEntry(msg, type));
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

  // デバイスは5分周期でしか BLE アドバタイズしないため、1回のスキャンで見つからなくても
  // kConnectRetryWindow の間は起床タイミングを待ってリトライし続ける。
  Future<void> _connect() async {
    if (!await _requestPermissions()) {
      _addLog('Bluetooth 権限が必要です', LogType.err);
      return;
    }

    _connectCancelled = false;
    final deadline = DateTime.now().add(kConnectRetryWindow);
    var attempt = 0;

    while (!_connectCancelled) {
      attempt++;
      if (await _tryConnectOnce(attempt)) return;
      if (_connectCancelled) return;

      if (DateTime.now().isAfter(deadline)) {
        _addLog('接続リトライを終了しました（${kConnectRetryWindow.inMinutes}分経過）', LogType.err);
        setState(() => _state = ConnState.disconnected);
        return;
      }
      await Future.delayed(kConnectRetryDelay);
    }
  }

  // 1回分の接続試行。成功したら true（この時点で _state == connected）
  Future<bool> _tryConnectOnce(int attempt) async {
    // ボンディング後に OS が自動接続済みの場合はスキャンを迂回（失敗時はスキャンへ自動フォールバック）
    final already = FlutterBluePlus.connectedDevices
        .where((d) => d.platformName.startsWith('car-iot-'))
        .toList();

    if (already.isNotEmpty) {
      try {
        _device = already.first;
        setState(() {
          _state = ConnState.connecting;
          _deviceName = _device!.platformName;
        });
        _addLog('OS 接続済みデバイスを使用: $_deviceName', LogType.sys);
        await _discoverAndSubscribe();
        return true;
      } catch (_) {
        // 切断直後で stale だった場合はスキャンへフォールバック
        _addLog('OS 接続経由失敗 → スキャンへ切り替え', LogType.sys);
        _cleanup();
      }
    }

    // 通常スキャン
    try {
      setState(() => _state = ConnState.scanning);
      _addLog('スキャン開始... ($attempt回目)', LogType.sys);

      final found = Completer<BluetoothDevice>();
      final scanSub = FlutterBluePlus.onScanResults.listen((results) {
        if (results.isNotEmpty && !found.isCompleted) {
          _addLog('発見: ${results.first.device.platformName}', LogType.sys);
          found.complete(results.first.device);
        }
      });

      await FlutterBluePlus.startScan(
        withServices: [Guid(kMeasService)],
        timeout: const Duration(seconds: 10),
      );

      _device = await found.future.timeout(
        const Duration(seconds: 12),
        onTimeout: () => throw TimeoutException('デバイスが見つかりません (10s)'),
      );
      scanSub.cancel();
      await FlutterBluePlus.stopScan();

      setState(() {
        _state = ConnState.connecting;
        _deviceName = _device!.platformName;
      });
      _addLog('接続中: $_deviceName', LogType.sys);

      await _device!.connect(timeout: const Duration(seconds: 10));
      await _discoverAndSubscribe();
      return true;
    } catch (e) {
      _addLog('エラー: $e', LogType.err);
      await FlutterBluePlus.stopScan();
      _cleanup();
      return false;
    }
  }

  // リトライ待機中（未接続）にユーザーが中止したい場合の入口
  Future<void> _cancelConnect() async {
    _connectCancelled = true;
    await FlutterBluePlus.stopScan();
    _addLog('接続試行を中止しました', LogType.sys);
    _cleanup();
  }

  Future<void> _discoverAndSubscribe() async {
    final services = await _device!.discoverServices();

    // 計測サービス: Notify 購読
    final measSvc = _findService(services, kMeasService);
    if (measSvc != null) {
      for (final (key, uuid) in [
        ('vMain', kVoltMainChar),
        ('curr',  kCurrChar),
        ('pwr',   kPwrChar),
        ('vSub',  kVoltSubChar),
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
      _addLog('計測サービス: Notify 開始', LogType.sys);

      // OBD-II: チャンク分割データのため専用ハンドラで再構成する
      final obdChar = _findChar(measSvc, kObdChar);
      if (obdChar != null) {
        await obdChar.setNotifyValue(true);
        _notifySubs.add(obdChar.onValueReceived.listen(_onObdChunk));
      }
    }

    _connSub = _device!.connectionState.listen((s) {
      if (s == BluetoothConnectionState.disconnected) {
        final autoReconnect = !_userDisconnected;
        _userDisconnected = false;
        _addLog(autoReconnect ? '予期しない切断: 自動再接続...' : '切断されました', LogType.sys);
        _uploader.flush();
        _cleanup();
        if (autoReconnect) {
          // ESP32 の起動・アドバタイズ開始を待ってからスキャン
          Future.delayed(const Duration(seconds: 3), _connect);
        }
      }
    });

    setState(() => _state = ConnState.connected);
    WakelockPlus.enable();
    _addLog('接続完了', LogType.sys);
  }

  // ---------- OBD-II チャンク受信 ----------

  void _onObdChunk(List<int> v) {
    try {
      final reading = _obdAssembler.add(v);
      if (reading != null) {
        setState(() => _obdReading = reading);
        _uploader.add(reading);
      }
    } catch (e) {
      _addLog('OBDデータ解析エラー: $e', LogType.err);
    }
  }

  // ---------- 切断 ----------

  Future<void> _disconnect() async {
    _userDisconnected = true;
    await _device?.disconnect();
    _addLog('手動切断', LogType.sys);
    _cleanup();
  }

  void _cleanup() {
    _cancelSubscriptions();
    _obdAssembler.reset();
    WakelockPlus.disable();
    setState(() {
      _state = ConnState.disconnected;
      _deviceName = '';
      _vMain = _curr = _pwr = _vSub = null;
      _obdReading = null;
    });
  }

  void _cancelSubscriptions() {
    for (final sub in _notifySubs) { sub.cancel(); }
    _notifySubs.clear();
    _connSub?.cancel();
    _connSub = null;
  }

  // ---------- UUID 検索 ----------

  BluetoothService? _findService(List<BluetoothService> services, String uuid) {
    try {
      return services.firstWhere(
        (s) => s.serviceUuid.toString().toLowerCase() == uuid.toLowerCase(),
      );
    } catch (_) {
      _addLog('サービスが見つかりません: $uuid', LogType.err);
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
    _cancelSubscriptions();
    _uploader.flush();
    _uploader.dispose();
    WakelockPlus.disable();
    _logScroll.dispose();
    super.dispose();
  }

  // ---------- UI ----------

  @override
  Widget build(BuildContext context) {
    final isConn = _state == ConnState.connected;
    return Scaffold(
      appBar: AppBar(
        title: const Text('BLE ダッシュボード'),
        backgroundColor: AppColors.surface,
        foregroundColor: AppColors.primary,
      ),
      body: IndexedStack(
        index: _tabIndex,
        children: [
          ListView(
            padding: const EdgeInsets.all(16),
            children: [
              AuthCard(
                userEmail: _userEmail,
                busy: _authBusy,
                onSignIn: _signIn,
                onSignOut: _signOut,
              ),
              const SizedBox(height: 12),
              ConnCard(
                state: _state,
                deviceName: _deviceName,
                onConnect: _connect,
                onDisconnect: isConn ? _disconnect : _cancelConnect,
              ),
              const SizedBox(height: 12),
              DebugToggleCard(
                value: _debugMode,
                onChanged: (v) => setState(() => _debugMode = v),
              ),
              if (_debugMode) ...[
                const SizedBox(height: 12),
                LogCard(log: _log, scroll: _logScroll),
              ],
              const SizedBox(height: 24),
            ],
          ),
          ListView(
            padding: const EdgeInsets.all(16),
            children: [
              MeasCard(vMain: _vMain, curr: _curr, pwr: _pwr, vSub: _vSub),
              const SizedBox(height: 24),
            ],
          ),
          ListView(
            padding: const EdgeInsets.all(16),
            children: [
              ObdCard(reading: _obdReading),
              const SizedBox(height: 24),
            ],
          ),
        ],
      ),
      bottomNavigationBar: BottomNavigationBar(
        currentIndex: _tabIndex,
        onTap: (i) => setState(() => _tabIndex = i),
        backgroundColor: AppColors.surface,
        selectedItemColor: AppColors.primary,
        unselectedItemColor: Colors.grey,
        type: BottomNavigationBarType.fixed,
        items: const [
          BottomNavigationBarItem(icon: Icon(Icons.bluetooth), label: '接続'),
          BottomNavigationBarItem(icon: Icon(Icons.battery_full), label: 'バッテリー'),
          BottomNavigationBarItem(icon: Icon(Icons.speed), label: 'OBD'),
        ],
      ),
    );
  }
}
