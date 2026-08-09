#pragma once
#include <NimBLEDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <atomic>
#include "../config.h"
#include "../domain/sensor_factory.h"

class BleScanner
{
public:
  QueueHandle_t queue = nullptr;
  std::atomic<bool> registrationMode{false}; // true のとき bleTargets フィルタをスキップ

  void setup();
  void start(int seconds);      // ブロッキング版。menu.cppのBLE登録画面が使用
  void startAsync(int seconds); // 非同期版。即座に戻る。完了確認はisScanning()をポーリングする
  bool isScanning();
  void stop(); // 強制停止（タイムアウト時のフェイルセーフ）
  void clearResults();
  void deinit();

private:
  NimBLEScan *_scan = nullptr;
};

extern BleScanner bleScanner;
