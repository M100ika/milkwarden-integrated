#include <Arduino.h>
#include "config.h"
#include "devices/loadcell/loadcell.h"
#include "devices/wifi/wifi.h"
#include "devices/espnow/espnow.h"
#include "devices/telnet/telnet.h"
#include "modules/storage/nvs_manager.h"
#include "modules/heartbeat/heartbeat.h"
#include "modules/cloud/cloud.h"
#include "tasks/freeRTOS_tasks.h"

static void initAllSystems() {
    Serial.begin(115200);
    loadSettings();    // restore calibration & auto-zero params from NVS
    loadConfigFile();  // optional override from LittleFS /config.ini
    initLoadCell();    // HX711 begin + tare (or restore offset from NVS)
    initComms();       // WIFI_AP_STA + WiFiMulti task + channel event handlers
    initEspNow();      // ESP-NOW init + register Master peer
    startAllTasks();   // HX711 ring-buffer task on Core 1
    initTelnet();      // Telnet server + CLI command handlers
}

void setup() {
    initAllSystems();
}

void loop() {
    telnet.loop();
    updateAutoZero();
    updateMeasurements();
}
