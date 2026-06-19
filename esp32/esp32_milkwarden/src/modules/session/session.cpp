#include "session.h"
#include "config.h"
#include "devices/beam/beam.h"
#include "devices/cfmu910/cfmu910.h"
#include "devices/espnow/espnow.h"
#include "devices/loadcell/loadcell.h"
#include "modules/cloud/cloud.h"
#include "modules/ntp/ntp.h"
#include "modules/tlog/tlog.h"
#include "modules/storage/nvs_manager.h"
#include "devices/valve/valve.h"
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
    pkt.esp_id       = getDeviceId();
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
    pkt.esp_id         = getDeviceId();
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
    tlog("[Session] SessionPacket sent: rfid=%s wi=%.0f wf=%.0f reason=%u espnow=%d cloud=%d",
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
    tlog("[Session] Init OK");
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
    uint32_t     msStart     = 0;
    uint32_t     msRfidStart = 0;
    bool         rfidStarted = false;
    uint8_t      lastMsgState  = MSG_STATE_OK;
    uint32_t     msValveTrigger = 0;
    bool         valveArmed    = false;
    bool         valveOpen     = false;
    uint32_t     msValveOpened = 0;
    bool         sensorInitSent = false;

    for (;;) {
        // ── Force reset from Telnet ───────────────────────────────────────────
        if (s_forceReset) {
            state         = SESSION_IDLE;
            memset(rfid, 0, sizeof(rfid));
            wInitial      = 0;
            startTime     = 0;
            s_forceReset  = false;
            closeValve(); valveArmed = false; valveOpen = false;
            resetRfidConfirmation();
            rfidTaskPause();
            tlog("[Session] Force reset → IDLE");
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
                msRfidStart   = ms;
                rfidStarted   = false;
                sensorInitSent = false;
                state = SESSION_COW_PRESENT;
                tlog("[Session] Cow detected → COW_PRESENT (RFID starts in %us)", RFID_START_DELAY_MS / 1000);
            }
            break;

        case SESSION_COW_PRESENT:
            if (beam == 1) {
                if (rfidStarted) rfidTaskPause();
                rfidStarted    = false;
                sensorInitSent = false;
                state = SESSION_IDLE;
                tlog("[Session] Beam lost → IDLE");
                break;
            }
            if (!rfidStarted && ms - msRfidStart >= RFID_START_DELAY_MS) {
                rfidStarted = true;
                rfidTaskResume();
                tlog("[Session] RFID scan started (%u reads)", RFID_SCAN_COUNT);
                if (!sensorInitSent) {
                    CmdPacket sCmd = {};
                    sCmd.type             = PKT_TYPE_CMD;
                    sCmd.target_sensor_id = SENSOR_ID;
                    sCmd.cmd              = CMD_MEASURE_INIT;
                    espnowSendCmd(sCmd);
                    sensorInitSent = true;
                    tlog("[Session] Sensor CMD_MEASURE_INIT sent");
                }
            }
            if (rfidStarted) {
                bool done      = false;
                bool confirmed = getRfidResult(rfid, sizeof(rfid), &rfidRssi, &done);
                if (confirmed || done) {
                    rfidTaskPause();
                    rfidStarted = false;
                    if (!confirmed) {
                        memset(rfid, 0, sizeof(rfid));
                        tlog("[Session] RFID: no tag → MILKING without tag");
                    } else {
                        tlog("[Session] RFID OK: %s → MILKING", rfid);
                    }
                    wInitial    = w;
                    startTime   = now;
                    msStart     = ms;
                    wDropCheck     = w;
                    msDropCheck    = ms;
                    msValveTrigger = ms;
                    valveArmed     = true;
                    valveOpen      = false;
                    state = SESSION_MILKING;
                }
            }
            break;

        case SESSION_MILKING:
            // Valve: open after delay, close after duration
            if (valveArmed && !valveOpen && ms - msValveTrigger >= valveOpenDelayMs) {
                openValve();
                valveOpen     = true;
                msValveOpened = ms;
                tlog("[Valve] Opened");
            }
            if (valveOpen && ms - msValveOpened >= valveOpenDurationMs) {
                closeValve();
                valveOpen  = false;
                valveArmed = false;
                tlog("[Valve] Closed");
            }

            // Weight drop detection
            if (ms - msDropCheck >= WEIGHT_DROP_WINDOW_MS) {
                if (wDropCheck - w > WEIGHT_DROP_G) {
                    tlog("[Session] Weight drop %.0f→%.0f g → BUCKET CHANGE",
                                  wDropCheck, w);
                    publishSession(rfid, wInitial, wDropCheck, startTime,
                                   startTime + (ms - msStart) / 1000,
                                   END_REASON_BUCKET_CHANGE);
                    resetRfidConfirmation();
                    memset(rfid, 0, sizeof(rfid));
                    msRfidStart   = ms;
                    rfidStarted   = false;
                    sensorInitSent = false;   // new bucket → measure initial level again
                    wInitial    = 0;
                    startTime   = now;
                    msStart     = ms;
                    wDropCheck  = w;
                    msDropCheck = ms;
                    closeValve(); valveArmed = false; valveOpen = false;
                    state = SESSION_COW_PRESENT;
                    break;
                }
                wDropCheck  = w;
                msDropCheck = ms;
            }

            // Cow left
            if (beam == 1) {
                tlog("[Session] Beam open → SESSION END");
                closeValve(); valveArmed = false; valveOpen = false;

                // Request final distance measurement, wait up to SENSOR_CMD_TIMEOUT_MS
                {
                    CmdPacket sCmd = {};
                    sCmd.type             = PKT_TYPE_CMD;
                    sCmd.target_sensor_id = SENSOR_ID;
                    sCmd.cmd              = CMD_MEASURE_FINAL;
                    clearFinalDistanceReady();
                    espnowSendCmd(sCmd);
                    tlog("[Session] Sensor CMD_MEASURE_FINAL sent, waiting %us...",
                         SENSOR_CMD_TIMEOUT_MS / 1000);
                    uint32_t tWait = millis();
                    while (!isFinalDistanceReady() &&
                           millis() - tWait < SENSOR_CMD_TIMEOUT_MS) {
                        vTaskDelay(pdMS_TO_TICKS(200));
                    }
                    tlog("[Session] Sensor final: %s (dist=%u mm)",
                         isFinalDistanceReady() ? "OK" : "TIMEOUT",
                         getDistanceFinalMm());
                }
                sensorInitSent = false;

                publishSession(rfid, wInitial, w, startTime,
                               startTime + (ms - msStart) / 1000,
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
