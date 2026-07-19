#include "valve.h"
#include "config.h"
#include <Arduino.h>

uint32_t valveOpenDelayMs    = VALVE_OPEN_DELAY_MS;
uint32_t valveOpenDurationMs = VALVE_OPEN_DURATION_MS;
uint32_t valveCooldownMs     = VALVE_COOLDOWN_MS;

void initValve() {
#if VALVE_ENABLED
    pinMode(VALVE_PIN, OUTPUT);
    digitalWrite(VALVE_PIN, LOW);
#endif
}

void openValve() {
#if VALVE_ENABLED
    digitalWrite(VALVE_PIN, HIGH);
#endif
}

void closeValve() {
#if VALVE_ENABLED
    digitalWrite(VALVE_PIN, LOW);
#endif
}
