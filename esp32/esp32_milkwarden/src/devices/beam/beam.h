#pragma once

#include <stdint.h>

void    initBeam();
uint8_t readBeam();   // 0 = beam present, 1 = beam interrupted
