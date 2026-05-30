#include "ota.h"
#include "config.h"
#include "devices/telnet/telnet.h"
#include <WiFiClientSecure.h>
#include <HTTPUpdate.h>

void startOTA() {
    telnet.println("\n[OTA] Connecting to GitHub...");

    WiFiClientSecure client;
    client.setInsecure();
    client.setHandshakeTimeout(30);

    httpUpdate.rebootOnUpdate(false);
    httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

    httpUpdate.onProgress([](int current, int total) {
        if (total <= 0) return;
        int pct    = (current * 100) / total;
        int filled = pct / 5;
        char bar[21] = {};
        for (int i = 0; i < 20; i++) {
            if      (i < filled)  bar[i] = '=';
            else if (i == filled) bar[i] = '>';
            else                  bar[i] = ' ';
        }
        telnet.printf("\r[OTA] [%s] %3d%%  %d / %d B", bar, pct, current, total);
    });

    t_httpUpdate_return ret = httpUpdate.update(client, OTA_FIRMWARE_URL);

    telnet.println();
    switch (ret) {
        case HTTP_UPDATE_FAILED:
            telnet.printf("[OTA] Failed! Error (%d): %s\n",
                          httpUpdate.getLastError(),
                          httpUpdate.getLastErrorString().c_str());
            break;
        case HTTP_UPDATE_NO_UPDATES:
            telnet.println("[OTA] No updates available.");
            break;
        case HTTP_UPDATE_OK:
            telnet.println("[OTA] Success! Rebooting in 2 seconds...");
            delay(2000);
            ESP.restart();
            break;
    }
}
