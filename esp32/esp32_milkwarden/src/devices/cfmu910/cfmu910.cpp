#include "cfmu910.h"
#include "config.h"
#include <HardwareSerial.h>
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string.h>

static HardwareSerial RFIDSerial(2);

// ─── Protocol helpers ─────────────────────────────────────────────────────────

static uint16_t crc16(const uint8_t* buf, uint8_t len) {
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int j = 0; j < 8; j++)
            crc = (crc & 1) ? (crc >> 1) ^ 0x8408 : (crc >> 1);
    }
    return crc;
}

static void sendCmd(uint8_t addr, uint16_t cmd, const uint8_t* data, uint8_t dataLen) {
    uint8_t buf[64];
    uint8_t idx = 0;
    buf[idx++] = 0xCF;
    buf[idx++] = addr;
    buf[idx++] = cmd >> 8;
    buf[idx++] = cmd & 0xFF;
    buf[idx++] = dataLen;
    for (int i = 0; i < dataLen; i++) buf[idx++] = data[i];
    uint16_t c = crc16(buf, idx);
    buf[idx++] = c >> 8;
    buf[idx++] = c & 0xFF;
    RFIDSerial.write(buf, idx);
}

// Format: CF ADDR CMD(2) LEN STATUS [RSSI(2) ANT EPC_LEN EPC(...)] CRC(2)
static bool parsePacket(const uint8_t* buf, uint8_t len,
                        uint8_t* epcOut, uint8_t& epcLen, int16_t& rssi) {
    if (len < 8)        return false;
    if (buf[0] != 0xCF) return false;
    if (buf[5] != 0x00) return false;   // status 0x00 = tag present

    rssi   = (int16_t)((buf[6] << 8) | buf[7]);
    epcLen = buf[9];
    if (10 + epcLen + 2 > len) return false;
    memcpy(epcOut, buf + 10, epcLen);
    return true;
}

// ─── Init ─────────────────────────────────────────────────────────────────────

void initCFMU910() {
    RFIDSerial.begin(CFMU910_BAUD, SERIAL_8N1, CFMU910_RX_PIN, CFMU910_TX_PIN);

    String acc = "";
    uint32_t t = millis();
    while (millis() - t < 8000) {
        if (RFIDSerial.available()) {
            char c = RFIDSerial.read();
            acc += c;
            t = millis();
            if (acc.endsWith("ConfigReader OK\n")) break;
        } else {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }
    sendCmd(0xFF, 0x0050, nullptr, 0);
    delay(500);
    while (RFIDSerial.available()) RFIDSerial.read();

    Serial.println("[RFID] CF-MU910 init OK");
}

// ─── Single blocking scan ─────────────────────────────────────────────────────

bool cfmu910Scan(char* epcHex, uint8_t bufLen, int16_t* rssiOut) {
    const uint8_t invData[] = {0x00, 0x00, 0x00, 0x00, 0x02};
    sendCmd(0xFF, 0x0001, invData, sizeof(invData));

    static uint8_t stream[1024];
    uint16_t streamLen = 0;
    uint32_t t = millis();
    while (millis() - t < 3000) {
        if (RFIDSerial.available()) {
            stream[streamLen++] = RFIDSerial.read();
            t = millis();
            if (streamLen >= sizeof(stream) - 1) break;
        } else {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }

    uint8_t bestEpc[32] = {};
    uint8_t bestEpcLen  = 0;
    int16_t bestRssi    = INT16_MIN;
    bool    found       = false;

    for (int i = 0; i < (int)streamLen - 1; i++) {
        if (stream[i] != 0xCF || stream[i + 1] != 0x00) continue;
        if (i + 5 >= streamLen) break;

        uint8_t pktLen    = stream[i + 4];
        uint8_t totalSize = 1 + 1 + 2 + 1 + pktLen + 2;
        if (i + totalSize > streamLen) break;

        uint8_t epc[32];
        uint8_t epcLen = 0;
        int16_t rssi   = 0;

        if (parsePacket(stream + i, totalSize, epc, epcLen, rssi)) {
            if (!found || rssi > bestRssi) {
                bestRssi   = rssi;
                bestEpcLen = epcLen;
                memcpy(bestEpc, epc, epcLen);
            }
            found = true;
        }
        i += totalSize - 1;
    }

    if (!found) return false;

    uint8_t maxBytes = (bufLen - 1) / 2;
    uint8_t n        = bestEpcLen < maxBytes ? bestEpcLen : maxBytes;
    for (uint8_t i = 0; i < n; i++)
        snprintf(epcHex + i * 2, 3, "%02X", bestEpc[i]);
    epcHex[n * 2] = '\0';

    if (rssiOut) *rssiOut = bestRssi;
    return true;
}

// ─── Background confirmation task ─────────────────────────────────────────────

static SemaphoreHandle_t  s_mutex;
static volatile bool      s_paused    = false;
static volatile bool      s_confirmed = false;
static volatile int16_t   s_rssi      = 0;
static char               s_confirmedEpc[25] = {};
static char               s_streakEpc[25]    = {};
static int                s_streak           = 0;

static void rfidBgTask(void* pv) {
    char    epc[25];
    int16_t rssi;

    for (;;) {
        if (!s_paused) {
            if (cfmu910Scan(epc, sizeof(epc), &rssi)) {
                xSemaphoreTake(s_mutex, portMAX_DELAY);
                if (strcmp(epc, s_streakEpc) == 0) {
                    s_streak++;
                } else {
                    s_streak = 1;
                    strncpy(s_streakEpc, epc, sizeof(s_streakEpc) - 1);
                }
                if (!s_confirmed && s_streak >= RFID_CONFIRM_COUNT) {
                    s_confirmed = true;
                    s_rssi      = rssi;
                    strncpy(s_confirmedEpc, epc, sizeof(s_confirmedEpc) - 1);
                    Serial.printf("[RFID] Tag confirmed (%d): %s  RSSI=%.1f dBm\n",
                                  RFID_CONFIRM_COUNT, epc, rssi / 10.0f);
                }
                xSemaphoreGive(s_mutex);
            } else {
                // No tag: reset streak to avoid confirming across gaps
                xSemaphoreTake(s_mutex, portMAX_DELAY);
                s_streak = 0;
                s_streakEpc[0] = '\0';
                xSemaphoreGive(s_mutex);
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

void startRfidTask() {
    s_mutex = xSemaphoreCreateMutex();
    xTaskCreatePinnedToCore(rfidBgTask, "rfidBg", 4096, NULL, 1, NULL, 0);
    Serial.println("[RFID] Background task started");
}

bool getRfidConfirmed(char* epcOut, uint8_t bufLen, int16_t* rssiOut) {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool ok = s_confirmed;
    if (ok) {
        strncpy(epcOut, s_confirmedEpc, bufLen - 1);
        epcOut[bufLen - 1] = '\0';
        if (rssiOut) *rssiOut = s_rssi;
    }
    xSemaphoreGive(s_mutex);
    return ok;
}

void resetRfidConfirmation() {
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_confirmed    = false;
    s_streak       = 0;
    s_streakEpc[0] = '\0';
    s_confirmedEpc[0] = '\0';
    xSemaphoreGive(s_mutex);
}

void rfidTaskPause()  { s_paused = true; }
void rfidTaskResume() { s_paused = false; }
