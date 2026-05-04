#include "ota.h"
#include "config.h"
#include "devices/telnet/telnet.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>

void startOTA() {
    telnet.println("\n[OTA] Connecting to GitHub...");

    WiFiClientSecure client;
    client.setInsecure();          // skip SSL cert check (required for GitHub redirects)
    client.setHandshakeTimeout(30);

    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    if (!http.begin(client, OTA_FIRMWARE_URL)) {
        telnet.println("[OTA] Unable to connect to server.");
        return;
    }

    int httpCode = http.GET();
    telnet.printf("[OTA] HTTP check code: %d\n", httpCode);

    if (httpCode != 200) {
        telnet.printf("[OTA] Error: server returned code %d\n", httpCode);
        http.end();
        return;
    }

    telnet.println("[OTA] File found. Starting download...");
    http.end(); // free memory before the update stream

    httpUpdate.rebootOnUpdate(false);
    httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    t_httpUpdate_return ret = httpUpdate.update(client, OTA_FIRMWARE_URL);

    switch (ret) {
        case HTTP_UPDATE_FAILED:
            telnet.printf("[OTA] Update failed! Error (%d): %s\n",
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
