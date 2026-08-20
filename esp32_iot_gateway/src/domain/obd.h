#pragma once
#include <stddef.h>
#include <stdint.h>
#include <time.h>

// Honda N-VAN で実車確認済みの OBD-II Mode 01 PID を扱う（OBD.md 参照）
struct OBDReading
{
  uint16_t rpm;           // 0x0C: (A*256+B)/4 [rpm]
  uint8_t  speedKmh;     // 0x0D: A [km/h]
  uint8_t  loadPct;      // 0x04: A*100/255 [%]
  uint8_t  mapKpa;       // 0x0B: A [kPa 絶対圧]
  uint8_t  baroKpa;      // 0x33: A [kPa]
  int8_t   boostKpa;     // mapKpa - baroKpa [kPa]（obdComputeDerived()で計算）
  uint8_t  throttlePct;  // 0x11: A*100/255 [%]
  float    timingDeg;    // 0x0E: A/2.0-64.0 [°BTDC]
  float    ecuVoltage;   // 0x42: (A*256+B)/1000.0 [V]
  float    mafGs;        // 0x66: (B*256+C)/32 [g/s]（0x10 MAF 非対応のため代替）
  int16_t  coolantC;     // 0x67 Sensor1: B-40 [°C]（0x05 水温 非対応のため代替）
  float    fuelRateLph; // MAF 推算: mafGs / (14.7*0.745) * 3.6 [L/h]（obdComputeDerived()で計算）

  // 追加確定PID（OBD.md「確定した取得可能データ一覧」参照）
  float    stftPct;                 // 0x06: (A-128)*100/128 [%]
  float    ltftPct;                 // 0x07: (A-128)*100/128 [%]
  float    o2B1s2V;                // 0x15: A/200 [V]
  float    o2B1s2TrimPct;         // 0x15: (B-128)*100/128 [%]
  uint16_t engineRunTimeSec;      // 0x1F: A*256+B [秒]
  uint16_t milDistanceKm;          // 0x21: A*256+B [km]
  float    o2S1Ratio;              // 0x24: (A*256+B)*2/65536
  float    o2S1Voltage;            // 0x24: (C*256+D)*8/65536 [V]
  uint8_t  evapPurgePct;           // 0x2E: A*100/255 [%]
  uint8_t  warmupsSinceCleared;    // 0x30: A [回]
  uint16_t distanceSinceClearedKm;// 0x31: A*256+B [km]
  float    catalystTempC;          // 0x3C: (A*256+B)/10-40 [°C]
  float    absoluteLoadPct;        // 0x43: (A*256+B)*100/255 [%]
  float    commandedAfr;            // 0x44: (A*256+B)*2/65536
  uint8_t  throttleBPct;           // 0x47: A*100/255 [%]
  uint8_t  accelPedalDPct;        // 0x49: A*100/255 [%]
  uint8_t  accelPedalEPct;        // 0x4A: A*100/255 [%]
  uint8_t  fuelType;                // 0x51: A（1=ガソリン）
  float    secO2TrimStPct;       // 0x55: (A-128)*100/128 [%]
  float    secO2TrimLtPct;       // 0x56: (A-128)*100/128 [%]

  bool     valid;
  time_t   ts;

  // 末尾追加（ObdBlePacketとのオフセット互換のため既存フィールドより後ろに置く）
  int16_t  iatC;  // 0x68 Sensor1: B-40 [°C]（インタークーラー前後どちらか未確定）
  int16_t  iat2C; // 0x68 Sensor2: C-40 [°C]（同上）

  // Mode22 (UDS ReadDataByIdentifier) 追加分。Mode01のkPids[]多PIDバッチ機構
  // （obdParseMultiResponse前提）とは別経路で単発問い合わせする（service/obdpoll.cpp参照）。
  // validMaskはkPids[]専用のためATFの成否はこのフラグ単独で管理する。
  int16_t  atfTempC;      // DID 0x2201: A-40 [°C]（OBD.md「Mode 22 実機テスト候補」参照、実車未テスト）
  bool     atfTempValid;

  // kPids[]（service/obdpoll.cpp）の配列順に対応するビットマスク。ビットiが立っていれば
  // kPids[i]のPIDはデコード成功。valid=trueでも一部グループがタイムアウトした場合は
  // 該当PID分のフィールドが0初期値のまま残るため、本物の0とタイムアウトを区別するのに使う。
  uint32_t validMask;
};

// ObdBlePacketのボディレイアウトのバージョン。ボディのフィールド構成（順序・型・増減）を
// 変えたら必ずインクリメントすること。アプリ側は自分が対応しているバージョンと一致しない
// パケットのパースを拒否する（安全側に倒す。ObdReading.fromBytes()がnullを返す）ため、
// 単純に上げ忘れると新しいファームのデータがアプリ側で一切表示されなくなる点に注意。
static const uint8_t OBD_BLE_SCHEMA_VERSION = 1;

// BLE Notify 送信用（パディングなしで詰めた固定レイアウト）。
// OBDReading をそのまま memcpy するとコンパイラのパディング/アライメントに依存してしまうため、
// BLE経由で送る際はこの構造体に変換してから使う（device/ble_peripheral.cpp 参照）。
//
// ヘッダ（メタ情報）とボディ（実データ）を分離している。validMask/validは「後続のボディを
// どう解釈すべきか」を示すメタ情報であり、実データより先に読める位置にある方が自然なため
// ヘッダ側に置いた。
#pragma pack(push, 1)
struct ObdBlePacket
{
  // ---- ヘッダ ----

  // ボディのレイアウトバージョン（OBD_BLE_SCHEMA_VERSION固定）。バージョンによって以降の
  // ヘッダ・ボディの解釈自体が変わりうるため、パケットの一番先頭に置く（extOffsetより前）。
  // extOffsetは「サイズが同じか」しか保証しないため、サイズは同じだが意味が変わった変更
  // （フィールドの入れ替え等）をアプリ側が検出できるようにするための値。
  uint8_t  schemaVersion;

  // ヘッダ部分（このバイト自身を含む、schemaVersion〜validMaskまで）の全長。常に
  // offsetof(ObdBlePacket, rpm) 固定。アプリ側はこの値からボディの開始位置（bytes[headerLen]）
  // を逆算できるため、将来ヘッダにフィールドを追加してもボディ側オフセットの
  // ハードコード（アプリ側の定数）を直さずに済む。
  uint8_t  headerLen;

  // TLV拡張フィールド領域の開始位置（bytes[extOffset]）。常に sizeof(ObdBlePacket) 固定
  // （obdReadingToBlePacket()がセットする、コア部分＝ヘッダ+ボディの全長でもある）。
  // アプリ側はこの値を直接使えるため、コア構造体のサイズが変わってもオフセットの
  // ハードコード（アプリ側の定数）を直さずに済む。
  uint8_t  extOffset;

  uint8_t  valid;     // 全体の有効性フラグ（bool を1バイト固定で送る）
  uint32_t validMask; // kPids[]（service/obdpoll.cpp）の配列順に対応するPIDごとのデコード成否

  // ---- ボディ ----

  uint16_t rpm;
  uint8_t  speedKmh;
  uint8_t  loadPct;
  uint8_t  mapKpa;
  uint8_t  baroKpa;
  int8_t   boostKpa;
  uint8_t  throttlePct;
  float    timingDeg;
  float    ecuVoltage;
  float    mafGs;
  int16_t  coolantC;
  float    fuelRateLph;

  float    stftPct;
  float    ltftPct;
  float    o2B1s2V;
  float    o2B1s2TrimPct;
  uint16_t engineRunTimeSec;
  uint16_t milDistanceKm;
  float    o2S1Ratio;
  float    o2S1Voltage;
  uint8_t  evapPurgePct;
  uint8_t  warmupsSinceCleared;
  uint16_t distanceSinceClearedKm;
  float    catalystTempC;
  float    absoluteLoadPct;
  float    commandedAfr;
  uint8_t  throttleBPct;
  uint8_t  accelPedalDPct;
  uint8_t  accelPedalEPct;
  uint8_t  fuelType;
  float    secO2TrimStPct;
  float    secO2TrimLtPct;

  uint32_t ts; // time_t は環境依存サイズのため uint32_t に固定

  int16_t  iatC;
  int16_t  iat2C;
};
#pragma pack(pop)

// OBDReading → ObdBlePacket 変換
void obdReadingToBlePacket(const OBDReading &r, ObdBlePacket &out);

// ObdBlePacket後方互換のためのTLV拡張フィールド領域（device/ble_peripheral.cpp の notifyObd()が
// ObdBlePacketの直後に連結して送る）。フィールド追加のたびにObdBlePacketの構造体レイアウトを
// 変えるとアプリ側(mobile/lib/models/obd_reading.dart)の固定オフセット読み取りが全部ズレて
// 壊れるため、コア構造体(ObdBlePacket)に入れたくない・今後も増減しうる値はこちらに追加する。
// フォーマット: [extCount:1] ([fieldId:1][len:1][data:len])×extCount
// アプリ側は知らないfieldIdをlen分読み飛ばせるため、双方の更新タイミングがズレても壊れない。
enum class ObdExtFieldId : uint8_t
{
  AtfTempC = 1,     // int16_t、DID 0x2201の A-40 [°C]（実車未テスト）
  AtfTempValid = 2, // uint8_t（0/1）
};

// 拡張フィールド領域をエンコードする。bufSizeが不足する場合は入りきる分だけで打ち切る
// （呼び出し側は拡張フィールド追加時にバッファサイズを見直すこと）。戻り値は書き込んだバイト数。
size_t obdEncodeExtFields(const OBDReading &r, uint8_t *buf, size_t bufSize);

// boostKpa/fuelRateLphの派生値を計算する（valid=falseの場合は何もしない）
void obdComputeDerived(OBDReading &r);

// デコード共通ルール: data[0]!=0x41 または data[1]!=要求PID または dlc不足の場合は false を返す
// （data は can.cpp 側で ISO-TP PCI バイトを剥がし済み。data[2]以降がA,B,C...のペイロード）

bool obdDecodeRpm(const uint8_t *data, uint8_t dlc, OBDReading &out);
bool obdDecodeSpeed(const uint8_t *data, uint8_t dlc, OBDReading &out);
bool obdDecodeLoad(const uint8_t *data, uint8_t dlc, OBDReading &out);
bool obdDecodeMap(const uint8_t *data, uint8_t dlc, OBDReading &out);
bool obdDecodeBaro(const uint8_t *data, uint8_t dlc, OBDReading &out);
bool obdDecodeThrottle(const uint8_t *data, uint8_t dlc, OBDReading &out);
bool obdDecodeTiming(const uint8_t *data, uint8_t dlc, OBDReading &out);
bool obdDecodeEcuVoltage(const uint8_t *data, uint8_t dlc, OBDReading &out);

// PID 0x66: sensors=data[2]（N-VANでは0x01）, MAF=(data[3]*256+data[4])/32 g/s
bool obdDecodeMafAlt(const uint8_t *data, uint8_t dlc, OBDReading &out);

// PID 0x67: bitmap=data[2]（0x03=S1+S2）, Sensor1温度=data[3]-40 °C
bool obdDecodeCoolantAlt(const uint8_t *data, uint8_t dlc, OBDReading &out);

// 追加確定PID（OBD.md「確定した取得可能データ一覧」参照）
bool obdDecodeShortTermFuelTrim(const uint8_t *data, uint8_t dlc, OBDReading &out);   // 0x06
bool obdDecodeLongTermFuelTrim(const uint8_t *data, uint8_t dlc, OBDReading &out);    // 0x07
bool obdDecodeO2SensorB1S2(const uint8_t *data, uint8_t dlc, OBDReading &out);        // 0x15（電圧+燃料トリムの2値）
bool obdDecodeEngineRunTime(const uint8_t *data, uint8_t dlc, OBDReading &out);       // 0x1F
bool obdDecodeMilDistance(const uint8_t *data, uint8_t dlc, OBDReading &out);         // 0x21
bool obdDecodeO2Sensor1WideBand(const uint8_t *data, uint8_t dlc, OBDReading &out);   // 0x24（ratio+電圧の2値）
bool obdDecodeEvapPurge(const uint8_t *data, uint8_t dlc, OBDReading &out);           // 0x2E
bool obdDecodeWarmupsSinceCleared(const uint8_t *data, uint8_t dlc, OBDReading &out); // 0x30
bool obdDecodeDistanceSinceCleared(const uint8_t *data, uint8_t dlc, OBDReading &out);// 0x31
bool obdDecodeCatalystTemp(const uint8_t *data, uint8_t dlc, OBDReading &out);        // 0x3C
bool obdDecodeAbsoluteLoad(const uint8_t *data, uint8_t dlc, OBDReading &out);        // 0x43
bool obdDecodeCommandedAfr(const uint8_t *data, uint8_t dlc, OBDReading &out);        // 0x44
bool obdDecodeThrottleB(const uint8_t *data, uint8_t dlc, OBDReading &out);           // 0x47
bool obdDecodeAccelPedalD(const uint8_t *data, uint8_t dlc, OBDReading &out);         // 0x49
bool obdDecodeAccelPedalE(const uint8_t *data, uint8_t dlc, OBDReading &out);         // 0x4A
bool obdDecodeFuelType(const uint8_t *data, uint8_t dlc, OBDReading &out);            // 0x51
bool obdDecodeSecO2TrimShortTerm(const uint8_t *data, uint8_t dlc, OBDReading &out);  // 0x55
bool obdDecodeSecO2TrimLongTerm(const uint8_t *data, uint8_t dlc, OBDReading &out);   // 0x56

// PID 0x68: bitmap=data[2]（0x03=S1+S2）, Sensor1温度=data[3]-40 [°C], Sensor2温度=data[4]-40 [°C]
// （インタークーラー前後どちらがSensor1/2に対応するかは未確定）
bool obdDecodeChargeAirTemp(const uint8_t *data, uint8_t dlc, OBDReading &out);       // 0x68

// DID 0x2201 (Mode22/UDS ReadDataByIdentifier): ATF/CVT油温。A-40 [°C]
// data は can.cpp が返す `62 [DID_HI] [DID_LO] data...` ペイロード（Mode01応答とはヘッダ形式が異なる）。
// 実車未テスト（OBD.md「Mode 22 実機テスト候補」参照）。
bool obdDecodeAtfTemp(const uint8_t *data, uint8_t dlc, OBDReading &out);

// 多PID応答（`41 [PID_a][data_a...] [PID_b][data_b...] ...`）をPIDごとのセグメントに
// 分解する（経緯はCONTEXT_ARCHIVE.mdの「ISO-TPマルチフレーム対応・多PID要求」参照）。PIDの並び順・省略はECU任せなので
// 位置固定では解釈できず、内蔵のPID→データ長テーブルを頼りにTLV的に歩く。
// テーブルに無いPIDに遭遇した場合はそこで安全に打ち切る（以降のセグメント境界が不明なため）。
// data は canReceiveObdResponse() が返す `41 ...` から始まるペイロード。
// 戻り値: 未知PIDに遭遇して打ち切った場合はtrueを返し、unknownPidOutが非nullptrなら
// そのPID値を書き込む（データ不足による打ち切り・正常終了時はfalse）。domain層はログを
// 出さない（ハードウェア非依存の純粋関数という制約のため）。ログ出力は呼び出し側（service層）
// の責務。
typedef void (*ObdMultiSegmentCb)(uint8_t pid, const uint8_t *data, uint8_t len, void *ctx);
bool obdParseMultiResponse(const uint8_t *data, uint8_t dlc, ObdMultiSegmentCb cb, void *ctx,
                            uint8_t *unknownPidOut = nullptr);
