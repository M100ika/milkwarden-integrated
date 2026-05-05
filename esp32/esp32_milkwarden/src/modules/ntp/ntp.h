#pragma once

#include <time.h>
#include <stddef.h>

void    initNTP();
bool    isTimeSynced();
time_t  getUnixTime();

// dateBuf >= 11 bytes ("2026-05-05\0"), timeBuf >= 9 bytes ("14:30:00\0")
void    formatNTPTime(char* dateBuf, size_t dateLen, char* timeBuf, size_t timeLen);
