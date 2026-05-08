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

// Read raw bytes from UART into dst, return how many bytes received within timeoutMs.
static uint16_t readRaw(uint8_t* dst, uint16_t maxLen, uint32_t timeoutMs) {
    uint16_t n = 0;
    uint32_t deadline = millis() + timeoutMs;
    while (millis() < deadline && n < maxLen) {
        if (RFIDSerial.available()) {
            dst[n++] = (uint8_t)RFIDSerial.read();
        } else {
            vTaskDelay(pdMS_TO_TICKS(2));
        }
    }
    return n;
}

// Response frame layout (INVENTORYISO_CONTINUE / 0x0001):
//  [0]     HEAD   = 0xCF
//  [1]     ADDR   = 0x00
//  [2-3]   CMD    = 0x0001
//  [4]     LEN    = STATUS + payload length
//  [5]     STATUS (0x00 = tag, 0x12 = done/no tag)
//  [6-7]   RSSI   (signed 0.1 dBm)
//  [8]     Antenna
//  [9]     Channel
//  [10]    EPC_LEN
//  [11..10+EPC_LEN]  EPC bytes
//  [end-1, end]      CRC16
static bool parseTagPacket(const uint8_t* buf, uint8_t len,
                           uint8_t* epcOut, uint8_t& epcLen, int16_t& rssi) {
    if (len < 12)        return false;
    if (buf[0] != 0xCF)  return false;
    if (buf[5] != 0x00)  return false;  // 0x00 = tag found

    rssi   = (int16_t)((buf[6] << 8) | buf[7]);
    // buf[8] = Antenna, buf[9] = Channel
    epcLen = buf[10];
    if (epcLen == 0 || epcLen > 32)          return false;
    if (11 + (int)epcLen + 2 > (int)len)     return false;
    memcpy(epcOut, buf + 11, epcLen);
    return true;
}

// ─── Init ─────────────────────────────────────────────────────────────────────

void initCFMU910() {
    pinMode(CFMU910_EN_PIN, OUTPUT);
    digitalWrite(CFMU910_EN_PIN, LOW);
    vTaskDelay(pdMS_TO_TICKS(50));
    digitalWrite(CFMU910_EN_PIN, HIGH);
    Serial.println("[RFID] EN=HIGH, waiting for boot...");

    RFIDSerial.begin(CFMU910_BAUD, SERIAL_8N1, CFMU910_RX_PIN, CFMU910_TX_PIN);

    // Wait for the module boot banner ("ConfigReader OK\n") up to 8 s.
    String acc = "";
    uint32_t deadline = millis() + 8000;
    bool booted = false;
    while (millis() < deadline) {
        if (RFIDSerial.available()) {
            char c = (char)RFIDSerial.read();
            acc += c;
            if (acc.endsWith("ConfigReader OK\n")) { booted = true; break; }
        } else {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }
    if (booted)
        Serial.println("[RFID] Boot banner OK");
    else
        Serial.printf("[RFID] Boot banner timeout (got %d bytes: %s)\n",
                      acc.length(), acc.c_str());

    // MODULE_INIT — stop any ongoing action, get a clean state.
    sendCmd(0xFF, 0x0050, nullptr, 0);
    vTaskDelay(pdMS_TO_TICKS(200));

    // Read and print MODULE_INIT response for diagnostics.
    uint8_t resp[16] = {};
    uint16_t rn = readRaw(resp, sizeof(resp), 300);
    if (rn >= 6) {
        Serial.printf("[RFID] MODULE_INIT resp: status=0x%02X (%s)\n",
                      resp[5], resp[5] == 0 ? "OK" : "ERR");
    } else {
        Serial.printf("[RFID] MODULE_INIT: no response (%u bytes)\n", rn);
    }

    // GET_INFO — read hardware/firmware version.
    sendCmd(0xFF, 0x0051, nullptr, 0);
    vTaskDelay(pdMS_TO_TICKS(300));
    uint8_t info[100] = {};
    uint16_t in = readRaw(info, sizeof(info), 400);
    if (in >= 7 && info[5] == 0x00) {
        // HardVer at [6], FirmVer at [6+32], both null-terminated ASCII
        Serial.printf("[RFID] HW: %s  FW: %s\n",
                      (char*)(info + 6), (char*)(info + 6 + 32));
    } else {
        Serial.printf("[RFID] GET_INFO: no/bad response (%u bytes)\n", in);
    }

    // Flush any remaining bytes.
    while (RFIDSerial.available()) RFIDSerial.read();
    Serial.println("[RFID] CF-MU910 init done");
}

// ─── Single blocking scan ─────────────────────────────────────────────────────
// Sends INVENTORY_CONTINUE (InvType=time, 2 s), collects responses, returns
// the tag with the strongest RSSI. Returns false if no tag found.

bool cfmu910Scan(char* epcHex, uint8_t bufLen, int16_t* rssiOut) {
    // InvType=0x00 (time), InvParam = 2 seconds (big-endian 4 bytes)
    const uint8_t invData[] = {0x00, 0x00, 0x00, 0x00, 0x02};
    sendCmd(0xFF, 0x0001, invData, sizeof(invData));

    static uint8_t stream[1024];
    uint16_t streamLen = 0;
    uint32_t deadline = millis() + 3000;
    while (millis() < deadline && streamLen < sizeof(stream) - 1) {
        if (RFIDSerial.available()) {
            stream[streamLen++] = (uint8_t)RFIDSerial.read();
        } else {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }

    uint8_t bestEpc[32] = {};
    uint8_t bestEpcLen  = 0;
    int16_t bestRssi    = INT16_MIN;
    bool    found       = false;

    for (int i = 0; i < (int)streamLen - 6; i++) {
        if (stream[i] != 0xCF) continue;

        // Minimal sanity: HEAD + ADDR + CMD(2) + LEN
        if (i + 4 >= streamLen) break;
        uint8_t pktLen    = stream[i + 4];
        int     totalSize = 7 + pktLen;  // HEAD+ADDR+CMD(2)+LEN + data + CRC(2)
        if (i + totalSize > (int)streamLen) break;

        uint8_t epc[32];
        uint8_t epcLen = 0;
        int16_t rssi   = 0;

        if (parseTagPacket(stream + i, (uint8_t)totalSize, epc, epcLen, rssi)) {
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
    uint8_t n = (bestEpcLen < maxBytes) ? bestEpcLen : maxBytes;
    for (uint8_t i = 0; i < n; i++)
        snprintf(epcHex + i * 2, 3, "%02X", bestEpc[i]);
    epcHex[n * 2] = '\0';

    if (rssiOut) *rssiOut = bestRssi;
    return true;
}

// ─── Diagnostic raw scan (prints hex dump to Serial) ─────────────────────────

void cfmu910Diag() {
    Serial.println("[RFID-DIAG] Sending INVENTORY for 2 s...");
    const uint8_t invData[] = {0x00, 0x00, 0x00, 0x00, 0x02};
    sendCmd(0xFF, 0x0001, invData, sizeof(invData));

    uint8_t stream[512] = {};
    uint16_t n = readRaw(stream, sizeof(stream), 3000);

    Serial.printf("[RFID-DIAG] Received %u bytes:\n", n);
    for (uint16_t i = 0; i < n; i++) {
        Serial.printf("%02X ", stream[i]);
        if ((i + 1) % 16 == 0) Serial.println();
    }
    if (n % 16 != 0) Serial.println();

    // Try to parse tags from the dump.
    int tags = 0;
    for (int i = 0; i < (int)n - 6; i++) {
        if (stream[i] != 0xCF) continue;
        if (i + 4 >= n) break;
        uint8_t pktLen    = stream[i + 4];
        int     totalSize = 7 + pktLen;
        if (i + totalSize > (int)n) break;

        uint8_t epc[32]; uint8_t epcLen = 0; int16_t rssi = 0;
        if (parseTagPacket(stream + i, (uint8_t)totalSize, epc, epcLen, rssi)) {
            char hex[65] = {};
            for (uint8_t j = 0; j < epcLen && j < 32; j++)
                snprintf(hex + j * 2, 3, "%02X", epc[j]);
            Serial.printf("[RFID-DIAG] Tag #%d: EPC=%s  RSSI=%.1f dBm\n",
                          ++tags, hex, rssi / 10.0f);
        }
        i += totalSize - 1;
    }
    if (tags == 0) Serial.println("[RFID-DIAG] No tags parsed.");
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
                xSemaphoreTake(s_mutex, portMAX_DELAY);
                s_streak = 0;
                s_streakEpc[0] = '\0';
                xSemaphoreGive(s_mutex);
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        vTaskDelay(pdMS_TO_TICKS(50));
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
