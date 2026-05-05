#include "freeRTOS_tasks.h"
#include "devices/loadcell/loadcell.h"
#include "devices/cfmu910/cfmu910.h"
#include "modules/session/session.h"

void startAllTasks() {
    // Core 1: HX711 bit-bang task (isolated from WiFi stack on Core 0)
    xTaskCreatePinnedToCore(
        hx711Task, "hx711", 2048, NULL, 2, &hx711TaskHandle, 1);

    // Core 0: session state machine + 500 ms snapshot sender
    xTaskCreatePinnedToCore(
        sessionTask, "session", 4096, NULL, 1, NULL, 0);

    // Core 0: background RFID scanning with confirmation logic
    startRfidTask();
}
