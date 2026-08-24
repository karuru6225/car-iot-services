#include "shadow.h"
#include "mqtt.h"
#include "mode_context.h"
#include "../logger.h"
#include "../config.h"
#include "../domain/telemetry.h"
#include <ArduinoJson.h>
#include <cstdio>

static std::optional<OperationMode> s_overridePending = std::nullopt;
static const char *s_overrideNextModeReport = nullptr; // nullptr = null（通常時）
// deltaで受信した最新のTIMED_CONTINUOUS継続期限（絶対UNIX時刻）。
// shadowPublishConfig()はmodeCtx.mode()==TIMED_CONTINUOUSの間だけこれをreportedに含める
// （TIMED_CONTINUOUSでなくなれば自動的にnullが送られるため、明示的なクリア処理は不要）
static std::optional<time_t> s_continuousUntilTime = std::nullopt;

namespace
{
struct OverrideModeEntry
{
  const char *name;
  OperationMode mode;
};
// override_next_mode で受け付ける文字列 → モードの対応表
const OverrideModeEntry kOverrideModes[] = {
    {"timed_continuous", OperationMode::TIMED_CONTINUOUS},
};

// default_mode で受け付ける文字列 ⇔ モードの対応表（起動時デフォルトモード、NVS保存）。
// TIMED_CONTINUOUSは継続期限(continuous_until_time)が別途必要な一時モードのため対象外
const OverrideModeEntry kDefaultModes[] = {
    {"deep_sleep", OperationMode::DEEP_SLEEP},
    {"continuous", OperationMode::CONTINUOUS},
    {"light_sleep", OperationMode::LIGHT_SLEEP},
};

const char *defaultModeName(OperationMode m)
{
  for (const auto &entry : kDefaultModes)
    if (entry.mode == m)
      return entry.name;
  return nullptr;
}
} // namespace

std::optional<OperationMode> getShadowOverrideMode()
{
  if (!s_overridePending) return std::nullopt;
  OperationMode m = *s_overridePending;
  s_overridePending = std::nullopt;
  return m;
}

std::optional<time_t> getShadowContinuousUntilTime()
{
  return s_continuousUntilTime;
}

static void deltaTopic(char *buf, size_t len)
{
  snprintf(buf, len, "$aws/things/%s/shadow/update/delta", getDeviceId());
}

void shadowPublishConfig(bool clearDesired)
{
  char topic[128];
  snprintf(topic, sizeof(topic), "$aws/things/%s/shadow/update", getDeviceId());

  // TIMED_CONTINUOUS中のみ継続期限をreportedに含める。それ以外のモードでは自動的にnullが送られる
  std::optional<time_t> untilTimeReport =
      (modeCtx.mode() == OperationMode::TIMED_CONTINUOUS) ? s_continuousUntilTime : std::nullopt;

  // NVSに設定済みのデフォルトモードをreportedに含める（未設定ならnull）
  const char *defaultModeReport = nullptr;
  if (auto defaultMode = getDefaultMode())
    defaultModeReport = defaultModeName(*defaultMode);

  char payload[256];
  int len = buildConfigPayload(payload, sizeof(payload), clearDesired, s_overrideNextModeReport,
                               untilTimeReport, defaultModeReport);
  s_overrideNextModeReport = nullptr; // ACK 送信後にリセット（通常時は null）

  if (mqtt.publish(topic, (const uint8_t *)payload, (size_t)len))
    logger.println("[SHADOW] config published");
  else
    logger.println("[SHADOW] config publish failed");
}

void shadowSetup()
{
  char topic[128];
  deltaTopic(topic, sizeof(topic));

  if (mqtt.subscribe(topic))
    logger.println("[SHADOW] delta subscribed");
  else
    logger.println("[SHADOW] delta subscribe failed");
}

bool shadowPollDelta(uint32_t timeoutMs)
{
  char recvTopic[128];
  static char payload[512];

  if (!mqtt.pollMqtt(recvTopic, sizeof(recvTopic), payload, sizeof(payload), timeoutMs))
    return false;

  char expected[128];
  deltaTopic(expected, sizeof(expected));
  if (strcmp(recvTopic, expected) != 0)
    return false;

  JsonDocument doc;
  if (deserializeJson(doc, payload) != DeserializationError::Ok)
    return false;

  JsonObject state = doc["state"];
  if (state.isNull())
    return false;

  bool changed = false;

  if (state["ah_offset"].is<int32_t>())
  {
    setAhOffset(state["ah_offset"].as<int32_t>());
    logger.printf("[SHADOW] ah_offset → %d\n", getAhOffset());
    changed = true;
  }

  if (state["debug_log"].is<bool>())
  {
    bool en = state["debug_log"].as<bool>();
    setDebugLogEnabled(en);
    logger.printf("[SHADOW] debug_log → %s\n", en ? "on" : "off");
    changed = true;
  }

  if (state["chg_start_v"].is<float>())
  {
    setChgStartV(state["chg_start_v"].as<float>());
    logger.printf("[SHADOW] chg_start_v → %.2f\n", getChgStartV());
    changed = true;
  }

  if (state["chg_stop_v"].is<float>())
  {
    setChgStopV(state["chg_stop_v"].as<float>());
    logger.printf("[SHADOW] chg_stop_v → %.2f\n", getChgStopV());
    changed = true;
  }

  if (state["chg_min_diff_v"].is<float>())
  {
    setChgMinDiffV(state["chg_min_diff_v"].as<float>());
    logger.printf("[SHADOW] chg_min_diff_v → %.2f\n", getChgMinDiffV());
    changed = true;
  }

  if (state["charging"].is<bool>())
  {
    setCharging(state["charging"].as<bool>());
    logger.printf("[SHADOW] charging → %s\n", isCharging() ? "on" : "off");
    changed = true;
  }

  if (state["continuous_until_time"].is<uint32_t>())
  {
    // OTAジョブ再チェックがsetup()時にしか走らないため（CONTEXT.md参照）、
    // 上限（現在時刻から24時間後まで）を設けてTIMED_CONTINUOUSが際限なく長引かないようにする
    time_t requested = (time_t)state["continuous_until_time"].as<uint32_t>();
    time_t maxUntil = time(nullptr) + 1440 * 60;
    s_continuousUntilTime = requested > maxUntil ? maxUntil : requested;
    logger.printf("[SHADOW] continuous_until_time → %ld\n", (long)*s_continuousUntilTime);
    changed = true;
  }

  if (state["default_mode"].is<const char *>())
  {
    const char *req = state["default_mode"].as<const char *>();
    for (const auto &entry : kDefaultModes)
    {
      if (strcmp(req, entry.name) == 0)
      {
        setDefaultMode(entry.mode);
        logger.printf("[SHADOW] default_mode → %s\n", entry.name);
        changed = true;
        break;
      }
    }
  }

  if (state["override_next_mode"].is<const char *>())
  {
    const char *req = state["override_next_mode"].as<const char *>();
    for (const auto &entry : kOverrideModes)
    {
      if (strcmp(req, entry.name) == 0)
      {
        s_overridePending = entry.mode;
        s_overrideNextModeReport = entry.name; // 次の shadowPublishConfig で ACK
        logger.printf("[SHADOW] override_next_mode → %s\n", entry.name);
        changed = true;
        break;
      }
    }
  }

  if (changed)
    shadowPublishConfig(true); // reported を更新して desired をクリア

  return changed;
}
