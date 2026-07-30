#include "obd.h"

// 応答パケットの共通チェック: [0]=PCI [1]=0x41(Mode01応答) [2]=PID [3]=A [4]=B ...
static bool checkHeader(const uint8_t *data, uint8_t dlc, uint8_t pid, uint8_t minDlc)
{
  if (dlc < minDlc)
    return false;
  if (data[1] != 0x41 || data[2] != pid)
    return false;
  return true;
}

bool obdDecodeRpm(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  if (!checkHeader(data, dlc, 0x0C, 5))
    return false;
  out.rpm = (uint16_t)(((uint16_t)data[3] * 256 + data[4]) / 4);
  return true;
}

bool obdDecodeSpeed(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  if (!checkHeader(data, dlc, 0x0D, 4))
    return false;
  out.speed_kmh = data[3];
  return true;
}

bool obdDecodeLoad(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  if (!checkHeader(data, dlc, 0x04, 4))
    return false;
  out.load_pct = (uint8_t)((uint16_t)data[3] * 100 / 255);
  return true;
}

bool obdDecodeMap(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  if (!checkHeader(data, dlc, 0x0B, 4))
    return false;
  out.map_kpa = data[3];
  return true;
}

bool obdDecodeBaro(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  if (!checkHeader(data, dlc, 0x33, 4))
    return false;
  out.baro_kpa = data[3];
  return true;
}

bool obdDecodeThrottle(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  if (!checkHeader(data, dlc, 0x11, 4))
    return false;
  out.throttle_pct = (uint8_t)((uint16_t)data[3] * 100 / 255);
  return true;
}

bool obdDecodeTiming(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  if (!checkHeader(data, dlc, 0x0E, 4))
    return false;
  out.timing_deg = data[3] / 2.0f - 64.0f;
  return true;
}

bool obdDecodeEcuVoltage(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  if (!checkHeader(data, dlc, 0x42, 5))
    return false;
  out.ecu_voltage = ((uint16_t)data[3] * 256 + data[4]) / 1000.0f;
  return true;
}

bool obdDecodeMafAlt(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  if (!checkHeader(data, dlc, 0x66, 6))
    return false;
  out.maf_gs = ((uint16_t)data[4] * 256 + data[5]) / 32.0f;
  return true;
}

bool obdDecodeCoolantAlt(const uint8_t *data, uint8_t dlc, OBDReading &out)
{
  if (!checkHeader(data, dlc, 0x67, 5))
    return false;
  out.coolant_c = (int16_t)data[4] - 40;
  return true;
}
