#pragma once

#include <Arduino.h>

// ─── Measurement state ────────────────────────────────────────────────────────
extern bool          isMeasuring;
extern unsigned long previousMillis;

// ─── Auto-zero settings (persisted by nvs_manager) ───────────────────────────
extern bool     autoZeroEnabled;
extern float    autoZeroThreshold;
extern uint32_t autoZeroHoldMs;

// ─── Public API ───────────────────────────────────────────────────────────────
void updateAutoZero();
void updateMeasurements();
void resetAutoZeroState(); // call when disabling auto-zero via Telnet
