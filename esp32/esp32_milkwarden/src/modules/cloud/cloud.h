#pragma once

#include "config.h"

int sendToCloud(const SessionPacket& pkt);
const char* cloudLastBody();
