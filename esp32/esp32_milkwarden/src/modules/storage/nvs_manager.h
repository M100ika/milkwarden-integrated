#pragma once

#include <stdint.h>

void loadSettings();
void saveSettings();
void loadConfigFile();

// Device ID stored in NVS. Returns 0 if not set. Configure via telnet: setid <1-4>
uint8_t getDeviceId();
void    setDeviceId(uint8_t id);
