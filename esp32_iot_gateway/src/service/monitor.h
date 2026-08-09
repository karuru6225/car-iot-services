#pragma once
#include "../domain/measurement.h"
#include "../domain/sensor_factory.h"
#include "../config.h"

struct MeasureResult {
  SensorReading reading;
  SensorVariant ble[QUEUE_SIZE];
  int bleCount = 0;
};

MeasureResult measure();                       // 非同期BLEスキャン開始＋アナログ計測のみ。即座に戻る
bool collectBle(MeasureResult &result);         // スキャン完了していればBLEキューを収集してtrue。未完了ならfalse
void publishBattery(const SensorReading &reading);
void publishBle(const MeasureResult &result);   // collectBle()がtrueを返した後に呼ぶ
