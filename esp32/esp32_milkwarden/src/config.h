#pragma once

#include <stdint.h>

// ─── Firmware ─────────────────────────────────────────────────────────────────
#define FIRMWARE_VERSION            "1.4.9.5"
#define BOOTLOG_ENABLED             0          // 1 = show boot log on telnet connect, 0 = off

// ─── WiFi credentials ─────────────────────────────────────────────────────────
struct WifiCredential { const char* ssid; const char* password; };
static const WifiCredential WIFI_CREDENTIALS[] = {
    { "smart2",   "Kazatu2025" },
};
#define WIFI_LED_PIN                2
#define WIFI_RECONNECT_INTERVAL_MS  10000U

// ─── NTP ──────────────────────────────────────────────────────────────────────
#define NTP_SERVER                  "pool.ntp.org"
#define NTP_GMT_OFFSET_SEC          18000      // UTC+5 Kazakhstan
#define NTP_DAYLIGHT_OFFSET_SEC     0

// ─── ESP-NOW ──────────────────────────────────────────────────────────────────
#define ESPNOW_DEFAULT_CHANNEL      1
#define ESPNOW_LOG_ENABLED  0
// Update to the actual MAC of the Master ESP32 before flashing
static const uint8_t MASTER_MAC[6] = {
  0xEC, 0xE3, 0x34, 0x46, 0xD1, 0xA4
};

// ─── Cloud endpoint ───────────────────────────────────────────────────────────
#define CLOUD_ENDPOINT_URL          "https://uhtbdcackeowuidgpkbt.supabase.co/rest/v1/sessions"
#define CLOUD_API_KEY               "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6InVodGJkY2Fja2Vvd3VpZGdwa2J0Iiwicm9sZSI6ImFub24iLCJpYXQiOjE3NzgzOTYzNzUsImV4cCI6MjA5Mzk3MjM3NX0.djYzj3yu4yTqX0XS1eTF-bLbB6tWCjeC9hKOeaoWhQo"
#define CLOUD_POST_TIMEOUT_MS       5000

// ─── Session logic ────────────────────────────────────────────────────────────
#define RFID_SCAN_COUNT             10         // total scans per batch; most-frequent tag wins
#define RFID_START_DELAY_MS         10000U     // delay after cow detected before RFID starts
#define WEIGHT_DROP_G               10000.0f   // 10 kg drop threshold
#define WEIGHT_DROP_WINDOW_MS       3000U      // detection window
#define SNAPSHOT_INTERVAL_MS        500U

// ─── Packet type markers ──────────────────────────────────────────────────────
#define PKT_TYPE_SNAPSHOT           0x01
#define PKT_TYPE_SESSION            0x02

// msg_state
#define MSG_STATE_OK                1
#define MSG_STATE_FAIL              2

// device_state (SnapshotPacket)
#define DEV_STATE_IDLE              0
#define DEV_STATE_COW_PRESENT       1
#define DEV_STATE_MILKING           2

// end_reason (SessionPacket)
#define END_REASON_COW_LEFT         0
#define END_REASON_BUCKET_CHANGE    1

// ─── SnapshotPacket — sent every 500 ms ───────────────────────────────────────
struct __attribute__((packed)) SnapshotPacket {
    uint8_t  type;           // PKT_TYPE_SNAPSHOT
    uint8_t  esp_id;
    uint32_t ip_addr;
    char     rfid_tag[25];   // confirmed EPC hex, empty if none
    uint8_t  beam_state;     // 0=present 1=interrupted
    uint8_t  device_state;   // DEV_STATE_*
    float    weight;
    uint32_t timestamp;      // Unix time (NTP)
    uint8_t  msg_state;      // result of previous send: MSG_STATE_*
};

// ─── SessionPacket — sent on session end / bucket change ─────────────────────
struct __attribute__((packed)) SessionPacket {
    uint8_t  type;           // PKT_TYPE_SESSION
    uint8_t  esp_id;
    uint32_t ip_addr;
    char     rfid_tag[25];
    float    weight_initial;
    float    weight_final;
    uint32_t start_time;     // Unix time
    uint32_t end_time;       // Unix time
    uint8_t  end_reason;     // END_REASON_*
    uint8_t  msg_state;
};

// ─── Valve ────────────────────────────────────────────────────────────────────
#define VALVE_ENABLED               1        // 1 = valve active, 0 = off (all calls become no-ops)
#define VALVE_PIN                   23
#define VALVE_OPEN_DELAY_MS         10000U   // delay after RFID confirmed before opening
#define VALVE_OPEN_DURATION_MS      10000U   // how long valve stays open

// ─── Beam break sensor ────────────────────────────────────────────────────────
#define BEAM_PIN                    13   // INPUT_PULLUP; LOW=beam OK, HIGH=interrupted
#define BEAM_DEBOUNCE_MS            50

// ─── CF-MU910 RFID (UART2) ────────────────────────────────────────────────────
#define CFMU910_RX_PIN              3
#define CFMU910_TX_PIN              1
#define CFMU910_BAUD                115200
#define CFMU910_EN_PIN              22   // HIGH = module enabled

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
