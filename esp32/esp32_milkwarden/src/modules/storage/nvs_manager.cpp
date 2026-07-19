#include "nvs_manager.h"
#include "config.h"
#include "devices/loadcell/loadcell.h"
#include "devices/valve/valve.h"
#include "modules/heartbeat/heartbeat.h"
#include "modules/tlog/tlog.h"
#include <Preferences.h>
#include <LittleFS.h>

static Preferences prefs;

// ─── NVS ─────────────────────────────────────────────────────────────────────

void loadSettings() {
    prefs.begin("scale", true);
    calibration_factor = prefs.getFloat("factor",  DEFAULT_CALIB_FACTOR);
    calibration_offset = prefs.getLong( "offset",  DEFAULT_CALIB_OFFSET);
    num_samples        = prefs.getInt(  "samples", DEFAULT_NUM_SAMPLES);
    autoZeroEnabled    = prefs.getBool( "az_en",   DEFAULT_AUTOZERO_ENABLED);
    autoZeroThreshold  = prefs.getFloat("az_thr",  DEFAULT_AUTOZERO_THRESHOLD);
    autoZeroHoldMs     = prefs.getUInt( "az_time", DEFAULT_AUTOZERO_HOLD_MS);
    valveOpenDelayMs    = prefs.getUInt( "v_delay",  VALVE_OPEN_DELAY_MS);
    valveOpenDurationMs = prefs.getUInt( "v_dur",    VALVE_OPEN_DURATION_MS);
    valveCooldownMs     = prefs.getUInt( "v_cool",   VALVE_COOLDOWN_MS);
    prefs.end();
    tlog("[NVS] factor=%.4f  offset=%ld  samples=%d",
                  calibration_factor, calibration_offset, num_samples);
}

void saveSettings() {
    prefs.begin("scale", false);
    prefs.putFloat("factor",  calibration_factor);
    prefs.putLong( "offset",  calibration_offset);
    prefs.putInt(  "samples", num_samples);
    prefs.putBool( "az_en",   autoZeroEnabled);
    prefs.putFloat("az_thr",  autoZeroThreshold);
    prefs.putUInt( "az_time", autoZeroHoldMs);
    prefs.putUInt( "v_delay",  valveOpenDelayMs);
    prefs.putUInt( "v_dur",    valveOpenDurationMs);
    prefs.putUInt( "v_cool",   valveCooldownMs);
    prefs.end();
    tlog("[NVS] Settings saved.");
}

// ─── Device ID ───────────────────────────────────────────────────────────────

uint8_t getDeviceId() {
    Preferences p;
    p.begin("device", true);
    uint8_t id = p.getUChar("id", 0);
    p.end();
    return id;
}

void setDeviceId(uint8_t id) {
    Preferences p;
    p.begin("device", false);
    p.putUChar("id", id);
    p.end();
    tlog("[NVS] Device ID saved: %u", id);
}

// ─── SPIFFS / LittleFS config.ini reader ─────────────────────────────────────
//
// Reads /config.ini from LittleFS and overrides NVS-loaded values.
// To upload: copy data/config.ini to the ESP32 with `pio run -t uploadfs`
// (requires board_build.filesystem = littlefs in platformio.ini).
//
// Supported sections and keys:
//   [scale]    factor=<float>  samples=<int>
//   [autozero] enabled=<true|false>  threshold=<float>  hold_ms=<int>

void loadConfigFile() {
    if (!LittleFS.begin(false)) {
        tlog("[Config] LittleFS not mounted, skipping config.ini");
        return;
    }
    if (!LittleFS.exists("/config.ini")) {
        tlog("[Config] /config.ini not found");
        LittleFS.end();
        return;
    }

    File f = LittleFS.open("/config.ini", "r");
    if (!f) {
        tlog("[Config] Cannot open /config.ini");
        LittleFS.end();
        return;
    }

    String section;
    int    parsed = 0;

    while (f.available()) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.isEmpty() || line.startsWith(";") || line.startsWith("#")) continue;

        if (line.startsWith("[") && line.endsWith("]")) {
            section = line.substring(1, line.length() - 1);
            section.toLowerCase();
            continue;
        }

        int eq = line.indexOf('=');
        if (eq < 0) continue;
        String key = line.substring(0, eq); key.trim(); key.toLowerCase();
        String val = line.substring(eq + 1); val.trim();

        if (section == "scale") {
            // factor is managed via NVS only (Telnet cal commands) — not overridable from file
            if (key == "samples") { num_samples = val.toInt(); parsed++; }

        } else if (section == "autozero") {
            if      (key == "enabled")   { autoZeroEnabled = (val == "true" || val == "1"); parsed++; }
            else if (key == "threshold") { autoZeroThreshold = val.toFloat(); parsed++; }
            else if (key == "hold_ms")   { autoZeroHoldMs = (uint32_t)val.toInt(); parsed++; }
        }
    }

    f.close();
    LittleFS.end();
    tlog("[Config] Loaded %d value(s) from /config.ini", parsed);
}
