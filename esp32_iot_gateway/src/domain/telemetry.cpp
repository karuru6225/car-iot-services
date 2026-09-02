#include "telemetry.h"
#include <stdio.h>
#include <ArduinoJson.h>
#include <esp_rom_crc.h>
#include "../config.h"


int buildConfigPayload(char *buf, size_t size, bool clearDesired, const char *overrideNextMode,
                       std::optional<time_t> continuousUntilTime, const char *defaultMode)
{
  // override_next_mode: "timed_continuous" または null（通常時）
  char overrideStr[32];
  if (overrideNextMode)
    snprintf(overrideStr, sizeof(overrideStr), "\"%s\"", overrideNextMode);
  else
    strcpy(overrideStr, "null");

  // continuous_until_time: TIMED_CONTINUOUS中の継続期限（絶対UNIX時刻）または null
  char untilStr[24];
  if (continuousUntilTime)
    snprintf(untilStr, sizeof(untilStr), "%ld", (long)*continuousUntilTime);
  else
    strcpy(untilStr, "null");

  // default_mode: NVSに設定済みのデフォルトモード名（"light_sleep"等）または null（未設定時）
  char defaultModeStr[32];
  if (defaultMode)
    snprintf(defaultModeStr, sizeof(defaultModeStr), "\"%s\"", defaultMode);
  else
    strcpy(defaultModeStr, "null");

  if (clearDesired)
    return snprintf(buf, size,
                    "{\"state\":{"
                    "\"reported\":{"
                    "\"ah_offset\":%d,"
                    "\"chg_start_v\":%.2f,"
                    "\"chg_stop_v\":%.2f,"
                    "\"chg_min_diff_v\":%.2f,"
                    "\"debug_log\":%s,"
                    "\"charging\":%s,"
                    "\"override_next_mode\":%s,"
                    "\"continuous_until_time\":%s,"
                    "\"default_mode\":%s,"
                    "\"fw_version\":\"" FIRMWARE_VERSION "\""
                    "},\"desired\":null}}",
                    getAhOffset(),
                    getChgStartV(), getChgStopV(), getChgMinDiffV(),
                    getDebugLogEnabled() ? "true" : "false",
                    isCharging() ? "true" : "false",
                    overrideStr, untilStr, defaultModeStr);
  return snprintf(buf, size,
                  "{\"state\":{\"reported\":{"
                  "\"ah_offset\":%d,"
                  "\"chg_start_v\":%.2f,"
                  "\"chg_stop_v\":%.2f,"
                  "\"chg_min_diff_v\":%.2f,"
                  "\"debug_log\":%s,"
                  "\"charging\":%s,"
                  "\"override_next_mode\":%s,"
                  "\"continuous_until_time\":%s,"
                  "\"default_mode\":%s,"
                  "\"fw_version\":\"" FIRMWARE_VERSION "\""
                  "}}}",
                  getAhOffset(),
                  getChgStartV(), getChgStopV(), getChgMinDiffV(),
                  getDebugLogEnabled() ? "true" : "false",
                  isCharging() ? "true" : "false",
                  overrideStr, untilStr, defaultModeStr);
}


// ─── ITelemetryEncoder（encode* は基底クラスが実装）────────────────────────

size_t ITelemetryEncoder::encodeBattery(uint8_t *buf, size_t cap,
                                        const VoltageReading &main,
                                        const VoltageReading &sub,
                                        const PowerReading &pwr,
                                        time_t ts)
{
  JsonDocument doc;
  doc["t"]  = "battery";
  doc["m"]  = main.voltage;
  doc["s"]  = sub.voltage;
  doc["i"]  = pwr.current;
  doc["p"]  = pwr.power;
  doc["tp"] = pwr.temp;
  doc["ah"] = pwr.ah;
  doc["ts"] = (uint32_t)ts;
  return serialize(doc, buf, cap);
}

size_t ITelemetryEncoder::encodeThermometer(uint8_t *buf, size_t cap,
                                            const ThermometerData &d)
{
  JsonDocument doc;
  doc["t"]  = "thermometer";
  doc["a"]  = d.address;
  doc["tp"] = d.temp;
  doc["h"]  = d.humidity;
  doc["bt"] = d.battery;
  doc["rs"] = d.rssi;
  doc["mf"] = d.mfHex;
  doc["fw"] = FIRMWARE_VERSION;
  return serialize(doc, buf, cap);
}

size_t ITelemetryEncoder::encodeCo2(uint8_t *buf, size_t cap,
                                    const Co2MeterData &d)
{
  JsonDocument doc;
  doc["t"]   = "co2meter";
  doc["a"]   = d.address;
  doc["tp"]  = d.temp;
  doc["h"]   = d.humidity;
  doc["co2"] = d.co2;
  doc["bt"]  = d.battery;
  doc["rs"]  = d.rssi;
  doc["mf"]  = d.mfHex;
  doc["fw"]  = FIRMWARE_VERSION;
  return serialize(doc, buf, cap);
}

// ─── 派生クラス：serialize のみ ───────────────────────────────────────────────

size_t JsonTelemetryEncoder::serialize(JsonDocument &doc, uint8_t *buf, size_t cap)
{
  return serializeJson(doc, (char *)buf, cap);
}

namespace
{
// MessagePack仕様で「未使用」と規定されているバイト値。本体（常にfixmap: 0x80-0x8F始まり）
// と衝突しないため、ヘッダ無しの旧形式パケットと確実に判別できる（JPEG/PNG等のマジックナンバーと同じ発想）。
constexpr uint8_t kBinMagic   = 0xC1;
constexpr uint8_t kBinVersion = 1;
} // namespace

size_t MsgPackTelemetryEncoder::serialize(JsonDocument &doc, uint8_t *buf, size_t cap)
{
  // [magic 1B][version 1B][msgpack本体][CRC32 4B(LE, magic~本体まで)]
  // ESP32↔SIM7080G間のUARTにパリティ・CRCがなく稀にビット化けがそのまま
  // クラウドまで届く問題があり、ingest Lambda側での検証用（詳細はCONTEXT.md参照）。
  // magic/versionを先頭に置くことで、ファームとLambdaのデプロイ順序に関係なく
  // 新旧どちらの形式が届いても自動判別できる。
  if (cap < 2 + 4) // magic(1) + version(1) + crc(4) の最低限
    return 0;

  buf[0] = kBinMagic;
  buf[1] = kBinVersion;

  size_t bodyLen = serializeMsgPack(doc, buf + 2, cap - 2 - 4);
  if (bodyLen == 0)
    return 0;

  size_t headerAndBodyLen = 2 + bodyLen;
  uint32_t crc = esp_rom_crc32_le(0, buf, headerAndBodyLen);
  buf[headerAndBodyLen]     = (uint8_t)(crc & 0xFF);
  buf[headerAndBodyLen + 1] = (uint8_t)((crc >> 8) & 0xFF);
  buf[headerAndBodyLen + 2] = (uint8_t)((crc >> 16) & 0xFF);
  buf[headerAndBodyLen + 3] = (uint8_t)((crc >> 24) & 0xFF);
  return headerAndBodyLen + 4;
}
