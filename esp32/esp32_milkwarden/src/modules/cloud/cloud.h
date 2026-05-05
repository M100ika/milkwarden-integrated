#pragma once

#include "config.h"

// Sends pkt to CLOUD_ENDPOINT_URL via HTTPS POST.
// Returns true on HTTP 2xx. Hard timeout: CLOUD_POST_TIMEOUT_MS.
// Always returns false immediately if WiFi is not connected.
bool sendToCloud(const DataPacket& pkt);
