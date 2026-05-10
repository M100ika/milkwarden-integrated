#include "nextion.h"
#include <Arduino.h>
#include <stdio.h>

#define NX Serial

static const uint8_t _END[3] = {0xFF, 0xFF, 0xFF};

static void nxSend(const char* cmd) {
    NX.print(cmd);
    NX.write(_END, 3);
}

void initNextion() {
    NX.begin(NEXTION_BAUD);
    delay(150);
    nxSend("page 0");
}

// Base = (esp_id - 1) * 6
// t[Base]   = IP
// t[Base+1] = RFID
// t[Base+3] = Weight
// t[Base+4] = Time
// p[id-1]   = Visibility
void nextionUpdateSnap(const SnapshotPacket& p) {
    if (p.esp_id < 1 || p.esp_id > SLAVE_COUNT) return;

    uint8_t base = (p.esp_id - 1) * 6;
    char buf[64];

    // IP
    uint32_t ip = p.ip_addr;
    snprintf(buf, sizeof(buf), "t%u.txt=\"%u.%u.%u.%u\"", base,
        (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
        (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));
    nxSend(buf);

    // RFID
    snprintf(buf, sizeof(buf), "t%u.txt=\"%.16s\"", base + 1,
        p.rfid_tag[0] ? p.rfid_tag : "---");
    nxSend(buf);

    // Weight: grams → kg
    snprintf(buf, sizeof(buf), "t%u.txt=\"%.2f\"", base + 3,
        (double)p.weight / 1000.0);
    nxSend(buf);

    // Time: extract HH:MM:SS from Unix timestamp (UTC+5 from slave NTP)
    uint32_t sod = p.timestamp % 86400UL;
    snprintf(buf, sizeof(buf), "t%u.txt=\"%02u:%02u:%02u\"", base + 4,
        (unsigned)(sod / 3600),
        (unsigned)((sod % 3600) / 60),
        (unsigned)(sod % 60));
    nxSend(buf);

    // Visibility: show if cow present or milking
    snprintf(buf, sizeof(buf), "vis p%u,%u", p.esp_id - 1,
        p.device_state >= DEV_STATE_COW_PRESENT ? 1 : 0);
    nxSend(buf);
}
