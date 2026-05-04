#include "menu.h"
#include <esp_system.h>
#include "../device/button.h"
#include "../device/oled.h"
#include "../device/ble_scan.h"
#include "../device/ads.h"
#include "../device/ina228.h"
#include "../domain/ble_targets.h"
#include "../domain/sensor_factory.h"
#include "../config.h"
#include <Arduino.h>

// ---- 状態定義 ----

enum class MenuState
{
  MAIN,
  BLE_SCAN,
  BLE_SCAN_RESULT,
  BLE_REMOVE,
  BLE_REMOVE_CONFIRM,
  SENSOR,
  SYS_INFO,
  DONE_CONTINUOUS,
};

// ---- 共有状態 ----

static const char *MAIN_ITEMS[] = {
    "BLE: Register",
    "BLE: Remove",
    "Sensor View",
    "System Info",
    "Continuous",
    "Restart",
};
static const int MAIN_ITEM_COUNT = 6;

struct ScanResult
{
  char addr[18];
  int8_t rssi;
};
static const int MAX_SCAN_RESULTS = 20;
static ScanResult s_scanResults[MAX_SCAN_RESULTS];
static int s_scanResultCount = 0;

static int s_cursor = 0;
static int s_confirmCursor = 1; // 0=Yes, 1=No
static int s_removeTarget = 0;
static int s_savedCursor = 0; // CONFIRM から BLE_REMOVE に戻るときのカーソル復元用

// ---- tick 関数（各状態の描画 + 入力処理、次状態を返す） ----

static MenuState tickMain(ButtonEvent ev)
{
  oledShowMenu("Menu", MAIN_ITEMS, MAIN_ITEM_COUNT, s_cursor);
  if (ev == ButtonEvent::BTN0_SHORT)
  {
    s_cursor = (s_cursor + 1) % MAIN_ITEM_COUNT;
  }
  else if (ev == ButtonEvent::BTN1_SHORT)
  {
    switch (s_cursor)
    {
    case 0:
      return MenuState::BLE_SCAN;
    case 1:
      return MenuState::BLE_REMOVE;
    case 2:
      return MenuState::SENSOR;
    case 3:
      return MenuState::SYS_INFO;
    case 4:
      return MenuState::DONE_CONTINUOUS;
    case 5:
      esp_restart();
    }
  }
  return MenuState::MAIN;
}

static MenuState tickBleScan(ButtonEvent)
{
  oledShowMessage("BLE Scanning...", "(10 sec)");

  // キューをクリア
  SensorVariant dummy;
  while (xQueueReceive(bleScanner.queue, &dummy, 0) == pdTRUE)
  {
  }

  // 全 SwitchBot 機器を対象にスキャン
  bleScanner.registrationMode = true;
  bleScanner.start(SCAN_TIME);
  bleScanner.registrationMode = false;
  bleScanner.clearResults();

  // アドレスで重複排除しながら結果を収集
  s_scanResultCount = 0;
  SensorVariant v;
  while (xQueueReceive(bleScanner.queue, &v, 0) == pdTRUE)
  {
    char addr[18] = {};
    int8_t rssi = 0;
    std::visit([&](auto &d)
               {
      strncpy(addr, d.address, 17);
      rssi = d.rssi; }, v);

    bool dup = false;
    for (int i = 0; i < s_scanResultCount; i++)
    {
      if (strcasecmp(s_scanResults[i].addr, addr) == 0)
      {
        dup = true;
        break;
      }
    }
    if (!dup && s_scanResultCount < MAX_SCAN_RESULTS)
    {
      strncpy(s_scanResults[s_scanResultCount].addr, addr, 17);
      s_scanResults[s_scanResultCount].addr[17] = '\0';
      s_scanResults[s_scanResultCount].rssi = rssi;
      s_scanResultCount++;
    }
  }

  return MenuState::BLE_SCAN_RESULT;
}

static MenuState tickBleScanResult(ButtonEvent ev)
{
  if (s_scanResultCount == 0)
  {
    oledShowMessage("No devices found", "BTN1 long: back");
    if (ev == ButtonEvent::BTN1_LONG)
      return MenuState::MAIN;
    return MenuState::BLE_SCAN_RESULT;
  }

  char items[MAX_SCAN_RESULTS][20];
  const char *ptrs[MAX_SCAN_RESULTS];
  for (int i = 0; i < s_scanResultCount; i++)
  {
    strncpy(items[i], s_scanResults[i].addr, 19);
    items[i][19] = '\0';
    ptrs[i] = items[i];
  }
  oledShowMenu("BLE Register", ptrs, s_scanResultCount, s_cursor);

  if (ev == ButtonEvent::BTN0_SHORT)
  {
    s_cursor = (s_cursor + 1) % s_scanResultCount;
  }
  else if (ev == ButtonEvent::BTN1_SHORT)
  {
    bleTargets.add(s_scanResults[s_cursor].addr);
    bleTargets.save();
    oledShowMessage("Registered!", s_scanResults[s_cursor].addr);
    delay(1500);
    return MenuState::MAIN;
  }
  else if (ev == ButtonEvent::BTN1_LONG)
  {
    return MenuState::MAIN;
  }
  return MenuState::BLE_SCAN_RESULT;
}

static MenuState tickBleRemove(ButtonEvent ev)
{
  if (bleTargets.count == 0)
  {
    oledShowMessage("No registered", "BTN1 long: back");
    if (ev == ButtonEvent::BTN1_LONG)
      return MenuState::MAIN;
    return MenuState::BLE_REMOVE;
  }

  if (s_cursor >= bleTargets.count)
    s_cursor = bleTargets.count - 1;

  char title[20];
  snprintf(title, sizeof(title), "Remove (%d)", bleTargets.count);
  const char *ptrs[MAX_TARGETS];
  for (int i = 0; i < bleTargets.count; i++)
    ptrs[i] = bleTargets.data[i];
  oledShowMenu(title, ptrs, bleTargets.count, s_cursor);

  if (ev == ButtonEvent::BTN0_SHORT)
  {
    s_cursor = (s_cursor + 1) % bleTargets.count;
  }
  else if (ev == ButtonEvent::BTN1_SHORT)
  {
    s_removeTarget = s_cursor;
    s_savedCursor = s_cursor;
    s_confirmCursor = 1; // デフォルト No
    return MenuState::BLE_REMOVE_CONFIRM;
  }
  else if (ev == ButtonEvent::BTN1_LONG)
  {
    return MenuState::MAIN;
  }
  return MenuState::BLE_REMOVE;
}

static MenuState tickBleRemoveConfirm(ButtonEvent ev)
{
  oledShowConfirm("Delete?", bleTargets.data[s_removeTarget], s_confirmCursor);

  if (ev == ButtonEvent::BTN0_SHORT)
  {
    s_confirmCursor = 1 - s_confirmCursor;
  }
  else if (ev == ButtonEvent::BTN1_SHORT)
  {
    if (s_confirmCursor == 0)
    {
      bleTargets.remove(bleTargets.data[s_removeTarget]);
      bleTargets.save();
      oledShowMessage("Deleted!", nullptr);
      delay(1000);
    }
    return MenuState::BLE_REMOVE;
  }
  else if (ev == ButtonEvent::BTN1_LONG)
  {
    return MenuState::BLE_REMOVE;
  }
  return MenuState::BLE_REMOVE_CONFIRM;
}

static MenuState tickSensor(ButtonEvent ev)
{
  static unsigned long lastUpdate = 0;
  SensorReading r{
      {adsReadDiff01()},
      {adsReadDiff23()},
      {ina228.readCurrent(), ina228.readPower(), ina228.readTemp()},
      0};
  oledShowSensorData(r);
  if (ev == ButtonEvent::BTN1_LONG)
    return MenuState::MAIN;
  if (millis() - lastUpdate > 500)
  {
    ina228.printStatus();
    lastUpdate = millis();
  }
  return MenuState::SENSOR;
}

static MenuState tickSysInfo(ButtonEvent ev)
{
  oledShowMessage(FIRMWARE_VERSION, getDeviceId());
  if (ev == ButtonEvent::BTN1_LONG)
    return MenuState::MAIN;
  return MenuState::SYS_INFO;
}

// ---- エントリポイント ----

OperationMode enterMenuMode()
{
  bleTargets.load();

  MenuState state = MenuState::MAIN;
  s_cursor = 0;

  while (true)
  {
    ButtonEvent ev = button.read();
    MenuState next = state;

    switch (state)
    {
    case MenuState::MAIN:
      next = tickMain(ev);
      break;
    case MenuState::BLE_SCAN:
      next = tickBleScan(ev);
      break;
    case MenuState::BLE_SCAN_RESULT:
      next = tickBleScanResult(ev);
      break;
    case MenuState::BLE_REMOVE:
      next = tickBleRemove(ev);
      break;
    case MenuState::BLE_REMOVE_CONFIRM:
      next = tickBleRemoveConfirm(ev);
      break;
    case MenuState::SENSOR:
      next = tickSensor(ev);
      break;
    case MenuState::SYS_INFO:
      next = tickSysInfo(ev);
      break;
    case MenuState::DONE_CONTINUOUS:
      return OperationMode::CONTINUOUS;
    }

    if (next == MenuState::DONE_CONTINUOUS)
    {
      return OperationMode::CONTINUOUS;
    }

    if (next != state)
    {
      // CONFIRM → BLE_REMOVE の遷移ではカーソル位置を復元
      if (state == MenuState::BLE_REMOVE_CONFIRM && next == MenuState::BLE_REMOVE)
      {
        s_cursor = s_savedCursor;
        if (bleTargets.count > 0 && s_cursor >= bleTargets.count)
          s_cursor = bleTargets.count - 1;
      }
      else
      {
        s_cursor = 0;
      }
      state = next;
    }

    delay(50);
  }
}
