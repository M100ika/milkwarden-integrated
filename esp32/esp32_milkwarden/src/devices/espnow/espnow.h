#pragma once

#include "config.h"

void initEspNow();

// Returns true if esp_now_send() was accepted by the driver.
// Delivery confirmation is async (onSent callback).
bool sendDataPacket(const DataPacket& pkt);
