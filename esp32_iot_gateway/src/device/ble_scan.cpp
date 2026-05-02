#include "ble_scan.h"
#include "../domain/ble_targets.h"

BleScanner bleScanner;

class SwitchBotCallback : public BLEAdvertisedDeviceCallbacks
{
  void onResult(BLEAdvertisedDevice dev)
  {
    if (!dev.haveManufacturerData())
      return;
    std::string mf = dev.getManufacturerData();
    if (mf.length() < 2)
      return;

    uint16_t companyId = ((uint8_t)mf[1] << 8) | (uint8_t)mf[0];
    if (companyId != SWITCHBOT_COMPANY_ID)
      return;

    std::string addr = dev.getAddress().toString();
    if (!bleTargets.isTarget(addr.c_str()))
      return;

    std::string sd = dev.haveServiceData() ? dev.getServiceData() : std::string();
    SensorVariant v = SensorParserFactory::parse(addr.c_str(), dev.getRSSI(), mf, sd);
    xQueueSend(bleScanner.queue, &v, 0);
  }
};

void BleScanner::setup()
{
  queue = xQueueCreate(QUEUE_SIZE, sizeof(SensorVariant));
  BLEDevice::init("");
  _scan = BLEDevice::getScan();
  _scan->setAdvertisedDeviceCallbacks(new SwitchBotCallback());
  _scan->setActiveScan(true);
  _scan->setInterval(100);
  _scan->setWindow(99);
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
  BLEDevice::deinit(true);
}
