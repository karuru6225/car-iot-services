#include "infra__ble_scan.h"
#include "domain__targets.h"
#include "register_mode.h"

BleScanner scanner;

class SwitchBotCallback : public BLEAdvertisedDeviceCallbacks
{
  void onResult(BLEAdvertisedDevice dev)
  {
    if (!dev.haveManufacturerData()) return;
    String mf = dev.getManufacturerData();
    if (mf.length() < 2) return;

    uint16_t companyId = ((uint8_t)mf[1] << 8) | (uint8_t)mf[0];
    if (companyId != SWITCHBOT_COMPANY_ID) return;

    const char *addr = dev.getAddress().toString().c_str();

    if (regMode.isScanning())
    {
      regMode.foundDevice(addr);
      return;
    }

    if (!targets.isTarget(addr)) return;

    String sd = dev.haveServiceData() ? dev.getServiceData() : String();
    SwitchBotData d = SwitchBotData::parse(addr, dev.getRSSI(), mf, sd);
    xQueueSend(scanner.queue, &d, 0);
  }
};

void BleScanner::setup()
{
  queue = xQueueCreate(QUEUE_SIZE, sizeof(SwitchBotData));
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
