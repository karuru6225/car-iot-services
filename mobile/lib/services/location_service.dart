import 'dart:async';

import 'package:geolocator/geolocator.dart';

// GPS位置情報を継続取得し、直近の既知の位置をキャッシュしておくだけのクラス。
// 権限リクエストはble_home_screen.dart側（_requestPermissions()）に集約するため
// ここでは行わない。呼び出し側で許可済みであることを前提にstart()する。
class LocationService {
  Position? lastPosition;
  StreamSubscription<Position>? _sub;

  void start() {
    _sub ??= Geolocator.getPositionStream(
      locationSettings: const LocationSettings(
        accuracy: LocationAccuracy.high,
        distanceFilter: 0,
      ),
    ).listen((p) => lastPosition = p, onError: (_) {});
  }

  void stop() {
    _sub?.cancel();
    _sub = null;
    lastPosition = null;
  }
}
