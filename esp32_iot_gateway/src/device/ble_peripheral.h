#pragma once
#include <NimBLEDevice.h>
#include <atomic>

class BlePeripheral {
public:
  void setup();
  void startAdvertising();
  void enablePairing();    // MITM Passkey モード + OLED にコード表示
  void disablePairing();   // ペアリングモード解除（キャンセル時）
  void notify(float vMain, float i, float p, float vSub,
              float temp, float ah, uint32_t ts, bool lteConnected);
  bool isConnected()    const { return _connected; }
  bool isAuthComplete() const { return _authComplete; }
  void stop();

private:
  NimBLECharacteristic* _pVoltMainChar = nullptr;
  NimBLECharacteristic* _pCurrChar     = nullptr;
  NimBLECharacteristic* _pPwrChar      = nullptr;
  NimBLECharacteristic* _pVoltSubChar  = nullptr;
  NimBLECharacteristic* _pTempChar     = nullptr;
  NimBLECharacteristic* _pAhChar       = nullptr;
  NimBLECharacteristic* _pTsChar       = nullptr;
  NimBLECharacteristic* _pLteChar      = nullptr;
  std::atomic<bool> _connected{false};
  std::atomic<bool> _authComplete{false};
  uint32_t _passkey      = 0;

  friend class BlePeripheralServerCb;
  friend class BlePeripheralSecurityCb;
};

extern BlePeripheral blePeripheral;
