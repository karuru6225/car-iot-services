#include "menu.h"
#include "menu_util.h"
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
  MENU_NAV,
  BLE_SCAN,
  BLE_SCAN_RESULT,
  BLE_REMOVE,
  BLE_REMOVE_CONFIRM,
  SENSOR,
  SYS_INFO,
  RELAY_MODE,
  CONFIRM,     // 汎用確認ダイアログ
  AH_OFFSET,
  CHG_TIMEOUT,
  CHARGING,
  RESTART,
  DONE_CONTINUOUS,
};

// ---- 確認ダイアログ定義 ----

struct ConfirmDef
{
  const char *title;
  const char *message;
  void (*onConfirm)();
};

static void doNvsClear()
{
  clearMenuData();
  oledShowMessage("NVS Cleared", "Restarting...");
  delay(1500);
  esp_restart();
}

static void doAhReset()
{
  ina228.resetCharge();
  oledShowMessage("Ah Reset", "Done");
  delay(1000);
}

// ---- メニュー定義 ----

struct MenuItem
{
  const char *label;
  const char *path;
  MenuState targetState;
  ConfirmDef confirmDef; // CONFIRM 状態のときのみ有効
};

static const MenuItem ITEMS[] = {
    // path="/"
    {"BLE Settings", "/",             MenuState::MENU_NAV,        {}},
    {"Battery",      "/",             MenuState::MENU_NAV,        {}},
    {"Sensor View",  "/",             MenuState::SENSOR,          {}},
    {"System",       "/",             MenuState::MENU_NAV,        {}},
    {"Continuous",   "/",             MenuState::DONE_CONTINUOUS, {}},
    {"Restart",      "/",             MenuState::RESTART,         {}},
    // path="/BLE Settings"
    {"Register",     "/BLE Settings", MenuState::BLE_SCAN,        {}},
    {"Remove",       "/BLE Settings", MenuState::BLE_REMOVE,      {}},
    // path="/Battery"
    {"Ah Offset",    "/Battery",      MenuState::AH_OFFSET,       {}},
    {"Ah Reset",     "/Battery",      MenuState::CONFIRM,         {"Ah Reset?", "Reset charge counter", doAhReset}},
    {"Chg Timeout",  "/Battery",      MenuState::CHG_TIMEOUT,     {}},
    {"Start Charge", "/Battery",      MenuState::CHARGING,        {}},
    // path="/System"
    {"Info",         "/System",       MenuState::SYS_INFO,        {}},
    {"Relay Mode",   "/System",       MenuState::RELAY_MODE,      {}},
    {"NVS Clear",    "/System",       MenuState::CONFIRM,         {"NVS Clear?", "Keep MQTT host", doNvsClear}},
};
static const int ITEM_COUNT = sizeof(ITEMS) / sizeof(ITEMS[0]);

// ---- ナビゲーション状態 ----

static int s_index = 0;

// ---- BLE スキャン結果（tickBleScan / tickBleScanResult で共有） ----

struct ScanResult { char addr[18]; int8_t rssi; };
static const int MAX_SCAN_RESULTS = 20;
static ScanResult s_scanResults[MAX_SCAN_RESULTS];
static int s_scanResultCount = 0;

// ---- BLE Remove で Remove ↔ RemoveConfirm をまたぐ状態 ----

static int s_bleRemoveTarget = 0;

// ---- 汎用確認ダイアログの現在の定義 ----

static ConfirmDef s_confirm;

// ---- tick 関数 ----

static MenuState tickMenuNav(ButtonEvent ev)
{
  char labelBufs[16][20];
  const char *labels[16];
  const MenuItem *matched[16];
  int count = 0;
  for (int i = 0; i < ITEM_COUNT; i++)
  {
    if (strcmp(ITEMS[i].path, pathGet()) == 0)
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
      if (item->targetState == MenuState::CONFIRM)
        s_confirm = item->confirmDef;
      return item->targetState;
    }
  }
  else if (ev == ButtonEvent::BTN1_LONG)
  {
    if (!pathIsRoot())
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
  while (xQueueReceive(bleScanner.queue, &dummy, 0) == pdTRUE) {}

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
    std::visit([&](auto &d) { strncpy(addr, d.address, 17); rssi = d.rssi; }, v);

    bool dup = false;
    for (int i = 0; i < s_scanResultCount; i++)
    {
      if (strcasecmp(s_scanResults[i].addr, addr) == 0) { dup = true; break; }
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
  static int cursor = 0;

  if (s_scanResultCount == 0)
  {
    oledShowMessage("No devices found", "BTN1 long: back");
    if (ev == ButtonEvent::BTN1_LONG) { cursor = 0; return MenuState::MENU_NAV; }
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
  oledShowMenu("BLE Register", ptrs, s_scanResultCount, cursor);

  if (ev == ButtonEvent::BTN0_SHORT)
  {
    cursor = (cursor + 1) % s_scanResultCount;
  }
  else if (ev == ButtonEvent::BTN1_SHORT)
  {
    bleTargets.add(s_scanResults[cursor].addr);
    bleTargets.save();
    oledShowMessage("Registered!", s_scanResults[cursor].addr);
    delay(1500);
    cursor = 0;
    return MenuState::MENU_NAV;
  }
  else if (ev == ButtonEvent::BTN1_LONG)
  {
    cursor = 0;
    return MenuState::MENU_NAV;
  }
  return MenuState::BLE_SCAN_RESULT;
}

static MenuState tickBleRemove(ButtonEvent ev)
{
  static int cursor = 0;

  if (bleTargets.count == 0)
  {
    oledShowMessage("No registered", "BTN1 long: back");
    if (ev == ButtonEvent::BTN1_LONG) { cursor = 0; return MenuState::MENU_NAV; }
    return MenuState::BLE_REMOVE;
  }

  if (cursor >= bleTargets.count) cursor = bleTargets.count - 1;

  char title[20];
  snprintf(title, sizeof(title), "Remove (%d)", bleTargets.count);
  const char *ptrs[MAX_TARGETS];
  for (int i = 0; i < bleTargets.count; i++) ptrs[i] = bleTargets.data[i];
  oledShowMenu(title, ptrs, bleTargets.count, cursor);

  if (ev == ButtonEvent::BTN0_SHORT)
  {
    cursor = (cursor + 1) % bleTargets.count;
  }
  else if (ev == ButtonEvent::BTN1_SHORT)
  {
    s_bleRemoveTarget = cursor;
    return MenuState::BLE_REMOVE_CONFIRM;
  }
  else if (ev == ButtonEvent::BTN1_LONG)
  {
    cursor = 0;
    return MenuState::MENU_NAV;
  }
  return MenuState::BLE_REMOVE;
}

static MenuState tickBleRemoveConfirm(ButtonEvent ev)
{
  static int cursor = 1; // 0=Yes, 1=No
  static bool needsInit = true;
  if (needsInit) { cursor = 1; needsInit = false; }

  oledShowConfirm("Delete?", bleTargets.data[s_bleRemoveTarget], cursor);

  if (ev == ButtonEvent::BTN0_SHORT)
  {
    cursor = 1 - cursor;
  }
  else if (ev == ButtonEvent::BTN1_SHORT)
  {
    if (cursor == 0)
    {
      bleTargets.remove(bleTargets.data[s_bleRemoveTarget]);
      bleTargets.save();
      oledShowMessage("Deleted!", nullptr);
      delay(1000);
    }
    needsInit = true;
    return MenuState::BLE_REMOVE;
  }
  else if (ev == ButtonEvent::BTN1_LONG)
  {
    needsInit = true;
    return MenuState::BLE_REMOVE;
  }
  return MenuState::BLE_REMOVE_CONFIRM;
}

static MenuState tickSensor(ButtonEvent ev)
{
  static unsigned long lastUpdate = 0;
  SensorReading r{
      {adsReadDiffMain()},
      {adsReadDiffSub()},
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
  static RelayMode edit = RelayMode::SLEEP_INDICATOR;
  static bool needsInit = true;
  if (needsInit) { edit = getRelayMode(); needsInit = false; }

  const char *modeStr = (edit == RelayMode::SLEEP_INDICATOR) ? "Sleep Indicator" : "Off";
  oledShowMessage("Relay Mode", modeStr);

  if (ev == ButtonEvent::BTN0_SHORT)
  {
    edit = (edit == RelayMode::SLEEP_INDICATOR) ? RelayMode::RELAY_OFF : RelayMode::SLEEP_INDICATOR;
  }
  else if (ev == ButtonEvent::BTN1_SHORT)
  {
    setRelayMode(edit);
    needsInit = true;
    return MenuState::MENU_NAV;
  }
  else if (ev == ButtonEvent::BTN1_LONG)
  {
    needsInit = true;
    return MenuState::MENU_NAV;
  }
  return MenuState::RELAY_MODE;
}

static MenuState tickConfirm(ButtonEvent ev)
{
  static int cursor = 1; // 0=Yes, 1=No
  static bool needsInit = true;
  if (needsInit) { cursor = 1; needsInit = false; }

  oledShowConfirm(s_confirm.title, s_confirm.message, cursor);

  if (ev == ButtonEvent::BTN0_SHORT)
  {
    cursor = 1 - cursor;
  }
  else if (ev == ButtonEvent::BTN1_SHORT)
  {
    if (cursor == 0) s_confirm.onConfirm();
    needsInit = true;
    return MenuState::MENU_NAV;
  }
  else if (ev == ButtonEvent::BTN1_LONG)
  {
    needsInit = true;
    return MenuState::MENU_NAV;
  }
  return MenuState::CONFIRM;
}

static MenuState tickAhOffset(ButtonEvent ev)
{
  static int32_t edit = 0;
  static bool needsInit = true;
  if (needsInit) { edit = getAhOffset(); needsInit = false; }

  char valStr[20];
  snprintf(valStr, sizeof(valStr), "%d Ah", edit);
  oledShowMessage("Ah Offset", valStr);

  if (ev == ButtonEvent::BTN0_SHORT)
  {
    edit = std::min(edit + 50, 300);
  }
  else if (ev == ButtonEvent::BTN0_LONG)
  {
    edit = std::max(edit - 50, 0);
  }
  else if (ev == ButtonEvent::BTN1_SHORT)
  {
    setAhOffset(edit);
    oledShowMessage("Ah Offset", "Saved");
    delay(1000);
    needsInit = true;
    return MenuState::MENU_NAV;
  }
  else if (ev == ButtonEvent::BTN1_LONG)
  {
    needsInit = true;
    return MenuState::MENU_NAV;
  }
  return MenuState::AH_OFFSET;
}

static const uint32_t CHG_TIMEOUT_OPTS[] = {10, 20, 30, 60};
static const int CHG_TIMEOUT_COUNT = 4;

static MenuState tickChgTimeout(ButtonEvent ev)
{
  static int editIdx = 0;
  static bool needsInit = true;
  if (needsInit) {
    uint32_t cur = getChgTimeoutMin();
    editIdx = 0;
    for (int i = 0; i < CHG_TIMEOUT_COUNT; i++) {
      if (CHG_TIMEOUT_OPTS[i] == cur) { editIdx = i; break; }
    }
    needsInit = false;
  }

  char valStr[20];
  snprintf(valStr, sizeof(valStr), "%u min", CHG_TIMEOUT_OPTS[editIdx]);
  oledShowMessage("Chg Timeout", valStr);

  if (ev == ButtonEvent::BTN0_SHORT)
  {
    editIdx = (editIdx + 1) % CHG_TIMEOUT_COUNT;
  }
  else if (ev == ButtonEvent::BTN1_SHORT)
  {
    setChgTimeoutMin(CHG_TIMEOUT_OPTS[editIdx]);
    oledShowMessage("Chg Timeout", "Saved");
    delay(1000);
    needsInit = true;
    return MenuState::MENU_NAV;
  }
  else if (ev == ButtonEvent::BTN1_LONG)
  {
    needsInit = true;
    return MenuState::MENU_NAV;
  }
  return MenuState::CHG_TIMEOUT;
}

static MenuState tickCharging(ButtonEvent ev)
{
  static bool needsInit = true;
  static unsigned long startMs = 0;
  static unsigned long lastReadMs = 0;
  static uint32_t timeoutMs = 0;
  static float vMain = 0.0f, vSub = 0.0f;

  if (needsInit) {
    vMain = adsReadDiffMain();
    vSub  = adsReadDiffSub();
    if (vSub <= vMain) {
      char msg[24];
      snprintf(msg, sizeof(msg), "M:%.2fV S:%.2fV", vMain, vSub);
      oledShowMessage("Cannot charge:", msg);
      delay(2000);
      needsInit = true;
      return MenuState::MENU_NAV;
    }
    digitalWrite(CHG_ON_PIN, HIGH);
    timeoutMs  = getChgTimeoutMin() * 60UL * 1000UL;
    startMs    = millis();
    lastReadMs = 0;
    needsInit  = false;
  }

  unsigned long elapsed = millis() - startMs;

  // タイムアウト
  if (elapsed >= timeoutMs) {
    digitalWrite(CHG_ON_PIN, LOW);
    oledShowMessage("Charge done", "");
    delay(1500);
    needsInit = true;
    return MenuState::MENU_NAV;
  }

  // 2秒ごとに電圧を更新
  if (millis() - lastReadMs >= 2000) {
    vMain = adsReadDiffMain();
    vSub  = adsReadDiffSub();
    lastReadMs = millis();
  }

  oledShowCharging(vMain, vSub, (int)((timeoutMs - elapsed) / 1000));

  // どのボタンでも終了
  if (ev != ButtonEvent::NONE) {
    digitalWrite(CHG_ON_PIN, LOW);
    oledShowMessage("Charge stopped", "");
    delay(1000);
    needsInit = true;
    return MenuState::MENU_NAV;
  }
  return MenuState::CHARGING;
}

// ---- エントリポイント ----

OperationMode enterMenuMode()
{
  bleTargets.load();

  MenuState state = MenuState::MENU_NAV;
  pathReset();
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
    case MenuState::CONFIRM:            next = tickConfirm(ev);          break;
    case MenuState::AH_OFFSET:          next = tickAhOffset(ev);         break;
    case MenuState::CHG_TIMEOUT:        next = tickChgTimeout(ev);       break;
    case MenuState::CHARGING:           next = tickCharging(ev);         break;
    case MenuState::RESTART:            oledClear(); esp_restart();      break;
    case MenuState::DONE_CONTINUOUS:    break;
    }

    if (next == MenuState::DONE_CONTINUOUS)
    {
      oledClear();
      return OperationMode::CONTINUOUS;
    }

    if (next != state)
    {
      if (next == MenuState::MENU_NAV) s_index = 0;
      state = next;
    }

    delay(50);
  }
}
