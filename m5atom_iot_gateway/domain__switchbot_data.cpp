#include "domain__switchbot_data.h"

// WoIOSensor Manufacturer Data フォーマット
//   [0-1]  Company ID
//   [2-7]  MAC アドレス
//   [8]    シーケンス
//   [9]    不明
//   [10]   温度 小数部 (0-9)
//   [11]   温度 整数部 (bit6-0) + 符号 (bit7: 1=正)
//   [12]   湿度 (%)
// Service Data フォーマット: [0]=デバイス種別 [1]=ステータス [2]=バッテリー(%)
SwitchBotData SwitchBotData::parse(const char *addr, int8_t rssi,
                                    const String &mfData,
                                    const String &serviceData)
{
  SwitchBotData d = {};
  strncpy(d.address, addr, sizeof(d.address) - 1);
  d.rssi = rssi;

  if (mfData.length() >= 13)
  {
    uint8_t dec     = (uint8_t)mfData[10] & 0x0F;
    uint8_t tempInt = (uint8_t)mfData[11] & 0x7F;
    bool    pos     = ((uint8_t)mfData[11] & 0x80) != 0;
    d.humidity = (uint8_t)mfData[12];
    d.temp     = tempInt + dec / 10.0f;
    if (!pos) d.temp = -d.temp;
    d.parsed = true;
  }

  if (serviceData.length() >= 3)
    d.battery = (uint8_t)serviceData[2];

  return d;
}
