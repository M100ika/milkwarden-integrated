#pragma once
#include <stdint.h>

// ─── Firmware ─────────────────────────────────────────────────────────────────
#define FIRMWARE_VERSION        "1.1.4"

// ─── WiFi STA (connect to router) ─────────────────────────────────────────────
struct WifiCredential { const char* ssid; const char* password; };
static const WifiCredential WIFI_CREDENTIALS[] = {
    { "smart2",   "Kazatu2025" },
    { "STARLINK", "" },          // open network, no password
};
#define WIFI_RECONNECT_MS       10000U

// ─── Nextion display: UART0 (Serial, GPIO1/3) ────────────────────────────────
#define NEXTION_BAUD            9600

// ─── Telnet — only debug channel ──────────────────────────────────────────────
#define TELNET_PORT             23

// ─── OTA ──────────────────────────────────────────────────────────────────────
#define OTA_FIRMWARE_URL \
    "https://github.com/M100ika/milkwarden-integrated/raw/refs/heads/main/esp32/esp32_master/esp32_master/build/firmware.bin"

// ─── Slave count ──────────────────────────────────────────────────────────────
#define SLAVE_COUNT             4

// ─── Packet type markers ──────────────────────────────────────────────────────
#define PKT_TYPE_SNAPSHOT       0x01
#define PKT_TYPE_SESSION        0x02

// device_state
#define DEV_STATE_IDLE          0
#define DEV_STATE_COW_PRESENT   1
#define DEV_STATE_MILKING       2

// end_reason
#define END_REASON_COW_LEFT         0
#define END_REASON_BUCKET_CHANGE    1

// ─── SnapshotPacket — 40 bytes ────────────────────────────────────────────────
struct __attribute__((packed)) SnapshotPacket {
    uint8_t  type;           // PKT_TYPE_SNAPSHOT
    uint8_t  esp_id;         // slave 1-4
    uint32_t ip_addr;
    char     rfid_tag[25];
    uint8_t  beam_state;     // 0=ok 1=interrupted
    uint8_t  device_state;   // DEV_STATE_*
    float    weight;         // grams
    uint32_t timestamp;      // Unix time (NTP UTC+5)
    uint8_t  msg_state;
};

// ─── SessionPacket — 48 bytes ─────────────────────────────────────────────────
struct __attribute__((packed)) SessionPacket {
    uint8_t  type;           // PKT_TYPE_SESSION
    uint8_t  esp_id;
    uint32_t ip_addr;
    char     rfid_tag[25];
    float    weight_initial;
    float    weight_final;
    uint32_t start_time;
    uint32_t end_time;
    uint8_t  end_reason;     // END_REASON_*
    uint8_t  msg_state;
};
