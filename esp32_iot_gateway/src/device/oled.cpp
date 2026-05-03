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

// TextSize=1: 1文字 6×8px、128px幅で21文字、64px高で8行
// タイトル行(y=0) + 区切り線(y=8) + アイテム最大6行(y=10,19,28,37,46,55)

void oledShowMenu(const char *title, const char *items[], int count, int cursor) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print(title);
  display.drawFastHLine(0, 8, 128, SSD1306_WHITE);

  const int MAX_VISIBLE = 6;
  int offset = cursor - MAX_VISIBLE + 1;
  if (offset < 0) offset = 0;
  for (int i = 0; i < MAX_VISIBLE && (i + offset) < count; i++) {
    int idx = i + offset;
    display.setCursor(0, 10 + i * 9);
    display.print(idx == cursor ? "> " : "  ");
    display.print(items[idx]);
  }
  display.display();
}

void oledShowMessage(const char *line1, const char *line2) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 20);
  display.println(line1);
  if (line2) {
    display.setCursor(0, 32);
    display.println(line2);
  }
  display.display();
}

void oledShowConfirm(const char *message, const char *item, int yesNoCursor) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(message);
  display.setCursor(0, 10);
  display.println(item);
  display.drawFastHLine(0, 20, 128, SSD1306_WHITE);
  display.setCursor(0, 55);
  display.print("BTN1 long: cancel");

  // Yes ボタン (x=8, w=44)
  if (yesNoCursor == 0) {
    display.fillRoundRect(8, 30, 44, 14, 2, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
  }
  display.setCursor(18, 33);
  display.print("Yes");
  display.setTextColor(SSD1306_WHITE);

  // No ボタン (x=76, w=40)
  if (yesNoCursor == 1) {
    display.fillRoundRect(76, 30, 40, 14, 2, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
  }
  display.setCursor(84, 33);
  display.print("No");
  display.setTextColor(SSD1306_WHITE);

  display.display();
}

void oledShowSensorData(float v1, float v2, float cur, float pwr, float temp) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.printf("V1:%.2fV V2:%.2fV", v1, v2);
  display.setCursor(0, 12);
  display.printf("Cur: %.2fA", cur);
  display.setCursor(0, 22);
  display.printf("Pwr: %.1fW", pwr);
  display.setCursor(0, 32);
  display.printf("Tmp: %.1fC", temp);
  display.setCursor(0, 56);
  display.print("BTN1 long: back");
  display.display();
}
