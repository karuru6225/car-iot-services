#include <Arduino.h>
#include <math.h>
#include <NimBLEDevice.h>

// カスタム計測サービス
#define MEAS_SERVICE_UUID "f3a8b2c1-d4e5-4f6a-7b8c-9d0e1f2a3b4c"
#define MEAS_VOLTAGE_UUID "f3a8b2c2-d4e5-4f6a-7b8c-9d0e1f2a3b4c"  // float32, V
#define MEAS_CURRENT_UUID "f3a8b2c3-d4e5-4f6a-7b8c-9d0e1f2a3b4c"  // float32, A
#define MEAS_POWER_UUID   "f3a8b2c4-d4e5-4f6a-7b8c-9d0e1f2a3b4c"  // float32, W
#define MEAS_TEMP_UUID    "f3a8b2c5-d4e5-4f6a-7b8c-9d0e1f2a3b4c"  // float32, °C

// カスタム設定サービス
#define CFG_SERVICE_UUID   "f3a8b2d1-d4e5-4f6a-7b8c-9d0e1f2a3b4c"
#define CFG_LOW_VOLT_UUID  "f3a8b2d2-d4e5-4f6a-7b8c-9d0e1f2a3b4c"  // float32, V  (低電圧アラート閾値)
#define CFG_SLEEP_SEC_UUID "f3a8b2d3-d4e5-4f6a-7b8c-9d0e1f2a3b4c"  // uint32, sec (DeepSleep 間隔)

static NimBLECharacteristic* pVoltChar    = nullptr;
static NimBLECharacteristic* pCurrentChar = nullptr;
static NimBLECharacteristic* pPowerChar   = nullptr;
static NimBLECharacteristic* pTempChar    = nullptr;
static bool clientConnected = false;

// 設定値（本番では NVS に永続化する）
static float    gLowVoltThreshold = 11.0f;
static uint32_t gSleepIntervalSec = 300;

class ServerCb : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer*) override {
    clientConnected = true;
    Serial.println("[BLE] connected");
  }
  void onDisconnect(NimBLEServer*) override {
    clientConnected = false;
    Serial.println("[BLE] disconnected — restarting advertising");
    NimBLEDevice::startAdvertising();
  }
};

static void notifyFloat(NimBLECharacteristic* pChar, float val) {
  pChar->setValue(reinterpret_cast<uint8_t*>(&val), sizeof(val));
  pChar->notify();
}

class CfgLowVoltCb : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* pChar) override {
    const std::string& v = pChar->getValue();
    if (v.size() == sizeof(float)) {
      memcpy(&gLowVoltThreshold, v.data(), sizeof(float));
      Serial.printf("[CFG] low_volt_threshold = %.3f V\n", gLowVoltThreshold);
    }
  }
};

class CfgSleepSecCb : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* pChar) override {
    const std::string& v = pChar->getValue();
    if (v.size() == sizeof(uint32_t)) {
      memcpy(&gSleepIntervalSec, v.data(), sizeof(uint32_t));
      Serial.printf("[CFG] sleep_interval_sec = %lu\n", gSleepIntervalSec);
    }
  }
};

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("[ble_verify] starting custom measurement service...");

  NimBLEDevice::init("car-iot-ble");
  NimBLEServer* pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new ServerCb());

  NimBLEService* pMeas = pServer->createService(MEAS_SERVICE_UUID);
  pVoltChar    = pMeas->createCharacteristic(MEAS_VOLTAGE_UUID, NIMBLE_PROPERTY::NOTIFY);
  pCurrentChar = pMeas->createCharacteristic(MEAS_CURRENT_UUID, NIMBLE_PROPERTY::NOTIFY);
  pPowerChar   = pMeas->createCharacteristic(MEAS_POWER_UUID,   NIMBLE_PROPERTY::NOTIFY);
  pTempChar    = pMeas->createCharacteristic(MEAS_TEMP_UUID,    NIMBLE_PROPERTY::NOTIFY);
  pMeas->start();

  // 設定サービス
  NimBLEService* pCfg = pServer->createService(CFG_SERVICE_UUID);

  NimBLECharacteristic* pLowVoltChar = pCfg->createCharacteristic(
    CFG_LOW_VOLT_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE
  );
  pLowVoltChar->setCallbacks(new CfgLowVoltCb());
  pLowVoltChar->setValue(reinterpret_cast<uint8_t*>(&gLowVoltThreshold), sizeof(gLowVoltThreshold));

  NimBLECharacteristic* pSleepSecChar = pCfg->createCharacteristic(
    CFG_SLEEP_SEC_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE
  );
  pSleepSecChar->setCallbacks(new CfgSleepSecCb());
  pSleepSecChar->setValue(reinterpret_cast<uint8_t*>(&gSleepIntervalSec), sizeof(gSleepIntervalSec));

  pCfg->start();

  NimBLEAdvertising* pAdv = NimBLEDevice::getAdvertising();
  pAdv->addServiceUUID(MEAS_SERVICE_UUID);
  pAdv->setScanResponse(true);
  NimBLEDevice::startAdvertising();

  Serial.println("[ble_verify] advertising as 'car-iot-ble'");
}

static uint32_t counter = 0;
static unsigned long lastSendMs = 0;

void loop() {
  unsigned long now = millis();
  if (!clientConnected || now - lastSendMs < 1000) return;
  lastSendMs = now;

  float t       = counter * 0.1f;
  float voltage = 12.0f + 0.5f * sinf(t);
  float current =  2.0f + 0.3f * cosf(t * 1.5f);
  float power   = voltage * current;
  float tempC   = 25.0f + 0.5f * sinf(t * 0.5f);

  notifyFloat(pVoltChar,    voltage);
  notifyFloat(pCurrentChar, current);
  notifyFloat(pPowerChar,   power);
  notifyFloat(pTempChar,    tempC);

  Serial.printf("[ble_verify] v=%.3fV i=%.3fA p=%.2fW t=%.2f°C\n",
    voltage, current, power, tempC);
  counter++;
}
