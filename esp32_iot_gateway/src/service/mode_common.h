#pragma once
#include <stdint.h>

// 電圧に基づく充電制御（CONTINUOUS / DEEP_SLEEP 共通）。判定ロジック自体は
// domain/charging.h の decideCharging()（ハードウェア非依存の純粋関数）に委譲する
void updateChargingState();

// 次の5分境界（UTC）までの秒数を返す。時刻未同期なら SLEEP_INTERVAL_SEC を返す
uint32_t secsToNextBoundary();

// measure()で開始した非同期BLEスキャンの完了を確認し、完了していれば収集してpublishする。
// スキャン中または既に収集済み（modeCtx.blePending()=false）なら何もしない
void pollBleCollect();

// Jobsの有無を確認し、あれば実行する。OTA成功時は esp_restart() するため戻らない。
// setup()（起動直後）と CONTINUOUS/TIMED_CONTINUOUS の5分サイクルごとの両方から呼ぶことで、
// CONTINUOUS系モードに留まり続けている間もOTAジョブを検知できるようにする
void checkAndHandleJob();

// BLE接続 or CAN応答(IGN ON相当)を検知したら true を返し、検知経路を viaBle に書き込む
// （true=BLE, false=CAN）。検知できなければ bleWaitSec 秒だけBLE接続を待った上で false を返す。
// DeepSleepModeHandler::beforeRun()（bleWaitSec=BLE_WAKE_WINDOW_SEC）とLIGHT_SLEEPの短周期ゲート
// （bleWaitSec=LIGHT_SLEEP_BLE_WAIT_SEC）の両方が使う共通ロジック
bool detectContinuousPromotionTrigger(bool &viaBle, uint32_t bleWaitSec);

// このサイクル分の送信を終える処理: shadow同期 → BLE収集・publish → キューflush/save →
// LTE切断・電源off → CAN off → OLED clear。DeepSleepModeHandler::run() と
// LightSleepModeHandler::run() の両方が使う（GPIO hold/esp_deep_sleep_start()は
// enterDeepSleepFor()側。呼び出し時点でoledInit()済みであること — LIGHT_SLEEPの
// 短周期ピーク中(oledInit()未実行)から呼んではいけない）
void finishCycleAndPowerDown();

// GPIO hold（充電中ならchgOnPin、v2基板なら自己保持回路のpwrHoldPin）を行った上で、
// sec秒後に起きるようdeep sleepへ入る（戻らない）。BOOTボタン(EXT0)も起床ソースとして維持する。
// DeepSleepModeHandler::run() と LIGHT_SLEEPの短周期ゲート/LightSleepModeHandler::run() の
// 両方が使う共通処理。呼び出し前にcanDeinit()等モード固有の停止処理を済ませておくこと
void enterDeepSleepFor(uint32_t sec);
