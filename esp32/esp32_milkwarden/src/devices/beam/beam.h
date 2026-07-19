#pragma once

#include <stdint.h>

extern uint32_t cowGoneConfirmMs;   // beam must stay clear this long before "cow left" fires

void    initBeam();
uint8_t readBeam();   // 0 = beam present, 1 = beam interrupted
