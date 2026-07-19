#pragma once

#include <stdint.h>

extern uint32_t valveOpenDelayMs;
extern uint32_t valveOpenDurationMs;
extern uint32_t valveCooldownMs;

void initValve();
void openValve();
void closeValve();
