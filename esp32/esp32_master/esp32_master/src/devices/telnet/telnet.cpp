#include "telnet.h"
#include "config.h"
#include "devices/wifi/wifi.h"
#include "modules/gateway/gateway.h"
#include <WiFi.h>
#include <Arduino.h>

ESPTelnet telnet;

static const char* stateStr(uint8_t s) {
    switch (s) {
        case DEV_STATE_IDLE:        return "IDLE";
        case DEV_STATE_COW_PRESENT: return "COW_PRESENT";
        case DEV_STATE_MILKING:     return "MILKING";
        default:                    return "?";
    }
}

static void handleCommand(String str) {
    str.trim();

    if (str == "status") {
        telnet.printf("[Master] Firmware  : %s\n", FIRMWARE_VERSION);
        telnet.printf("[Master] Uptime    : %lu s\n", millis() / 1000UL);
        telnet.printf("[WiFi]   Status    : %s\n", isWifiConnected() ? "CONNECTED" : "DISCONNECTED");
        if (isWifiConnected()) {
            telnet.printf("[WiFi]   SSID      : %s\n", WiFi.SSID().c_str());
            telnet.printf("[WiFi]   IP        : %s\n", WiFi.localIP().toString().c_str());
            telnet.printf("[WiFi]   Channel   : %d\n", WiFi.channel());
            telnet.printf("[WiFi]   RSSI      : %d dBm\n", WiFi.RSSI());
        }
        GatewayStats st = gatewayGetStats();
        telnet.println("[Stats]  Slave  snap  session");
        for (uint8_t i = 0; i < SLAVE_COUNT; i++)
            telnet.printf("         %u      %-5lu %-5lu\n",
                          (unsigned)(i + 1),
                          (unsigned long)st.snap[i],
                          (unsigned long)st.session[i]);

    } else if (str == "stats") {
        GatewayStats st = gatewayGetStats();
        telnet.println("Slave  Snapshots  Sessions");
        for (uint8_t i = 0; i < SLAVE_COUNT; i++)
            telnet.printf("  %u    %-9lu %-9lu\n",
                          (unsigned)(i + 1),
                          (unsigned long)st.snap[i],
                          (unsigned long)st.session[i]);

    } else if (str.startsWith("last ")) {
        uint8_t id = (uint8_t)str.substring(5).toInt();
        SnapshotPacket p;
        if (!gatewayGetLastSnap(id, &p)) {
            telnet.printf("[last] No data for slave %u yet.\n", (unsigned)id);
        } else {
            telnet.printf("[Slave %u] state=%-11s  weight=%.1f g\n",
                          (unsigned)id, stateStr(p.device_state), (double)p.weight);
            telnet.printf("          rfid=%s  beam=%s  ts=%lu\n",
                          p.rfid_tag[0] ? p.rfid_tag : "(none)",
                          p.beam_state ? "INTERRUPTED" : "OK",
                          (unsigned long)p.timestamp);
        }

    } else if (str == "reboot") {
        telnet.println("[System] Rebooting...");
        delay(300);
        ESP.restart();

    } else if (str == "help" || str == "") {
        telnet.println("status        — uptime, WiFi, packet counts");
        telnet.println("stats         — snap/session counters per slave");
        telnet.println("last <1-4>    — last snapshot from slave N");
        telnet.println("reboot        — restart");

    } else {
        telnet.println("[Unknown] " + str);
    }

    telnet.print("> ");
}

void initTelnet() {
    telnet.onConnect([](String ip) {
        telnet.printf("\n--- Milkwarden Master v%s ---\n", FIRMWARE_VERSION);
        if (isWifiConnected())
            telnet.printf("IP: %s  ch: %d\n",
                          WiFi.localIP().toString().c_str(), WiFi.channel());
        telnet.println("Type 'help'.");
        telnet.print("> ");
    });
    telnet.onInputReceived(handleCommand);
    telnet.begin(TELNET_PORT);
}
