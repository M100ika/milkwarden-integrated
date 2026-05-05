#include <Arduino.h>
#include "config.h"
#include "devices/loadcell/loadcell.h"
#include "devices/wifi/wifi.h"
#include "devices/beam/beam.h"
#include "devices/cfmu910/cfmu910.h"
#include "devices/espnow/espnow.h"
#include "devices/telnet/telnet.h"
#include "modules/storage/nvs_manager.h"
#include "modules/heartbeat/heartbeat.h"
#include "modules/ntp/ntp.h"
#include "modules/session/session.h"
#include "modules/cloud/cloud.h"
#include "tasks/freeRTOS_tasks.h"

static void initAllSystems() {
    Serial.begin(115200);
    loadSettings();    // restore calibration & auto-zero params from NVS
    loadConfigFile();  // optional override from LittleFS /config.ini
    initLoadCell();    // HX711 begin + tare (or restore offset from NVS)
    initComms();       // WIFI_AP_STA + WiFiMulti task + channel event handlers
    initNTP();         // register NTP server; syncs automatically once WiFi connects
    initBeam();        // beam break sensor on GPIO13
    initCFMU910();     // CF-MU910 RFID reader on UART2 (waits for module boot)
    initEspNow();      // ESP-NOW init + register Master peer
    initSession();     // session state machine shared state
    startAllTasks();   // hx711Task (Core1) + sessionTask + rfidBgTask (Core0)
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
