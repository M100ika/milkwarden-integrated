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
    httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    t_httpUpdate_return ret = httpUpdate.update(client, OTA_FIRMWARE_URL);

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
