#pragma once

#include "config.h"

void     initEspNow();
bool     espnowSendSnapshot(const SnapshotPacket& pkt);
bool     espnowSendSession(const SessionPacket& pkt);
bool     espnowSendCmd(const CmdPacket& pkt);

uint16_t getDistanceInitialMm();
uint16_t getDistanceFinalMm();
bool     isFinalDistanceReady();
void     clearFinalDistanceReady();
