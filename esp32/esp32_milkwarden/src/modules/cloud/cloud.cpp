#include "cloud.h"
#include "config.h"
#include "devices/wifi/wifi.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

bool sendToCloud(const DataPacket& pkt) {
    if (!isWifiConnected()) return false;

    WiFiClientSecure client;
    client.setInsecure();  // replace with setCACert() for production

    HTTPClient http;
    http.setTimeout(CLOUD_POST_TIMEOUT_MS);
    if (!http.begin(client, CLOUD_ENDPOINT_URL)) return false;

    http.addHeader("Content-Type", "application/json");

    char body[256];
    snprintf(body, sizeof(body),
        "{\"id\":%u,\"weight\":%.3f,\"rfid\":\"%s\","
        "\"beam\":%u,\"count\":%u,\"flags\":%u}",
        pkt.esp_id, pkt.weight, pkt.rfid_tag,
        pkt.beam_state, pkt.container_count, pkt.status_flags);

    int code = http.POST(body);
    http.end();

    if (code >= 200 && code < 300) return true;
    Serial.printf("[Cloud] HTTP %d\n", code);
    return false;
}
