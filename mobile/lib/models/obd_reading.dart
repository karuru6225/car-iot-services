import 'dart:typed_data';

// バイト列を先頭から順に読み進めるカーソル。読み取りメソッドを呼ぶたびに内部位置を
// 自動で進めるため、呼び出し側（ObdReading.fromBytes()）はオフセットを一切書かずに済む。
// ObdBlePacket（esp32_iot_gateway/src/domain/obd.h）のフィールド宣言順と呼び出し順を
// 一致させることだけが要件になる。
class _Reader {
  final ByteData _d;
  final Endian _e;
  int _pos;
  _Reader(Uint8List bytes, int start)
      : _d = ByteData.sublistView(bytes),
        _e = Endian.little,
        _pos = start;

  int get pos => _pos;

  void skip(int n) => _pos += n;

  int u8() {
    final v = _d.getUint8(_pos);
    _pos += 1;
    return v;
  }

  int i8() {
    final v = _d.getInt8(_pos);
    _pos += 1;
    return v;
  }

  int u16() {
    final v = _d.getUint16(_pos, _e);
    _pos += 2;
    return v;
  }

  int i16() {
    final v = _d.getInt16(_pos, _e);
    _pos += 2;
    return v;
  }

  int u32() {
    final v = _d.getUint32(_pos, _e);
    _pos += 4;
    return v;
  }

  double f32() {
    final v = _d.getFloat32(_pos, _e);
    _pos += 4;
    return v;
  }
}

// ObdReading（esp32_iot_gateway/src/domain/obd.h の ObdBlePacket）のDart側パース結果。
// コア構造体（bytes[0]のcoreLenバイト、coreLen自身を含む）のフィールド順・オフセットは
// ObdBlePacket と完全一致させること。コア構造体の直後にはTLV拡張フィールド領域が続く
// （ObdExtFieldId参照）。新しいセンサー値はこちら側で追加され、firmware側のフィールド追加では
// コア構造体側のオフセットは変わらない。
class ObdReading {
  final int rpm, speedKmh, loadPct, mapKpa, baroKpa, boostKpa, throttlePct;
  final double timingDeg, ecuVoltage, mafGs;
  final int coolantC;
  final double fuelRateLph;
  final double stftPct, ltftPct, o2B1s2V, o2B1s2TrimPct;
  final int engineRunTimeSec, milDistanceKm;
  final double o2S1Ratio, o2S1Voltage;
  final int evapPurgePct, warmupsSinceCleared, distanceSinceClearedKm;
  final double catalystTempC, absoluteLoadPct, commandedAfr;
  final int throttleBPct, accelPedalDPct, accelPedalEPct, fuelType;
  final double secO2TrimStPct, secO2TrimLtPct;
  final bool valid;
  final int ts;
  final int iatC, iat2C;
  final int validMask;
  // 以下はTLV拡張フィールド領域から読み取る（コア構造体には含まれない）
  final int atfTempC;
  final bool atfTempValid;

  ObdReading._({
    required this.rpm, required this.speedKmh, required this.loadPct,
    required this.mapKpa, required this.baroKpa, required this.boostKpa,
    required this.throttlePct, required this.timingDeg, required this.ecuVoltage,
    required this.mafGs, required this.coolantC, required this.fuelRateLph,
    required this.stftPct, required this.ltftPct, required this.o2B1s2V,
    required this.o2B1s2TrimPct, required this.engineRunTimeSec,
    required this.milDistanceKm, required this.o2S1Ratio, required this.o2S1Voltage,
    required this.evapPurgePct, required this.warmupsSinceCleared,
    required this.distanceSinceClearedKm, required this.catalystTempC,
    required this.absoluteLoadPct, required this.commandedAfr,
    required this.throttleBPct, required this.accelPedalDPct,
    required this.accelPedalEPct, required this.fuelType,
    required this.secO2TrimStPct, required this.secO2TrimLtPct,
    required this.valid, required this.ts,
    required this.iatC, required this.iat2C,
    required this.validMask,
    required this.atfTempC, required this.atfTempValid,
  });

  // ファーム側 domain/obd.h の ObdExtFieldId と対応させること。
  static const _extFieldAtfTempC = 1;
  static const _extFieldAtfTempValid = 2;

  factory ObdReading.fromBytes(Uint8List bytes) {
    // bytes[0] = coreLen（ObdBlePacket.coreLen、このバイト自身を含むコア構造体の全長）。
    // ハードコードせずここから読むことで、コア構造体のサイズが変わってもTLV拡張領域の
    // 開始位置を正しく特定できる。
    final coreLen = bytes[0];

    // コア構造体はObdBlePacketのフィールド宣言順どおりに1つずつ読み進める。
    // オフセットは_Readerが自動計算するため、フィールドの型・呼び出し順さえ
    // ObdBlePacketと一致させれば足りる（追加・削除時もオフセットの手計算が要らない）。
    final r = _Reader(bytes, 1); // coreLenバイトの直後から開始
    final rpm = r.u16();
    final speedKmh = r.u8();
    final loadPct = r.u8();
    final mapKpa = r.u8();
    final baroKpa = r.u8();
    final boostKpa = r.i8();
    final throttlePct = r.u8();
    final timingDeg = r.f32();
    final ecuVoltage = r.f32();
    final mafGs = r.f32();
    final coolantC = r.i16();
    final fuelRateLph = r.f32();
    final stftPct = r.f32();
    final ltftPct = r.f32();
    final o2B1s2V = r.f32();
    final o2B1s2TrimPct = r.f32();
    final engineRunTimeSec = r.u16();
    final milDistanceKm = r.u16();
    final o2S1Ratio = r.f32();
    final o2S1Voltage = r.f32();
    final evapPurgePct = r.u8();
    final warmupsSinceCleared = r.u8();
    final distanceSinceClearedKm = r.u16();
    final catalystTempC = r.f32();
    final absoluteLoadPct = r.f32();
    final commandedAfr = r.f32();
    final throttleBPct = r.u8();
    final accelPedalDPct = r.u8();
    final accelPedalEPct = r.u8();
    final fuelType = r.u8();
    final secO2TrimStPct = r.f32();
    final secO2TrimLtPct = r.f32();
    final valid = r.u8() != 0;
    final ts = r.u32();
    final iatC = r.i16();
    final iat2C = r.i16();
    final validMask = r.u32();

    // TLV拡張フィールド領域: [extCount:1]([fieldId:1][len:1][data:len])×extCount。
    // 知らないfieldIdはlen分読み飛ばす。firmware側で新しいフィールドが追加されても、
    // 対応するcaseを足すだけで済み、他フィールドのオフセット計算には影響しない。
    var atfTempC = 0;
    var atfTempValid = false;
    if (bytes.length > coreLen) {
      final ext = _Reader(bytes, coreLen);
      final extCount = ext.u8();
      for (var i = 0; i < extCount && ext.pos + 2 <= bytes.length; i++) {
        final fieldId = ext.u8();
        final len = ext.u8();
        if (ext.pos + len > bytes.length) break; // データ不足（壊れたパケット）、安全に打ち切り
        switch (fieldId) {
          case _extFieldAtfTempC:
            atfTempC = ext.i16();
            break;
          case _extFieldAtfTempValid:
            atfTempValid = ext.u8() != 0;
            break;
          default:
            ext.skip(len); // 未知のfieldIdはlen分読み飛ばす
        }
      }
    }

    return ObdReading._(
      rpm: rpm,
      speedKmh: speedKmh,
      loadPct: loadPct,
      mapKpa: mapKpa,
      baroKpa: baroKpa,
      boostKpa: boostKpa,
      throttlePct: throttlePct,
      timingDeg: timingDeg,
      ecuVoltage: ecuVoltage,
      mafGs: mafGs,
      coolantC: coolantC,
      fuelRateLph: fuelRateLph,
      stftPct: stftPct,
      ltftPct: ltftPct,
      o2B1s2V: o2B1s2V,
      o2B1s2TrimPct: o2B1s2TrimPct,
      engineRunTimeSec: engineRunTimeSec,
      milDistanceKm: milDistanceKm,
      o2S1Ratio: o2S1Ratio,
      o2S1Voltage: o2S1Voltage,
      evapPurgePct: evapPurgePct,
      warmupsSinceCleared: warmupsSinceCleared,
      distanceSinceClearedKm: distanceSinceClearedKm,
      catalystTempC: catalystTempC,
      absoluteLoadPct: absoluteLoadPct,
      commandedAfr: commandedAfr,
      throttleBPct: throttleBPct,
      accelPedalDPct: accelPedalDPct,
      accelPedalEPct: accelPedalEPct,
      fuelType: fuelType,
      secO2TrimStPct: secO2TrimStPct,
      secO2TrimLtPct: secO2TrimLtPct,
      valid: valid,
      ts: ts,
      iatC: iatC,
      iat2C: iat2C,
      validMask: validMask,
      atfTempC: atfTempC,
      atfTempValid: atfTempValid,
    );
  }
}
