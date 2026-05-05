#pragma once
#include "config.h"
#include <stdint.h>
#include <stddef.h>

// Called from ESP-NOW onReceive — converts packet to JSON and writes to Serial
void gatewayForward(const uint8_t* data, size_t len);

// Telnet CLI helpers
struct GatewayStats {
    uint32_t snap[SLAVE_COUNT];     // snapshot packets received per slave
    uint32_t session[SLAVE_COUNT];  // session packets received per slave
};

GatewayStats     gatewayGetStats();
bool             gatewayGetLastSnap(uint8_t id, SnapshotPacket* out);  // id 1-4
