#include "gateway.h"
#include "devices/nextion/nextion.h"
#include <Arduino.h>
#include <string.h>

static SnapshotPacket lastSnap[SLAVE_COUNT];
static bool           hasSnap[SLAVE_COUNT];
static GatewayStats   stats;

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
        nextionUpdateSnap(p);

    } else if (type == PKT_TYPE_SESSION && len >= sizeof(SessionPacket)) {
        SessionPacket p;
        memcpy(&p, data, sizeof(SessionPacket));
        if (p.esp_id >= 1 && p.esp_id <= SLAVE_COUNT)
            stats.session[p.esp_id - 1]++;
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
