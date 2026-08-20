import 'dart:typed_data';

// ObdReading（esp32_iot_gateway/src/domain/obd.h の ObdBlePacket）のDart側パース結果。
// コア構造体（下記CORE_LENバイト）のフィールド順・オフセットは ObdBlePacket と完全一致させること。
// コア構造体の直後にはTLV拡張フィールド領域が続く（ObdExtFieldId参照）。新しいセンサー値は
// こちら側で追加され、firmware側のフィールド追加ではコア構造体側のオフセットは変わらない。
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

  // コア構造体（ObdBlePacketのTLV拡張フィールドより前の部分）のバイト数。
  static const _coreLen = 95;

  // ファーム側 domain/obd.h の ObdExtFieldId と対応させること。
  static const _extFieldAtfTempC = 1;
  static const _extFieldAtfTempValid = 2;

  factory ObdReading.fromBytes(Uint8List bytes) {
    final d = ByteData.sublistView(bytes);
    const e = Endian.little;

    // TLV拡張フィールド領域: [extCount:1]([fieldId:1][len:1][data:len])×extCount。
    // 知らないfieldIdはlen分読み飛ばす。firmware側で新しいフィールドが追加されても、
    // 対応するcaseを足すだけで済み、他フィールドのオフセット計算には影響しない。
    var atfTempC = 0;
    var atfTempValid = false;
    if (bytes.length > _coreLen) {
      var pos = _coreLen;
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
      rpm: d.getUint16(0, e),
      speedKmh: d.getUint8(2),
      loadPct: d.getUint8(3),
      mapKpa: d.getUint8(4),
      baroKpa: d.getUint8(5),
      boostKpa: d.getInt8(6),
      throttlePct: d.getUint8(7),
      timingDeg: d.getFloat32(8, e),
      ecuVoltage: d.getFloat32(12, e),
      mafGs: d.getFloat32(16, e),
      coolantC: d.getInt16(20, e),
      fuelRateLph: d.getFloat32(22, e),
      stftPct: d.getFloat32(26, e),
      ltftPct: d.getFloat32(30, e),
      o2B1s2V: d.getFloat32(34, e),
      o2B1s2TrimPct: d.getFloat32(38, e),
      engineRunTimeSec: d.getUint16(42, e),
      milDistanceKm: d.getUint16(44, e),
      o2S1Ratio: d.getFloat32(46, e),
      o2S1Voltage: d.getFloat32(50, e),
      evapPurgePct: d.getUint8(54),
      warmupsSinceCleared: d.getUint8(55),
      distanceSinceClearedKm: d.getUint16(56, e),
      catalystTempC: d.getFloat32(58, e),
      absoluteLoadPct: d.getFloat32(62, e),
      commandedAfr: d.getFloat32(66, e),
      throttleBPct: d.getUint8(70),
      accelPedalDPct: d.getUint8(71),
      accelPedalEPct: d.getUint8(72),
      fuelType: d.getUint8(73),
      secO2TrimStPct: d.getFloat32(74, e),
      secO2TrimLtPct: d.getFloat32(78, e),
      valid: d.getUint8(82) != 0,
      ts: d.getUint32(83, e),
      iatC: d.getInt16(87, e),
      iat2C: d.getInt16(89, e),
      validMask: d.getUint32(91, e),
      atfTempC: atfTempC,
      atfTempValid: atfTempValid,
    );
  }
}
