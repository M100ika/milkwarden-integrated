#include "session.h"
#include "config.h"
#include "devices/beam/beam.h"
#include "devices/cfmu910/cfmu910.h"
#include "devices/espnow/espnow.h"
#include "devices/loadcell/loadcell.h"
#include "modules/cloud/cloud.h"
#include "modules/ntp/ntp.h"
#include <Arduino.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string.h>

// ─── Shared info (read by Telnet via getSessionInfo) ──────────────────────────

static SemaphoreHandle_t s_infoMutex;
static SessionInfo       s_info = {};
static volatile bool     s_forceReset = false;

// ─── Helpers ──────────────────────────────────────────────────────────────────

static uint32_t getIP() {
    return (uint32_t)WiFi.localIP();
}

static void publishSnapshot(SessionState state, const char* rfid,
                             uint8_t beam, float weight,
                             uint8_t& lastMsgState) {
    SnapshotPacket pkt = {};
    pkt.type         = PKT_TYPE_SNAPSHOT;
    pkt.esp_id       = ESP_DEVICE_ID;
    pkt.ip_addr      = getIP();
    strncpy(pkt.rfid_tag, rfid, sizeof(pkt.rfid_tag) - 1);
    
    pkt.beam_state   = beam;
    pkt.device_state = (uint8_t)state;
    pkt.weight       = weight;
    pkt.timestamp    = (uint32_t)getUnixTime();
    pkt.msg_state    = lastMsgState;

    bool ok      = espnowSendSnapshot(pkt);
    lastMsgState = ok ? MSG_STATE_OK : MSG_STATE_FAIL;
}

static void publishSession(const char* rfid,
                           float wInit, float wFinal,
                           uint32_t tStart, uint32_t tEnd,
                           uint8_t reason) {
    SessionPacket pkt = {};
    pkt.type           = PKT_TYPE_SESSION;
    pkt.esp_id         = ESP_DEVICE_ID;
    pkt.ip_addr        = getIP();
    strncpy(pkt.rfid_tag, rfid, sizeof(pkt.rfid_tag) - 1);
    pkt.weight_initial = wInit;
    pkt.weight_final   = wFinal;
    pkt.start_time     = tStart;
    pkt.end_time       = tEnd;
    pkt.end_reason     = reason;

    bool ok      = espnowSendSession(pkt);
    pkt.msg_state = ok ? MSG_STATE_OK : MSG_STATE_FAIL;

    int cloudCode = sendToCloud(pkt);
    Serial.printf("[Session] SessionPacket sent: rfid=%s wi=%.0f wf=%.0f reason=%u espnow=%d cloud=%d\n",
                  rfid, wInit, wFinal, reason, ok, cloudCode);
}

static void updateInfo(SessionState state, const char* rfid,
                       float wInit, float wCur, uint32_t tStart) {
    xSemaphoreTake(s_infoMutex, portMAX_DELAY);
    s_info.state          = state;
    strncpy(s_info.rfid, rfid, sizeof(s_info.rfid) - 1);
    s_info.weight_initial = wInit;
    s_info.weight_current = wCur;
    s_info.start_time     = tStart;
    xSemaphoreGive(s_infoMutex);
}

// ─── Public API ───────────────────────────────────────────────────────────────

void initSession() {
    s_infoMutex = xSemaphoreCreateMutex();
    Serial.println("[Session] Init OK");
}

void sessionForceReset() {
    s_forceReset = true;
}

void getSessionInfo(SessionInfo* out) {
    xSemaphoreTake(s_infoMutex, portMAX_DELAY);
    *out = s_info;
    xSemaphoreGive(s_infoMutex);
}

// ─── FreeRTOS task ────────────────────────────────────────────────────────────

void sessionTask(void* pv) {
    SessionState state    = SESSION_IDLE;
    char         rfid[25] = {};
    int16_t      rfidRssi = 0;
    float        wInitial    = 0;
    float        wDropCheck  = 0;
    uint32_t     msDropCheck = 0;
    uint32_t     startTime   = 0;
    uint32_t     msRfidStart = 0;
    uint8_t      lastMsgState = MSG_STATE_OK;

    for (;;) {
        // ── Force reset from Telnet ───────────────────────────────────────────
        if (s_forceReset) {
            state         = SESSION_IDLE;
            memset(rfid, 0, sizeof(rfid));
            wInitial      = 0;
            startTime     = 0;
            s_forceReset  = false;
            resetRfidConfirmation();
            rfidTaskPause();
            Serial.println("[Session] Force reset → IDLE");
        }

        float    w    = getFilteredWeight(num_samples);
        uint8_t  beam = readBeam();
        uint32_t now  = (uint32_t)getUnixTime();
        uint32_t ms   = millis();

        // ── State machine ─────────────────────────────────────────────────────
        switch (state) {

        case SESSION_IDLE:
            if (beam == 0) {
                resetRfidConfirmation();
                memset(rfid, 0, sizeof(rfid));
                msRfidStart = ms;
                rfidTaskResume();
                state = SESSION_COW_PRESENT;
                Serial.println("[Session] Cow detected → COW_PRESENT");
            }
            break;

        case SESSION_COW_PRESENT:
            if (beam == 1) {
                rfidTaskPause();
                state = SESSION_IDLE;
                Serial.println("[Session] Beam lost before RFID → IDLE");
                break;
            }
            {
                bool confirmed = getRfidConfirmed(rfid, sizeof(rfid), &rfidRssi);
                bool timeout   = (ms - msRfidStart) >= RFID_CONFIRM_TIMEOUT_MS;

                if (confirmed || timeout) {
                    rfidTaskPause();
                    if (!confirmed) {
                        memset(rfid, 0, sizeof(rfid));
                        Serial.println("[Session] RFID timeout → MILKING without tag");
                    } else {
                        Serial.printf("[Session] RFID OK: %s → MILKING\n", rfid);
                    }
                    wInitial    = w;
                    startTime   = now;
                    wDropCheck  = w;
                    msDropCheck = ms;
                    state = SESSION_MILKING;
                }
            }
            break;

        case SESSION_MILKING:
            // Weight drop detection
            if (ms - msDropCheck >= WEIGHT_DROP_WINDOW_MS) {
                if (wDropCheck - w > WEIGHT_DROP_G) {
                    Serial.printf("[Session] Weight drop %.0f→%.0f g → BUCKET CHANGE\n",
                                  wDropCheck, w);
                    publishSession(rfid, wInitial, w, startTime, now,
                                   END_REASON_BUCKET_CHANGE);
                    resetRfidConfirmation();
                    memset(rfid, 0, sizeof(rfid));
                    msRfidStart = ms;
                    wInitial    = 0;
                    startTime   = now;
                    wDropCheck  = w;
                    msDropCheck = ms;
                    rfidTaskResume();
                    state = SESSION_COW_PRESENT;
                    break;
                }
                wDropCheck  = w;
                msDropCheck = ms;
            }

            // Cow left
            if (beam == 1) {
                Serial.println("[Session] Beam open → SESSION END");
                publishSession(rfid, wInitial, w, startTime, now,
                               END_REASON_COW_LEFT);
                memset(rfid, 0, sizeof(rfid));
                wInitial  = 0;
                startTime = 0;
                rfidTaskPause();
                state = SESSION_IDLE;
            }
            break;
        }

        // ── Snapshot every 500 ms (always) ───────────────────────────────────
        publishSnapshot(state, rfid, beam, w, lastMsgState);
        updateInfo(state, rfid, wInitial, w, startTime);

        vTaskDelay(pdMS_TO_TICKS(SNAPSHOT_INTERVAL_MS));
    }
}
