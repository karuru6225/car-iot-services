#include "pubqueue.h"
#include "mqtt.h"
#include "logger.h"
#include "../device/lte.h"
#include "../config.h"
#include "../domain/telemetry.h"
#include <SPIFFS.h>
#include <esp_sleep.h>
#include <stdio.h>

PubQueue queue(false);

// ─── コンストラクタ ───────────────────────────────────────────────────────────

PubQueue::PubQueue(bool useSpiffs) : _useSpiffs(useSpiffs) {}

// ─── RTC メモリ（DeepSleep をまたいで保持される）─────────────────────────────

static const uint32_t RTC_MAGIC = 0xB0FFB0FF;

RTC_DATA_ATTR static uint32_t g_magic = 0;
RTC_DATA_ATTR static QueueEntry g_buf[OFFLINE_BUFFER_MAX];
RTC_DATA_ATTR static int g_head = 0;
RTC_DATA_ATTR static int g_tail = 0;
RTC_DATA_ATTR static int g_count = 0;

// ─── 内部ユーティリティ ───────────────────────────────────────────────────────

static void macToStr(const uint8_t *addr, char *out)
{
  snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
           addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
}

static void strToMac(const char *str, uint8_t *out)
{
  unsigned int b[6] = {};
  sscanf(str, "%02X:%02X:%02X:%02X:%02X:%02X",
         &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]);
  for (int i = 0; i < 6; i++)
    out[i] = (uint8_t)b[i];
}

static void buildTopic(const QueueEntry &e, char *buf, size_t len)
{
  const char *id = getDeviceId();
  switch (e.type)
  {
  case EntryType::Shadow:
    snprintf(buf, len, "$aws/things/%s/shadow/update", id);
    break;
  case EntryType::Thermometer:
  case EntryType::Co2:
    snprintf(buf, len, "sensors/%s/data", id);
    break;
  }
}

static void buildPayload(const QueueEntry &e, char *buf, size_t len)
{
  switch (e.type)
  {
  case EntryType::Shadow:
  {
    VoltageReading v1 = {e.shadow.v1};
    VoltageReading v2 = {e.shadow.v2};
    PowerReading pwr = {e.shadow.current, e.shadow.power, e.shadow.temp};
    buildShadowPayload(buf, len, v1, v2, pwr, (time_t)e.shadow.ts);
    break;
  }
  case EntryType::Thermometer:
  {
    ThermometerData d;
    macToStr(e.thermo.addr, d.address);
    d.rssi = e.thermo.rssi;
    d.temp = e.thermo.temp / 10.0f;
    d.humidity = e.thermo.humidity;
    d.battery = e.thermo.battery;
    d.mfHex[0] = '\0';
    buildThermometerPayload(buf, len, d, "");
    break;
  }
  case EntryType::Co2:
  {
    Co2MeterData d;
    macToStr(e.co2.addr, d.address);
    d.rssi = e.co2.rssi;
    d.temp = e.co2.temp / 10.0f;
    d.humidity = e.co2.humidity;
    d.co2 = e.co2.co2;
    d.battery = e.co2.battery;
    d.mfHex[0] = '\0';
    buildCo2Payload(buf, len, d, "");
    break;
  }
  }
}

// ─── PubQueue 実装 ────────────────────────────────────────────────────────────

void PubQueue::push(const QueueEntry &e)
{
  if (g_count >= OFFLINE_BUFFER_MAX)
  {
    // 上限超え: 最古エントリを捨てる
    g_head = (g_head + 1) % OFFLINE_BUFFER_MAX;
    g_count--;
  }
  g_buf[g_tail] = e;
  g_tail = (g_tail + 1) % OFFLINE_BUFFER_MAX;
  g_count++;
}

void PubQueue::pushShadow(const SensorReading &r)
{
  QueueEntry e;
  e.type = EntryType::Shadow;
  e.shadow.v1 = r.v1.voltage;
  e.shadow.v2 = r.v2.voltage;
  e.shadow.current = r.pwr.current;
  e.shadow.power = r.pwr.power;
  e.shadow.temp = r.pwr.temp;
  e.shadow.ts = (uint32_t)r.ts;
  push(e);
}

void PubQueue::pushThermometer(const ThermometerData &d)
{
  QueueEntry e;
  e.type = EntryType::Thermometer;
  strToMac(d.address, e.thermo.addr);
  e.thermo.rssi = d.rssi;
  e.thermo.temp = (int16_t)(d.temp * 10);
  e.thermo.humidity = d.humidity;
  e.thermo.battery = d.battery;
  push(e);
}

void PubQueue::pushCo2(const Co2MeterData &d)
{
  QueueEntry e;
  e.type = EntryType::Co2;
  strToMac(d.address, e.co2.addr);
  e.co2.rssi = d.rssi;
  e.co2.temp = (int16_t)(d.temp * 10);
  e.co2.humidity = d.humidity;
  e.co2.co2 = d.co2;
  e.co2.battery = d.battery;
  push(e);
}

void PubQueue::flush()
{
  if (empty())
    return;
  if (!lte.isConnected())
    return;

  char topic[80];
  char payload[PAYLOAD_SENSOR_SIZE];
  int flushed = 0;

  while (!empty())
  {
    buildTopic(g_buf[g_head], topic, sizeof(topic));
    buildPayload(g_buf[g_head], payload, sizeof(payload));

    if (!mqtt.publish(topic, payload))
    {
      logger.println("[QUEUE] MQTT 失敗、flush 停止");
      break;
    }

    g_head = (g_head + 1) % OFFLINE_BUFFER_MAX;
    g_count--;
    flushed++;
  }

  if (flushed > 0)
    logger.printf("[QUEUE] %d 件送信、残 %d 件\n", flushed, g_count);
}

void PubQueue::load()
{
  if (g_magic == RTC_MAGIC)
    return; // DeepSleep 復帰: RTC メモリ有効

  // 電源投入: RTC 初期化
  g_head = g_tail = g_count = 0;
  g_magic = RTC_MAGIC;

  if (!_useSpiffs)
    return;

  File f = SPIFFS.open(OFFLINE_BUFFER_PATH, "r");
  if (!f)
    return;

  uint32_t magic = 0;
  uint16_t count = 0;
  if (f.read((uint8_t *)&magic, 4) != 4 || magic != RTC_MAGIC)
  {
    f.close();
    return;
  }
  if (f.read((uint8_t *)&count, 2) != 2)
  {
    f.close();
    return;
  }

  int n = min((int)count, OFFLINE_BUFFER_MAX);
  for (int i = 0; i < n; i++)
  {
    QueueEntry e;
    if (f.read((uint8_t *)&e, sizeof(e)) != sizeof(e))
      break;
    push(e);
  }
  f.close();
  logger.printf("[QUEUE] SPIFFS から %d 件ロード\n", g_count);
}

void PubQueue::save()
{
  if (!_useSpiffs)
    return;

  if (empty())
  {
    SPIFFS.remove(OFFLINE_BUFFER_PATH);
    return;
  }

  File f = SPIFFS.open(OFFLINE_BUFFER_PATH, "w");
  if (!f)
  {
    logger.println("[QUEUE] SPIFFS 保存失敗");
    return;
  }

  uint32_t magic = RTC_MAGIC;
  uint16_t count = (uint16_t)g_count;
  f.write((uint8_t *)&magic, 4);
  f.write((uint8_t *)&count, 2);
  for (int i = 0; i < g_count; i++)
  {
    int idx = (g_head + i) % OFFLINE_BUFFER_MAX;
    f.write((uint8_t *)&g_buf[idx], sizeof(QueueEntry));
  }
  f.close();
  logger.printf("[QUEUE] SPIFFS に %d 件保存\n", g_count);
}

int PubQueue::size() const { return g_count; }
bool PubQueue::empty() const { return g_count == 0; }
