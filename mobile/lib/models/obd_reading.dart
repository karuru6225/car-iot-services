import 'dart:typed_data';

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
    final d = ByteData.sublistView(bytes);
    const e = Endian.little;

    // bytes[0] = coreLen（ObdBlePacket.coreLen、このバイト自身を含むコア構造体の全長）。
    // ハードコードせずここから読むことで、コア構造体のサイズが変わってもTLV拡張領域の
    // 開始位置を正しく特定できる（コア構造体内の各フィールドオフセットは別途固定値のまま）。
    final coreLen = bytes[0];

    // TLV拡張フィールド領域: [extCount:1]([fieldId:1][len:1][data:len])×extCount。
    // 知らないfieldIdはlen分読み飛ばす。firmware側で新しいフィールドが追加されても、
    // 対応するcaseを足すだけで済み、他フィールドのオフセット計算には影響しない。
    var atfTempC = 0;
    var atfTempValid = false;
    if (bytes.length > coreLen) {
      var pos = coreLen;
      final extCount = bytes[pos];
      pos++;
      for (var i = 0; i < extCount && pos + 2 <= bytes.length; i++) {
        final fieldId = bytes[pos];
        final len = bytes[pos + 1];
        pos += 2;
        if (pos + len > bytes.length) break; // データ不足（壊れたパケット）、安全に打ち切り
        switch (fieldId) {
          case _extFieldAtfTempC:
            atfTempC = ByteData.sublistView(bytes, pos, pos + len).getInt16(0, e);
            break;
          case _extFieldAtfTempValid:
            atfTempValid = bytes[pos] != 0;
            break;
          // 未知のfieldIdは何もしない（posはこの後len分進めてスキップする）
        }
        pos += len;
      }
    }

    return ObdReading._(
      rpm: d.getUint16(1, e),
      speedKmh: d.getUint8(3),
      loadPct: d.getUint8(4),
      mapKpa: d.getUint8(5),
      baroKpa: d.getUint8(6),
      boostKpa: d.getInt8(7),
      throttlePct: d.getUint8(8),
      timingDeg: d.getFloat32(9, e),
      ecuVoltage: d.getFloat32(13, e),
      mafGs: d.getFloat32(17, e),
      coolantC: d.getInt16(21, e),
      fuelRateLph: d.getFloat32(23, e),
      stftPct: d.getFloat32(27, e),
      ltftPct: d.getFloat32(31, e),
      o2B1s2V: d.getFloat32(35, e),
      o2B1s2TrimPct: d.getFloat32(39, e),
      engineRunTimeSec: d.getUint16(43, e),
      milDistanceKm: d.getUint16(45, e),
      o2S1Ratio: d.getFloat32(47, e),
      o2S1Voltage: d.getFloat32(51, e),
      evapPurgePct: d.getUint8(55),
      warmupsSinceCleared: d.getUint8(56),
      distanceSinceClearedKm: d.getUint16(57, e),
      catalystTempC: d.getFloat32(59, e),
      absoluteLoadPct: d.getFloat32(63, e),
      commandedAfr: d.getFloat32(67, e),
      throttleBPct: d.getUint8(71),
      accelPedalDPct: d.getUint8(72),
      accelPedalEPct: d.getUint8(73),
      fuelType: d.getUint8(74),
      secO2TrimStPct: d.getFloat32(75, e),
      secO2TrimLtPct: d.getFloat32(79, e),
      valid: d.getUint8(83) != 0,
      ts: d.getUint32(84, e),
      iatC: d.getInt16(88, e),
      iat2C: d.getInt16(90, e),
      validMask: d.getUint32(92, e),
      atfTempC: atfTempC,
      atfTempValid: atfTempValid,
    );
  }
}
