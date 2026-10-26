/*
 * =========================================================================================
 *   SMART BIOSPHERE CABIN - ESP32 FIRMWARE
 *   Cultivation engine for broccoli microgreens, remapped for the SchoolSTEM relay layout
 * =========================================================================================
 *   Target board : ESP32 Dev Module (WROOM-32 / NodeMCU ESP32)
 *   Relay boards : 4-CH + two 2-CH boards (Fan Out + LED, Pump 1 + Pump 2)
 *   Sensors      : DHT11 (air temperature + RH), 2x capacitive soil moisture V2.0
 *
 *   PIN MAP (chosen from the wiring table; alternates are documented per line)
 *   ---------------------------------------------------------------------------------------
 *   4-CH relay IN1  GPIO 25  Peltier 12V element
 *   4-CH relay IN2  GPIO 27  Fan In relay
 *   4-CH relay IN3  GPIO 26  Spare / free actuator
 *   4-CH relay IN4  GPIO 14  HPL grow light (2x top)
 *   2-CH "ujung" IN2 GPIO 27 Fan Out relay (Y-spliced to the same GPIO 27)
 *   2-CH "ujung" IN1 GPIO 32 LED strip / bottom lighting       (alternate: GPIO 12)
 *   2-CH "tengah" IN2 GPIO 19 DC pump 1 / watering zone A
 *   2-CH "tengah" IN1 GPIO 18 DC pump 2 / watering zone B       (alternate: GPIO 17)
 *   Soil moisture 1 GPIO 34  Capacitive V2.0 AOUT (ADC1)
 *   Soil moisture 2 GPIO 35  Capacitive V2.0 AOUT (ADC1)
 *   DHT11 DATA      GPIO 4   Temperature + relative humidity
 *   Onboard LED     GPIO 2   Heartbeat / fault indicator
 *
 *   The firmware is non-blocking (no delay in the control loop), exposes a CORS-enabled
 *   REST API, and serves the standalone dashboard embedded from dashboard_html.h.
 * =========================================================================================
 */

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <Update.h>
#include <DHT.h>
#include <math.h>

#include "dashboard_html.h"

// =========================================================================================
// 1. HARDWARE TOPOLOGY, PIN MAP & CALIBRATION
// =========================================================================================

// --- Wi-Fi credentials ---------------------------------------------------------------
// If the router is unavailable the board starts its own fallback access point:
//   SSID: BroccoliCabin_AP   Password: biosphere123   URL: http://192.168.4.1/
const char* WIFI_SSID     = "Tryout_SA_0509";
const char* WIFI_PASSWORD = "SAujian1234";

#define WIFI_CONNECT_TIMEOUT_MS 15000UL
#define FALLBACK_AP_SSID        "BroccoliCabin_AP"
#define FALLBACK_AP_PASSWORD    "biosphere123"
#define MDNS_HOSTNAME           "biosphere"
#define OTA_PASSWORD            "biosphere123"

// --- Sensors -------------------------------------------------------------------------
#define PIN_DHT11                4    // DHT11 DATA
#define PIN_SOIL1                34   // Capacitive soil 1 AOUT (ADC1, input-only)
#define PIN_SOIL2                35   // Capacitive soil 2 AOUT (ADC1, input-only)
#define DHTTYPE                  DHT11

// --- Relay outputs -------------------------------------------------------------------
// Set RELAY_ACTIVE_LOW to 0 only if your relay board triggers on HIGH.
#define RELAY_ACTIVE_LOW         1

#if RELAY_ACTIVE_LOW
  #define RELAY_ON               LOW
  #define RELAY_OFF              HIGH
#else
  #define RELAY_ON               HIGH
  #define RELAY_OFF              LOW
#endif

// 4-channel relay board
#define PIN_RELAY_PELTIER        25   // IN1
#define PIN_RELAY_FAN            27   // IN2
#define PIN_RELAY_SPARE          26   // IN3 (under the buck converter)
#define PIN_RELAY_GROWLIGHT      14   // IN4

// 2-channel "ujung" relay board
// IN2 is the Fan Out relay and is Y-spliced to the same GPIO 27 as 4-CH IN2.
#define PIN_RELAY_LED_STRIP      32   // IN1 (alternate: 12)

// 2-channel "tengah" relay board
#define PIN_RELAY_PUMP1          19   // IN2
#define PIN_RELAY_PUMP2          18   // IN1 (alternate: 17)

// Status indicator (onboard blue LED on most ESP32 Dev Modules)
#define PIN_STATUS_LED           2

// --- Soil calibration ----------------------------------------------------------------
// Capacitive V2.0 output is inversely proportional to moisture.
// Calibrate each probe individually from the dashboard if needed; these are safe defaults.
#define SOIL1_DRY_RAW            3200
#define SOIL1_WET_RAW            1400
#define SOIL2_DRY_RAW            3200
#define SOIL2_WET_RAW            1400

// --- Pump safety / fluidics ----------------------------------------------------------
#define PUMP_FLOW_RATE_ML_S      10.0f  // Estimated pump flow, used only for tank estimates
#define TANK_MAX_CAPACITY_ML     1000.0f

// =========================================================================================
// 2. AGRONOMIC STATE
// =========================================================================================

enum GrowthPhase {
  PHASE_GERMINATION_BLACKOUT = 1, // Days 1-3: no grow light, higher RH
  PHASE_AUTOTROPHIC_LIGHT    = 2  // Days 4-10: 16 h light / 8 h dark
};

enum ClimateStatus {
  STATUS_OPTIMAL,
  STATUS_WASPADA,
  STATUS_KRITIS,
  STATUS_SAFEMODE
};

struct AgronomicMetrics {
  float temperature;        // deg C
  float humidity;           // % RH
  float soil1;              // %
  float soil2;              // %
  float soilAvg;            // %
  float vpSat;              // kPa
  float vpAct;              // kPa
  float vpd;                // kPa
  float plantComfortScore;  // 0 - 100 %
  ClimateStatus status;
  bool dhtFault;
  bool soil1Fault;
  bool soil2Fault;
};

struct PumpRuntime {
  bool active;
  uint32_t startedAt;
  uint32_t lastStoppedAt;
};

struct SystemState {
  // Configurable setpoints
  float targetTempMin = 18.0f;
  float targetTempMax = 22.0f;
  float targetRhMin   = 50.0f;
  float targetRhMax   = 65.0f;
  float targetSoilMin = 45.0f;
  float targetSoilMax = 70.0f;

  int soil1DryRaw = SOIL1_DRY_RAW;
  int soil1WetRaw = SOIL1_WET_RAW;
  int soil2DryRaw = SOIL2_DRY_RAW;
  int soil2WetRaw = SOIL2_WET_RAW;
  int lastRaw1 = 0;
  int lastRaw2 = 0;

  // Operational state
  bool autoMode = true;
  GrowthPhase phase = PHASE_GERMINATION_BLACKOUT;
  uint8_t dayCounter = 1;

  // Relay states
  bool peltier = false;
  bool fan = false;
  bool spare = false;
  bool growLight = false;
  bool ledStrip = false;
  bool pump1 = false;
  bool pump2 = false;

  // Pump pulse bookkeeping
  PumpRuntime pump1Run = {false, 0, 0};
  PumpRuntime pump2Run = {false, 0, 0};
  uint32_t pump1OnSince = 0;
  uint32_t pump2OnSince = 0;

  // OTA update state
  bool otaInProgress = false;
  bool otaUploadOk = false;
  bool otaWebUpload = false;
  bool otaRebootPending = false;
  uint32_t otaRebootAt = 0;
  uint32_t otaLastActivity = 0;

  // Emergency flush
  bool flushActive = false;
  uint32_t flushStartTime = 0;

  // Single-pump timed pulse (dashboard quick actions)
  bool pulseActive = false;
  uint8_t pulsePump = 0;
  uint32_t pulseStartTime = 0;
  uint32_t pulseDurationMs = 0;

  // Sensor fault debouncing
  bool dhtFault = false;

  // Manual restart request
  bool restartPending = false;
  uint32_t restartAt = 0;

  // Reservoir estimates
  float tank1VolumeMl = TANK_MAX_CAPACITY_ML;
  float tank2VolumeMl = TANK_MAX_CAPACITY_ML;
};

struct SensorHealth {
  bool dhtOk = false;
  bool soil1Ok = false;
  bool soil2Ok = false;
  bool wifiConnected = false;
  bool apFallback = false;

  uint32_t lastDhtOk = 0;
  uint32_t lastSoil1Ok = 0;
  uint32_t lastSoil2Ok = 0;
  uint32_t lastDhtReinit = 0;
  uint32_t lastWifiAttempt = 0;
  uint32_t lastWifiRecovery = 0;
  uint32_t wifiDownSince = 0;

  uint16_t dhtFails = 0;
  uint16_t soil1Fails = 0;
  uint16_t soil2Fails = 0;
  uint16_t dhtRecoveries = 0;
  uint16_t soil1Recoveries = 0;
  uint16_t soil2Recoveries = 0;
  uint16_t dhtReinits = 0;
  uint16_t wifiReconnects = 0;
  uint16_t wifiAttempts = 0;
};

// Circular buffers for the rolling telemetry chart (last 20 samples)
#define HISTORY_LENGTH 20
float tempHistory[HISTORY_LENGTH];
float rhHistory[HISTORY_LENGTH];
float soil1History[HISTORY_LENGTH];
float soil2History[HISTORY_LENGTH];
uint8_t historyHead = 0;
uint8_t historyCount = 0;

// System log ring buffer
#define LOG_CAPACITY 8
struct LogEntry {
  char timestamp[12];
  char message[96];
};
LogEntry logBuffer[LOG_CAPACITY];
uint8_t logHead = 0;
uint8_t logCount = 0;

// Global instances
DHT dht(PIN_DHT11, DHTTYPE);
WebServer server(80);

AgronomicMetrics metrics;
SystemState state;
SensorHealth health;

// Non-blocking timing trackers
uint32_t lastSensorPollTime   = 0;
uint32_t lastClimateLoopTime  = 0;
uint32_t lastLedBlinkTime     = 0;
uint32_t lastVolumeUpdateTime = 0;
uint32_t lastDayIncrementTime = 0;
bool ledState = false;

// =========================================================================================
// 3. LOGGING & JSON HELPERS
// =========================================================================================

void getFormattedTimestamp(char* buffer, size_t maxLen) {
  uint32_t sec  = millis() / 1000;
  uint32_t hrs  = sec / 3600;
  uint32_t mins = (sec % 3600) / 60;
  uint32_t secs = sec % 60;
  snprintf(buffer, maxLen, "%02u:%02u:%02u", hrs, mins, secs);
}

void addSystemLog(const char* msg) {
  char ts[12];
  getFormattedTimestamp(ts, sizeof(ts));

  strncpy(logBuffer[logHead].timestamp, ts, sizeof(logBuffer[logHead].timestamp) - 1);
  logBuffer[logHead].timestamp[sizeof(logBuffer[logHead].timestamp) - 1] = '\0';

  strncpy(logBuffer[logHead].message, msg, sizeof(logBuffer[logHead].message) - 1);
  logBuffer[logHead].message[sizeof(logBuffer[logHead].message) - 1] = '\0';

  Serial.printf("[%s] %s\n", ts, msg);

  logHead = (logHead + 1) % LOG_CAPACITY;
  if (logCount < LOG_CAPACITY) logCount++;
}

String jsonEscape(const char* input) {
  String out;
  out.reserve(strlen(input) + 8);
  for (const char* p = input; *p; ++p) {
    switch (*p) {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n";  break;
      case '\r': out += "\\r";  break;
      case '\t': out += "\\t";  break;
      default:   out += *p;     break;
    }
  }
  return out;
}

void appendHistoryArray(String& json, const float* values) {
  json += "[";
  for (uint8_t i = 0; i < historyCount; i++) {
    uint8_t idx = (historyHead + HISTORY_LENGTH - historyCount + i) % HISTORY_LENGTH;
    json += String(values[idx], 1);
    if (i < historyCount - 1) json += ",";
  }
  json += "]";
}

// =========================================================================================
// 4. AGRONOMIC CALCULATIONS (TETENS VPD & PLANT COMFORT SCORE)
// =========================================================================================

float calculateVpSat(float T) {
  return 0.61078f * expf((17.27f * T) / (T + 237.3f));
}

float calculateVpAct(float vpSat, float rh) {
  return vpSat * (rh / 100.0f);
}

float calculateVPD(float T, float rh, float& outVpSat, float& outVpAct) {
  outVpSat = calculateVpSat(T);
  outVpAct = calculateVpAct(outVpSat, rh);
  float vpd = outVpSat - outVpAct;
  return (vpd < 0.0f) ? 0.0f : vpd;
}

float calculatePlantComfortScore(float T, float rh, float vpd) {
  // Temperature: target 20 deg C, comfortable 18-22 deg C (weight 40%)
  float tScore = 1.0f;
  float tDev = fabsf(T - 20.0f);
  if (tDev <= 2.0f) {
    tScore = 1.0f - (tDev / 2.0f) * 0.15f;
  } else if (T > 24.0f) {
    tScore = 0.4f - (T - 24.0f) * 0.15f;
    if (tScore < 0.0f) tScore = 0.0f;
  } else {
    tScore = 0.85f - ((tDev - 2.0f) / 2.0f) * 0.45f;
    if (tScore < 0.1f) tScore = 0.1f;
  }

  // Humidity: target 57.5%, comfortable 50-65% (weight 30%)
  float rhScore = 1.0f;
  float rhDev = fabsf(rh - 57.5f);
  if (rhDev <= 7.5f) {
    rhScore = 1.0f - (rhDev / 7.5f) * 0.15f;
  } else {
    rhScore = 0.85f - ((rhDev - 7.5f) / 20.0f) * 0.70f;
    if (rhScore < 0.0f) rhScore = 0.0f;
  }

  // VPD: target 0.60 kPa, comfortable 0.40-0.80 kPa (weight 30%)
  float vpdScore = 1.0f;
  float vpdDev = fabsf(vpd - 0.60f);
  if (vpdDev <= 0.20f) {
    vpdScore = 1.0f - (vpdDev / 0.20f) * 0.15f;
  } else {
    vpdScore = 0.85f - ((vpdDev - 0.20f) / 0.50f) * 0.75f;
    if (vpdScore < 0.0f) vpdScore = 0.0f;
  }

  float total = (tScore * 0.40f + rhScore * 0.30f + vpdScore * 0.30f) * 100.0f;
  return constrain(total, 0.0f, 100.0f);
}

// =========================================================================================
// 5. HARDWARE DRIVERS & SAFETY INTERLOCKS
// =========================================================================================

void rawRelayWrite(uint8_t pin, bool active) {
  digitalWrite(pin, active ? RELAY_ON : RELAY_OFF);
}

void initRelayPin(uint8_t pin) {
  // Preload the OFF level before switching the pin to OUTPUT to avoid boot chatter.
  digitalWrite(pin, RELAY_OFF);
  pinMode(pin, OUTPUT);
  digitalWrite(pin, RELAY_OFF);
}

/**
 * Peltier and fan share the 4-CH board. The fan is interlocked: the Peltier may never
 * run without the heatsink / circulation fan. Turning the Peltier off leaves the fan
 * state untouched so humidity control can keep it running if needed.
 */
void applyPeltier(bool enable) {
  if (enable) {
    rawRelayWrite(PIN_RELAY_FAN, true);
    state.fan = true;
    rawRelayWrite(PIN_RELAY_PELTIER, true);
    state.peltier = true;
  } else {
    rawRelayWrite(PIN_RELAY_PELTIER, false);
    state.peltier = false;
  }
}

void applyFan(bool enable) {
  if (!enable && state.peltier) {
    addSystemLog("SAFETY: fan OFF blocked while Peltier is ON; Peltier forced OFF");
    applyPeltier(false);
  }
  rawRelayWrite(PIN_RELAY_FAN, enable);
  state.fan = enable;
}

void applySpare(bool enable) {
  rawRelayWrite(PIN_RELAY_SPARE, enable);
  state.spare = enable;
}

void applyGrowLight(bool enable) {
  rawRelayWrite(PIN_RELAY_GROWLIGHT, enable);
  state.growLight = enable;
}

void applyLedStrip(bool enable) {
  rawRelayWrite(PIN_RELAY_LED_STRIP, enable);
  state.ledStrip = enable;
}

void setPump(uint8_t pump, bool enable) {
  uint32_t now = millis();
  if (pump == 1) {
    rawRelayWrite(PIN_RELAY_PUMP1, enable);
    state.pump1 = enable;
    if (enable) {
      state.pump1OnSince = now;
    }
  } else if (pump == 2) {
    rawRelayWrite(PIN_RELAY_PUMP2, enable);
    state.pump2 = enable;
    if (enable) {
      state.pump2OnSince = now;
    }
  }
}

void stopAllPumps() {
  setPump(1, false);
  setPump(2, false);
}

/**
 * Safe-mode fallback: keep air moving, cut heating/cooling and all water actuators.
 */
void triggerSafeModeFallback() {
  if (!state.dhtFault) {
    addSystemLog("CRITICAL: DHT11 read failed. Safe mode: fan ON, Peltier/pumps OFF");
  }
  state.dhtFault = true;
  health.dhtOk = false;
  metrics.status = STATUS_SAFEMODE;
  applyPeltier(false);
  applyFan(true);
  applyGrowLight(false);
  applyLedStrip(false);
  applySpare(false);
  stopAllPumps();
}

// =========================================================================================
// 6. SENSOR ACQUISITION
// =========================================================================================

int readAdcAverage(uint8_t pin) {
  // 12 samples, drop the two lowest and two highest, average the middle eight.
  // This rejects contact noise and single-sample spikes without adding delay().
  const uint8_t samplesCount = 12;
  int samples[samplesCount];
  for (uint8_t i = 0; i < samplesCount; i++) {
    samples[i] = analogRead(pin);
    delayMicroseconds(250);
  }

  for (uint8_t i = 1; i < samplesCount; i++) {
    int key = samples[i];
    int j = i - 1;
    while (j >= 0 && samples[j] > key) {
      samples[j + 1] = samples[j];
      j--;
    }
    samples[j + 1] = key;
  }

  uint32_t sum = 0;
  for (uint8_t i = 2; i < samplesCount - 2; i++) sum += samples[i];
  return (int)(sum / (samplesCount - 4));
}

float adcToMoisturePct(int raw, int dryRaw, int wetRaw) {
  if (dryRaw == wetRaw) return 0.0f;
  float pct = (float)(dryRaw - raw) * 100.0f / (float)(dryRaw - wetRaw);
  return constrain(pct, 0.0f, 100.0f);
}

void evaluateClimateStatus() {
  float minSoil = 100.0f;
  bool anySoil = false;
  if (!metrics.soil1Fault) { minSoil = fminf(minSoil, metrics.soil1); anySoil = true; }
  if (!metrics.soil2Fault) { minSoil = fminf(minSoil, metrics.soil2); anySoil = true; }

  if (state.dhtFault) {
    metrics.status = STATUS_SAFEMODE;
  } else if (metrics.temperature > 24.0f || metrics.humidity > 75.0f ||
             (anySoil && minSoil < 35.0f)) {
    metrics.status = STATUS_KRITIS;
  } else if (metrics.soil1Fault || metrics.soil2Fault) {
    metrics.status = STATUS_WASPADA;
  } else if (metrics.temperature > 22.0f || metrics.temperature < 18.0f ||
             metrics.humidity > 65.0f || metrics.humidity < 50.0f ||
             (anySoil && (minSoil < 45.0f || minSoil > 70.0f)) ||
             metrics.vpd < 0.40f || metrics.vpd > 0.80f) {
    metrics.status = STATUS_WASPADA;
  } else {
    metrics.status = STATUS_OPTIMAL;
  }
}

void pollSensors() {
  // ---- DHT11 (temperature + relative humidity) -------------------------------------
  float rawT  = dht.readTemperature();
  float rawRh = dht.readHumidity();

  bool dhtValid = !isnan(rawT) && !isnan(rawRh) &&
                  rawT >= -10.0f && rawT <= 60.0f &&
                  rawRh >= 0.0f && rawRh <= 100.0f;

  if (!dhtValid) {
    health.dhtOk = false;
    if (health.dhtFails < 999) health.dhtFails++;

    if (health.dhtFails >= 3 && !state.dhtFault) {
      triggerSafeModeFallback();
    }

    // Self-heal: retry the DHT bus every 15 s after a sustained fault.
    if (health.dhtFails >= 6 &&
        (millis() - health.lastDhtReinit >= 15000UL)) {
      dht.begin();
      health.lastDhtReinit = millis();
      if (health.dhtReinits < 999) health.dhtReinits++;
      addSystemLog("Self-heal: DHT11 bus re-initialized");
    }
  } else {
    health.dhtOk = true;
    health.dhtFails = 0;
    health.lastDhtOk = millis();
    if (state.dhtFault) {
      state.dhtFault = false;
      if (health.dhtRecoveries < 999) health.dhtRecoveries++;
      addSystemLog("DHT11 recovered; automatic climate control resumed");
    }
    metrics.temperature = rawT;
    metrics.humidity = rawRh;
  }

  // ---- Capacitive soil sensors ------------------------------------------------------
  int raw1 = readAdcAverage(PIN_SOIL1);
  int raw2 = readAdcAverage(PIN_SOIL2);
  state.lastRaw1 = raw1;
  state.lastRaw2 = raw2;

  bool valid1 = raw1 > 50 && raw1 < 4050;
  bool valid2 = raw2 > 50 && raw2 < 4050;

  metrics.soil1Fault = !valid1;
  metrics.soil2Fault = !valid2;
  health.soil1Ok = valid1;
  health.soil2Ok = valid2;

  if (valid1) {
    metrics.soil1 = adcToMoisturePct(raw1, state.soil1DryRaw, state.soil1WetRaw);
    health.lastSoil1Ok = millis();
    if (health.soil1Fails >= 3) {
      if (health.soil1Recoveries < 999) health.soil1Recoveries++;
      addSystemLog("Self-heal: soil probe 1 readable again");
    }
    health.soil1Fails = 0;
  } else {
    if (health.soil1Fails < 999) health.soil1Fails++;
    if (health.soil1Fails == 5) {
      addSystemLog("Fault: soil probe 1 unreadable; pump 1 disabled");
    }
  }

  if (valid2) {
    metrics.soil2 = adcToMoisturePct(raw2, state.soil2DryRaw, state.soil2WetRaw);
    health.lastSoil2Ok = millis();
    if (health.soil2Fails >= 3) {
      if (health.soil2Recoveries < 999) health.soil2Recoveries++;
      addSystemLog("Self-heal: soil probe 2 readable again");
    }
    health.soil2Fails = 0;
  } else {
    if (health.soil2Fails < 999) health.soil2Fails++;
    if (health.soil2Fails == 5) {
      addSystemLog("Fault: soil probe 2 unreadable; pump 2 disabled");
    }
  }

  if (valid1 && valid2) {
    metrics.soilAvg = (metrics.soil1 + metrics.soil2) / 2.0f;
  } else if (valid1) {
    metrics.soilAvg = metrics.soil1;
  } else if (valid2) {
    metrics.soilAvg = metrics.soil2;
  }

  // ---- Agronomic metrics ------------------------------------------------------------
  if (!state.dhtFault) {
    metrics.vpd = calculateVPD(metrics.temperature, metrics.humidity,
                               metrics.vpSat, metrics.vpAct);
    metrics.plantComfortScore = calculatePlantComfortScore(
        metrics.temperature, metrics.humidity, metrics.vpd);
  }

  evaluateClimateStatus();

  // ---- Rolling history --------------------------------------------------------------
  tempHistory[historyHead]   = metrics.temperature;
  rhHistory[historyHead]     = metrics.humidity;
  soil1History[historyHead]  = metrics.soil1;
  soil2History[historyHead]  = metrics.soil2;
  historyHead = (historyHead + 1) % HISTORY_LENGTH;
  if (historyCount < HISTORY_LENGTH) historyCount++;
}

// =========================================================================================
// 7. CLOSED-LOOP CONTROL
// =========================================================================================

void updatePumpAutomation(uint8_t pump, float soil, bool soilValid, PumpRuntime& runtime) {
  if (state.flushActive) return;

  uint32_t now = millis();

  if (!soilValid) {
    if (runtime.active) {
      setPump(pump, false);
      runtime.active = false;
      runtime.lastStoppedAt = now;
    }
    return;
  }

  if (runtime.active) {
    bool reachedTarget = soil >= state.targetSoilMax;
    bool timedOut = (now - runtime.startedAt) >= 8000UL; // 8 s per watering pulse
    if (reachedTarget || timedOut) {
      setPump(pump, false);
      runtime.active = false;
      runtime.lastStoppedAt = now;
      char msg[64];
      snprintf(msg, sizeof(msg), "Pump %u OFF: %s", pump,
               reachedTarget ? "soil target reached" : "8 s pulse finished");
      addSystemLog(msg);
    }
    return;
  }

  // Wait at least 60 s after a pulse so the water can spread before re-checking.
  if (soil < state.targetSoilMin && (now - runtime.lastStoppedAt) >= 60000UL) {
    setPump(pump, true);
    runtime.active = true;
    runtime.startedAt = now;
    char msg[64];
    snprintf(msg, sizeof(msg), "Pump %u ON: soil %.1f%% below %.1f%%",
             pump, soil, state.targetSoilMin);
    addSystemLog(msg);
  }
}

void executeAutomatedClimateControl() {
  if (state.otaInProgress) return;
  if (!state.autoMode) return;
  if (state.dhtFault) return;

  // ---- Photoperiod ---------------------------------------------------------------
  if (state.phase == PHASE_GERMINATION_BLACKOUT) {
    if (state.growLight) applyGrowLight(false);
    if (state.ledStrip) applyLedStrip(false);
  } else {
    uint32_t dayMillis = millis() % (24UL * 3600UL * 1000UL);
    bool lightWindow = dayMillis < (16UL * 3600UL * 1000UL);
    if (state.growLight != lightWindow) applyGrowLight(lightWindow);
    if (state.ledStrip != lightWindow) applyLedStrip(lightWindow);
  }

  // ---- Temperature regulation (Peltier + interlocked fan) -------------------------
  if (metrics.temperature > state.targetTempMax) {
    if (!state.peltier) {
      applyPeltier(true);
      addSystemLog("Cooling ON: temperature above target maximum");
    }
  } else if (metrics.temperature <= state.targetTempMin + 0.5f) {
    if (state.peltier) {
      applyPeltier(false);
      addSystemLog("Cooling OFF: temperature target reached");
    }
  }

  // ---- Humidity evacuation via the combined fan -----------------------------------
  if (metrics.humidity > state.targetRhMax) {
    if (!state.fan) {
      applyFan(true);
      addSystemLog("Fan ON: humidity above target maximum");
    }
  } else if (metrics.humidity <= state.targetRhMax - 5.0f && !state.peltier) {
    if (state.fan) {
      applyFan(false);
      addSystemLog("Fan OFF: humidity normalized");
    }
  }

  // ---- Dual-zone watering ----------------------------------------------------------
  if (!state.flushActive && !state.pulseActive) {
    updatePumpAutomation(1, metrics.soil1, !metrics.soil1Fault, state.pump1Run);
    updatePumpAutomation(2, metrics.soil2, !metrics.soil2Fault, state.pump2Run);
  }
}

void stopSinglePulse() {
  if (!state.pulseActive) return;
  setPump(state.pulsePump, false);
  state.pulseActive = false;
  state.pulsePump = 0;
}

void processEmergencyFlush() {
  if (!state.flushActive) return;
  if (millis() - state.flushStartTime >= 5000UL) {
    state.flushActive = false;
    stopAllPumps();
    addSystemLog("Emergency flush finished (5 s)");
  }
}

void processSinglePulse() {
  if (!state.pulseActive) return;
  if (millis() - state.pulseStartTime >= state.pulseDurationMs) {
    char msg[64];
    snprintf(msg, sizeof(msg), "Timed pulse finished: pump %u OFF", state.pulsePump);
    setPump(state.pulsePump, false);
    state.pulseActive = false;
    state.pulsePump = 0;
    addSystemLog(msg);
  }
}

void processPumpHardLimit() {
  // Manual pump toggles are allowed, but never for more than 30 s.
  if (state.pump1 && !state.flushActive && (millis() - state.pump1OnSince >= 30000UL)) {
    setPump(1, false);
    addSystemLog("SAFETY: Pump 1 stopped after 30 s hard limit");
  }
  if (state.pump2 && !state.flushActive && (millis() - state.pump2OnSince >= 30000UL)) {
    setPump(2, false);
    addSystemLog("SAFETY: Pump 2 stopped after 30 s hard limit");
  }
}

void updateTankVolumes() {
  uint32_t now = millis();
  float dt = (float)(now - lastVolumeUpdateTime) / 1000.0f;
  lastVolumeUpdateTime = now;

  if (state.pump1) {
    state.tank1VolumeMl -= PUMP_FLOW_RATE_ML_S * dt;
    if (state.tank1VolumeMl < 0.0f) state.tank1VolumeMl = 0.0f;
  }
  if (state.pump2) {
    state.tank2VolumeMl -= PUMP_FLOW_RATE_ML_S * dt;
    if (state.tank2VolumeMl < 0.0f) state.tank2VolumeMl = 0.0f;
  }
}

void processStatusLed() {
  uint32_t interval = 1000;
  if (metrics.status == STATUS_SAFEMODE)      interval = 100;
  else if (metrics.status == STATUS_KRITIS)   interval = 250;
  else if (metrics.status == STATUS_WASPADA) interval = 500;

  if (millis() - lastLedBlinkTime >= interval) {
    lastLedBlinkTime = millis();
    ledState = !ledState;
    digitalWrite(PIN_STATUS_LED, ledState ? HIGH : LOW);
  }
}

void processDaySequencer() {
  if (millis() - lastDayIncrementTime < (24UL * 3600UL * 1000UL)) return;
  lastDayIncrementTime = millis();
  if (state.dayCounter >= 10) return;

  state.dayCounter++;
  if (state.dayCounter > 3) state.phase = PHASE_AUTOTROPHIC_LIGHT;

  char msg[64];
  snprintf(msg, sizeof(msg), "Sequencer advanced to day %u", state.dayCounter);
  addSystemLog(msg);
}

// =========================================================================================
// 8. REST API
// =========================================================================================

void handleCORS() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers",
                    "Content-Type, Origin, Authorization, Accept");
}

void handleOptions() {
  handleCORS();
  server.send(204);
}

void handleRoot() {
  handleCORS();
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

void handleNotFound() {
  handleCORS();
  server.send(404, "application/json", "{\"error\":\"not found\"}");
}

// =========================================================================================
// 9. OTA FIRMWARE UPDATE
// =========================================================================================

void otaCutOutputs() {
  applyPeltier(false);
  applyFan(false);
  applyGrowLight(false);
  applyLedStrip(false);
  applySpare(false);
  stopAllPumps();
}

void handleOTAForm() {
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html; charset=utf-8", OTA_HTML);
}

void handleOTAUpload() {
  HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    state.otaInProgress = true;
    state.otaUploadOk = false;
    state.otaWebUpload = true;
    state.otaRebootPending = false;
    state.otaLastActivity = millis();
    otaCutOutputs();
    addSystemLog("OTA update started; outputs forced OFF");
    Serial.printf("[OTA] Receiving: %s\n", upload.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    state.otaLastActivity = millis();
    if (state.otaInProgress && Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    state.otaLastActivity = millis();
    if (state.otaInProgress && Update.end(true)) {
      state.otaUploadOk = true;
      state.otaWebUpload = false;
      addSystemLog("OTA upload complete; rebooting");
      Serial.printf("[OTA] Success: %u bytes\n", (unsigned)upload.totalSize);
    } else {
      Update.printError(Serial);
      state.otaInProgress = false;
      state.otaWebUpload = false;
      addSystemLog("OTA update failed");
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    state.otaInProgress = false;
    state.otaUploadOk = false;
    state.otaWebUpload = false;
    addSystemLog("OTA upload aborted");
  }
}

void handleOTAUpdate() {
  bool ok = state.otaUploadOk && !Update.hasError();

  server.sendHeader("Connection", "close");
  String page;
  page.reserve(900);
  if (ok) {
    page += F("<!DOCTYPE html><html><head><meta charset='UTF-8'>"
              "<meta name='viewport' content='width=device-width,initial-scale=1'>"
              "<title>OTA Success</title></head>"
              "<body style='font-family:system-ui;max-width:560px;margin:40px auto;padding:24px;"
              "background-color:#FFF7FA;background-image:radial-gradient(#FAD4DF 1.2px,transparent 1.2px);"
              "background-size:22px 22px;color:#2E262C'>"
              "<div style='background:#fff;border:2px solid #F9CCD7;border-radius:24px;padding:26px;"
              "box-shadow:0 8px 24px rgba(244,63,94,.08)'>"
              "<span style='display:inline-block;background:#D1FAE5;border:1px solid #A7F3D0;"
              "color:#065F46;border-radius:999px;padding:4px 12px;font-size:12px;font-weight:800'>"
              "UPDATE OK</span>"
              "<h1 style='font-size:1.5rem;margin:14px 0 8px'>🌱 Update successful</h1>"
              "<p style='color:#6F5D67;line-height:1.55'>The cabin is rebooting with the new firmware. "
              "Capy is keeping the power cable safe. Reconnect in about 10 seconds.</p>"
              "<p style='margin-top:18px'><a href='/' style='color:#0369A1;font-weight:800'>"
              "← Back to dashboard</a></p></div></body></html>");
    state.otaRebootPending = true;
    state.otaRebootAt = millis() + 1500UL;
  } else {
    page += F("<!DOCTYPE html><html><head><meta charset='UTF-8'>"
              "<meta name='viewport' content='width=device-width,initial-scale=1'>"
              "<title>OTA Failed</title></head>"
              "<body style='font-family:system-ui;max-width:560px;margin:40px auto;padding:24px;"
              "background-color:#FFF7FA;background-image:radial-gradient(#FAD4DF 1.2px,transparent 1.2px);"
              "background-size:22px 22px;color:#2E262C'>"
              "<div style='background:#fff;border:2px solid #F9CCD7;border-radius:24px;padding:26px;"
              "box-shadow:0 8px 24px rgba(244,63,94,.08)'>"
              "<span style='display:inline-block;background:#FFE4E6;border:1px solid #FECDD3;"
              "color:#BE123C;border-radius:999px;padding:4px 12px;font-size:12px;font-weight:800'>"
              "OTA NEEDS HELP</span>"
              "<h1 style='font-size:1.5rem;margin:14px 0 8px'>🛠️ Update failed</h1>"
              "<p style='color:#6F5D67;line-height:1.55'>Check the OTA partition scheme and the .bin file, "
              "then retry. If web OTA keeps failing, upload once over USB.</p>"
              "<p style='margin-top:18px'><a href='/update' style='color:#BE123C;font-weight:800'>"
              "← Back to OTA</a></p></div></body></html>");
    state.otaInProgress = false;
    state.otaWebUpload = false;
  }
  server.send(200, "text/html; charset=utf-8", page);
}

String currentIpString() {
  if (WiFi.status() == WL_CONNECTED) return WiFi.localIP().toString();
  return WiFi.softAPIP().toString();
}

void startFallbackAP() {
  if (health.apFallback) return;

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(FALLBACK_AP_SSID, FALLBACK_AP_PASSWORD);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  health.apFallback = true;
  health.lastWifiAttempt = millis();
  Serial.printf("[Wi-Fi] Fallback AP active: %s / %s at %s\n",
                FALLBACK_AP_SSID, FALLBACK_AP_PASSWORD,
                WiFi.softAPIP().toString().c_str());
  addSystemLog("Fallback AP active; still retrying the STA link");
}

void processWiFiSelfHeal() {
  if (state.otaInProgress) return;

  uint32_t now = millis();
  bool connected = (WiFi.status() == WL_CONNECTED);

  if (connected) {
    if (!health.wifiConnected) {
      health.wifiConnected = true;
      health.wifiDownSince = 0;
      health.lastWifiRecovery = now;
      if (health.wifiReconnects < 999) health.wifiReconnects++;
      addSystemLog("Self-heal: WiFi link recovered");
    }

    // Drop the fallback AP after a stable STA link, but keep it during the first minute.
    if (health.apFallback && (now - health.lastWifiRecovery >= 60000UL)) {
      WiFi.softAPdisconnect(true);
      WiFi.mode(WIFI_STA);
      health.apFallback = false;
      addSystemLog("Fallback AP stopped; STA link is stable");
    }
    return;
  }

  if (health.wifiConnected) {
    health.wifiConnected = false;
    health.wifiDownSince = now;
    addSystemLog("WiFi link lost; starting automatic recovery");
  }
  if (health.wifiDownSince == 0) health.wifiDownSince = now;

  // Retry the stored STA credentials every 15 s.
  if (now - health.lastWifiAttempt >= 15000UL) {
    health.lastWifiAttempt = now;
    if (health.wifiAttempts < 999) health.wifiAttempts++;
    WiFi.reconnect();
  }

  // If the router stays unreachable for 30 s, bring up the fallback AP.
  if (!health.apFallback && (now - health.wifiDownSince >= 30000UL)) {
    startFallbackAP();
  }
}

void handleTelemetryApi() {
  handleCORS();
  if (server.method() == HTTP_OPTIONS) {
    server.send(204);
    return;
  }

  char uptimeStr[12];
  getFormattedTimestamp(uptimeStr, sizeof(uptimeStr));

  const char* statusStr = "OPTIMAL";
  if (metrics.status == STATUS_SAFEMODE)      statusStr = "SAFEMODE";
  else if (metrics.status == STATUS_KRITIS)   statusStr = "KRITIS";
  else if (metrics.status == STATUS_WASPADA) statusStr = "WASPADA";

  String json;
  json.reserve(4096);

  json += "{";
  json += "\"ip\":\"" + currentIpString() + "\",";
  json += "\"uptime\":\"" + String(uptimeStr) + "\",";
  json += "\"status\":\"" + String(statusStr) + "\",";
  json += "\"autoMode\":" + String(state.autoMode ? "true" : "false") + ",";
  json += "\"phase\":" + String((int)state.phase) + ",";
  json += "\"day\":" + String(state.dayCounter) + ",";

  json += "\"temp\":" + String(metrics.temperature, 2) + ",";
  json += "\"rh\":" + String(metrics.humidity, 2) + ",";
  json += "\"soil1\":" + String(metrics.soil1, 2) + ",";
  json += "\"soil2\":" + String(metrics.soil2, 2) + ",";
  json += "\"soil\":" + String(metrics.soilAvg, 2) + ",";
  json += "\"adc1\":" + String(state.lastRaw1) + ",";
  json += "\"adc2\":" + String(state.lastRaw2) + ",";
  json += "\"soil1Voltage\":" + String(state.lastRaw1 * (3.3f / 4095.0f), 2) + ",";
  json += "\"soil2Voltage\":" + String(state.lastRaw2 * (3.3f / 4095.0f), 2) + ",";
  // Backwards-compatible aliases for the first soil probe
  json += "\"rawSoilAdc\":" + String(state.lastRaw1) + ",";
  json += "\"soilVoltage\":" + String(state.lastRaw1 * (3.3f / 4095.0f), 2) + ",";

  json += "\"vpd\":" + String(metrics.vpd, 3) + ",";
  json += "\"vpSat\":" + String(metrics.vpSat, 3) + ",";
  json += "\"vpAct\":" + String(metrics.vpAct, 3) + ",";
  json += "\"pcs\":" + String(metrics.plantComfortScore, 1) + ",";
  json += "\"tank1Vol\":" + String(state.tank1VolumeMl, 1) + ",";
  json += "\"tank2Vol\":" + String(state.tank2VolumeMl, 1) + ",";

  json += "\"sensorFault\":" + String(state.dhtFault ? "true" : "false") + ",";
  json += "\"dhtFault\":" + String(state.dhtFault ? "true" : "false") + ",";
  json += "\"soil1Fault\":" + String(metrics.soil1Fault ? "true" : "false") + ",";
  json += "\"soil2Fault\":" + String(metrics.soil2Fault ? "true" : "false") + ",";

  json += "\"relays\":{";
  json += "\"peltier\":" + String(state.peltier ? "true" : "false") + ",";
  json += "\"fan\":" + String(state.fan ? "true" : "false") + ",";
  json += "\"spare\":" + String(state.spare ? "true" : "false") + ",";
  json += "\"growLight\":" + String(state.growLight ? "true" : "false") + ",";
  json += "\"ledStrip\":" + String(state.ledStrip ? "true" : "false") + ",";
  json += "\"pump1\":" + String(state.pump1 ? "true" : "false") + ",";
  json += "\"pump2\":" + String(state.pump2 ? "true" : "false");
  json += "},";

  json += "\"history\":{";
  json += "\"temp\":";   appendHistoryArray(json, tempHistory);
  json += ",\"rh\":";    appendHistoryArray(json, rhHistory);
  json += ",\"soil1\":"; appendHistoryArray(json, soil1History);
  json += ",\"soil2\":"; appendHistoryArray(json, soil2History);
  json += "},";

  json += "\"thresholds\":{";
  json += "\"tempMin\":" + String(state.targetTempMin, 1) + ",";
  json += "\"tempMax\":" + String(state.targetTempMax, 1) + ",";
  json += "\"rhMin\":" + String(state.targetRhMin, 1) + ",";
  json += "\"rhMax\":" + String(state.targetRhMax, 1) + ",";
  json += "\"soilMin\":" + String(state.targetSoilMin, 1) + ",";
  json += "\"soilMax\":" + String(state.targetSoilMax, 1) + ",";
  json += "\"dry1\":" + String(state.soil1DryRaw) + ",";
  json += "\"wet1\":" + String(state.soil1WetRaw) + ",";
  json += "\"dry2\":" + String(state.soil2DryRaw) + ",";
  json += "\"wet2\":" + String(state.soil2WetRaw);
  json += "},";

  json += "\"pins\":{";
  json += "\"dht\":" + String(PIN_DHT11) + ",";
  json += "\"soil1\":" + String(PIN_SOIL1) + ",";
  json += "\"soil2\":" + String(PIN_SOIL2) + ",";
  json += "\"peltier\":" + String(PIN_RELAY_PELTIER) + ",";
  json += "\"spare\":" + String(PIN_RELAY_SPARE) + ",";
  json += "\"fan\":" + String(PIN_RELAY_FAN) + ",";
  json += "\"growLight\":" + String(PIN_RELAY_GROWLIGHT) + ",";
  json += "\"ledStrip\":" + String(PIN_RELAY_LED_STRIP) + ",";
  json += "\"pump1\":" + String(PIN_RELAY_PUMP1) + ",";
  json += "\"pump2\":" + String(PIN_RELAY_PUMP2) + ",";
  json += "\"statusLed\":" + String(PIN_STATUS_LED);
  json += "},";

  uint32_t nowMs = millis();
  uint32_t dhtAge = health.lastDhtOk ? (nowMs - health.lastDhtOk) : 0;
  uint32_t soil1Age = health.lastSoil1Ok ? (nowMs - health.lastSoil1Ok) : 0;
  uint32_t soil2Age = health.lastSoil2Ok ? (nowMs - health.lastSoil2Ok) : 0;

  json += "\"health\":{";
  json += "\"dhtOk\":" + String(health.dhtOk ? "true" : "false") + ",";
  json += "\"dhtAgeMs\":" + String(dhtAge) + ",";
  json += "\"dhtFails\":" + String(health.dhtFails) + ",";
  json += "\"dhtReinits\":" + String(health.dhtReinits) + ",";
  json += "\"dhtRecoveries\":" + String(health.dhtRecoveries) + ",";
  json += "\"soil1Ok\":" + String(health.soil1Ok ? "true" : "false") + ",";
  json += "\"soil1AgeMs\":" + String(soil1Age) + ",";
  json += "\"soil1Fails\":" + String(health.soil1Fails) + ",";
  json += "\"soil1Recoveries\":" + String(health.soil1Recoveries) + ",";
  json += "\"soil2Ok\":" + String(health.soil2Ok ? "true" : "false") + ",";
  json += "\"soil2AgeMs\":" + String(soil2Age) + ",";
  json += "\"soil2Fails\":" + String(health.soil2Fails) + ",";
  json += "\"soil2Recoveries\":" + String(health.soil2Recoveries) + ",";
  json += "\"wifiConnected\":" + String(health.wifiConnected ? "true" : "false") + ",";
  json += "\"wifiRssi\":" + String(WiFi.RSSI()) + ",";
  json += "\"wifiReconnects\":" + String(health.wifiReconnects) + ",";
  json += "\"wifiAttempts\":" + String(health.wifiAttempts) + ",";
  json += "\"apFallback\":" + String(health.apFallback ? "true" : "false") + ",";
  json += "\"heap\":" + String(ESP.getFreeHeap()) + ",";
  json += "\"uptimeMs\":" + String(nowMs) + ",";
  json += "\"sensorsOk\":" + String((health.dhtOk && health.soil1Ok && health.soil2Ok) ? "true" : "false");
  json += "},";

  json += "\"ota\":{";
  json += "\"web\":true,\"arduino\":true,\"hostname\":\"" + String(MDNS_HOSTNAME) + "\"";
  json += "},";

  json += "\"logs\":[";
  for (uint8_t i = 0; i < logCount; i++) {
    uint8_t idx = (logHead + LOG_CAPACITY - logCount + i) % LOG_CAPACITY;
    json += "{\"t\":\"" + String(logBuffer[idx].timestamp) + "\",\"m\":\"" +
            jsonEscape(logBuffer[idx].message) + "\"}";
    if (i < logCount - 1) json += ",";
  }
  json += "]";
  json += "}";

  server.send(200, "application/json", json);
}

void handleControlApi() {
  handleCORS();
  if (server.method() == HTTP_OPTIONS) {
    server.send(204);
    return;
  }

  if (state.otaInProgress) {
    server.send(503, "application/json", "{\"error\":\"OTA update in progress\"}");
    return;
  }

  if (!server.hasArg("action")) {
    server.send(400, "application/json", "{\"error\":\"missing action\"}");
    return;
  }

  String action = server.arg("action");

  if (action == "mode") {
    state.autoMode = (server.arg("val") == "auto");
    addSystemLog(state.autoMode ? "Mode set to AUTOMATIC climate control"
                                : "Mode set to MANUAL override");
  } else if (action == "phase") {
    int p = server.arg("val").toInt();
    if (p == 1) {
      state.phase = PHASE_GERMINATION_BLACKOUT;
      addSystemLog("Growth phase set to germination blackout");
    } else if (p == 2) {
      state.phase = PHASE_AUTOTROPHIC_LIGHT;
      addSystemLog("Growth phase set to autotrophic light");
    }
  } else if (action == "day") {
    int d = server.arg("val").toInt();
    state.dayCounter = constrain(d, 1, 10);
    state.phase = (state.dayCounter <= 3) ? PHASE_GERMINATION_BLACKOUT
                                          : PHASE_AUTOTROPHIC_LIGHT;
    char msg[64];
    snprintf(msg, sizeof(msg), "Day counter set to day %u", state.dayCounter);
    addSystemLog(msg);
  } else if (action == "toggleRelay") {
    if (state.autoMode) {
      state.autoMode = false;
      addSystemLog("Relay override: mode switched to MANUAL");
    }

    int ch = server.arg("ch").toInt();
    switch (ch) {
      case 1:
        applyPeltier(!state.peltier);
        addSystemLog(state.peltier ? "Manual: Peltier ON (fan interlocked ON)"
                                   : "Manual: Peltier OFF");
        break;
      case 2:
        applyFan(!state.fan);
        addSystemLog(state.fan ? "Manual: Fan ON" : "Manual: Fan OFF");
        break;
      case 3:
        applySpare(!state.spare);
        addSystemLog(state.spare ? "Manual: Spare relay ON" : "Manual: Spare relay OFF");
        break;
      case 4:
        applyGrowLight(!state.growLight);
        addSystemLog(state.growLight ? "Manual: Grow light ON" : "Manual: Grow light OFF");
        break;
      case 5:
        applyLedStrip(!state.ledStrip);
        addSystemLog(state.ledStrip ? "Manual: LED strip ON" : "Manual: LED strip OFF");
        break;
      case 6:
        setPump(1, !state.pump1);
        addSystemLog(state.pump1 ? "Manual: Pump 1 ON" : "Manual: Pump 1 OFF");
        break;
      case 7:
        setPump(2, !state.pump2);
        addSystemLog(state.pump2 ? "Manual: Pump 2 ON" : "Manual: Pump 2 OFF");
        break;
      default:
        server.send(400, "application/json", "{\"error\":\"relay channel must be 1-7\"}");
        return;
    }
  } else if (action == "pulsePump") {
    uint8_t pump = (server.arg("pump").toInt() == 2) ? 2 : 1;
    uint32_t seconds = constrain(server.arg("seconds").toInt(), 1, 30);
    if (state.flushActive) {
      server.send(409, "application/json", "{\"error\":\"flush is active\"}");
      return;
    }
    stopSinglePulse();
    setPump(pump, true);
    state.pulseActive = true;
    state.pulsePump = pump;
    state.pulseStartTime = millis();
    state.pulseDurationMs = seconds * 1000UL;
    char msg[64];
    snprintf(msg, sizeof(msg), "Manual pulse: pump %u ON for %u s", pump, seconds);
    addSystemLog(msg);
  } else if (action == "flush") {
    stopSinglePulse();
    state.flushActive = true;
    state.flushStartTime = millis();
    setPump(1, true);
    setPump(2, true);
    addSystemLog("EMERGENCY FLUSH: both pumps ON for 5 s");
  } else if (action == "refill") {
    int tank = server.arg("tank").toInt();
    if (tank == 1) {
      state.tank1VolumeMl = TANK_MAX_CAPACITY_ML;
      addSystemLog("Tank 1 estimate reset to 1000 mL");
    } else if (tank == 2) {
      state.tank2VolumeMl = TANK_MAX_CAPACITY_ML;
      addSystemLog("Tank 2 estimate reset to 1000 mL");
    }
  } else if (action == "calibrateSoil") {
    int sensor = server.arg("sensor").toInt();
    String type = server.arg("type");
    int raw = (sensor == 2) ? state.lastRaw2 : state.lastRaw1;
    if (raw <= 50 || raw >= 4050) {
      server.send(400, "application/json", "{\"error\":\"ADC reading is not valid\"}");
      return;
    }
    if (sensor == 2) {
      if (type == "dry") state.soil2DryRaw = raw;
      else if (type == "wet") state.soil2WetRaw = raw;
    } else {
      if (type == "dry") state.soil1DryRaw = raw;
      else if (type == "wet") state.soil1WetRaw = raw;
    }
    char msg[64];
    snprintf(msg, sizeof(msg), "Soil %d %s calibration stored: ADC %d",
             sensor, type.c_str(), raw);
    addSystemLog(msg);
  } else if (action == "setThreshold") {
    String param = server.arg("param");
    float val = server.arg("val").toFloat();
    if (param == "tempMin")      state.targetTempMin = constrain(val, 5.0f, 35.0f);
    else if (param == "tempMax") state.targetTempMax = constrain(val, 5.0f, 40.0f);
    else if (param == "rhMin")   state.targetRhMin = constrain(val, 10.0f, 95.0f);
    else if (param == "rhMax")   state.targetRhMax = constrain(val, 10.0f, 95.0f);
    else if (param == "soilMin") state.targetSoilMin = constrain(val, 5.0f, 95.0f);
    else if (param == "soilMax") state.targetSoilMax = constrain(val, 5.0f, 95.0f);
    addSystemLog("Agronomic threshold updated");
  } else if (action == "reinitSensors") {
    dht.begin();
    health.lastDhtReinit = millis();
    if (health.dhtReinits < 999) health.dhtReinits++;
    health.dhtFails = 0;
    health.soil1Fails = 0;
    health.soil2Fails = 0;
    addSystemLog("Manual sensor re-initialization requested");
  } else if (action == "reconnectWiFi") {
    health.lastWifiAttempt = 0;
    health.wifiDownSince = millis();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    addSystemLog("Manual WiFi reconnect requested");
  } else if (action == "allOff") {
    state.autoMode = false;
    stopSinglePulse();
    state.flushActive = false;
    applyPeltier(false);
    applyFan(false);
    applyGrowLight(false);
    applyLedStrip(false);
    applySpare(false);
    stopAllPumps();
    addSystemLog("Manual: ALL outputs OFF");
  } else if (action == "restart") {
    state.restartPending = true;
    state.restartAt = millis() + 800UL;
    addSystemLog("Manual restart requested");
  } else {
    server.send(400, "application/json", "{\"error\":\"unknown action\"}");
    return;
  }

  server.send(200, "application/json", "{\"success\":true}");
}

// =========================================================================================
// 9. SETUP
// =========================================================================================

void setup() {
  Serial.begin(115200);
  delay(400);

  Serial.println();
  Serial.println("============================================================");
  Serial.println("  SMART BIOSPHERE CABIN - ESP32 STARTUP");
  Serial.println("  Peltier 25 | Fan 27 (x2 relays) | Spare 26 | Grow 14");
  Serial.println("  LED 32 | Pump1 19 | Pump2 18 | Soil 34/35 | DHT11 4");
  Serial.println("============================================================");

  // Relay outputs: force OFF before OUTPUT to avoid boot chatter.
  initRelayPin(PIN_RELAY_PELTIER);
  initRelayPin(PIN_RELAY_SPARE);
  initRelayPin(PIN_RELAY_FAN);
  initRelayPin(PIN_RELAY_GROWLIGHT);
  initRelayPin(PIN_RELAY_LED_STRIP);
  initRelayPin(PIN_RELAY_PUMP1);
  initRelayPin(PIN_RELAY_PUMP2);

  pinMode(PIN_STATUS_LED, OUTPUT);
  digitalWrite(PIN_STATUS_LED, LOW);

  // ADC1 only: GPIO 34/35 are input-only and remain usable with Wi-Fi active.
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  pinMode(PIN_SOIL1, INPUT);
  pinMode(PIN_SOIL2, INPUT);

  dht.begin();

  // Reasonable starting display values until the first sensor poll.
  metrics.temperature = 20.0f;
  metrics.humidity = 60.0f;
  metrics.soil1 = 50.0f;
  metrics.soil2 = 50.0f;
  metrics.soilAvg = 50.0f;
  metrics.plantComfortScore = 90.0f;
  metrics.status = STATUS_OPTIMAL;

  // ---- Wi-Fi ---------------------------------------------------------------------
  Serial.printf("Connecting to Wi-Fi: %s ", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint32_t wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED &&
         (millis() - wifiStart < WIFI_CONNECT_TIMEOUT_MS)) {
    delay(250);
    Serial.print(".");
    digitalWrite(PIN_STATUS_LED, !digitalRead(PIN_STATUS_LED));
  }

  if (WiFi.status() == WL_CONNECTED) {
    health.wifiConnected = true;
    health.lastWifiRecovery = millis();
    Serial.printf("\n[Wi-Fi] Connected. IP: %s\n", WiFi.localIP().toString().c_str());
    char msg[96];
    snprintf(msg, sizeof(msg), "WiFi connected: %s", WiFi.localIP().toString().c_str());
    addSystemLog(msg);
  } else {
    Serial.println("\n[Wi-Fi] STA timeout, starting fallback AP...");
    startFallbackAP();
  }

  if (MDNS.begin(MDNS_HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("[mDNS] http://%s.local/\n", MDNS_HOSTNAME);
  }

  // ---- OTA (Arduino IDE network port + web upload) -------------------------------
  ArduinoOTA.setHostname(MDNS_HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.onStart([]() {
    state.otaInProgress = true;
    state.otaWebUpload = false;
    otaCutOutputs();
    addSystemLog("ArduinoOTA update started; outputs forced OFF");
  });
  ArduinoOTA.onEnd([]() {
    addSystemLog("ArduinoOTA update complete; rebooting");
  });
  ArduinoOTA.onError([](ota_error_t error) {
    state.otaInProgress = false;
    Serial.printf("[OTA] ArduinoOTA error %u\n", (unsigned)error);
    addSystemLog("ArduinoOTA update error");
  });
  ArduinoOTA.begin();
  Serial.printf("[OTA] ArduinoOTA ready: %s.local (password: %s)\n",
                MDNS_HOSTNAME, OTA_PASSWORD);
  Serial.println("[OTA] Web updater: http://<ip>/update");

  // ---- HTTP routes ---------------------------------------------------------------
  server.on("/", HTTP_GET, handleRoot);
  server.on("/", HTTP_OPTIONS, handleOptions);
  server.on("/dashboard", HTTP_GET, handleRoot);
  server.on("/dashboard.html", HTTP_GET, handleRoot);
  server.on("/api/telemetry", HTTP_GET, handleTelemetryApi);
  server.on("/api/telemetry", HTTP_OPTIONS, handleOptions);
  server.on("/api/control", HTTP_ANY, handleControlApi);
  server.on("/update", HTTP_GET, handleOTAForm);
  server.on("/update", HTTP_POST, handleOTAUpdate, handleOTAUpload);
  server.onNotFound(handleNotFound);
  server.begin();

  lastVolumeUpdateTime = millis();
  addSystemLog("Hardware initialized; web server listening on port 80");
  addSystemLog("Automatic climate control ready");
}

// =========================================================================================
// 10. MAIN LOOP
// =========================================================================================

void loop() {
  server.handleClient();
  ArduinoOTA.handle();
  processWiFiSelfHeal();

  if (state.otaRebootPending && millis() >= state.otaRebootAt) {
    Serial.println("[OTA] Rebooting into new firmware...");
    ESP.restart();
  }

  if (state.restartPending && millis() >= state.restartAt) {
    Serial.println("[SYS] Manual restart requested.");
    ESP.restart();
  }

  // If a browser OTA upload stalls, release the controller and restore auto control.
  if (state.otaInProgress && state.otaWebUpload &&
      (millis() - state.otaLastActivity >= 60000UL)) {
    Update.abort();
    state.otaInProgress = false;
    state.otaWebUpload = false;
    addSystemLog("Self-heal: stalled OTA upload cancelled");
  }

  uint32_t now = millis();

  // DHT11 supports at most one reading per second; 2.5 s keeps it comfortable.
  if (now - lastSensorPollTime >= 2500UL) {
    lastSensorPollTime = now;
    pollSensors();
  }

  if (now - lastClimateLoopTime >= 1000UL) {
    lastClimateLoopTime = now;
    executeAutomatedClimateControl();
  }

  processEmergencyFlush();
  processSinglePulse();
  processPumpHardLimit();
  updateTankVolumes();
  processStatusLed();
  processDaySequencer();
}
