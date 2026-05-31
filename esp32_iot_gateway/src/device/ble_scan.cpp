#include "ble_scan.h"
#include "../domain/ble_targets.h"
#include "../domain/sensor_filter.h"

BleScanner bleScanner;

class SwitchBotCallback : public NimBLEAdvertisedDeviceCallbacks
{
  void onResult(NimBLEAdvertisedDevice *dev)
  {
    if (!dev->haveManufacturerData())
      return;
    std::string mf = dev->getManufacturerData();
    if (mf.length() < 2)
      return;

    uint16_t companyId = ((uint8_t)mf[1] << 8) | (uint8_t)mf[0];
    if (companyId != SWITCHBOT_COMPANY_ID)
      return;

    std::string addr = dev->getAddress().toString();
    if (!bleScanner.registrationMode && !bleTargets.isTarget(addr.c_str()))
      return;

    std::string sd = dev->haveServiceData() ? dev->getServiceData() : std::string();
    SensorVariant v = SensorParserFactory::parse(addr.c_str(), dev->getRSSI(), mf, sd);
#ifdef BLE_MEDIAN_FILTER
    sensorFilter.apply(v);
#endif
    xQueueSend(bleScanner.queue, &v, 0);
  }
};

void BleScanner::setup()
{
#ifdef BLE_MEDIAN_FILTER
  sensorFilter.begin();
#endif
  queue = xQueueCreate(QUEUE_SIZE, sizeof(SensorVariant));
  // デバイス名に MAC 上位 3 バイトを付加して複数台を区別可能にする
  // "esp32-gw-aabbccddeeff" → "car-iot-aabbcc"
  const char *id = getDeviceId();
  char bleName[20];
  snprintf(bleName, sizeof(bleName), "car-iot-%.6s", id + 9); // 9 = strlen("esp32-gw-")
  NimBLEDevice::init(bleName);
  _scan = NimBLEDevice::getScan();
  _scan->setAdvertisedDeviceCallbacks(new SwitchBotCallback());
  _scan->setActiveScan(true);
  _scan->setInterval(320); // 320ms ごとに 100ms スキャン → 220ms アドバタイズ窓を確保
  _scan->setWindow(100);
}

void BleScanner::start(int seconds)
{
  _scan->start(seconds, false);
}

void BleScanner::clearResults()
{
  _scan->clearResults();
}

void BleScanner::deinit()
{
  // BLE Peripheral と共存するため deinit しない
}
