#include "freeRTOS_tasks.h"
#include "devices/loadcell/loadcell.h"

// WiFi stack runs on Core 0; HX711 task runs on Core 1 to avoid bit-bang interference.
void startAllTasks() {
    xTaskCreatePinnedToCore(
        hx711Task,         // task function
        "hx711Task",       // name
        2048,              // stack (bytes)
        NULL,              // parameter
        2,                 // priority (> loop=1, < WiFi=19)
        &hx711TaskHandle,  // handle for pause/resume
        1                  // Core 1
    );
    // broadcastIpTask is created inside initWiFi() once WiFi connects
}
