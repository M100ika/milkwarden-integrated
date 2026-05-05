#pragma once

#include <stdint.h>
#include <stdbool.h>

void    initCFMU910();
void    startRfidTask();           // launch background scanning task

// Single blocking scan (~3 s). Returns true if tag found.
// epcHex: null-terminated hex string; rssiOut: 0.1 dBm units.
bool    cfmu910Scan(char* epcHex, uint8_t bufLen, int16_t* rssiOut);

// Returns true when RFID_CONFIRM_COUNT identical scans accumulated.
// Safe to call from any task.
bool    getRfidConfirmed(char* epcOut, uint8_t bufLen, int16_t* rssiOut);

// Reset streak counter and confirmed state (call on new cow / bucket change).
void    resetRfidConfirmation();

// Pause/resume background scan task (use before manual Telnet scans).
void    rfidTaskPause();
void    rfidTaskResume();
