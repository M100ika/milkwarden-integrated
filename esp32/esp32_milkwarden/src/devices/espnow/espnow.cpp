#include "espnow.h"
#include "config.h"
#include <esp_now.h>
#include <WiFi.h>
#include <string.h>
#include "modules/tlog/tlog.h"
#include "modules/storage/nvs_manager.h"

static uint32_t s_lastFailLog  = 0;
static uint32_t s_failStreak   = 0;

static volatile uint16_t s_distanceInitial  = 0;
static volatile uint16_t s_distanceFinal    = 0;
static volatile bool     s_finalReady       = false;

static const uint8_t s_broadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

uint16_t getDistanceInitialMm()  { return s_distanceInitial; }
uint16_t getDistanceFinalMm()    { return s_distanceFinal; }
bool     isFinalDistanceReady()  { return s_finalReady; }
void     clearFinalDistanceReady() { s_finalReady = false; s_distanceFinal = 0; }

static void onReceive(const uint8_t*, const uint8_t* data, int len) {
    if (len < 1) return;
    if (data[0] != PKT_TYPE_SENSOR) return;
    if (len < (int)sizeof(SensorPacket)) return;

    SensorPacket pkt;
    memcpy(&pkt, data, sizeof(pkt));
    if (pkt.target_slave_id != getDeviceId()) return;

    if (pkt.cmd_response == CMD_MEASURE_INIT) {
        s_distanceInitial = pkt.distance_mm;
        tlog("[ESP-NOW] Sensor INIT dist=%u mm  bat=%u%%", pkt.distance_mm, pkt.battery_pct);
    } else if (pkt.cmd_response == CMD_MEASURE_FINAL) {
        s_distanceFinal = pkt.distance_mm;
        s_finalReady    = true;
        tlog("[ESP-NOW] Sensor FINAL dist=%u mm  bat=%u%%", pkt.distance_mm, pkt.battery_pct);
    }
}

static void onSent(const uint8_t* mac, esp_now_send_status_t status) {
    if (status == ESP_NOW_SEND_SUCCESS) {
        s_failStreak = 0;
        return;
    }
    s_failStreak++;
    uint32_t now = millis();
    if (now - s_lastFailLog >= 10000) {
#if ESPNOW_LOG_ENABLED
        tlog("[ESP-NOW] Delivery failed x%u  ch=%d  wifi=%s",
             s_failStreak,
             WiFi.channel(),
             WiFi.status() == WL_CONNECTED ? "UP" : "DOWN");
#endif
        s_lastFailLog = now;
    }
}

void initEspNow() {
    if (esp_now_init() != ESP_OK) {
        tlog("[ESP-NOW] Init failed");
        return;
    }
    esp_now_register_send_cb(onSent);
    esp_now_register_recv_cb(onReceive);

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, MASTER_MAC, 6);
    peer.channel = 0;   // 0 = follow current WiFi channel
    peer.encrypt = false;
    if (esp_now_add_peer(&peer) != ESP_OK)
        tlog("[ESP-NOW] Add peer failed — check MASTER_MAC in config.h");
    else
        tlog("[ESP-NOW] Init OK");

    // Broadcast peer for sensor commands
    esp_now_peer_info_t bcPeer = {};
    memcpy(bcPeer.peer_addr, s_broadcastMac, 6);
    bcPeer.channel = 0;
    bcPeer.encrypt = false;
    esp_now_add_peer(&bcPeer);
}

bool espnowSendCmd(const CmdPacket& pkt) {
    esp_err_t r = esp_now_send(s_broadcastMac,
                               reinterpret_cast<const uint8_t*>(&pkt),
                               sizeof(CmdPacket));
    return r == ESP_OK;
}

bool espnowSendSnapshot(const SnapshotPacket& pkt) {
    esp_err_t r = esp_now_send(MASTER_MAC,
                               reinterpret_cast<const uint8_t*>(&pkt),
                               sizeof(SnapshotPacket));
    return r == ESP_OK;
}

bool espnowSendSession(const SessionPacket& pkt) {
    esp_err_t r = esp_now_send(MASTER_MAC,
                               reinterpret_cast<const uint8_t*>(&pkt),
                               sizeof(SessionPacket));
    return r == ESP_OK;
}
