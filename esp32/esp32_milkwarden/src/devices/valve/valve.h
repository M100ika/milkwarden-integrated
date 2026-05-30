#pragma once

#include <stdint.h>

extern uint32_t valveOpenDelayMs;
extern uint32_t valveOpenDurationMs;

void initValve();
void openValve();
void closeValve();
