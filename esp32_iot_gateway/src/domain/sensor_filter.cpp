#include "sensor_filter.h"
#include "co2meter.h"
#include <esp_sleep.h>
#include <string.h>
#include <type_traits>

static const uint32_t FILTER_MAGIC = 0xF11EB0FF;
RTC_DATA_ATTR static uint32_t      g_filterMagic = 0;
RTC_DATA_ATTR static SensorHistory g_history[MAX_TARGETS];

SensorFilter sensorFilter;

template <typename T>
static T median3(T a, T b, T c)
{
  if (a > b) std::swap(a, b);
  if (b > c) std::swap(b, c);
  if (a > b) std::swap(a, b);
  return b;
}

void SensorFilter::begin()
{
  if (g_filterMagic != FILTER_MAGIC) {
    memset(g_history, 0, sizeof(g_history));
    g_filterMagic = FILTER_MAGIC;
  }
}

SensorHistory *SensorFilter::findOrCreate(const char *addr)
{
  for (int i = 0; i < MAX_TARGETS; i++) {
    if (strncmp(g_history[i].address, addr, 17) == 0)
      return &g_history[i];
  }
  for (int i = 0; i < MAX_TARGETS; i++) {
    if (g_history[i].address[0] == '\0') {
      strncpy(g_history[i].address, addr, sizeof(g_history[i].address) - 1);
      return &g_history[i];
    }
  }
  return nullptr;
}

void SensorFilter::apply(SensorVariant &v)
{
  std::visit([this](auto &d) {
    if (!d.parsed) return;
    SensorHistory *h = findOrCreate(d.address);
    if (!h) return;

    uint8_t i   = h->idx;
    h->temps[i] = d.temp;
    h->hums[i]  = d.humidity;
    if constexpr (std::is_same_v<std::decay_t<decltype(d)>, Co2MeterData>)
      h->co2s[i] = d.co2;
    h->idx = (i + 1) % 3;
    if (h->count < 3) h->count++;

    if (h->count < 3) return;

    d.temp     = median3(h->temps[0], h->temps[1], h->temps[2]);
    d.humidity = median3(h->hums[0], h->hums[1], h->hums[2]);
    if constexpr (std::is_same_v<std::decay_t<decltype(d)>, Co2MeterData>)
      d.co2 = median3(h->co2s[0], h->co2s[1], h->co2s[2]);
  }, v);
}
