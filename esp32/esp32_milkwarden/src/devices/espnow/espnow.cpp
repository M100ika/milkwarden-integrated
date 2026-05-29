#include "espnow.h"
#include "config.h"
#include <esp_now.h>
#include <WiFi.h>
#include "modules/tlog/tlog.h"

static uint32_t s_lastFailLog  = 0;
static uint32_t s_failStreak   = 0;

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

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, MASTER_MAC, 6);
    peer.channel = 0;   // 0 = follow current WiFi channel
    peer.encrypt = false;
    if (esp_now_add_peer(&peer) != ESP_OK)
        tlog("[ESP-NOW] Add peer failed — check MASTER_MAC in config.h");
    else
        tlog("[ESP-NOW] Init OK");
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
