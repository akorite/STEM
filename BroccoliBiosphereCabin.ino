/*
 * =========================================================================================
 *   SMART HYPER-MONITORED BIOSPHERE CABIN - FIRMWARE ARCHITECTURE
 *   Dedicated Cultivation Engine for Broccoli Microgreens (Brassica oleracea var. italica)
 * =========================================================================================
 *   Target Platform : ESP32 Dev Module (WROOM-32 / NodeMCU ESP32)
 *   Standard        : Non-Blocking Real-Time Architecture (Zero delay()), Fault-Tolerant,
 *                     Advanced Agronomic Telemetry, Cyber-Agri Glassmorphism Web Dashboard
 * =========================================================================================
 */

#include <WiFi.h>
#include <WebServer.h>
#include <DHT.h>
#include <ESP32Servo.h>
#include <math.h>

// =========================================================================================
// 1. HARDWARE TOPOLOGY PINOUT & CALIBRATION CONSTANTS
// =========================================================================================

// Wi-Fi Credentials
const char* WIFI_SSID     = "Tryout_SA_0509";
const char* WIFI_PASSWORD = "SAujian1234";

// Sensors
#define PIN_DHT22               14   // GPIO 14: DHT22 (Air Temperature & Relative Humidity RH)
#define PIN_SOIL_ADC            34   // GPIO 34 (ADC1): Capacitive Soil Moisture Sensor

#define DHTTYPE                 DHT22

// 6-Channel Relay Module (Active-LOW Trigger)
#define RELAY_TRIGGER_ON        LOW
#define RELAY_TRIGGER_OFF       HIGH

#define PIN_RELAY_PELTIER       19   // CH1: Peltier Thermoelectric Module 12V (Cooling)
#define PIN_RELAY_FAN_HEATSINK  18   // CH2: External Heatsink Fan 12V (Interlocked with CH1)
#define PIN_RELAY_BLOWER        5    // CH3: DC Blower 12V (Turbulence & Moisture Evacuation)
#define PIN_RELAY_GROWLIGHT     17   // CH4: Full-Spectrum LED Grow Light
#define PIN_RELAY_SPRAY_T1      16   // CH5: Nano Ultrasonic Spray Tank 1 (Raw Water)
#define PIN_RELAY_SPRAY_T2      4    // CH6: Nano Ultrasonic Spray Tank 2 (Micro Nutrients)

// Mechanical Valve Servomotors
#define PIN_SERVO_VALVE_T1      25   // GPIO 25: Tank 1 Flow Valve (0° Closed, 90° Open)
#define PIN_SERVO_VALVE_T2      26   // GPIO 26: Tank 2 Flow Valve (0° Closed, 90° Open)

// Visual Indicator
#define PIN_STATUS_LED          27   // GPIO 27: System Heartbeat / Health Indicator LED

// Capacitive Soil Moisture Sensor ADC Calibration Constants
// ESP32 ADC: 12-bit (0 - 4095). Sensor output is inversely proportional to moisture.
#define SOIL_DRY_RAW            3200 // Raw ADC in ambient dry air (0% Moisture)
#define SOIL_WET_RAW            1400 // Raw ADC fully submerged in water (100% Moisture)

// Ultrasonic Mist Flow Rate Calibration (approx. 48 mL/hour = 0.8 mL/second)
#define SPRAY_FLOW_RATE_ML_S    0.8f
#define TANK_MAX_CAPACITY_ML    1000.0f

// =========================================================================================
// 2. AGRONOMIC TELEMETRY & SYSTEM STATE STRUCTURES
// =========================================================================================

enum GrowthPhase {
  PHASE_GERMINATION_BLACKOUT = 1, // Days 1-3: Blackout, high RH (60-70%), pure Tank 1 (Raw)
  PHASE_AUTOTROPHIC_LIGHT    = 2  // Days 4-10: 16h/8h Photoperiod, VPD 0.6-0.8 kPa, Tank 2 (Nutrients)
};

enum ClimateStatus {
  STATUS_OPTIMAL,
  STATUS_WASPADA,
  STATUS_KRITIS,
  STATUS_SAFEMODE
};

struct AgronomicMetrics {
  float temperature;        // °C
  float humidity;           // % RH
  float soilMoisture;       // %
  float vpSat;              // Saturated Vapor Pressure (kPa)
  float vpAct;              // Actual Vapor Pressure (kPa)
  float vpd;                // Vapor Pressure Deficit (kPa)
  float plantComfortScore;  // Plant Comfort Score (0 - 100%)
  ClimateStatus status;
  bool sensorFault;
};

struct SystemState {
  // Configurable Agronomic Setpoints / Thresholds
  float targetTempMin = 18.0f;
  float targetTempMax = 22.0f;
  float targetRhMin   = 50.0f;
  float targetRhMax   = 65.0f;
  float targetSoilMin = 45.0f;
  float targetSoilMax = 70.0f;
  int soilDryRaw      = SOIL_DRY_RAW;
  int soilWetRaw      = SOIL_WET_RAW;
  int lastRawSoilAdc  = 2240;

  // Operational Modes
  bool autoMode;            // true = AUTOMATIC CLIMATE CONTROL, false = MANUAL OVERRIDE
  GrowthPhase phase;        // Current Growth Phase
  uint8_t dayCounter;       // Days in Cultivation (1 - 10)
  uint8_t activeTank;       // 1 = Tank 1, 2 = Tank 2

  // Actuator Relay States (true = ON, false = OFF)
  bool peltier;
  bool heatsinkFan;
  bool blower;
  bool growLight;
  bool sprayT1;
  bool sprayT2;

  // Servomotor Valve Angles (0° - 90°)
  int valve1Angle;
  int valve2Angle;

  // Tank Volumetrics (mL)
  float tank1VolumeMl;
  float tank2VolumeMl;

  // Anti-Fungal Purge Machine
  bool purgeActive;
  uint32_t purgeStartTime;
  uint32_t lastPurgeCheckTime;

  // Emergency Flush State Machine
  bool emergencyFlushActive;
  uint32_t emergencyFlushStartTime;

  // Automated Spray Pulse Machine
  bool autoSprayActive;
  uint32_t autoSprayStartTime;
  uint32_t lastAutoSprayTime;
};

// Circular Buffers for Telemetry History (Last 20 Points)
#define HISTORY_LENGTH 20
float tempHistory[HISTORY_LENGTH];
float rhHistory[HISTORY_LENGTH];
uint8_t historyHead = 0;
uint8_t historyCount = 0;

// System Log Console Buffer (Last 5 Timestamped Events)
#define LOG_CAPACITY 5
struct LogEntry {
  char timestamp[12];
  char message[64];
};
LogEntry logBuffer[LOG_CAPACITY];
uint8_t logHead = 0;
uint8_t logCount = 0;

// Global Instances
DHT dht(PIN_DHT22, DHTTYPE);
Servo servoValve1;
Servo servoValve2;
WebServer server(80);

AgronomicMetrics metrics;
SystemState state;

// Non-Blocking Timing Trackers
uint32_t lastSensorPollTime    = 0;
uint32_t lastClimateLoopTime   = 0;
uint32_t lastLedBlinkTime      = 0;
uint32_t lastVolumeUpdateTime  = 0;
uint32_t lastDayIncrementTime  = 0;

bool ledState = false;

// =========================================================================================
// 3. LOGGING & TIME UTILITIES
// =========================================================================================

void getFormattedTimestamp(char* buffer, size_t maxLen) {
  uint32_t sec = millis() / 1000;
  uint32_t hrs = (sec / 3600);
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
  if (logCount < LOG_CAPACITY) {
    logCount++;
  }
}

// =========================================================================================
// 4. ADVANCED AGRONOMIC METRICS & COMPUTATION (TETENS VPD & PCS)
// =========================================================================================

/**
 * @brief Computes Tetens Equation Saturated Vapor Pressure (VPsat)
 * Equation: VPsat = 0.61078 * exp((17.27 * T) / (T + 237.3))  [kPa]
 */
float calculateVpSat(float T) {
  return 0.61078f * expf((17.27f * T) / (T + 237.3f));
}

/**
 * @brief Computes Actual Vapor Pressure (VPact)
 * Equation: VPact = VPsat * (RH / 100.0)  [kPa]
 */
float calculateVpAct(float vpSat, float rh) {
  return vpSat * (rh / 100.0f);
}

/**
 * @brief Computes Vapor Pressure Deficit (VPD)
 * Equation: VPD = VPsat - VPact  [kPa]
 * Optimal for Broccoli Microgreens: 0.40 - 0.80 kPa
 */
float calculateVPD(float T, float rh, float &outVpSat, float &outVpAct) {
  outVpSat = calculateVpSat(T);
  outVpAct = calculateVpAct(outVpSat, rh);
  float vpd = outVpSat - outVpAct;
  return (vpd < 0.0f) ? 0.0f : vpd;
}

/**
 * @brief Formulates Plant Comfort Score (PCS) 0 - 100%
 * Based on weighted deviations from optimal broccoli microgreens criteria:
 * - Temperature Target: 20.0°C (Tolerance: 18.0 - 22.0°C; Pathogenic risk > 24.0°C) [Weight: 40%]
 * - Relative Humidity Target: 57.5% (Tolerance: 50.0 - 65.0%) [Weight: 30%]
 * - Target VPD: 0.60 kPa (Tolerance: 0.40 - 0.80 kPa) [Weight: 30%]
 */
float calculatePlantComfortScore(float T, float rh, float vpd) {
  // 1. Temperature Deviation Score (Weight: 40%)
  float tScore = 1.0f;
  float tDev = fabsf(T - 20.0f);
  if (tDev <= 2.0f) {
    tScore = 1.0f - (tDev / 2.0f) * 0.15f; // Within 18-22°C: 85% - 100%
  } else if (T > 24.0f) {
    // Critical Pythium root rot threshold
    float severeDev = T - 24.0f;
    tScore = 0.4f - (severeDev * 0.15f);
    if (tScore < 0.0f) tScore = 0.0f;
  } else {
    // Between 22-24°C or < 18°C
    tScore = 0.85f - ((tDev - 2.0f) / 2.0f) * 0.45f;
    if (tScore < 0.1f) tScore = 0.1f;
  }

  // 2. Relative Humidity Deviation Score (Weight: 30%)
  float rhScore = 1.0f;
  float rhDev = fabsf(rh - 57.5f);
  if (rhDev <= 7.5f) {
    rhScore = 1.0f - (rhDev / 7.5f) * 0.15f; // Within 50-65%: 85% - 100%
  } else {
    rhScore = 0.85f - ((rhDev - 7.5f) / 20.0f) * 0.70f;
    if (rhScore < 0.0f) rhScore = 0.0f;
  }

  // 3. Vapor Pressure Deficit Deviation Score (Weight: 30%)
  float vpdScore = 1.0f;
  float vpdDev = fabsf(vpd - 0.60f);
  if (vpdDev <= 0.20f) {
    vpdScore = 1.0f - (vpdDev / 0.20f) * 0.15f; // Within 0.4 - 0.8 kPa: 85% - 100%
  } else {
    vpdScore = 0.85f - ((vpdDev - 0.20f) / 0.50f) * 0.75f;
    if (vpdScore < 0.0f) vpdScore = 0.0f;
  }

  float totalScore = (tScore * 0.40f + rhScore * 0.30f + vpdScore * 0.30f) * 100.0f;
  return constrain(totalScore, 0.0f, 100.0f);
}

// =========================================================================================
// 5. HARDWARE DRIVERS, INTERLOCKS & SAFETY MECHANISMS
// =========================================================================================

void rawRelayWrite(uint8_t pin, bool active) {
  digitalWrite(pin, active ? RELAY_TRIGGER_ON : RELAY_TRIGGER_OFF);
}

/**
 * @brief Enforces Thermal Safety Interlock on CH1 (Peltier) and CH2 (Heatsink Fan)
 * Specification: Peltier (CH1) is strictly FORBIDDEN to turn ON without CH2 active simultaneously!
 */
void applyPeltierInterlock(bool enablePeltier) {
  if (enablePeltier) {
    // CH2 Heatsink Fan MUST activate first/simultaneously
    rawRelayWrite(PIN_RELAY_FAN_HEATSINK, true);
    state.heatsinkFan = true;

    rawRelayWrite(PIN_RELAY_PELTIER, true);
    state.peltier = true;
  } else {
    // Turn off Peltier immediately
    rawRelayWrite(PIN_RELAY_PELTIER, false);
    state.peltier = false;
    // Heatsink Fan can be shut off or maintained as needed
  }
}

/**
 * @brief Controls Heatsink Fan with safety interlock check
 */
void applyHeatsinkFan(bool enableFan) {
  if (!enableFan && state.peltier) {
    // If Peltier is currently active, fan CANNOT be turned off! Safety auto-shutdown.
    addSystemLog("SAFETY INTERLOCK: Peltier forced OFF due to Fan deactivation!");
    applyPeltierInterlock(false);
  }
  rawRelayWrite(PIN_RELAY_FAN_HEATSINK, enableFan);
  state.heatsinkFan = enableFan;
}

/**
 * @brief Controls Air Blower (CH3)
 */
void applyBlower(bool enableBlower) {
  rawRelayWrite(PIN_RELAY_BLOWER, enableBlower);
  state.blower = enableBlower;
}

/**
 * @brief Controls LED Grow Light (CH4)
 */
void applyGrowLight(bool enableLight) {
  rawRelayWrite(PIN_RELAY_GROWLIGHT, enableLight);
  state.growLight = enableLight;
}

/**
 * @brief Anti-Mix Dual-Tank Valve & Spray Control Logic
 * Specification: Opens designated servo valve (90°) ONLY when spray line is active;
 * locks valve to 0° when spray stops to prevent cross-contamination.
 */
void applySprayAndValves(bool spray1, bool spray2) {
  // Tank 1 Control
  if (spray1) {
    servoValve1.write(90);
    state.valve1Angle = 90;
    rawRelayWrite(PIN_RELAY_SPRAY_T1, true);
    state.sprayT1 = true;
  } else {
    rawRelayWrite(PIN_RELAY_SPRAY_T1, false);
    state.sprayT1 = false;
    servoValve1.write(0);
    state.valve1Angle = 0;
  }

  // Tank 2 Control
  if (spray2) {
    servoValve2.write(90);
    state.valve2Angle = 90;
    rawRelayWrite(PIN_RELAY_SPRAY_T2, true);
    state.sprayT2 = true;
  } else {
    rawRelayWrite(PIN_RELAY_SPRAY_T2, false);
    state.sprayT2 = false;
    servoValve2.write(0);
    state.valve2Angle = 0;
  }
}

/**
 * @brief Robust Safe-Mode Fallback on Sensor Read Fault (NaN)
 */
void triggerSafeModeFallback() {
  metrics.status = STATUS_SAFEMODE;
  metrics.sensorFault = true;

  // Safe mode protocol: Peltier OFF, Blower ON (prevent stagnant buildup), Sprays OFF, Valves 0°
  applyPeltierInterlock(false);
  applyBlower(true);
  applySprayAndValves(false, false);

  addSystemLog("CRITICAL: Sensor read NaN! System reverted to SAFE-MODE.");
}

// =========================================================================================
// 6. SENSOR ACQUISITION & TELEMETRY ENGINE
// =========================================================================================

void pollSensors() {
  float rawT = dht.readTemperature();
  float rawRh = dht.readHumidity();

  // Handle DHT22 read failure
  if (isnan(rawT) || isnan(rawRh)) {
    triggerSafeModeFallback();
    return;
  }

  metrics.sensorFault = false;
  metrics.temperature = rawT;
  metrics.humidity = rawRh;

  // Capacitive Soil Moisture Sensor (ADC1, GPIO 34)
  int rawSoilAdc = analogRead(PIN_SOIL_ADC);
  state.lastRawSoilAdc = rawSoilAdc;
  // Map inverted ADC values: SOIL_DRY_RAW (3200) -> 0%, SOIL_WET_RAW (1400) -> 100%
  float soilPct = (float)(SOIL_DRY_RAW - rawSoilAdc) * 100.0f / (float)(SOIL_DRY_RAW - SOIL_WET_RAW);
  metrics.soilMoisture = constrain(soilPct, 0.0f, 100.0f);

  // Compute Advanced Agronomic Metrics
  metrics.vpd = calculateVPD(metrics.temperature, metrics.humidity, metrics.vpSat, metrics.vpAct);
  metrics.plantComfortScore = calculatePlantComfortScore(metrics.temperature, metrics.humidity, metrics.vpd);

  // Climate Status Evaluation
  if (metrics.temperature > 24.0f || metrics.humidity > 75.0f || metrics.soilMoisture < 35.0f) {
    metrics.status = STATUS_KRITIS;
  } else if (metrics.temperature > 22.0f || metrics.temperature < 18.0f ||
             metrics.humidity > 65.0f || metrics.humidity < 50.0f ||
             metrics.soilMoisture < 45.0f || metrics.soilMoisture > 70.0f ||
             metrics.vpd < 0.40f || metrics.vpd > 0.80f) {
    metrics.status = STATUS_WASPADA;
  } else {
    metrics.status = STATUS_OPTIMAL;
  }

  // Push to circular history buffer
  tempHistory[historyHead] = metrics.temperature;
  rhHistory[historyHead] = metrics.humidity;
  historyHead = (historyHead + 1) % HISTORY_LENGTH;
  if (historyCount < HISTORY_LENGTH) {
    historyCount++;
  }
}

// =========================================================================================
// 7. CLOSED-LOOP CLIMATE CONTROL & GROWTH SEQUENCER
// =========================================================================================

void executeAutomatedClimateControl() {
  if (!state.autoMode || metrics.sensorFault) {
    return; // Manual mode or fault condition suppresses automated control
  }

  // --- Growth Phase Sequencer: Grow Light Scheduling ---
  if (state.phase == PHASE_GERMINATION_BLACKOUT) {
    // Days 1-3: Blackout Phase (Grow Light MUST remain OFF)
    applyGrowLight(false);
  } else {
    // Days 4-10: Autotrophic Light Phase (16 hours ON / 8 hours OFF photoperiod)
    uint32_t dayMillis = millis() % (24UL * 3600UL * 1000UL);
    bool isLightCycle = (dayMillis < (16UL * 3600UL * 1000UL));
    applyGrowLight(isLightCycle);
  }

  // --- Temperature Regulation (Peltier & Interlocked Heatsink Fan) ---
  // Target: 18.0°C - 22.0°C (Critical > 24.0°C)
  if (metrics.temperature > 22.0f) {
    if (!state.peltier) {
      applyPeltierInterlock(true);
      addSystemLog("Cooling activated: Temp > 22.0C");
    }
  } else if (metrics.temperature <= 19.5f) {
    if (state.peltier) {
      applyPeltierInterlock(false);
      applyHeatsinkFan(false);
      addSystemLog("Cooling target reached: Peltier OFF");
    }
  }

  // --- Air Circulation & Humidity Evacuation (Blower DC 12V) ---
  // If not currently in dedicated purge cycle, evaluate standard humidity threshold
  if (!state.purgeActive) {
    if (metrics.humidity > 65.0f) {
      if (!state.blower) {
        applyBlower(true);
        addSystemLog("Blower ON: Humidity > 65% (Evacuating moisture)");
      }
    } else if (metrics.humidity <= 60.0f && !state.peltier) {
      if (state.blower) {
        applyBlower(false);
        addSystemLog("Blower OFF: Air humidity normalized");
      }
    }
  }

  // --- Dual-Tank Nano Spray Pulsing ---
  // In Germination: High RH (60-70%), pure Tank 1 (Demineralized raw water)
  // In Autotrophic: VPD target 0.6 - 0.8 kPa, Tank 2 (Micro nutrients)
  uint32_t now = millis();
  if (!state.emergencyFlushActive && !state.autoSprayActive) {
    bool triggerSpray = false;
    uint8_t targetTank = state.activeTank;

    if (state.phase == PHASE_GERMINATION_BLACKOUT) {
      targetTank = 1; // Pure Tank 1
      if (metrics.soilMoisture < 50.0f || metrics.humidity < 60.0f) {
        triggerSpray = true;
      }
    } else {
      // Autotrophic phase
      if (metrics.soilMoisture < 48.0f || metrics.vpd > 0.80f) {
        triggerSpray = true;
      }
    }

    // Interval between automatic spray pulses: 5 minutes (300,000 ms)
    if (triggerSpray && (now - state.lastAutoSprayTime >= 300000UL)) {
      state.autoSprayActive = true;
      state.autoSprayStartTime = now;
      state.lastAutoSprayTime = now;

      if (targetTank == 1) {
        applySprayAndValves(true, false);
        addSystemLog("Auto-Spray Pulse: Tank 1 (Raw Water) 5s");
      } else {
        applySprayAndValves(false, true);
        addSystemLog("Auto-Spray Pulse: Tank 2 (Nutrients) 5s");
      }
    }
  }

  // Check completion of automated 5s spray pulse
  if (state.autoSprayActive && (now - state.autoSprayStartTime >= 5000UL)) {
    state.autoSprayActive = false;
    applySprayAndValves(false, false);
  }
}

/**
 * @brief Anti-Fungal Purge Cycle State Machine
 * Specification: Every 30 minutes, if air humidity > 65%, run Blower (CH3)
 * for 45 seconds to break stagnant boundary layer on leaves.
 */
void processAntiFungalPurge() {
  uint32_t now = millis();

  // Check 30-minute interval (1,800,000 ms)
  if (!state.purgeActive && (now - state.lastPurgeCheckTime >= 1800000UL)) {
    state.lastPurgeCheckTime = now;
    if (metrics.humidity > 65.0f) {
      state.purgeActive = true;
      state.purgeStartTime = now;
      applyBlower(true);
      addSystemLog("Anti-Fungal Purge: Blower running 45s (RH > 65%)");
    }
  }

  // Manage 45-second active purge duration
  if (state.purgeActive && (now - state.purgeStartTime >= 45000UL)) {
    state.purgeActive = false;
    if (!state.autoMode || metrics.humidity <= 65.0f) {
      applyBlower(false);
    }
    addSystemLog("Anti-Fungal Purge cycle completed.");
  }
}

/**
 * @brief Emergency Flush State Machine (5 seconds spray from Tank 1)
 */
void processEmergencyFlush() {
  if (state.emergencyFlushActive) {
    if (millis() - state.emergencyFlushStartTime >= 5000UL) {
      state.emergencyFlushActive = false;
      applySprayAndValves(false, false);
      addSystemLog("Emergency Flush cycle finished.");
    }
  }
}

/**
 * @brief Tank Reservoir Volumetric Depletion Tracker
 */
void updateTankVolumes() {
  uint32_t now = millis();
  float dtSeconds = (float)(now - lastVolumeUpdateTime) / 1000.0f;
  lastVolumeUpdateTime = now;

  if (state.sprayT1) {
    state.tank1VolumeMl -= (SPRAY_FLOW_RATE_ML_S * dtSeconds);
    if (state.tank1VolumeMl < 0.0f) state.tank1VolumeMl = 0.0f;
  }
  if (state.sprayT2) {
    state.tank2VolumeMl -= (SPRAY_FLOW_RATE_ML_S * dtSeconds);
    if (state.tank2VolumeMl < 0.0f) state.tank2VolumeMl = 0.0f;
  }
}

/**
 * @brief Status LED Heartbeat / Fault Signaler
 */
void processStatusLed() {
  uint32_t now = millis();
  uint32_t blinkInterval = 1000; // Normal: 1s

  if (metrics.status == STATUS_SAFEMODE) {
    blinkInterval = 100; // Ultra-fast blink for fault
  } else if (metrics.status == STATUS_KRITIS) {
    blinkInterval = 250; // Fast blink for critical
  } else if (metrics.status == STATUS_WASPADA) {
    blinkInterval = 500; // Moderate blink for warning
  }

  if (now - lastLedBlinkTime >= blinkInterval) {
    lastLedBlinkTime = now;
    ledState = !ledState;
    digitalWrite(PIN_STATUS_LED, ledState ? HIGH : LOW);
  }
}

// =========================================================================================
// 8. REST API & HTTP SERVER HANDLERS (CORS-ENABLED HEADLESS API)
// =========================================================================================

void handleCORS() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type, Origin, Authorization, Accept");
}

void handleOptions() {
  handleCORS();
  server.send(204);
}

void handleRoot() {
  handleCORS();
  server.send(200, "application/json", 
    "{\"service\":\"Broccoli Biosphere Cabin REST API\","
    "\"status\":\"online\","
    "\"version\":\"2.0.0\","
    "\"description\":\"Headless REST API server for autonomous microgreen biosphere.\","
    "\"endpoints\":[\"/api/telemetry\",\"/api/control\"]}"
  );
}

void handleTelemetryApi() {
  handleCORS();
  if (server.method() == HTTP_OPTIONS) {
    server.send(204);
    return;
  }

  char uptimeStr[12];
  getFormattedTimestamp(uptimeStr, sizeof(uptimeStr));

  // Determine status label
  const char* statusStr = "OPTIMAL";
  if (metrics.status == STATUS_SAFEMODE) statusStr = "SAFEMODE";
  else if (metrics.status == STATUS_KRITIS) statusStr = "KRITIS";
  else if (metrics.status == STATUS_WASPADA) statusStr = "WASPADA";

  // Build JSON response dynamically without dynamic heap fragmentation
  String json = "{";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  json += "\"uptime\":\"" + String(uptimeStr) + "\",";
  json += "\"status\":\"" + String(statusStr) + "\",";
  json += "\"autoMode\":" + String(state.autoMode ? "true" : "false") + ",";
  json += "\"phase\":" + String((int)state.phase) + ",";
  json += "\"day\":" + String(state.dayCounter) + ",";
  json += "\"activeTank\":" + String(state.activeTank) + ",";
  json += "\"temp\":" + String(metrics.temperature, 2) + ",";
  json += "\"rh\":" + String(metrics.humidity, 2) + ",";
  json += "\"soil\":" + String(metrics.soilMoisture, 2) + ",";
  json += "\"vpd\":" + String(metrics.vpd, 3) + ",";
  json += "\"pcs\":" + String(metrics.plantComfortScore, 1) + ",";
  json += "\"tank1Vol\":" + String(state.tank1VolumeMl, 1) + ",";
  json += "\"tank2Vol\":" + String(state.tank2VolumeMl, 1) + ",";

  // Relays State Object
  json += "\"relays\":{";
  json += "\"peltier\":" + String(state.peltier ? "true" : "false") + ",";
  json += "\"fan\":" + String(state.heatsinkFan ? "true" : "false") + ",";
  json += "\"blower\":" + String(state.blower ? "true" : "false") + ",";
  json += "\"light\":" + String(state.growLight ? "true" : "false") + ",";
  json += "\"spray1\":" + String(state.sprayT1 ? "true" : "false") + ",";
  json += "\"spray2\":" + String(state.sprayT2 ? "true" : "false");
  json += "},";

  // Valve Servomotors Object
  json += "\"valves\":{";
  json += "\"v1\":" + String(state.valve1Angle) + ",";
  json += "\"v2\":" + String(state.valve2Angle);
  json += "},";

  // Telemetry History (Last 20 points)
  json += "\"history\":{";
  json += "\"temp\":[";
  for (uint8_t i = 0; i < historyCount; i++) {
    uint8_t idx = (historyHead + HISTORY_LENGTH - historyCount + i) % HISTORY_LENGTH;
    json += String(tempHistory[idx], 1);
    if (i < historyCount - 1) json += ",";
  }
  json += "],\"rh\":[";
  for (uint8_t i = 0; i < historyCount; i++) {
    uint8_t idx = (historyHead + HISTORY_LENGTH - historyCount + i) % HISTORY_LENGTH;
    json += String(rhHistory[idx], 1);
    if (i < historyCount - 1) json += ",";
  }
  json += "]},";

  // Log Console (Last 5 events)
  json += "\"logs\":[";
  for (uint8_t i = 0; i < logCount; i++) {
    uint8_t idx = (logHead + LOG_CAPACITY - logCount + i) % LOG_CAPACITY;
    json += "{\"t\":\"" + String(logBuffer[idx].timestamp) + "\",\"m\":\"" + String(logBuffer[idx].message) + "\"}";
    if (i < logCount - 1) json += ",";
  }
  json += "]";

    // Hardware GPIO Pinout Mapping
  json += "\"pins\":{\"dht\":14,\"soil\":34,\"peltier\":19,\"fan\":18,\"blower\":5,\"light\":17,\"spray1\":16,\"spray2\":4,\"servo1\":25,\"servo2\":26,\"led\":27},";

  // Sensor Raw ADC & Calculations
  float soilVolts = (float)state.lastRawSoilAdc * (3.3f / 4095.0f);
  json += "\"rawSoilAdc\":" + String(state.lastRawSoilAdc) + ",";
  json += "\"soilVoltage\":" + String(soilVolts, 2) + ",";
  json += "\"vpSat\":" + String(metrics.vpSat, 3) + ",";
  json += "\"vpAct\":" + String(metrics.vpAct, 3) + ",";

  // Active Agronomic Thresholds
  json += "\"thresholds\":{";
  json += "\"tempMin\":" + String(state.targetTempMin, 1) + ",";
  json += "\"tempMax\":" + String(state.targetTempMax, 1) + ",";
  json += "\"rhMin\":" + String(state.targetRhMin, 1) + ",";
  json += "\"rhMax\":" + String(state.targetRhMax, 1) + ",";
  json += "\"soilMin\":" + String(state.targetSoilMin, 1) + ",";
  json += "\"soilMax\":" + String(state.targetSoilMax, 1) + ",";
  json += "\"dryRaw\":" + String(state.soilDryRaw) + ",";
  json += "\"wetRaw\":" + String(state.soilWetRaw);
  json += "},";

  json += "}";

  server.send(200, "application/json", json);
}

void handleControlApi() {
  handleCORS();
  if (server.method() == HTTP_OPTIONS) {
    server.send(204);
    return;
  }

  if (!server.hasArg("action")) {
    server.send(400, "application/json", "{\"error\":\"Missing action parameter\"}");
    return;
  }

  String action = server.arg("action");

  if (action == "mode") {
    String val = server.arg("val");
    state.autoMode = (val == "auto");
    addSystemLog(state.autoMode ? "Mode set to AUTOMATIC CLIMATE" : "Mode set to MANUAL OVERRIDE");
  } 
  else if (action == "phase") {
    int p = server.arg("val").toInt();
    if (p == 1) {
      state.phase = PHASE_GERMINATION_BLACKOUT;
      addSystemLog("Switched to Phase 1: Germination Blackout");
    } else if (p == 2) {
      state.phase = PHASE_AUTOTROPHIC_LIGHT;
      addSystemLog("Switched to Phase 2: Autotrophic Light");
    }
  } 
  else if (action == "day") {
    int d = server.arg("val").toInt();
    state.dayCounter = constrain(d, 1, 10);
    // Automatic phase synchronization with day counter
    if (state.dayCounter <= 3) {
      state.phase = PHASE_GERMINATION_BLACKOUT;
    } else {
      state.phase = PHASE_AUTOTROPHIC_LIGHT;
    }
    char msg[32];
    snprintf(msg, sizeof(msg), "Day counter set to: Day %u", state.dayCounter);
    addSystemLog(msg);
  } 
  else if (action == "tank") {
    int t = server.arg("val").toInt();
    if (t == 1 || t == 2) {
      state.activeTank = t;
      char msg[32];
      snprintf(msg, sizeof(msg), "Active Tank set to: Tank %u", state.activeTank);
      addSystemLog(msg);
    }
  } 
  else if (action == "flush") {
    // 5-second emergency flush with Tank 1
    state.emergencyFlushActive = true;
    state.emergencyFlushStartTime = millis();
    applySprayAndValves(true, false);
    addSystemLog("EMERGENCY FLUSH: Tank 1 spray activated 5s");
  } 
  else if (action == "refill") {
    int t = server.arg("tank").toInt();
    if (t == 1) {
      state.tank1VolumeMl = TANK_MAX_CAPACITY_ML;
      addSystemLog("Tank 1 refilled to 1000 mL");
    } else if (t == 2) {
      state.tank2VolumeMl = TANK_MAX_CAPACITY_ML;
      addSystemLog("Tank 2 refilled to 1000 mL");
    }
  } 
  else if (action == "toggleRelay") {
    // If in Auto mode, automatically switch to Manual override so the user's direct command takes effect
    if (state.autoMode) {
      state.autoMode = false;
      addSystemLog("Relay toggled: Mode automatically switched to MANUAL OVERRIDE");
    }
    int ch = server.arg("ch").toInt();
    switch (ch) {
      case 1: // Peltier (GPIO 19) with Interlock to Fan (GPIO 18)
        applyPeltierInterlock(!state.peltier);
        addSystemLog(state.peltier ? "Manual: Peltier (GPIO 19) ON (Interlock Fan Active)" : "Manual: Peltier (GPIO 19) OFF");
        break;
      case 2: // Heatsink Fan (GPIO 18)
        applyHeatsinkFan(!state.heatsinkFan);
        addSystemLog(state.heatsinkFan ? "Manual: Heatsink Fan (GPIO 18) ON" : "Manual: Heatsink Fan (GPIO 18) OFF");
        break;
      case 3: // Blower (GPIO 5)
        applyBlower(!state.blower);
        addSystemLog(state.blower ? "Manual: Blower (GPIO 5) ON" : "Manual: Blower (GPIO 5) OFF");
        break;
      case 4: // Grow Light (GPIO 17)
        applyGrowLight(!state.growLight);
        addSystemLog(state.growLight ? "Manual: Grow Light (GPIO 17) ON" : "Manual: Grow Light (GPIO 17) OFF");
        break;
      case 5: // Spray T1 (GPIO 16)
        applySprayAndValves(!state.sprayT1, false);
        addSystemLog(state.sprayT1 ? "Manual: Spray T1 (GPIO 16) ON" : "Manual: Spray T1 (GPIO 16) OFF");
        break;
      case 6: // Spray T2 (GPIO 4)
        applySprayAndValves(false, !state.sprayT2);
        addSystemLog(state.sprayT2 ? "Manual: Spray T2 (GPIO 4) ON" : "Manual: Spray T2 OFF");
        break;
      default:
        break;
    }
  }
  else if (action == "setServo") {
    int s = server.arg("servo").toInt();
    int angle = constrain(server.arg("angle").toInt(), 0, 90);
    if (s == 1) {
      servoValve1.write(angle);
      state.valve1Angle = angle;
      char msg[40]; snprintf(msg, sizeof(msg), "Servo 1 (GPIO 25) set to %d deg", angle);
      addSystemLog(msg);
    } else if (s == 2) {
      servoValve2.write(angle);
      state.valve2Angle = angle;
      char msg[40]; snprintf(msg, sizeof(msg), "Servo 2 (GPIO 26) set to %d deg", angle);
      addSystemLog(msg);
    }
  }
  else if (action == "setThreshold") {
    String param = server.arg("param");
    float val = server.arg("val").toFloat();
    if (param == "tempMin") state.targetTempMin = val;
    else if (param == "tempMax") state.targetTempMax = val;
    else if (param == "rhMin") state.targetRhMin = val;
    else if (param == "rhMax") state.targetRhMax = val;
    else if (param == "soilMin") state.targetSoilMin = val;
    else if (param == "soilMax") state.targetSoilMax = val;
    addSystemLog("Agronomic threshold parameter updated.");
  }

  server.send(200, "application/json", "{\"success\":true}");
}

// =========================================================================================
// 9. SETUP & INITIALIZATION ROUTINE
// =========================================================================================

void setup() {
  Serial.begin(115200);
  delay(500); // Allow power rails to stabilize

  Serial.println("\n========================================================");
  Serial.println("  SMART HYPER-MONITORED BIOSPHERE CABIN - ESP32 STARTUP ");
  Serial.println("  Cultivation Engine for Broccoli Microgreens           ");
  Serial.println("========================================================");

  // 1. Safe Relay Initialization (Active-LOW Protection)
  // Set output states to HIGH (OFF) BEFORE enabling OUTPUT mode to prevent relay chatter
  digitalWrite(PIN_RELAY_PELTIER, RELAY_TRIGGER_OFF);
  digitalWrite(PIN_RELAY_FAN_HEATSINK, RELAY_TRIGGER_OFF);
  digitalWrite(PIN_RELAY_BLOWER, RELAY_TRIGGER_OFF);
  digitalWrite(PIN_RELAY_GROWLIGHT, RELAY_TRIGGER_OFF);
  digitalWrite(PIN_RELAY_SPRAY_T1, RELAY_TRIGGER_OFF);
  digitalWrite(PIN_RELAY_SPRAY_T2, RELAY_TRIGGER_OFF);

  pinMode(PIN_RELAY_PELTIER, OUTPUT);
  pinMode(PIN_RELAY_FAN_HEATSINK, OUTPUT);
  pinMode(PIN_RELAY_BLOWER, OUTPUT);
  pinMode(PIN_RELAY_GROWLIGHT, OUTPUT);
  pinMode(PIN_RELAY_SPRAY_T1, OUTPUT);
  pinMode(PIN_RELAY_SPRAY_T2, OUTPUT);

  // 2. Status LED & Sensor Pins
  pinMode(PIN_STATUS_LED, OUTPUT);
  digitalWrite(PIN_STATUS_LED, LOW);
  pinMode(PIN_SOIL_ADC, INPUT);

  // 3. Mechanical Servomotor Valve Initialization
  servoValve1.attach(PIN_SERVO_VALVE_T1);
  servoValve2.attach(PIN_SERVO_VALVE_T2);
  servoValve1.write(0); // 0° Closed
  servoValve2.write(0); // 0° Closed

  // 4. Initialize State Structures
  state.autoMode = true;
  state.phase = PHASE_GERMINATION_BLACKOUT;
  state.dayCounter = 1;
  state.activeTank = 1;
  state.peltier = false;
  state.heatsinkFan = false;
  state.blower = false;
  state.growLight = false;
  state.sprayT1 = false;
  state.sprayT2 = false;
  state.valve1Angle = 0;
  state.valve2Angle = 0;
  state.tank1VolumeMl = TANK_MAX_CAPACITY_ML;
  state.tank2VolumeMl = TANK_MAX_CAPACITY_ML;
  state.purgeActive = false;
  state.purgeStartTime = 0;
  state.lastPurgeCheckTime = 0;
  state.emergencyFlushActive = false;
  state.emergencyFlushStartTime = 0;
  state.autoSprayActive = false;
  state.autoSprayStartTime = 0;
  state.lastAutoSprayTime = 0;

  // 5. Initialize DHT22 Sensor
  dht.begin();
  addSystemLog("Hardware pins & sensors initialized.");

  // 6. Connect to Wi-Fi Network
  Serial.printf("Connecting to Wi-Fi SSID: %s ", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint32_t wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - wifiStart < 12000UL)) {
    delay(400);
    Serial.print(".");
    digitalWrite(PIN_STATUS_LED, !digitalRead(PIN_STATUS_LED));
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[Wi-Fi] Connected successfully!");
    Serial.printf("[Wi-Fi] Assigned IP Address: %s\n", WiFi.localIP().toString().c_str());
    char msg[64];
    snprintf(msg, sizeof(msg), "WiFi Connected: %s", WiFi.localIP().toString().c_str());
    addSystemLog(msg);
  } else {
    Serial.println("\n[Wi-Fi] Connection timed out. Starting Emergency Fallback AP...");
    WiFi.mode(WIFI_AP);
    WiFi.softAP("BroccoliCabin_AP", "biosphere123");
    Serial.printf("[Wi-Fi AP] Fallback AP Started. IP: %s\n", WiFi.softAPIP().toString().c_str());
    addSystemLog("WiFi STA timeout; Fallback AP Started.");
  }

  // 7. Register Web Server Routes & Start
  server.on("/", HTTP_GET, handleRoot);
  server.on("/", HTTP_OPTIONS, handleOptions);
  server.on("/api/telemetry", HTTP_GET, handleTelemetryApi);
  server.on("/api/telemetry", HTTP_OPTIONS, handleOptions);
  server.on("/api/control", HTTP_ANY, handleControlApi);
  server.on("/api/control", HTTP_OPTIONS, handleOptions);

  server.begin();
  addSystemLog("Web Server listening on Port 80.");
  addSystemLog("Autonomous Climate Loop initialized.");
}

// =========================================================================================
// 10. MAIN NON-BLOCKING SUPERVISORY LOOP (ZERO delay())
// =========================================================================================

void loop() {
  // Always handle HTTP client requests immediately
  server.handleClient();

  uint32_t currentMillis = millis();

  // 1. Sensor Polling Routine (Every 2000 ms - Non-blocking DHT22 & ADC)
  if (currentMillis - lastSensorPollTime >= 2000UL) {
    lastSensorPollTime = currentMillis;
    pollSensors();
  }

  // 2. Closed-Loop Climate Control Logic (Every 1000 ms)
  if (currentMillis - lastClimateLoopTime >= 1000UL) {
    lastClimateLoopTime = currentMillis;
    executeAutomatedClimateControl();
  }

  // 3. Anti-Fungal Purge Cycle Routine (30-minute interval check, 45s blower)
  processAntiFungalPurge();

  // 4. Emergency Flush Routine (5-second Tank 1 spray)
  processEmergencyFlush();

  // 5. Tank Reservoir Depletion Integrator
  updateTankVolumes();

  // 6. System Status LED Heartbeat / Fault Signaler
  processStatusLed();

  // 7. Simulated Cultivation Day Tracker (Advances day counter every 24 hours)
  if (currentMillis - lastDayIncrementTime >= (24UL * 3600UL * 1000UL)) {
    lastDayIncrementTime = currentMillis;
    if (state.dayCounter < 10) {
      state.dayCounter++;
      if (state.dayCounter > 3) {
        state.phase = PHASE_AUTOTROPHIC_LIGHT;
      }
      char msg[40];
      snprintf(msg, sizeof(msg), "Sequencer auto-advanced to Day %u", state.dayCounter);
      addSystemLog(msg);
    }
  }
}
