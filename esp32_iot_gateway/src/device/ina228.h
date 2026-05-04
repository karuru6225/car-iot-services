#pragma once
#include <Arduino.h>

class Ina228
{
public:
  bool init();
  float readVBus();       // V
  float readCurrent();    // A
  float readPower();      // W
  float readTemp();       // °C
  float readCharge();     // Ah
  void resetCharge();
  void printStatus();

private:
  static constexpr uint8_t ADDR = 0x40;
  static constexpr float CURRENT_LSB = 208e-6f; // A/bit
  static constexpr uint16_t SHUNT_CAL = 4096;
  static constexpr uint16_t CONFIG =
    (1 << 4)        // ADCRANGE=1
    | (0b111 << 9)  // VBUSCT=7h 4120us
    | (0b111 << 6)  // VSHCT=7h 4120us
    | (0b111 << 3)  // VTCT=7h 4120us
    | (0b111 << 0); // AVG=7h 1024 samples

  bool _initialized = false;

  void wr16(uint8_t reg, uint16_t v);
  uint16_t rd16(uint8_t reg);
  int32_t rd24s(uint8_t reg);
  uint32_t rd24u(uint8_t reg);
  uint64_t rd40u(uint8_t reg);
  float readShuntVoltage(); // V (printStatus 内部用)
};

extern Ina228 ina228;
