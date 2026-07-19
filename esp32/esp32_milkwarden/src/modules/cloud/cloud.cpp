#include "cloud.h"
#include "config.h"
#include "devices/wifi/wifi.h"
#include "devices/espnow/espnow.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <stdio.h>
#include <stdarg.h>

static char s_lastBody[256];

const char* cloudLastBody() { return s_lastBody; }

// Appends formatted text at `pos`, clamped to bufSize-1. Returns the new pos.
static int appendf(char* buf, size_t bufSize, int pos, const char* fmt, ...) {
    if (pos < 0 || (size_t)pos >= bufSize) return pos;
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf + pos, bufSize - pos, fmt, args);
    va_end(args);
    if (n < 0) return pos;
    pos += n;
    if ((size_t)pos > bufSize - 1) pos = (int)(bufSize - 1);
    return pos;
}

int sendToCloud(const SessionPacket& pkt, const WeightSample* log, int logCount) {
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

    // Sized for the base fields plus up to WEIGHT_LOG_MAX_SAMPLES pairs of
    // 5-digit values in two JSON arrays (worst case ~1.4 KB at 90 samples).
    static char body[3072];
    int pos = 0;
    pos = appendf(body, sizeof(body), pos,
        "{\"esp_id\":%u,\"rfid\":\"%s\","
        "\"weight_initial\":%.1f,\"weight_final\":%.1f,"
        "\"start_time\":%lu,\"end_time\":%lu,\"end_reason\":%u,"
        "\"distance_initial_mm\":%u,\"distance_final_mm\":%u,"
        "\"weight_log_t\":[",
        pkt.esp_id, pkt.rfid_tag,
        pkt.weight_initial / 1000.0f, pkt.weight_final / 1000.0f,
        (unsigned long)pkt.start_time,
        (unsigned long)pkt.end_time,
        pkt.end_reason,
        getDistanceInitialMm(),
        getDistanceFinalMm());

    for (int i = 0; i < logCount; i++)
        pos = appendf(body, sizeof(body), pos, "%s%u", i ? "," : "", log[i].t_offset_s);

    pos = appendf(body, sizeof(body), pos, "],\"weight_log_g\":[");

    for (int i = 0; i < logCount; i++)
        pos = appendf(body, sizeof(body), pos, "%s%u", i ? "," : "", log[i].weight_g);

    appendf(body, sizeof(body), pos, "]}");

    int code = http.POST(body);
    if (code != 201) {
        String resp = http.getString();
        snprintf(s_lastBody, sizeof(s_lastBody), "%s", resp.c_str());
    }
    http.end();
    return code;
}
