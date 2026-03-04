#include "view.h"
#include "infra__logger.h"

class MultiOutput : public Print
{
public:
  MultiOutput()
  {
    M5.Display.setFont(&fonts::efontJA_12);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setTextWrap(true);
  }
  size_t write(uint8_t c) override
  {
    Serial.write(c);
    M5.Display.write(c);
    return 1;
  }
  size_t write(const uint8_t *buf, size_t size) override
  {
    Serial.write(buf, size);
    M5.Display.write(buf, size);
    return size;
  }
};

static MultiOutput MOut;

View view;

void View::clear()
{
  M5.Display.clear();
  M5.Display.setCursor(0, 0);
}

void View::sensorData(const SwitchBotData &d)
{
  MOut.printf("= %s =\n", d.address);
  if (d.parsed)
  {
    MOut.print("温度      : ");
    MOut.print(d.temp, 1);
    MOut.println(" C");
    MOut.print("湿度      : ");
    MOut.print(d.humidity);
    MOut.println(" %");
    MOut.print("バッテリー: ");
    MOut.print(d.battery);
    MOut.println(" %");
    MOut.println("");
  }
  else
  {
    MOut.println("[解析スキップ]");
    MOut.println("データ長不足");
  }
}

void View::message(const char *msg)
{
  MOut.println(msg);
}

void View::registerModeStart()
{
  clear();
  MOut.println("=== アドレス登録モード ===");
  MOut.println("スキャン中...");
}

void View::registerNoDevices()
{
  MOut.println("デバイスが見つかりません");
}

void View::registerList(const RegEntry *list, int count, int selected)
{
  // Serial: 全件表示
  Serial.println();
  Serial.println("=== デバイス一覧 ===");
  for (int i = 0; i < count; i++)
  {
    Serial.print(i == selected ? " > " : "   ");
    Serial.print(list[i].address);
    if (!list[i].cancel)
      Serial.println(list[i].registered ? "  [登録済]" : "  [未登録]");
    else
      Serial.println();
  }
  Serial.println("[ 短押し: 次へ  |  長押し: 戻る / 登録済→削除 / 未登録→登録 ]");

  // Display: 選択位置を中心にしたウィンドウ表示
  clear();
  const int FONT_H    = M5.Display.fontHeight();
  const int MAX_ROWS  = M5.Display.height() / FONT_H;
  const int LIST_ROWS = MAX_ROWS - 2; // ヘッダー 1行 + フッター 1行を除く

  int start = selected - LIST_ROWS / 2;
  if (start > count - LIST_ROWS) start = count - LIST_ROWS;
  if (start < 0) start = 0;

  M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
  M5.Display.println("=== Device List ===");

  for (int i = start; i < count && i < start + LIST_ROWS; i++)
  {
    if (i == selected)
    {
      M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
      M5.Display.print(">");
    }
    else
    {
      M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
      M5.Display.print(" ");
    }
    if (list[i].cancel)
    {
      M5.Display.println(list[i].address);
    }
    else
    {
      char buf[22];
      snprintf(buf, sizeof(buf), "%s[%c]",
               list[i].address,
               list[i].registered ? 'R' : 'N');
      M5.Display.println(buf);
    }
  }

  M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
  M5.Display.println("[S:Next  L:Select]");
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
}

void View::registerCancel()
{
  clear();
  MOut.println("キャンセル。");
  MOut.println("スキャンモードに戻ります。");
}

void View::registerResult(bool wasRegistered, const char *addr)
{
  clear();
  MOut.println(wasRegistered ? "削除しました:" : "登録しました:");
  MOut.println(addr);
  MOut.println("スキャンモードに戻ります。");
}
