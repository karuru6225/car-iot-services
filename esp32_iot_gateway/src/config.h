#pragma once
#include <stdint.h>

#ifndef GIT_HASH
#define GIT_HASH "00000000"
#endif
#define FIRMWARE_VERSION "1.0.0+" GIT_HASH

// DeepSleep
static const uint32_t SLEEP_INTERVAL_SEC = 300;
