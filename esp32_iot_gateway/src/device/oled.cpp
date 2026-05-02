#include "oled.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SDA_PIN 17
#define SCL_PIN 18

static Adafruit_SSD1306 display(128, 64, &Wire, -1);

void oledInit()
{
  Wire.begin(SDA_PIN, SCL_PIN);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.display();
}

void oledPrint(const char *text)
{
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(text);
  display.display();
}

void oledShowOtaProgress(const char *stage, size_t current, size_t total)
{
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("OTA Update");
  display.println(stage);

  if (total > 0)
  {
    int pct = (int)(current * 100 / total);
    display.printf("%d%% (%u KB / %u KB)\n", pct, current / 1024, total / 1024);

    // プログレスバー（幅120px、y=36）
    static const int BAR_X = 4, BAR_Y = 36, BAR_W = 120, BAR_H = 8;
    display.drawRect(BAR_X, BAR_Y, BAR_W, BAR_H, SSD1306_WHITE);
    int filled = BAR_W * pct / 100;
    if (filled > 0)
      display.fillRect(BAR_X, BAR_Y, filled, BAR_H, SSD1306_WHITE);
  }

  display.display();
}

void oledShowStatus(float voltage, float voltage2, bool relayOn, bool btn0, bool btn1)
{
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);

  display.printf("Volt:  %.2f V\n", voltage);
  display.printf("Volt2: %.2f V\n", voltage2);
  display.printf("Relay: %s\n", relayOn ? "ON " : "OFF");
  display.printf("BTN0:  %s\n", btn0 ? "ON " : "OFF");
  display.printf("BTN1:  %s\n", btn1 ? "ON " : "OFF");

  display.display();
}
