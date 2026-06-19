#include "cloud.h"
#include "config.h"
#include "devices/wifi/wifi.h"
#include "devices/espnow/espnow.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <stdio.h>

static char s_lastBody[256];

const char* cloudLastBody() { return s_lastBody; }

int sendToCloud(const SessionPacket& pkt) {
    s_lastBody[0] = '\0';

    if (!isWifiConnected()) {
        snprintf(s_lastBody, sizeof(s_lastBody), "No WiFi");
        return 0;
    }

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.setTimeout(CLOUD_POST_TIMEOUT_MS);
    if (!http.begin(client, CLOUD_ENDPOINT_URL)) {
        snprintf(s_lastBody, sizeof(s_lastBody), "http.begin failed");
        return -1;
    }

    http.addHeader("Content-Type",  "application/json");
    http.addHeader("apikey",        CLOUD_API_KEY);
    http.addHeader("Authorization", "Bearer " CLOUD_API_KEY);
    http.addHeader("Prefer",        "return=minimal");

    char body[300];
    snprintf(body, sizeof(body),
        "{\"esp_id\":%u,\"rfid\":\"%s\","
        "\"weight_initial\":%.1f,\"weight_final\":%.1f,"
        "\"start_time\":%lu,\"end_time\":%lu,\"end_reason\":%u,"
        "\"distance_initial_mm\":%u,\"distance_final_mm\":%u}",
        pkt.esp_id, pkt.rfid_tag,
        pkt.weight_initial / 1000.0f, pkt.weight_final / 1000.0f,
        (unsigned long)pkt.start_time,
        (unsigned long)pkt.end_time,
        pkt.end_reason,
        getDistanceInitialMm(),
        getDistanceFinalMm());

    int code = http.POST(body);
    if (code != 201) {
        String resp = http.getString();
        snprintf(s_lastBody, sizeof(s_lastBody), "%s", resp.c_str());
    }
    http.end();
    return code;
}
