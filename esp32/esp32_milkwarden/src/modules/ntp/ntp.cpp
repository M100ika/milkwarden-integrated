#include "ntp.h"
#include "config.h"
#include <Arduino.h>

void initNTP() {
    configTime(NTP_GMT_OFFSET_SEC, NTP_DAYLIGHT_OFFSET_SEC, NTP_SERVER);
    Serial.println("[NTP] Configured: " NTP_SERVER);
}

bool isTimeSynced() {
    time_t now;
    time(&now);
    return now > 1000000000UL;
}

time_t getUnixTime() {
    time_t now;
    time(&now);
    return now;
}

void formatNTPTime(char* dateBuf, size_t dateLen, char* timeBuf, size_t timeLen) {
    time_t    now = getUnixTime();
    struct tm ti;
    localtime_r(&now, &ti);
    if (dateBuf && dateLen) strftime(dateBuf, dateLen, "%Y-%m-%d", &ti);
    if (timeBuf && timeLen) strftime(timeBuf, timeLen, "%H:%M:%S", &ti);
}
