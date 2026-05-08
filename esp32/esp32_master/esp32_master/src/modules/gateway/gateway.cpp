#include "gateway.h"
#include <Arduino.h>
#include <string.h>
#include <stdio.h>

// Last snapshot per slave (index = esp_id - 1). Written from WiFi task,
// read from Telnet task — no mutex, acceptable for debug-only reads.
static SnapshotPacket lastSnap[SLAVE_COUNT];
static bool           hasSnap[SLAVE_COUNT];

static GatewayStats stats;

// ─── JSON serialisers ─────────────────────────────────────────────────────────

static void sendSnapshot(const SnapshotPacket& p) {
    char buf[200];
    // Convert uint32_t IP address to dotted decimal notation
    uint32_t ip = p.ip_addr;
    uint8_t b1 = (ip & 0xFF);
    uint8_t b2 = ((ip >> 8) & 0xFF);
    uint8_t b3 = ((ip >> 16) & 0xFF);
    uint8_t b4 = ((ip >> 24) & 0xFF);
    
    int n = snprintf(buf, sizeof(buf),
        "{\"type\":\"snap\",\"id\":%u,\"ip\":\"%u.%u.%u.%u\",\"rfid\":\"%s\","
        "\"beam\":%u,\"state\":%u,\"weight\":%.1f,\"ts\":%lu}\n",
        (unsigned)p.esp_id,
        (unsigned)b1, (unsigned)b2, (unsigned)b3, (unsigned)b4,
        p.rfid_tag,
        (unsigned)p.beam_state,
        (unsigned)p.device_state,
        (double)p.weight,
        (unsigned long)p.timestamp);
    if (n > 0) Serial.write((const uint8_t*)buf, (size_t)n);
}

static void sendSession(const SessionPacket& p) {
    char buf[200];
    int n = snprintf(buf, sizeof(buf),
        "{\"type\":\"session\",\"id\":%u,\"rfid\":\"%s\","
        "\"w_init\":%.1f,\"w_final\":%.1f,"
        "\"t_start\":%lu,\"t_end\":%lu,\"reason\":%u}\n",
        (unsigned)p.esp_id,
        p.rfid_tag,
        (double)p.weight_initial,
        (double)p.weight_final,
        (unsigned long)p.start_time,
        (unsigned long)p.end_time,
        (unsigned)p.end_reason);
    if (n > 0) Serial.write((const uint8_t*)buf, (size_t)n);
}

// ─── Public API ───────────────────────────────────────────────────────────────

void gatewayForward(const uint8_t* data, size_t len) {
    if (len < 2) return;
    uint8_t type = data[0];

    if (type == PKT_TYPE_SNAPSHOT && len >= sizeof(SnapshotPacket)) {
        SnapshotPacket p;
        memcpy(&p, data, sizeof(SnapshotPacket));
        if (p.esp_id >= 1 && p.esp_id <= SLAVE_COUNT) {
            uint8_t idx = p.esp_id - 1;
            memcpy(&lastSnap[idx], &p, sizeof(SnapshotPacket));
            hasSnap[idx] = true;
            stats.snap[idx]++;
        }
        sendSnapshot(p);

    } else if (type == PKT_TYPE_SESSION && len >= sizeof(SessionPacket)) {
        SessionPacket p;
        memcpy(&p, data, sizeof(SessionPacket));
        if (p.esp_id >= 1 && p.esp_id <= SLAVE_COUNT)
            stats.session[p.esp_id - 1]++;
        sendSession(p);
    }
}

GatewayStats gatewayGetStats() {
    return stats;
}

bool gatewayGetLastSnap(uint8_t id, SnapshotPacket* out) {
    if (id < 1 || id > SLAVE_COUNT) return false;
    uint8_t idx = id - 1;
    if (!hasSnap[idx]) return false;
    memcpy(out, &lastSnap[idx], sizeof(SnapshotPacket));
    return true;
}
