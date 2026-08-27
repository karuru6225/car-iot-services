import 'dart:io';

import 'package:flutter/services.dart';

import '../ble/ble_constants.dart';

// CDM(CompanionDeviceManager)経由の関連付け・killed状態からの自動起動を扱う。
// 実処理はAndroidネイティブ側(MainActivity.kt / CarIotCompanionService.kt)にあり、
// ここはMethodChannelの薄いラッパー。iOSでは常にno-op。
class CompanionDeviceService {
  static const _channel = MethodChannel('info.karuru.cariot_mobile/companion');

  // アプリ生存中にBLE検知された場合（ウォーム起動）、initState()を経由せずここに直接通知が来る
  VoidCallback? onAutoConnectTriggered;

  CompanionDeviceService() {
    _channel.setMethodCallHandler((call) async {
      if (call.method == 'autoConnectTriggered') onAutoConnectTriggered?.call();
    });
  }

  Future<void> associate() async {
    if (!Platform.isAndroid) return;
    await _channel.invokeMethod('associate', {
      'serviceUuid': kMeasService,
      'namePrefix': 'car-iot-',
    });
  }

  Future<bool> isAssociated() async {
    if (!Platform.isAndroid) return false;
    return await _channel.invokeMethod<bool>('isAssociated') ?? false;
  }

  // killed状態からCDM起動された場合のフラグを1回だけ取り出す（コールド起動用）
  Future<bool> consumeAutoConnectFlag() async {
    if (!Platform.isAndroid) return false;
    return await _channel.invokeMethod<bool>('consumeAutoConnectFlag') ?? false;
  }
}
