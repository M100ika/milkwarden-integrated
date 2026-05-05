#pragma once

#include "config.h"

void initEspNow();
bool espnowSendSnapshot(const SnapshotPacket& pkt);
bool espnowSendSession(const SessionPacket& pkt);
