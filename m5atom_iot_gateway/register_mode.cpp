#include "view.h" // M5Unified を BLE より先にインクルード
#include "register_mode.h"
#include "domain__targets.h"
#include "infra__ble_scan.h"
#include "infra__button.h"
#include "config.h"

RegisterMode regMode;

bool RegisterMode::isScanning() const
{
  return _scanning;
}

void RegisterMode::foundDevice(const char *addr)
{
  for (int i = 0; i < _foundCount; i++)
  {
    if (strcasecmp(_found[i], addr) == 0) return;
  }
  if (_foundCount < MAX_FOUND)
  {
    strncpy(_found[_foundCount], addr, 17);
    _found[_foundCount][17] = '\0';
    _foundCount++;
  }
}

void RegisterMode::buildList()
{
  _listCount = 0;
  strncpy(_list[0].address, "< Back >", 17);
  _list[0].address[17] = '\0';
  _list[0].registered  = false;
  _list[0].cancel      = true;
  _listCount = 1;

  for (int i = 0; i < targets.count; i++)
  {
    strncpy(_list[_listCount].address, targets.data[i], 17);
    _list[_listCount].address[17] = '\0';
    _list[_listCount].registered  = true;
    _list[_listCount].cancel      = false;
    _listCount++;
  }

  for (int i = 0; i < _foundCount; i++)
  {
    bool alreadyIn = false;
    for (int j = 0; j < targets.count; j++)
    {
      if (strcasecmp(_found[i], targets.data[j]) == 0) { alreadyIn = true; break; }
    }
    if (!alreadyIn && _listCount < MAX_TARGETS + MAX_FOUND + 1)
    {
      strncpy(_list[_listCount].address, _found[i], 17);
      _list[_listCount].address[17] = '\0';
      _list[_listCount].registered  = false;
      _list[_listCount].cancel      = false;
      _listCount++;
    }
  }
}

void RegisterMode::run()
{
  view.registerModeStart();

  _foundCount = 0;
  _selectIdx  = 0;
  _scanning   = true;
  scanner.start(SCAN_TIME);
  scanner.clearResults();
  _scanning = false;

  buildList();

  if (_listCount == 0)
  {
    view.registerNoDevices();
    delay(2000);
    return;
  }

  view.registerList(_list, _listCount, _selectIdx);

  while (true)
  {
    BtnEvent ev = button.check();
    if (ev == BTN_SHORT)
    {
      _selectIdx = (_selectIdx + 1) % _listCount;
      view.registerList(_list, _listCount, _selectIdx);
    }
    else if (ev == BTN_LONG)
    {
      if (_list[_selectIdx].cancel)
      {
        view.registerCancel();
      }
      else
      {
        const char *addr          = _list[_selectIdx].address;
        bool        wasRegistered = _list[_selectIdx].registered;
        if (wasRegistered)
          targets.remove(addr);
        else
          targets.add(addr);
        targets.save();
        view.registerResult(wasRegistered, addr);
      }
      return;
    }
    delay(10);
  }
}
