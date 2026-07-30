#include "obdpoll.h"
#include "logger.h"
#include "../device/can.h"
#include <time.h>

namespace
{
struct PidDecoder
{
  uint8_t pid;
  bool (*decode)(const uint8_t *, uint8_t, OBDReading &);
};

const PidDecoder kPids[] = {
    {0x0C, obdDecodeRpm},
    {0x0D, obdDecodeSpeed},
    {0x04, obdDecodeLoad},
    {0x0B, obdDecodeMap},
    {0x33, obdDecodeBaro},
    {0x11, obdDecodeThrottle},
    {0x0E, obdDecodeTiming},
    {0x42, obdDecodeEcuVoltage},
    {0x66, obdDecodeMafAlt},
    {0x67, obdDecodeCoolantAlt},
};
} // namespace

OBDReading obdPoll()
{
  OBDReading r = {};
  r.ts = time(nullptr);

  uint8_t data[8];
  uint8_t dlc;

  for (const auto &p : kPids)
  {
    if (canSendObdRequest(p.pid) && canReceiveObdResponse(data, &dlc, 100))
      if (p.decode(data, dlc, r))
        r.valid = true;
  }

  if (r.valid)
  {
    // boost = MAP - Baro（Baro未取得時は標準大気圧101kPaで代用）
    r.boost_kpa = (int8_t)(r.map_kpa - (r.baro_kpa > 0 ? r.baro_kpa : 101));
    // 燃費推算（MAFから）: OBD.md 参照
    if (r.maf_gs > 0)
      r.fuel_rate_lph = r.maf_gs / (14.7f * 0.745f) * 3.6f;

    logger.printf("[OBD] rpm=%u speed=%ukm/h load=%u%% map=%ukPa boost=%dkPa throttle=%u%% "
                  "timing=%.1f ecu=%.2fV maf=%.2fg/s coolant=%dC fuel=%.2fL/h\n",
                  r.rpm, r.speed_kmh, r.load_pct, r.map_kpa, r.boost_kpa, r.throttle_pct,
                  r.timing_deg, r.ecu_voltage, r.maf_gs, r.coolant_c, r.fuel_rate_lph);
  }
  else
  {
    logger.println("[OBD] 応答なし（IGN OFF または CAN 未接続）");
  }

  return r;
}
