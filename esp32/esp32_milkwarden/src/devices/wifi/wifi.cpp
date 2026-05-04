#include "wifi.h"
#include "config.h"
#include <WiFi.h>
#include <WiFiUdp.h>

void initWiFi() {
    pinMode(WIFI_LED_PIN, OUTPUT);

    WiFi.disconnect(true);
    delay(200);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    Serial.print("[WiFi] Connecting");
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < WIFI_MAX_ATTEMPTS) {
        delay(500);
        Serial.print(".");
        attempts++;
        if (attempts == 10) WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n[WiFi] IP: " + WiFi.localIP().toString());
        xTaskCreatePinnedToCore(broadcastIpTask, "IpTask", 2048, NULL, 1, NULL, 0);
        digitalWrite(WIFI_LED_PIN, HIGH);
    } else {
        Serial.println("\n[WiFi] Connection failed. Retrying in background.");
    }
}

// Broadcasts the station IP over UDP every 30 s so clients can discover it.
void broadcastIpTask(void* pv) {
    WiFiUDP udp;
    for (;;) {
        if (WiFi.status() == WL_CONNECTED) {
            udp.beginPacket("255.255.255.255", IP_BROADCAST_PORT);
            udp.printf("MILKA_STATION:%s", WiFi.localIP().toString().c_str());
            udp.endPacket();
        }
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}
