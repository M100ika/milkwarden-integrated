#include <Arduino.h>
#include "config.h"
#include "devices/loadcell/loadcell.h"
#include "devices/wifi/wifi.h"
#include "devices/telnet/telnet.h"
#include "modules/storage/nvs_manager.h"
#include "modules/heartbeat/heartbeat.h"
#include "tasks/freeRTOS_tasks.h"

static void initAllSystems() {
    Serial.begin(115200);
    loadSettings();    // restore calibration & auto-zero params from NVS
    loadConfigFile();  // optional override from LittleFS /config.ini
    initLoadCell();    // HX711 begin + tare (or restore offset from NVS)
    initWiFi();        // connect, LED, start UDP broadcast task
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
