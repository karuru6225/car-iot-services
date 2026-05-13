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
  MENU_NAV,         // 汎用ナビ（MENU_NAV は「サブメニューに入る」の targetState としても使用）
  BLE_SCAN,
  BLE_SCAN_RESULT,
  BLE_REMOVE,
  BLE_REMOVE_CONFIRM,
  SENSOR,
  SYS_INFO,
  RELAY_MODE,
  NVS_CLEAR_CONFIRM,
  AH_OFFSET,
  AH_RESET_CONFIRM,
  RESTART,
  DONE_CONTINUOUS,
};

// ---- メニュー定義 ----

struct MenuItem
{
  const char *label;
  const char *path;
  MenuState targetState; // MENU_NAV = サブメニューに入る、それ以外 = 画面遷移
};

static const MenuItem ITEMS[] = {
    // path="/"
    {"BLE Settings", "/",             MenuState::MENU_NAV        },
    {"Battery",      "/",             MenuState::MENU_NAV        },
    {"Sensor View",  "/",             MenuState::SENSOR          },
    {"System",       "/",             MenuState::MENU_NAV        },
    {"Continuous",   "/",             MenuState::DONE_CONTINUOUS },
    {"Restart",      "/",             MenuState::RESTART         },
    // path="/BLE Settings"
    {"Register",     "/BLE Settings", MenuState::BLE_SCAN        },
    {"Remove",       "/BLE Settings", MenuState::BLE_REMOVE      },
    // path="/Battery"
    {"Ah Offset",    "/Battery",      MenuState::AH_OFFSET       },
    {"Ah Reset",     "/Battery",      MenuState::AH_RESET_CONFIRM},
    // path="/System"
    {"Info",         "/System",       MenuState::SYS_INFO        },
    {"Relay Mode",   "/System",       MenuState::RELAY_MODE      },
    {"NVS Clear",    "/System",       MenuState::NVS_CLEAR_CONFIRM},
};
static const int ITEM_COUNT = sizeof(ITEMS) / sizeof(ITEMS[0]);

// ---- ナビゲーション状態 ----

static char s_currentPath[64] = "/";
static int  s_index = 0; // MENU_NAV のカーソル

// ---- アクション画面共有状態 ----

struct ScanResult
{
  char addr[18];
  int8_t rssi;
};
static const int MAX_SCAN_RESULTS = 20;
static ScanResult s_scanResults[MAX_SCAN_RESULTS];
static int s_scanResultCount = 0;

static int s_cursor = 0;        // BLE_REMOVE / BLE_SCAN_RESULT のカーソル
static int s_confirmCursor = 1; // 0=Yes, 1=No
static int s_removeTarget = 0;
static int s_savedCursor = 0;   // CONFIRM から BLE_REMOVE に戻るときのカーソル復元用
static RelayMode s_relayModeEdit = RelayMode::SLEEP_INDICATOR; // RELAY_MODE 編集中の値
static uint32_t  s_ahOffsetEdit  = 0;                          // AH_OFFSET 編集中の値

// ---- パスユーティリティ ----

static void pathPush(const char *label)
{
  if (strcmp(s_currentPath, "/") == 0)
  {
    snprintf(s_currentPath, sizeof(s_currentPath), "/%s", label);
  }
  else
  {
    size_t len = strlen(s_currentPath);
    snprintf(s_currentPath + len, sizeof(s_currentPath) - len, "/%s", label);
  }
}

static void pathPop()
{
  char *last = strrchr(s_currentPath, '/');
  if (last == nullptr || last == s_currentPath)
  {
    strcpy(s_currentPath, "/");
  }
  else
  {
    *last = '\0';
    if (s_currentPath[0] == '\0') strcpy(s_currentPath, "/");
  }
}

static const char *pathTitle()
{
  if (strcmp(s_currentPath, "/") == 0) return "Menu";
  const char *last = strrchr(s_currentPath, '/');
  return last ? last + 1 : s_currentPath;
}

// ---- tick 関数 ----

static MenuState tickMenuNav(ButtonEvent ev)
{
  char labelBufs[16][20];
  const char *labels[16];
  const MenuItem *matched[16];
  int count = 0;
  for (int i = 0; i < ITEM_COUNT; i++)
  {
    if (strcmp(ITEMS[i].path, s_currentPath) == 0)
    {
      if (ITEMS[i].targetState == MenuState::MENU_NAV)
        snprintf(labelBufs[count], sizeof(labelBufs[count]), "%s/", ITEMS[i].label);
      else
        snprintf(labelBufs[count], sizeof(labelBufs[count]), "%s", ITEMS[i].label);
      labels[count] = labelBufs[count];
      matched[count] = &ITEMS[i];
      count++;
    }
  }
  if (count == 0)
  {
    pathPop();
    return MenuState::MENU_NAV;
  }
  if (s_index >= count) s_index = 0;

  oledShowMenu(pathTitle(), labels, count, s_index);

  if (ev == ButtonEvent::BTN0_SHORT)
  {
    s_index = (s_index + 1) % count;
  }
  else if (ev == ButtonEvent::BTN1_SHORT)
  {
    const MenuItem *item = matched[s_index];
    if (item->targetState == MenuState::MENU_NAV)
    {
      pathPush(item->label);
      s_index = 0;
    }
    else
    {
      return item->targetState;
    }
  }
  else if (ev == ButtonEvent::BTN1_LONG)
  {
    if (strcmp(s_currentPath, "/") != 0)
    {
      pathPop();
      s_index = 0;
    }
  }
  return MenuState::MENU_NAV;
}

static MenuState tickBleScan(ButtonEvent)
{
  oledShowMessage("BLE Scanning...", "(10 sec)");

  SensorVariant dummy;
  while (xQueueReceive(bleScanner.queue, &dummy, 0) == pdTRUE)
  {
  }

  bleScanner.registrationMode = true;
  bleScanner.start(SCAN_TIME);
  bleScanner.registrationMode = false;
  bleScanner.clearResults();

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
    if (ev == ButtonEvent::BTN1_LONG) return MenuState::MENU_NAV;
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
    return MenuState::MENU_NAV;
  }
  else if (ev == ButtonEvent::BTN1_LONG)
  {
    return MenuState::MENU_NAV;
  }
  return MenuState::BLE_SCAN_RESULT;
}

static MenuState tickBleRemove(ButtonEvent ev)
{
  if (bleTargets.count == 0)
  {
    oledShowMessage("No registered", "BTN1 long: back");
    if (ev == ButtonEvent::BTN1_LONG) return MenuState::MENU_NAV;
    return MenuState::BLE_REMOVE;
  }

  if (s_cursor >= bleTargets.count) s_cursor = bleTargets.count - 1;

  char title[20];
  snprintf(title, sizeof(title), "Remove (%d)", bleTargets.count);
  const char *ptrs[MAX_TARGETS];
  for (int i = 0; i < bleTargets.count; i++) ptrs[i] = bleTargets.data[i];
  oledShowMenu(title, ptrs, bleTargets.count, s_cursor);

  if (ev == ButtonEvent::BTN0_SHORT)
  {
    s_cursor = (s_cursor + 1) % bleTargets.count;
  }
  else if (ev == ButtonEvent::BTN1_SHORT)
  {
    s_removeTarget = s_cursor;
    s_savedCursor = s_cursor;
    s_confirmCursor = 1;
    return MenuState::BLE_REMOVE_CONFIRM;
  }
  else if (ev == ButtonEvent::BTN1_LONG)
  {
    return MenuState::MENU_NAV;
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
  if (ev == ButtonEvent::BTN1_LONG) return MenuState::MENU_NAV;
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
  if (ev == ButtonEvent::BTN1_LONG) return MenuState::MENU_NAV;
  return MenuState::SYS_INFO;
}

static MenuState tickRelayMode(ButtonEvent ev)
{
  const char *modeStr = (s_relayModeEdit == RelayMode::SLEEP_INDICATOR) ? "Sleep Indicator" : "Off";
  oledShowMessage("Relay Mode", modeStr);

  if (ev == ButtonEvent::BTN0_SHORT)
  {
    s_relayModeEdit = (s_relayModeEdit == RelayMode::SLEEP_INDICATOR)
                        ? RelayMode::RELAY_OFF
                        : RelayMode::SLEEP_INDICATOR;
  }
  else if (ev == ButtonEvent::BTN1_SHORT)
  {
    setRelayMode(s_relayModeEdit);
    return MenuState::MENU_NAV;
  }
  else if (ev == ButtonEvent::BTN1_LONG)
  {
    return MenuState::MENU_NAV;
  }
  return MenuState::RELAY_MODE;
}

static MenuState tickNvsClearConfirm(ButtonEvent ev)
{
  // "device" ネームスペース（MQTT host）は残す
  oledShowConfirm("NVS Clear?", "Keep MQTT host", s_confirmCursor);

  if (ev == ButtonEvent::BTN0_SHORT)
  {
    s_confirmCursor = 1 - s_confirmCursor;
  }
  else if (ev == ButtonEvent::BTN1_SHORT)
  {
    if (s_confirmCursor == 0)
    {
      clearMenuData();
      oledShowMessage("NVS Cleared", "Restarting...");
      delay(1500);
      esp_restart();
    }
    return MenuState::MENU_NAV;
  }
  else if (ev == ButtonEvent::BTN1_LONG)
  {
    return MenuState::MENU_NAV;
  }
  return MenuState::NVS_CLEAR_CONFIRM;
}

static MenuState tickAhOffset(ButtonEvent ev)
{
  char valStr[20];
  snprintf(valStr, sizeof(valStr), "%u Ah  (BTN1:save)", s_ahOffsetEdit);
  oledShowMessage("Ah Offset", valStr);

  if (ev == ButtonEvent::BTN0_SHORT)
  {
    s_ahOffsetEdit = (s_ahOffsetEdit + 5 <= 300) ? s_ahOffsetEdit + 5 : 0;
  }
  else if (ev == ButtonEvent::BTN1_SHORT)
  {
    setAhOffset(s_ahOffsetEdit);
    oledShowMessage("Ah Offset", "Saved");
    delay(1000);
    return MenuState::MENU_NAV;
  }
  else if (ev == ButtonEvent::BTN1_LONG)
  {
    return MenuState::MENU_NAV;
  }
  return MenuState::AH_OFFSET;
}

static MenuState tickAhResetConfirm(ButtonEvent ev)
{
  oledShowConfirm("Ah Reset?", "Reset charge counter", s_confirmCursor);

  if (ev == ButtonEvent::BTN0_SHORT)
  {
    s_confirmCursor = 1 - s_confirmCursor;
  }
  else if (ev == ButtonEvent::BTN1_SHORT)
  {
    if (s_confirmCursor == 0)
    {
      ina228.resetCharge();
      oledShowMessage("Ah Reset", "Done");
      delay(1000);
    }
    return MenuState::MENU_NAV;
  }
  else if (ev == ButtonEvent::BTN1_LONG)
  {
    return MenuState::MENU_NAV;
  }
  return MenuState::AH_RESET_CONFIRM;
}

// ---- エントリポイント ----

OperationMode enterMenuMode()
{
  bleTargets.load();

  MenuState state = MenuState::MENU_NAV;
  strcpy(s_currentPath, "/");
  s_index = 0;

  while (true)
  {
    ButtonEvent ev = button.read();
    MenuState next = state;

    switch (state)
    {
    case MenuState::MENU_NAV:           next = tickMenuNav(ev);          break;
    case MenuState::BLE_SCAN:           next = tickBleScan(ev);          break;
    case MenuState::BLE_SCAN_RESULT:    next = tickBleScanResult(ev);    break;
    case MenuState::BLE_REMOVE:         next = tickBleRemove(ev);        break;
    case MenuState::BLE_REMOVE_CONFIRM: next = tickBleRemoveConfirm(ev); break;
    case MenuState::SENSOR:             next = tickSensor(ev);           break;
    case MenuState::SYS_INFO:           next = tickSysInfo(ev);          break;
    case MenuState::RELAY_MODE:         next = tickRelayMode(ev);        break;
    case MenuState::AH_OFFSET:          next = tickAhOffset(ev);          break;
    case MenuState::AH_RESET_CONFIRM:   next = tickAhResetConfirm(ev);   break;
    case MenuState::NVS_CLEAR_CONFIRM:  next = tickNvsClearConfirm(ev);  break;
    case MenuState::RESTART:            oledClear(); esp_restart();      break;
    case MenuState::DONE_CONTINUOUS:    break; // tickMenuNav() から直接返されるため到達しない
    }

    if (next == MenuState::DONE_CONTINUOUS)
    {
      oledClear();
      return OperationMode::CONTINUOUS;
    }

    if (next != state)
    {
      if (state == MenuState::BLE_REMOVE_CONFIRM && next == MenuState::BLE_REMOVE)
      {
        s_cursor = s_savedCursor;
        if (bleTargets.count > 0 && s_cursor >= bleTargets.count)
          s_cursor = bleTargets.count - 1;
      }
      else if (next == MenuState::MENU_NAV)
      {
        s_index = 0;
      }
      else
      {
        s_cursor = 0;
        if (next == MenuState::RELAY_MODE)
          s_relayModeEdit = getRelayMode();
        else if (next == MenuState::AH_OFFSET)
          s_ahOffsetEdit = getAhOffset();
        else if (next == MenuState::NVS_CLEAR_CONFIRM || next == MenuState::AH_RESET_CONFIRM)
          s_confirmCursor = 1;
      }
      state = next;
    }

    delay(50);
  }
}
