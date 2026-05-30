#pragma once

#include <stdint.h>
#include <stdbool.h>

void    initCFMU910();
void    startRfidTask();

// Single blocking scan (~3 s). Returns true if tag found.
// epcHex: null-terminated hex string; rssiOut: 0.1 dBm units.
bool    cfmu910Scan(char* epcHex, uint8_t bufLen, int16_t* rssiOut);

// Diagnostic: sends inventory, dumps raw bytes + parsed tags.
// printFn receives one formatted line at a time. If nullptr, outputs to Serial.
void    cfmu910Diag(void (*printFn)(const char*) = nullptr);

// Power control (10–33 dBm). cfmu910SetPower also saves to NVS.
bool    cfmu910SetPower(uint8_t dBm);
uint8_t cfmu910GetPower();   // returns current cached value (0 if unknown)

// Returns true when the most-frequent tag from the batch scan is confirmed.
bool    getRfidConfirmed(char* epcOut, uint8_t bufLen, int16_t* rssiOut);

// Atomic read of both confirmed and batchDone state — use instead of calling
// getRfidConfirmed + rfidBatchDone separately to avoid a race condition.
bool    getRfidResult(char* epcOut, uint8_t bufLen, int16_t* rssiOut, bool* batchDoneOut);

// Reset batch counters and confirmed state.
void    resetRfidConfirmation();

// Returns true when batch of RFID_SCAN_COUNT scans is complete (confirmed or no tag).
bool    rfidBatchDone();

// Pause/resume background scan task.
void    rfidTaskPause();
void    rfidTaskResume();
