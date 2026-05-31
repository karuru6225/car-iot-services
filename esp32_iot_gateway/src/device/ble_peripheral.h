#pragma once
#include <NimBLEDevice.h>

class BlePeripheral {
public:
  void setup();
  void startAdvertising();
  void enablePairing();    // MITM Passkey モード + OLED にコード表示
  void disablePairing();   // ペアリングモード解除（キャンセル時）
  void notify(float vMain, float i, float p, float vSub);
  bool isConnected()    const { return _connected; }
  bool isAuthComplete() const { return _authComplete; }
  void stop();

private:
  NimBLECharacteristic* _pVoltMainChar = nullptr;
  NimBLECharacteristic* _pCurrChar     = nullptr;
  NimBLECharacteristic* _pPwrChar      = nullptr;
  NimBLECharacteristic* _pVoltSubChar  = nullptr;
  bool     _connected    = false;
  bool     _authComplete = false;
  uint32_t _passkey      = 0;

  friend class BlePeripheralServerCb;
  friend class BlePeripheralSecurityCb;
};

extern BlePeripheral blePeripheral;
