#include <Arduino.h>
#include "config.h"
#include "devices/wifi/wifi.h"
#include "devices/telnet/telnet.h"
#include "devices/nextion/nextion.h"
#include "modules/gateway/gateway.h"

static void initAllSystems() {
    initNextion();              // Nextion on Serial (GPIO1/3) at 9600
    initComms();                // WIFI_STA + ESP-NOW recv → gatewayForward()
    initTelnet();               // Telnet CLI port 23 — only debug channel
}

void setup() {
    initAllSystems();
}

void loop() {
    telnet.loop();
}
