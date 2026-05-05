#pragma once

#include "config.h"

// Sends a completed session to CLOUD_ENDPOINT_URL via HTTPS POST.
// Returns true on HTTP 2xx. Always returns false if WiFi is not connected.
bool sendToCloud(const SessionPacket& pkt);
