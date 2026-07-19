#pragma once

#include "config.h"

int sendToCloud(const SessionPacket& pkt, const WeightSample* log, int logCount);
const char* cloudLastBody();
