#include "beam.h"
#include "config.h"
#include <Arduino.h>

void initBeam() {
    pinMode(BEAM_PIN, INPUT_PULLUP);
}

static uint8_t  s_stable   = 1;   // default INTERRUPTED (no cow)
static uint8_t  s_lastRaw  = 1;
static uint32_t s_changeMs = 0;

uint8_t readBeam() {
    uint8_t  raw = digitalRead(BEAM_PIN) == HIGH ? 1 : 0;
    uint32_t now = millis();
    if (raw != s_lastRaw) {
        s_lastRaw  = raw;
        s_changeMs = now;
    }
    if (now - s_changeMs >= BEAM_DEBOUNCE_MS)
        s_stable = raw;
    return s_stable;
}
