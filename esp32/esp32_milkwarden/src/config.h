#pragma once

// ─── Firmware ─────────────────────────────────────────────────────────────────
#define FIRMWARE_VERSION            "1.1.3"

// ─── WiFi ─────────────────────────────────────────────────────────────────────
#define WIFI_SSID                   "smart2"
#define WIFI_PASSWORD               "Kazatu2025"
#define WIFI_LED_PIN                2
#define WIFI_MAX_ATTEMPTS           20
#define IP_BROADCAST_PORT           4210

// ─── HX711 Pins ───────────────────────────────────────────────────────────────
#define LOADCELL_DOUT_PIN           34
#define LOADCELL_SCK_PIN            32
#define RAW_BUF_SIZE                64

// ─── OTA ──────────────────────────────────────────────────────────────────────
#define OTA_FIRMWARE_URL \
    "https://github.com/M100ika/milkwarden-integrated/raw/refs/heads/main/esp32/esp32_milkwarden/build/firmware.bin"

// ─── NVS defaults ─────────────────────────────────────────────────────────────
#define DEFAULT_CALIB_FACTOR        420.0f
#define DEFAULT_CALIB_OFFSET        0L
#define DEFAULT_NUM_SAMPLES         15
#define DEFAULT_AUTOZERO_ENABLED    false
#define DEFAULT_AUTOZERO_THRESHOLD  10.0f
#define DEFAULT_AUTOZERO_HOLD_MS    8000U

// ─── Timing intervals (ms) ────────────────────────────────────────────────────
#define MEASURE_INTERVAL_MS         1000
#define DISPLAY_INTERVAL_MS          500
#define AUTOZERO_CHECK_MS            500
