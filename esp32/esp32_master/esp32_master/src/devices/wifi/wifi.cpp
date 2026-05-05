#include "wifi.h"
#include "config.h"
#include "modules/gateway/gateway.h"
#include <WiFi.h>
#include <WiFiMulti.h>
#include <esp_now.h>
#include <esp_wifi.h>

static WiFiMulti wifiMulti;

// ESP-NOW receive callback — runs in WiFi task context
static void onReceive(const uint8_t* mac, const uint8_t* data, int len) {
    gatewayForward(data, (size_t)len);
}

static void onGotIp(WiFiEvent_t, WiFiEventInfo_t) {
    // Sync ESP-NOW channel to the router's channel
    uint8_t ch = (uint8_t)WiFi.channel();
    esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
}

static void onDisconnected(WiFiEvent_t, WiFiEventInfo_t) {
    // wifiManagerTask retries automatically
}

static void wifiManagerTask(void* pv) {
    for (;;) {
        if (WiFi.status() != WL_CONNECTED)
            wifiMulti.run(5000);
        vTaskDelay(pdMS_TO_TICKS(WIFI_RECONNECT_MS));
    }
}

void initComms() {
    WiFi.onEvent(onGotIp,        ARDUINO_EVENT_WIFI_STA_GOT_IP);
    WiFi.onEvent(onDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

    WiFi.mode(WIFI_STA);

    for (const auto& c : WIFI_CREDENTIALS)
        wifiMulti.addAP(c.ssid, c.password);

    xTaskCreatePinnedToCore(wifiManagerTask, "wifiMgr", 4096, NULL, 1, NULL, 0);

    if (esp_now_init() == ESP_OK)
        esp_now_register_recv_cb(onReceive);
}

bool isWifiConnected() {
    return WiFi.status() == WL_CONNECTED;
}
