#include "espnow.h"
#include "config.h"
#include <esp_now.h>
#include <WiFi.h>

static void onSent(const uint8_t* mac, esp_now_send_status_t status) {
    if (status != ESP_NOW_SEND_SUCCESS)
        Serial.println("[ESP-NOW] Delivery failed");
}

void initEspNow() {
    if (esp_now_init() != ESP_OK) {
        Serial.println("[ESP-NOW] Init failed");
        return;
    }
    esp_now_register_send_cb(onSent);

    // channel=0: always use the host's current WiFi channel (synced by wifi.cpp)
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, MASTER_MAC, 6);
    peer.channel = 0;
    peer.encrypt = false;
    if (esp_now_add_peer(&peer) != ESP_OK)
        Serial.println("[ESP-NOW] Add peer failed — check MASTER_MAC in config.h");
    else
        Serial.println("[ESP-NOW] Init OK");
}

bool sendDataPacket(const DataPacket& pkt) {
    esp_err_t r = esp_now_send(MASTER_MAC,
                               reinterpret_cast<const uint8_t*>(&pkt),
                               sizeof(DataPacket));
    return r == ESP_OK;
}
