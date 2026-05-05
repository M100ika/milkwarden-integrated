#include "beam.h"
#include "config.h"
#include <Arduino.h>

void initBeam() {
    pinMode(BEAM_PIN, INPUT_PULLUP);
}

uint8_t readBeam() {
    return digitalRead(BEAM_PIN) == HIGH ? 1 : 0;
}
