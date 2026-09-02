#pragma once
#include "operation_mode.h"

// LIGHT_SLEEP モードのハンドラ。DEEP_SLEEPの短周期版（詳細はCONTEXT.mdのTODO参照）。
// run(): このサイクル分の送信を終える → CAN off → 20〜30秒間隔のピークサイクルへ入る（戻らない）。
// 実際のCAN/BLE検知・短周期での再sleep判定は lightSleepShortWakeGate()（main.cppのsetup()冒頭
// から呼ばれる）が行う。DEEP_SLEEPと異なりbeforeRun()は使わない
// （LIGHT_SLEEP中はリブートで短周期ピークを回すため、loop()に戻ってくること自体がない）
class LightSleepModeHandler : public IOperationModeHandler
{
public:
  explicit LightSleepModeHandler(OperationModeContext &ctx) : _ctx(ctx) {}
  void run() override;

private:
  OperationModeContext &_ctx;
};

extern LightSleepModeHandler lightSleepMode;

// setup()冒頭（CAN/BLE初期化の直後、OLED/ADS/INA228/LTEなどの初期化より前）から呼ぶ。
// タイマー起床かつLIGHT_SLEEPの短周期ピーク中でなければ即座に何もせずreturnする
// （通常の起動フローをそのまま継続させる）。ピーク中の場合:
//   - 直前の起床が「5分境界到達」を意図したものだった場合、modeCtx.setMode(LIGHT_SLEEP)をセットして
//     return（呼び出し側は通常の初期化フローを継続し、最終的にLightSleepModeHandler::run()に
//     処理が渡る＝通常のDEEP_SLEEP起床と同等のフルサイクルを行う）
//   - BLE接続 or CAN応答を検知した場合、modeCtx.setMode(CONTINUOUS)+関連フラグをセットしてreturn
//     （通常の起動フローへフォールスルーし、最終的にContinuousModeHandlerに処理が渡る）
//   - 検知できず境界にまだ到達していなければ、再度短時間のdeep sleepに入り、この関数からreturnしない
void lightSleepShortWakeGate();
