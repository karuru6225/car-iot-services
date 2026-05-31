#include "ble_peripheral.h"
#include "oled.h"
#include "../config.h"
#include <NimBLEDevice.h>

#define MEAS_SERVICE_UUID    "f3a8b2c1-d4e5-4f6a-7b8c-9d0e1f2a3b4c"
#define MEAS_VMAIN_UUID      "f3a8b2c2-d4e5-4f6a-7b8c-9d0e1f2a3b4c"
#define MEAS_CURR_UUID       "f3a8b2c3-d4e5-4f6a-7b8c-9d0e1f2a3b4c"
#define MEAS_PWR_UUID        "f3a8b2c4-d4e5-4f6a-7b8c-9d0e1f2a3b4c"
#define MEAS_VSUB_UUID       "f3a8b2c5-d4e5-4f6a-7b8c-9d0e1f2a3b4c"

#define CFG_SERVICE_UUID     "f3a8b2d1-d4e5-4f6a-7b8c-9d0e1f2a3b4c"
#define CFG_AH_OFFSET_UUID   "f3a8b2d2-d4e5-4f6a-7b8c-9d0e1f2a3b4c"
#define CFG_CHG_TIMEOUT_UUID "f3a8b2d3-d4e5-4f6a-7b8c-9d0e1f2a3b4c"
#define CFG_CHG_START_UUID   "f3a8b2d4-d4e5-4f6a-7b8c-9d0e1f2a3b4c"
#define CFG_CHG_STOP_UUID    "f3a8b2d5-d4e5-4f6a-7b8c-9d0e1f2a3b4c"

BlePeripheral blePeripheral;

class BlePeripheralServerCb : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer*) override {
    blePeripheral._connected = true;
  }
  void onDisconnect(NimBLEServer*) override {
    blePeripheral._connected    = false;
    blePeripheral._authComplete = false;
    NimBLEDevice::startAdvertising();
  }
};

class BlePeripheralSecurityCb : public NimBLESecurityCallbacks {
  uint32_t onPassKeyRequest() override { return 0; }
  void     onPassKeyNotify(uint32_t) override {}
  bool     onSecurityRequest() override { return true; }
  bool     onConfirmPIN(uint32_t) override { return true; }
  void     onAuthenticationComplete(ble_gap_conn_desc* desc) override {
    if (desc->sec_state.authenticated) {
      blePeripheral._authComplete = true;
    }
  }
};

// 設定 Characteristic コールバック（Read は NVS から現在値を返す、Write は setter を呼ぶ）
class CfgAhOffsetCb : public NimBLECharacteristicCallbacks {
  void onRead(NimBLECharacteristic* pChar) override {
    int32_t v = getAhOffset();
    pChar->setValue(reinterpret_cast<uint8_t*>(&v), sizeof(v));
  }
  void onWrite(NimBLECharacteristic* pChar) override {
    if (pChar->getValue().size() == sizeof(int32_t)) {
      int32_t v;
      memcpy(&v, pChar->getValue().data(), sizeof(v));
      setAhOffset(v);
    }
  }
};

class CfgChgTimeoutCb : public NimBLECharacteristicCallbacks {
  void onRead(NimBLECharacteristic* pChar) override {
    uint32_t v = getChgTimeoutMin();
    pChar->setValue(reinterpret_cast<uint8_t*>(&v), sizeof(v));
  }
  void onWrite(NimBLECharacteristic* pChar) override {
    if (pChar->getValue().size() == sizeof(uint32_t)) {
      uint32_t v;
      memcpy(&v, pChar->getValue().data(), sizeof(v));
      setChgTimeoutMin(v);
    }
  }
};

class CfgChgStartCb : public NimBLECharacteristicCallbacks {
  void onRead(NimBLECharacteristic* pChar) override {
    float v = getChgStartV();
    pChar->setValue(reinterpret_cast<uint8_t*>(&v), sizeof(v));
  }
  void onWrite(NimBLECharacteristic* pChar) override {
    if (pChar->getValue().size() == sizeof(float)) {
      float v;
      memcpy(&v, pChar->getValue().data(), sizeof(v));
      setChgStartV(v);
    }
  }
};

class CfgChgStopCb : public NimBLECharacteristicCallbacks {
  void onRead(NimBLECharacteristic* pChar) override {
    float v = getChgStopV();
    pChar->setValue(reinterpret_cast<uint8_t*>(&v), sizeof(v));
  }
  void onWrite(NimBLECharacteristic* pChar) override {
    if (pChar->getValue().size() == sizeof(float)) {
      float v;
      memcpy(&v, pChar->getValue().data(), sizeof(v));
      setChgStopV(v);
    }
  }
};


void BlePeripheral::setup() {
  NimBLEDevice::setSecurityCallbacks(new BlePeripheralSecurityCb());
  NimBLEServer* pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new BlePeripheralServerCb());

  // 計測サービス（認証不要、Notify のみ）
  NimBLEService* pMeas = pServer->createService(MEAS_SERVICE_UUID);
  _pVoltMainChar = pMeas->createCharacteristic(MEAS_VMAIN_UUID, NIMBLE_PROPERTY::NOTIFY);
  _pCurrChar     = pMeas->createCharacteristic(MEAS_CURR_UUID,  NIMBLE_PROPERTY::NOTIFY);
  _pPwrChar      = pMeas->createCharacteristic(MEAS_PWR_UUID,   NIMBLE_PROPERTY::NOTIFY);
  _pVoltSubChar  = pMeas->createCharacteristic(MEAS_VSUB_UUID,  NIMBLE_PROPERTY::NOTIFY);
  pMeas->start();

  // 設定サービス（MITM 認証必要）
  NimBLEService* pCfg = pServer->createService(CFG_SERVICE_UUID);

  pCfg->createCharacteristic(CFG_AH_OFFSET_UUID,
    NIMBLE_PROPERTY::READ_AUTHEN | NIMBLE_PROPERTY::WRITE_AUTHEN)
    ->setCallbacks(new CfgAhOffsetCb());

  pCfg->createCharacteristic(CFG_CHG_TIMEOUT_UUID,
    NIMBLE_PROPERTY::READ_AUTHEN | NIMBLE_PROPERTY::WRITE_AUTHEN)
    ->setCallbacks(new CfgChgTimeoutCb());

  pCfg->createCharacteristic(CFG_CHG_START_UUID,
    NIMBLE_PROPERTY::READ_AUTHEN | NIMBLE_PROPERTY::WRITE_AUTHEN)
    ->setCallbacks(new CfgChgStartCb());

  pCfg->createCharacteristic(CFG_CHG_STOP_UUID,
    NIMBLE_PROPERTY::READ_AUTHEN | NIMBLE_PROPERTY::WRITE_AUTHEN)
    ->setCallbacks(new CfgChgStopCb());

  pCfg->start();
}

void BlePeripheral::startAdvertising() {
  NimBLEAdvertising* pAdv = NimBLEDevice::getAdvertising();
  pAdv->addServiceUUID(MEAS_SERVICE_UUID);
  pAdv->setScanResponse(true);
  NimBLEDevice::startAdvertising();
}

void BlePeripheral::enablePairing() {
  _authComplete = false;
  _passkey = esp_random() % 1000000;
  char keyStr[7];
  snprintf(keyStr, sizeof(keyStr), "%06lu", (unsigned long)_passkey);
  NimBLEDevice::setSecurityAuth(
    BLE_SM_PAIR_AUTHREQ_BOND | BLE_SM_PAIR_AUTHREQ_SC | BLE_SM_PAIR_AUTHREQ_MITM);
  NimBLEDevice::setSecurityPasskey(_passkey);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
  oledShowMessage("Pair Code:", keyStr);
}

void BlePeripheral::disablePairing() {
  _authComplete = false;
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
}

void BlePeripheral::notify(float vMain, float i, float p, float vSub) {
  if (!_connected) return;
  auto notifyFloat = [](NimBLECharacteristic* pChar, float val) {
    pChar->setValue(reinterpret_cast<uint8_t*>(&val), sizeof(val));
    pChar->notify();
  };
  notifyFloat(_pVoltMainChar, vMain);
  notifyFloat(_pCurrChar,     i);
  notifyFloat(_pPwrChar,      p);
  notifyFloat(_pVoltSubChar,  vSub);
}

void BlePeripheral::stop() {
  NimBLEDevice::stopAdvertising();
}
