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
