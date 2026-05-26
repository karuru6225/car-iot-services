#include "telemetry.h"
#include <stdio.h>
#include <ArduinoJson.h>
#include "../config.h"


int buildConfigPayload(char *buf, size_t size, bool clearDesired)
{
  const char *relayStr = (getRelayMode() == RelayMode::SLEEP_INDICATOR)
                           ? "sleep_indicator" : "off";
  if (clearDesired)
    return snprintf(buf, size,
                    "{\"state\":{"
                    "\"reported\":{"
                    "\"ah_offset\":%d,"
                    "\"relay_mode\":\"%s\","
                    "\"chg_start_v\":%.2f,"
                    "\"chg_stop_v\":%.2f,"
                    "\"debug_log\":%s,"
                    "\"charging\":%s,"
                    "\"fw_version\":\"" FIRMWARE_VERSION "\""
                    "},\"desired\":null}}",
                    getAhOffset(), relayStr,
                    getChgStartV(), getChgStopV(),
                    getDebugLogEnabled() ? "true" : "false",
                    isCharging() ? "true" : "false");
  return snprintf(buf, size,
                  "{\"state\":{\"reported\":{"
                  "\"ah_offset\":%d,"
                  "\"relay_mode\":\"%s\","
                  "\"chg_start_v\":%.2f,"
                  "\"chg_stop_v\":%.2f,"
                  "\"debug_log\":%s,"
                  "\"charging\":%s,"
                  "\"fw_version\":\"" FIRMWARE_VERSION "\""
                  "}}}",
                  getAhOffset(), relayStr,
                  getChgStartV(), getChgStopV(),
                  getDebugLogEnabled() ? "true" : "false",
                  isCharging() ? "true" : "false");
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

size_t MsgPackTelemetryEncoder::serialize(JsonDocument &doc, uint8_t *buf, size_t cap)
{
  return serializeMsgPack(doc, buf, cap);
}
