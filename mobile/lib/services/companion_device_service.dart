import 'dart:io';

import 'package:flutter/services.dart';

import '../ble/ble_constants.dart';

// CDM(CompanionDeviceManager)経由の関連付け・killed状態からの自動起動を扱う。
// 実処理はAndroidネイティブ側(MainActivity.kt / CarIotCompanionService.kt)にあり、
// ここはMethodChannelの薄いラッパー。iOSでは常にno-op。
class CompanionDeviceService {
  static const _channel = MethodChannel('info.karuru.cariot_mobile/companion');

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
}
