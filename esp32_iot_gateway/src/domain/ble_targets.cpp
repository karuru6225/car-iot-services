#include "ble_targets.h"
#include <Preferences.h>
#include <string.h>

static const char *NVS_NS  = "switchbot";
static const char *NVS_CNT = "count";

BleTargets bleTargets;

void BleTargets::clear()
{
  count = 0;
}

void BleTargets::save()
{
  Preferences p;
  p.begin(NVS_NS, false);
  p.putInt(NVS_CNT, count);
  for (int i = 0; i < count; i++)
  {
    char key[8];
    snprintf(key, sizeof(key), "a%d", i);
    p.putString(key, data[i]);
  }
  p.end();
}

void BleTargets::load()
{
  Preferences p;
  p.begin(NVS_NS, true);
  count = p.getInt(NVS_CNT, 0);
  if (count > MAX_TARGETS) count = MAX_TARGETS;
  for (int i = 0; i < count; i++)
  {
    char key[8];
    snprintf(key, sizeof(key), "a%d", i);
    String addr = p.getString(key, "");
    strncpy(data[i], addr.c_str(), 17);
    data[i][17] = '\0';
  }
  p.end();
}

bool BleTargets::isTarget(const char *addr) const
{
  if (count == 0) return true;
  for (int i = 0; i < count; i++)
  {
    if (strcasecmp(addr, data[i]) == 0) return true;
  }
  return false;
}

void BleTargets::add(const char *addr)
{
  for (int i = 0; i < count; i++)
  {
    if (strcasecmp(addr, data[i]) == 0) return;
  }
  if (count < MAX_TARGETS)
  {
    strncpy(data[count], addr, 17);
    data[count][17] = '\0';
    count++;
  }
}

void BleTargets::remove(const char *addr)
{
  for (int i = 0; i < count; i++)
  {
    if (strcasecmp(addr, data[i]) == 0)
    {
      for (int j = i; j < count - 1; j++)
        strncpy(data[j], data[j + 1], 18);
      count--;
      return;
    }
  }
}
