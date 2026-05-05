#include "cloud.h"
#include "config.h"
#include "devices/wifi/wifi.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <stdio.h>

bool sendToCloud(const SessionPacket& pkt) {
    if (!isWifiConnected()) return false;

    WiFiClientSecure client;
    client.setInsecure();   // replace with setCACert() for production

    HTTPClient http;
    http.setTimeout(CLOUD_POST_TIMEOUT_MS);
    if (!http.begin(client, CLOUD_ENDPOINT_URL)) return false;

    http.addHeader("Content-Type", "application/json");

    char body[256];
    snprintf(body, sizeof(body),
        "{\"id\":%u,\"rfid\":\"%s\","
        "\"weight_initial\":%.3f,\"weight_final\":%.3f,"
        "\"start_time\":%lu,\"end_time\":%lu,\"end_reason\":%u}",
        pkt.esp_id, pkt.rfid_tag,
        pkt.weight_initial, pkt.weight_final,
        (unsigned long)pkt.start_time,
        (unsigned long)pkt.end_time,
        pkt.end_reason);

    int code = http.POST(body);
    http.end();

    if (code >= 200 && code < 300) return true;
    Serial.printf("[Cloud] HTTP %d\n", code);
    return false;
}
