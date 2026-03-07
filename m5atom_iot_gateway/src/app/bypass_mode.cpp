#include "bypass_mode.h"

// Serial2 = SIM7080G の UART（infra/lte.h の SerialAT と同一）
// TinyGSM を引き込まないよう直接参照する
#define MODEM_SERIAL Serial2

BypassMode bypassMode;

void BypassMode::run()
{
  Serial.println("Enter bypass mode");

  String lineBuf = "";

  while (true)
  {
    // Serial → MODEM_SERIAL
    while (Serial.available())
    {
      char c = (char)Serial.read();
      Serial.write(c); // ローカルエコー

      if (c == '\r') continue; // CR は無視

      if (c == '\n')
      {
        if (lineBuf == "exit")
        {
          Serial.println("Exit bypass mode");
          return;
        }
        MODEM_SERIAL.print(lineBuf + "\r\n");
        lineBuf = "";
      }
      else
      {
        lineBuf += c;
      }
    }

    // MODEM_SERIAL → Serial
    while (MODEM_SERIAL.available())
    {
      Serial.write((char)MODEM_SERIAL.read());
    }

    delay(1);
  }
}
