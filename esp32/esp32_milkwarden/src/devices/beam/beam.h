#pragma once

#include <stdint.h>

extern uint32_t cowGoneConfirmMs;     // beam must stay clear this long before "cow left" fires
extern uint32_t cowArriveConfirmMs;   // beam must stay blocked this long before "cow arrived" fires

void    initBeam();
uint8_t readBeam();   // 0 = beam present, 1 = beam interrupted
