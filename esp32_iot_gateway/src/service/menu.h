#pragma once
#include <Arduino.h>

enum class OperationMode { DEEP_SLEEP, CONTINUOUS };

OperationMode enterMenuMode();
