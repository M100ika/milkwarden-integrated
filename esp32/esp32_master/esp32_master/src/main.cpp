#include <Arduino.h>
#include "config.h"
#include "devices/wifi/wifi.h"
#include "devices/telnet/telnet.h"
#include "modules/gateway/gateway.h"

static void initAllSystems() {
    Serial.begin(RPI_BAUD);   // UART0 → RPi; do NOT use Serial for debug
    initComms();              // WIFI_STA + ESP-NOW recv → gatewayForward()
    initTelnet();             // Telnet CLI port 23 — only debug channel
}

void setup() {
    initAllSystems();
}

void loop() {
    telnet.loop();
}
