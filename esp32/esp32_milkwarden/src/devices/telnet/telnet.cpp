#include "telnet.h"
#include "config.h"
#include "devices/loadcell/loadcell.h"
#include "modules/ota/ota.h"
#include "modules/storage/nvs_manager.h"
#include "modules/heartbeat/heartbeat.h"
#include <WiFi.h>
#include <climits>

ESPTelnet telnet;

// ─── Corner calibration state (local to Telnet CLI) ───────────────────────────
static float       cornerVal[4] = {0, 0, 0, 0};
static bool        cornerSet[4] = {false, false, false, false};
static const char* CORNER[4]    = {"FL", "FR", "BL", "BR"};

// ─── Diagnostic helpers ──────────────────────────────────────────────────────

static int cornerIndex(const String& name) {
    String s = name; s.toUpperCase();
    for (int i = 0; i < 4; i++) if (s == CORNER[i]) return i;
    return -1;
}

static void cornerReport() {
    int nSet = 0;
    for (int i = 0; i < 4; i++) if (cornerSet[i]) nSet++;
    if (nSet < 2) {
        telnet.println("[Corner] Need at least 2 corners measured.");
        telnet.println("  corner_test FL  (FR / BL / BR)");
        return;
    }
    float ref = 0;
    for (int i = 0; i < 4; i++) if (cornerSet[i] && cornerVal[i] > ref) ref = cornerVal[i];

    telnet.println("\n========= Corner Correction =========");
    telnet.printf("  Reference (max): %.2f g\n\n", ref);
    for (int i = 0; i < 4; i++) {
        if (!cornerSet[i]) { telnet.printf("  %s: not measured\n", CORNER[i]); continue; }
        float diff = cornerVal[i] - ref;
        float pct  = (ref > 0.1f) ? (diff / ref) * 100.0f : 0.0f;
        if      (fabsf(pct) < 0.3f) telnet.printf("  %s: %7.2f g  [OK]\n", CORNER[i], cornerVal[i]);
        else if (diff < 0)           telnet.printf("  %s: %7.2f g  [LOW  %.1f%% = %.2f g]  -> increase adjuster %s\n",
                                                   CORNER[i], cornerVal[i], fabsf(pct), fabsf(diff), CORNER[i]);
        else                         telnet.printf("  %s: %7.2f g  [HIGH %.1f%% = %.2f g]  -> decrease adjuster %s\n",
                                                   CORNER[i], cornerVal[i], pct, diff, CORNER[i]);
    }
    telnet.println("\n  When all [OK] — recalibrate: cal_tare → cal_weight.");
    telnet.println("=====================================\n");
}

static void runDiagnostics() {
    telnet.println("\n======= HX711 DIAGNOSTICS =======");
    hx711Pause();

    bool ready = false;
    for (int i = 0; i < 40; i++) {
        if (scale.is_ready()) { ready = true; break; }
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    telnet.println(ready ? "[OK]   HX711 responding (DOUT=LOW)"
                         : "[FAIL] HX711 not responding — check power and wiring");
    if (!ready) {
        telnet.printf("       DOUT=%d  SCK=%d\n", LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
        telnet.println("=================================\n");
        hx711Resume();
        return;
    }

    const int N = 20;
    long samples[N];
    int  failures = 0;
    telnet.printf("[INFO] Reading %d samples: ", N);
    for (int i = 0; i < N; i++) {
        if (scale.is_ready()) { samples[i] = safeRead(); telnet.print("."); }
        else                  { samples[i] = 0; failures++; telnet.print("X"); }
        vTaskDelay(pdMS_TO_TICKS(120));
    }
    hx711Resume();
    telnet.printf(" done (%d failures)\n", failures);

    if (failures > N / 2)
        telnet.println("[FAIL] More than half readings failed — unstable power supply");

    long vmin = samples[0], vmax = samples[0];
    long long sum = 0;
    for (int i = 0; i < N; i++) {
        if (samples[i] < vmin) vmin = samples[i];
        if (samples[i] > vmax) vmax = samples[i];
        sum += samples[i];
    }
    long avg   = (long)(sum / N);
    long noise = vmax - vmin;

    telnet.printf("[RAW]  min=%-12ld  max=%-12ld\n", vmin, vmax);
    telnet.printf("[RAW]  avg=%-12ld  noise(p-p)=%ld\n", avg, noise);

    if      (noise <  1000)  telnet.println("[OK]   Noise excellent  (< 1 000)");
    else if (noise < 10000)  telnet.println("[OK]   Noise acceptable (< 10 000)");
    else if (noise < 50000)  telnet.println("[WARN] Noise high       (< 50 000) — check power/shielding");
    else                     telnet.println("[FAIL] Noise extreme    (>= 50 000) — wiring problem");

    if      (avg == 0)         telnet.println("[WARN] avg=0 — DOUT floating (no sensor?)");
    else if (avg <= -8388607) {
        telnet.println("[FAIL] ADC saturated (min -8388608)");
        telnet.println("       CAUSE: no sensor, swapped E+/E- or A+/A-");
        telnet.println("       FIX 1: check 4 wires (Red=E+, Black=E-, Green=A+, White=A-)");
        telnet.println("       FIX 2: swap A+ and A-");
        telnet.println("       FIX 3: check HX711 power (need 3.3-5V)");
        telnet.println("       TIP:   try 'gain 64' or 'gain 32'");
    } else if (avg >= 8388607) {
        telnet.println("[FAIL] ADC saturated (max +8388607) — overload or swapped E+/E-");
    } else {
        telnet.println("[OK]   ADC not saturated");
    }

    telnet.printf("[CFG]  factor=%.4f  offset=%ld  samples=%d\n",
                  calibration_factor, calibration_offset, num_samples);
    telnet.printf("[CFG]  DOUT=%d  SCK=%d\n", LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
    telnet.printf("[CFG]  AutoZero=%s  thr=%.1fg  hold=%ums\n",
                  autoZeroEnabled ? "ON" : "OFF", autoZeroThreshold, autoZeroHoldMs);
    telnet.println("=================================\n");
}

static void noiseTest(int n) {
    if (n < 5) n = 5;
    if (n > 200) n = 200;
    telnet.printf("[Noise] Reading %d raw samples (hx711Task paused)...\n", n);
    long vmin = LONG_MAX, vmax = LONG_MIN;
    long long sum = 0;
    hx711Pause();
    for (int i = 0; i < n; i++) {
        long v = safeRead();
        if (v < vmin) vmin = v;
        if (v > vmax) vmax = v;
        sum += v;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    hx711Resume();
    long  avg     = (long)(sum / n);
    float g_noise = (float)(vmax - vmin) / fabsf(calibration_factor);
    telnet.printf("[Noise] avg=%ld  p-p raw=%ld  noise=%.3f g\n", avg, vmax - vmin, g_noise);
}

// ─── Command handler ──────────────────────────────────────────────────────────

static void handleCommand(String str) {
    str.trim();

    // Persistent state for two-point calibration sequence
    static float raw_1 = 0, weight_1 = 0;
    static float raw_2 = 0, weight_2 = 0;

    // ── Measurement ────────────────────────────────────────────────────────
    if (str == "start") {
        isMeasuring = true;
        telnet.println("[System] Measurement started.");

    } else if (str == "stop") {
        isMeasuring = false;
        telnet.println("[System] Measurement stopped.");

    } else if (str == "tare") {
        safeTare(15);
        calibration_offset = scale.get_offset();
        telnet.println("[Scale] Zeroed. Type 'save' to persist.");

    } else if (str.startsWith("samples ")) {
        int n = str.substring(8).toInt();
        if (n >= 1 && n <= 64) { num_samples = n; telnet.printf("[Scale] Samples: %d\n", n); }
        else                     telnet.println("[Scale] Valid range: 1..64");

    // ── Calibration ────────────────────────────────────────────────────────
    } else if (str.startsWith("calib ")) {
        calibration_factor = str.substring(6).toFloat();
        scale.set_scale(calibration_factor);
        telnet.printf("[Scale] Factor set to: %.4f\n", calibration_factor);

    } else if (str == "factor") {
        telnet.printf("[Scale] factor=%.4f  offset=%ld  samples=%d\n",
                      calibration_factor, (long)scale.get_offset(), num_samples);

    } else if (str == "cal_tare") {
        raw_1 = (float)safeReadAvg(20);
        calibration_offset = (long)raw_1;
        scale.set_offset(calibration_offset);
        telnet.printf("[Calib] Zero point RAW=%.0f. Place weight → cal_weight <g>\n", raw_1);

    } else if (str.startsWith("cal_weight ")) {
        weight_2 = str.substring(11).toFloat();
        if (weight_2 <= 0) {
            telnet.println("[Calib] Error: weight must be > 0");
        } else {
            raw_2 = (float)safeReadAvg(20);
            calibration_factor = (raw_2 - raw_1) / weight_2;
            scale.set_scale(calibration_factor);
            telnet.printf("[Calib] RAW=%.0f  New factor=%.4f\n", raw_2, calibration_factor);
            float check = getFilteredWeight(num_samples);
            telnet.printf("[Calib] Check: %.2f g  (expected ~%.1f g)\n", check, weight_2);
            telnet.println("[Calib] Done! Type 'save'.");
        }

    // Legacy two-point calibration
    } else if (str.startsWith("point1 ")) {
        weight_1 = str.substring(7).toFloat();
        raw_1    = (float)safeReadAvg(10);
        telnet.printf("[Calib] Point1: weight=%.2f  RAW=%.0f\n", weight_1, raw_1);

    } else if (str.startsWith("point2 ")) {
        weight_2 = str.substring(7).toFloat();
        raw_2    = (float)safeReadAvg(10);
        calibration_factor = (raw_2 - raw_1) / (weight_2 - weight_1);
        calibration_offset = (long)raw_1;
        scale.set_scale(calibration_factor);
        scale.set_offset(calibration_offset);
        telnet.printf("[Calib] factor=%.4f  offset=%ld\n", calibration_factor, calibration_offset);

    // ── Corner correction ─────────────────────────────────────────────────
    } else if (str.startsWith("corner_test ")) {
        String cname = str.substring(12); cname.trim();
        int idx = cornerIndex(cname);
        if (idx < 0) {
            telnet.println("[Corner] Use: FL FR BL BR");
        } else {
            telnet.printf("[Corner] Reading %s (%d samples)...\n", CORNER[idx], num_samples);
            cornerVal[idx] = getFilteredWeight(num_samples);
            cornerSet[idx] = true;
            telnet.printf("[Corner] %s = %.2f g\n", CORNER[idx], cornerVal[idx]);
            for (int i = 0; i < 4; i++)
                if (cornerSet[i]) telnet.printf("         %s=%.2f ", CORNER[i], cornerVal[i]);
            telnet.println();
        }

    } else if (str == "corner_report") {
        cornerReport();

    } else if (str == "corner_clear") {
        for (int i = 0; i < 4; i++) { cornerSet[i] = false; cornerVal[i] = 0; }
        telnet.println("[Corner] Data cleared.");

    // ── Auto-zero ─────────────────────────────────────────────────────────
    } else if (str == "autozero on") {
        autoZeroEnabled = true;
        telnet.printf("[AutoZero] Enabled. Threshold=%.1fg  Hold=%ums\n",
                      autoZeroThreshold, autoZeroHoldMs);

    } else if (str == "autozero off") {
        autoZeroEnabled = false;
        resetAutoZeroState();
        telnet.println("[AutoZero] Disabled.");

    } else if (str.startsWith("az_thr ")) {
        autoZeroThreshold = str.substring(7).toFloat();
        telnet.printf("[AutoZero] Threshold: %.1f g\n", autoZeroThreshold);

    } else if (str.startsWith("az_time ")) {
        autoZeroHoldMs = (uint32_t)str.substring(8).toInt();
        telnet.printf("[AutoZero] Hold time: %u ms\n", autoZeroHoldMs);

    // ── Diagnostics ───────────────────────────────────────────────────────
    } else if (str.startsWith("gain ")) {
        int g = str.substring(5).toInt();
        if (g == 128 || g == 64 || g == 32) {
            hx711Pause();
            scale.set_gain(g);
            if (scale.is_ready()) safeRead();
            vTaskDelay(pdMS_TO_TICKS(150));
            long v = scale.is_ready() ? safeRead() : LONG_MIN;
            hx711Resume();
            if (v == LONG_MIN)
                telnet.printf("[Gain] %d: HX711 did not respond\n", g);
            else
                telnet.printf("[Gain] %d: raw=%ld%s\n", g, v,
                              v <= -8388607 ? "  <- saturated (min)" :
                              v >= 8388607  ? "  <- saturated (max)" : "  <- signal OK!");
        } else {
            telnet.println("[Gain] Valid: 128 / 64 / 32");
        }

    } else if (str == "wiring") {
        telnet.println("\n=== Load Cell Wiring ===");
        telnet.println("HX711: E+  E-  A+  A-");
        telnet.println("  Red    -> E+");
        telnet.println("  Black  -> E-");
        telnet.println("  Green  -> A+");
        telnet.println("  White  -> A-");
        telnet.println("\nIf reading -8388608:");
        telnet.println("  1. Check all 4 wires (white wire often not fully seated)");
        telnet.println("  2. Swap A+ and A-");
        telnet.println("  3. Measure VCC-GND with multimeter (need 3.3-5V stable)");
        telnet.println("=======================\n");

    } else if (str == "diag") {
        runDiagnostics();

    } else if (str == "raw" || str.startsWith("raw ")) {
        int n = 5;
        if (str.length() > 4) n = constrain(str.substring(4).toInt(), 1, 50);
        long  raw_avg = safeReadAvg(n);
        float filt    = getFilteredWeight(num_samples);
        telnet.printf("[RAW] avg_raw=%ld  filtered=%.3fg  factor=%.4f  offset=%ld\n",
                      raw_avg, filt, calibration_factor, (long)scale.get_offset());

    } else if (str == "noise" || str.startsWith("noise ")) {
        int n = 30;
        if (str.length() > 6) n = str.substring(6).toInt();
        noiseTest(n);

    // ── System ────────────────────────────────────────────────────────────
    } else if (str == "save") {
        calibration_offset = scale.get_offset();
        saveSettings();
        telnet.println("[NVS] Settings saved to flash.");

    } else if (str == "status") {
        telnet.println("[Status] Version   : " FIRMWARE_VERSION);
        telnet.println("[Status] IP        : " + WiFi.localIP().toString());
        hx711Pause();
        bool hxOk = false;
        for (int i = 0; i < 40 && !hxOk; i++) {
            hxOk = scale.is_ready();
            if (!hxOk) vTaskDelay(pdMS_TO_TICKS(5));
        }
        hx711Resume();
        telnet.println("[Status] HX711     : " + String(hxOk ? "OK" : "NOT READY"));
        telnet.printf( "[Status] Factor    : %.4f\n", calibration_factor);
        telnet.printf( "[Status] Offset    : %ld\n",  (long)scale.get_offset());
        telnet.printf( "[Status] Samples   : %d\n",   num_samples);
        telnet.println("[Status] Measuring : " + String(isMeasuring ? "running" : "stopped"));
        telnet.printf( "[Status] AutoZero  : %s  thr=%.1fg  hold=%ums\n",
                       autoZeroEnabled ? "ON" : "OFF", autoZeroThreshold, autoZeroHoldMs);
        {
            xSemaphoreTake(scaleMutex, portMAX_DELAY);
            int cnt = rawCount;
            xSemaphoreGive(scaleMutex);
            telnet.printf("[Status] HX711 buf : %d / %d samples\n", cnt, RAW_BUF_SIZE);
        }
        float w = getFilteredWeight(num_samples);
        telnet.printf("[Status] Current   : %.2f g\n", w);

    } else if (str == "update") {
        startOTA();

    } else if (str != "") {
        telnet.println("[Unknown] " + str);
    }

    telnet.print("> ");
}

// ─── Telnet init ──────────────────────────────────────────────────────────────

void initTelnet() {
    telnet.onConnect([](String ip) {
        telnet.println("\n--- ESP32 Weight Station v" FIRMWARE_VERSION " ---");
        telnet.println("MEASUREMENT  : start | stop | tare | samples <n>");
        telnet.println("CALIBRATION  : cal_tare | cal_weight <g> | calib <factor> | factor");
        telnet.println("CORNER CORR  : corner_test <FL|FR|BL|BR> | corner_report | corner_clear");
        telnet.println("AUTO-ZERO    : autozero on|off | az_thr <g> | az_time <ms>");
        telnet.println("DIAGNOSTICS  : diag | raw [n] | noise [n] | gain <128|64|32> | wiring");
        telnet.println("SYSTEM       : save | status | update");
        telnet.print("> ");
    });

    telnet.onInputReceived(handleCommand);
    telnet.begin();
}
