#include "heartbeat.h"
#include "config.h"
#include "devices/loadcell/loadcell.h"
#include "devices/telnet/telnet.h"

// ─── Global state ─────────────────────────────────────────────────────────────

bool          isMeasuring    = false;
unsigned long previousMillis = 0;

bool     autoZeroEnabled   = DEFAULT_AUTOZERO_ENABLED;
float    autoZeroThreshold = DEFAULT_AUTOZERO_THRESHOLD;
uint32_t autoZeroHoldMs    = DEFAULT_AUTOZERO_HOLD_MS;

static unsigned long nearZeroSince = 0;
static bool          wasNearZero   = false;

// ─── Auto-zero ────────────────────────────────────────────────────────────────

void resetAutoZeroState() {
    wasNearZero   = false;
    nearZeroSince = 0;
}

void updateAutoZero() {
    if (!autoZeroEnabled || isMeasuring) return;

    static unsigned long lastCheck = 0;
    unsigned long now = millis();
    if (now - lastCheck < AUTOZERO_CHECK_MS) return;
    lastCheck = now;

    float w        = getFilteredWeight(5);
    bool  nearZero = fabsf(w) < autoZeroThreshold;

    if (nearZero && !wasNearZero) {
        nearZeroSince = now;
        wasNearZero   = true;
    } else if (!nearZero) {
        wasNearZero = false;
    } else if (nearZero && (now - nearZeroSince >= autoZeroHoldMs)) {
        safeTare(15);
        calibration_offset = scale.get_offset();
        nearZeroSince = now;
        if (telnet.isConnected()) telnet.println("\n[AutoZero] Auto-tare performed.");
        Serial.println("[AutoZero] Tared.");
    }
}

// ─── Measurement display ──────────────────────────────────────────────────────

void updateMeasurements() {
    static unsigned long lastDisplay = 0;
    unsigned long now = millis();
    if (now - lastDisplay < DISPLAY_INTERVAL_MS) return;
    lastDisplay = now;

    float weight = getFilteredWeight(num_samples);

    if (isMeasuring && telnet.isConnected()) {
        if (now - previousMillis >= MEASURE_INTERVAL_MS) {
            previousMillis = now;
            telnet.printf("\r[Weight] %8.2f g      ", weight);
        }
    }
}
