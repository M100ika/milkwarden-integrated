#include "wifi.h"
#include "config.h"
#include <WiFi.h>
#include <WiFiMulti.h>
#include <esp_wifi.h>

static WiFiMulti         wifiMulti;
static volatile uint8_t  activeChannel = ESPNOW_DEFAULT_CHANNEL;

// ─── Event callbacks (run in WiFi task context, not ISR) ──────────────────────

static void onGotIp(WiFiEvent_t, WiFiEventInfo_t) {
    uint8_t ch = static_cast<uint8_t>(WiFi.channel());
    activeChannel = ch;
    esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
    digitalWrite(WIFI_LED_PIN, HIGH);
    Serial.printf("[WiFi] Connected %s  ch=%u\n",
                  WiFi.localIP().toString().c_str(), ch);
}

static void onDisconnected(WiFiEvent_t, WiFiEventInfo_t) {
    activeChannel = ESPNOW_DEFAULT_CHANNEL;
    esp_wifi_set_channel(ESPNOW_DEFAULT_CHANNEL, WIFI_SECOND_CHAN_NONE);
    digitalWrite(WIFI_LED_PIN, LOW);
    Serial.printf("[WiFi] Disconnected, channel → %u\n", ESPNOW_DEFAULT_CHANNEL);
}

// ─── Background WiFiMulti task (Core 0) ───────────────────────────────────────
// Blocks itself, not the main loop. Retries every WIFI_RECONNECT_INTERVAL_MS.

static void wifiManagerTask(void* pv) {
    for (;;) {
        if (WiFi.status() != WL_CONNECTED)
            wifiMulti.run(5000);
        vTaskDelay(pdMS_TO_TICKS(WIFI_RECONNECT_INTERVAL_MS));
    }
}

// ─── Public API ───────────────────────────────────────────────────────────────

void initComms() {
    pinMode(WIFI_LED_PIN, OUTPUT);
    digitalWrite(WIFI_LED_PIN, LOW);

    WiFi.onEvent(onGotIp,        ARDUINO_EVENT_WIFI_STA_GOT_IP);
    WiFi.onEvent(onDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP_STA);
    esp_wifi_set_channel(ESPNOW_DEFAULT_CHANNEL, WIFI_SECOND_CHAN_NONE);

    for (const auto& c : WIFI_CREDENTIALS)
        wifiMulti.addAP(c.ssid, c.password);

    xTaskCreatePinnedToCore(wifiManagerTask, "wifiMgr", 4096, NULL, 1, NULL, 0);

    Serial.printf("[WiFi] Comms init, ESP-NOW base ch=%u\n", ESPNOW_DEFAULT_CHANNEL);
}

bool isWifiConnected() {
    return WiFi.status() == WL_CONNECTED;
}

uint8_t getCurrentChannel() {
    return activeChannel;
}
