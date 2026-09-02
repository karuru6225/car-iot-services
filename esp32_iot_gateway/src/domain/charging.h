#pragma once

// 電圧に基づく充電ヒステリシス判定の閾値
struct ChargingThresholds
{
  float startV;  // vMainがこの電圧未満で充電ON判定に入る
  float stopV;   // vMainがこの電圧以上で充電OFF判定に入る
  float minDiff; // sub-main の最小電圧差（これ未満でOFF方向）
};

// 現在の充電状態・計測電圧・閾値から、次の充電状態を判定する（ヒステリシス制御）。
// vMain < 10.0V（未接続/センサー異常相当）のときは状態を変えない。
// いずれの遷移条件にも合致しない場合は currentlyCharging をそのまま返す。
bool decideCharging(float vMain, float vSub, bool currentlyCharging, const ChargingThresholds &th);
