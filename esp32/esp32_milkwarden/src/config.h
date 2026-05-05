#pragma once

#include <stdint.h>

// ─── Firmware ─────────────────────────────────────────────────────────────────
#define FIRMWARE_VERSION            "1.2.1"

// ─── Device Identity ──────────────────────────────────────────────────────────
#define ESP_DEVICE_ID               1          // Slave number 1-4

// ─── WiFi credentials ─────────────────────────────────────────────────────────
struct WifiCredential { const char* ssid; const char* password; };
static const WifiCredential WIFI_CREDENTIALS[] = {
    { "smart2",     "Kazatu2025"  },
    { "NUGuest",     ""  },
    { "NU",     "1234512345"  },
    // { "backup_net", "backup_pass" },
};

#define WIFI_LED_PIN                2
#define WIFI_RECONNECT_INTERVAL_MS  10000U

// ─── ESP-NOW ──────────────────────────────────────────────────────────────────
#define ESPNOW_DEFAULT_CHANNEL      1
// Update to the actual MAC of the Master ESP32 before flashing
static const uint8_t MASTER_MAC[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

// ─── Cloud endpoint ───────────────────────────────────────────────────────────
#define CLOUD_ENDPOINT_URL          "https://your-server/api/milk"
#define CLOUD_POST_TIMEOUT_MS       2500

// ─── DataPacket (ESP-NOW payload) ─────────────────────────────────────────────
struct __attribute__((packed)) DataPacket {
    uint8_t  esp_id;
    float    weight;
    char     rfid_tag[15];
    uint8_t  beam_state;        // 0=clear  1=interrupted
    uint8_t  container_count;
    uint8_t  status_flags;      // bit0:is_final  bit1:cloud_error  bit2:wifi_connected
    uint32_t ip_addr;
};

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
