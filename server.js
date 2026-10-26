const http = require('http');
const fs = require('fs');
const path = require('path');

const PUBLIC_DIR = path.join(__dirname, 'public');
const DASHBOARD_FILE = path.join(__dirname, 'dashboard.html');
const PORT = 8080;

const MIME_TYPES = {
  '.html': 'text/html; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.js': 'application/javascript; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.svg': 'image/svg+xml',
  '.png': 'image/png',
  '.ico': 'image/x-icon',
  '.txt': 'text/plain; charset=utf-8'
};

// ---------------------------------------------------------------------------
// Simulation state (mirrors the ESP32 JSON contract)
// ---------------------------------------------------------------------------
let state = {
  ip: `127.0.0.1:${PORT}`,
  uptimeSec: 620,
  autoMode: true,
  phase: 2,
  day: 5,
  temp: 19.0,
  rh: 64.0,
  soil1: 56.4,
  soil2: 48.2,
  vpSat: 2.20,
  vpAct: 1.41,
  vpd: 0.79,
  pcs: 91.0,
  adc1: 2240,
  adc2: 2600,
  tank1Vol: 885.0,
  tank2Vol: 920.0,
  relays: {
    peltier: false,
    fan: false,
    spare: false,
    growLight: true,
    ledStrip: true,
    pump1: false,
    pump2: false
  },
  thresholds: {
    tempMin: 18.0,
    tempMax: 22.0,
    rhMin: 50.0,
    rhMax: 65.0,
    soilMin: 45.0,
    soilMax: 70.0,
    dry1: 3200,
    wet1: 1400,
    dry2: 3200,
    wet2: 1400
  },
  pins: {
    dht: 4,
    soil1: 34,
    soil2: 35,
    peltier: 25,
    spare: 26,
    fan: 27,
    growLight: 14,
    ledStrip: 32,
    pump1: 19,
    pump2: 18,
    statusLed: 2
  },
  health: {
    dhtOk: true,
    dhtAgeMs: 900,
    dhtFails: 0,
    dhtReinits: 0,
    dhtRecoveries: 0,
    soil1Ok: true,
    soil1AgeMs: 700,
    soil1Fails: 0,
    soil1Recoveries: 0,
    soil2Ok: true,
    soil2AgeMs: 700,
    soil2Fails: 0,
    soil2Recoveries: 0,
    wifiConnected: true,
    wifiRssi: -48,
    wifiReconnects: 0,
    wifiAttempts: 0,
    apFallback: false,
    heap: 242000,
    uptimeMs: 620000,
    sensorsOk: true
  },
  history: {
    temp: [19.0, 19.1, 19.0, 19.1, 19.2, 19.1, 19.0, 19.1, 19.0, 19.0, 19.1, 19.0, 19.0, 19.1, 19.1, 19.0, 19.0, 19.0, 19.0, 19.0],
    rh: [64.0, 64.2, 64.4, 64.3, 64.1, 64.0, 64.1, 64.0, 63.9, 64.0, 64.2, 64.1, 64.0, 64.0, 64.1, 64.0, 64.0, 64.0, 64.0, 64.0],
    soil1: [55.0, 55.2, 55.4, 55.6, 55.8, 56.0, 56.1, 56.2, 56.2, 56.3, 56.3, 56.4, 56.4, 56.4, 56.4, 56.4, 56.4, 56.4, 56.4, 56.4],
    soil2: [49.5, 49.4, 49.3, 49.2, 49.1, 49.0, 48.9, 48.8, 48.7, 48.6, 48.5, 48.4, 48.3, 48.2, 48.2, 48.2, 48.2, 48.2, 48.2, 48.2]
  },
  logs: [
    { t: '00:10:15', m: 'Firmware v2 online: 7 relay channels, dual soil probes' },
    { t: '00:09:00', m: 'Telemetry stable: 19.0 C, RH 64.0%, VPD 0.79 kPa' },
    { t: '00:07:30', m: 'Dual-zone watering engine initialized' },
    { t: '00:05:42', m: 'Automatic climate control active - PCS 88.8%' },
    { t: '00:00:00', m: 'Biosphere cabin controller started' }
  ]
};

const pumpTimers = { 1: null, 2: null };
const pumpLastStop = { 1: 0, 2: 0 };
let lastHistoryPush = 0;

function timeString() {
  return new Date().toTimeString().split(' ')[0];
}

function log(message) {
  state.logs.unshift({ t: timeString(), m: message });
  if (state.logs.length > 12) state.logs.pop();
}

function clamp(value, min, max) {
  return Math.min(max, Math.max(min, value));
}

function calcVpd(temp, rh) {
  const vpSat = 0.61078 * Math.exp((17.27 * temp) / (temp + 237.3));
  const vpAct = vpSat * (rh / 100.0);
  return { vpSat, vpAct, vpd: Math.max(0, vpSat - vpAct) };
}

function calcPcs(temp, rh, vpd) {
  let tScore = 1.0;
  const tDev = Math.abs(temp - 20.0);
  if (tDev <= 2.0) tScore = 1.0 - (tDev / 2.0) * 0.15;
  else if (temp > 24.0) tScore = Math.max(0, 0.4 - (temp - 24.0) * 0.15);
  else tScore = Math.max(0.1, 0.85 - ((tDev - 2.0) / 2.0) * 0.45);

  let rhScore = 1.0;
  const rhDev = Math.abs(rh - 57.5);
  if (rhDev <= 7.5) rhScore = 1.0 - (rhDev / 7.5) * 0.15;
  else rhScore = Math.max(0, 0.85 - ((rhDev - 7.5) / 20.0) * 0.70);

  let vpdScore = 1.0;
  const vpdDev = Math.abs(vpd - 0.60);
  if (vpdDev <= 0.20) vpdScore = 1.0 - (vpdDev / 0.20) * 0.15;
  else vpdScore = Math.max(0, 0.85 - ((vpdDev - 0.20) / 0.50) * 0.75);

  return clamp((tScore * 0.40 + rhScore * 0.30 + vpdScore * 0.30) * 100.0, 0, 100);
}

function updateStatus() {
  const minSoil = Math.min(state.soil1, state.soil2);
  if (state.temp > 24 || state.rh > 75 || minSoil < 35) {
    state.status = 'KRITIS';
  } else if (
    state.temp > state.thresholds.tempMax || state.temp < state.thresholds.tempMin ||
    state.rh > state.thresholds.rhMax || state.rh < state.thresholds.rhMin ||
    minSoil < state.thresholds.soilMin || minSoil > state.thresholds.soilMax ||
    state.vpd < 0.40 || state.vpd > 0.80
  ) {
    state.status = 'WASPADA';
  } else {
    state.status = 'OPTIMAL';
  }
}

function startPump(pump, seconds, reason) {
  state.relays[`pump${pump}`] = true;
  if (pumpTimers[pump]) clearTimeout(pumpTimers[pump]);
  pumpTimers[pump] = setTimeout(() => {
    state.relays[`pump${pump}`] = false;
    pumpLastStop[pump] = state.uptimeSec;
    pumpTimers[pump] = null;
    log(`Pump ${pump} OFF after ${seconds}s (${reason})`);
  }, seconds * 1000);
  log(`Pump ${pump} ON for ${seconds}s (${reason})`);
}

function stopPump(pump, reason) {
  if (pumpTimers[pump]) {
    clearTimeout(pumpTimers[pump]);
    pumpTimers[pump] = null;
  }
  if (state.relays[`pump${pump}`]) {
    state.relays[`pump${pump}`] = false;
    pumpLastStop[pump] = state.uptimeSec;
    if (reason) log(`Pump ${pump} OFF (${reason})`);
  }
}

function simulateAutomation() {
  // Composition: grow light + LED strip follow the 16h photoperiod in phase 2.
  if (state.autoMode) {
    const lightOn = state.phase === 2;
    if (state.relays.growLight !== lightOn) {
      state.relays.growLight = lightOn;
      log(`Auto: grow light ${lightOn ? 'ON' : 'OFF'} (phase ${state.phase})`);
    }
    if (state.relays.ledStrip !== lightOn) {
      state.relays.ledStrip = lightOn;
      log(`Auto: LED strip ${lightOn ? 'ON' : 'OFF'}`);
    }

    if (state.temp > state.thresholds.tempMax && !state.relays.peltier) {
      state.relays.peltier = true;
      state.relays.fan = true;
      log('Auto: Peltier ON + fan interlocked (temperature above target)');
    } else if (state.temp <= state.thresholds.tempMin + 0.5 && state.relays.peltier) {
      state.relays.peltier = false;
      log('Auto: Peltier OFF (temperature target reached)');
    }

    if (state.rh > state.thresholds.rhMax && !state.relays.fan) {
      state.relays.fan = true;
      log('Auto: fan ON (humidity above target)');
    } else if (state.rh <= state.thresholds.rhMax - 5 && !state.relays.peltier && state.relays.fan) {
      state.relays.fan = false;
      log('Auto: fan OFF (humidity normalized)');
    }

    [1, 2].forEach((pump) => {
      const soil = pump === 1 ? state.soil1 : state.soil2;
      const alreadyOn = state.relays[`pump${pump}`];
      const cooledDown = state.uptimeSec - pumpLastStop[pump] >= 60;
      if (!alreadyOn && soil < state.thresholds.soilMin && cooledDown) {
        startPump(pump, 8, `soil ${soil.toFixed(1)}% below target`);
      }
    });
  }
}

function telemetryTick() {
  state.uptimeSec += 1;

  const targetTemp = 19.2 + Math.sin(state.uptimeSec * 0.15) * 0.15 -
    (state.relays.peltier ? 0.9 : 0) + (state.relays.pump1 || state.relays.pump2 ? 0.05 : 0);
  const targetRh = 64.5 + Math.cos(state.uptimeSec * 0.15) * 0.15 +
    (state.relays.pump1 || state.relays.pump2 ? 1.5 : 0) - (state.relays.fan ? 2.5 : 0);

  state.temp += (targetTemp - state.temp) * 0.18;
  state.rh += (targetRh - state.rh) * 0.18;
  state.temp = +clamp(state.temp, 15, 30).toFixed(2);
  state.rh = +clamp(state.rh, 20, 95).toFixed(2);

  if (state.relays.pump1) state.soil1 = Math.min(95, state.soil1 + 0.55);
  else state.soil1 = Math.max(0, state.soil1 - 0.006);
  if (state.relays.pump2) state.soil2 = Math.min(95, state.soil2 + 0.55);
  else state.soil2 = Math.max(0, state.soil2 - 0.006);

  state.soil1 = +state.soil1.toFixed(2);
  state.soil2 = +state.soil2.toFixed(2);

  state.adc1 = Math.round(state.thresholds.dry1 - (state.soil1 / 100) * (state.thresholds.dry1 - state.thresholds.wet1));
  state.adc2 = Math.round(state.thresholds.dry2 - (state.soil2 / 100) * (state.thresholds.dry2 - state.thresholds.wet2));

  const vpd = calcVpd(state.temp, state.rh);
  state.vpSat = +vpd.vpSat.toFixed(3);
  state.vpAct = +vpd.vpAct.toFixed(3);
  state.vpd = +vpd.vpd.toFixed(3);
  state.pcs = +calcPcs(state.temp, state.rh, state.vpd).toFixed(1);

  if (state.relays.pump1) state.tank1Vol = Math.max(0, state.tank1Vol - 0.08);
  if (state.relays.pump2) state.tank2Vol = Math.max(0, state.tank2Vol - 0.08);

  updateStatus();
  simulateAutomation();

  if (state.uptimeSec - lastHistoryPush >= 3) {
    lastHistoryPush = state.uptimeSec;
    ['temp', 'rh', 'soil1', 'soil2'].forEach((key) => {
      state.history[key].push(state[key]);
      if (state.history[key].length > 20) state.history[key].shift();
    });
  }
}

function payload() {
  const soilAvg = +((state.soil1 + state.soil2) / 2).toFixed(2);
  const h = String(Math.floor(state.uptimeSec / 3600)).padStart(2, '0');
  const m = String(Math.floor((state.uptimeSec % 3600) / 60)).padStart(2, '0');
  const s = String(state.uptimeSec % 60).padStart(2, '0');

  return {
    ip: state.ip,
    uptime: `${h}:${m}:${s}`,
    status: state.status,
    autoMode: state.autoMode,
    phase: state.phase,
    day: state.day,
    temp: +state.temp.toFixed(2),
    rh: +state.rh.toFixed(2),
    soil1: state.soil1,
    soil2: state.soil2,
    soil: soilAvg,
    adc1: state.adc1,
    adc2: state.adc2,
    soil1Voltage: +(state.adc1 * (3.3 / 4095)).toFixed(2),
    soil2Voltage: +(state.adc2 * (3.3 / 4095)).toFixed(2),
    rawSoilAdc: state.adc1,
    soilVoltage: +(state.adc1 * (3.3 / 4095)).toFixed(2),
    vpd: state.vpd,
    vpSat: state.vpSat,
    vpAct: state.vpAct,
    pcs: state.pcs,
    tank1Vol: +state.tank1Vol.toFixed(1),
    tank2Vol: +state.tank2Vol.toFixed(1),
    sensorFault: false,
    dhtFault: false,
    soil1Fault: false,
    soil2Fault: false,
    relays: state.relays,
    thresholds: state.thresholds,
    pins: state.pins,
    health: {
      ...state.health,
      uptimeMs: state.uptimeSec * 1000,
      dhtAgeMs: 900,
      soil1AgeMs: 700,
      soil2AgeMs: 700
    },
    ota: { web: false, arduino: false, hostname: null },
    history: state.history,
    logs: state.logs
  };
}

function serveStatic(req, res, filePath) {
  fs.readFile(filePath, (err, content) => {
    if (err) {
      if (err.code === 'ENOENT') {
        res.writeHead(404, { 'Content-Type': 'text/plain' });
        res.end('404 Not Found');
      } else {
        res.writeHead(500, { 'Content-Type': 'text/plain' });
        res.end('500 Internal Server Error: ' + err.message);
      }
      return;
    }
    const ext = path.extname(filePath).toLowerCase();
    const contentType = MIME_TYPES[ext] || 'application/octet-stream';
    res.writeHead(200, { 'Content-Type': contentType });
    res.end(content);
  });
}

function handleControl(url, res) {
  const action = url.searchParams.get('action');
  const nowTs = timeString();

  if (action === 'flush') {
    startPump(1, 5, 'emergency flush');
    startPump(2, 5, 'emergency flush');
    log('EMERGENCY FLUSH: both pumps ON for 5s');
  } else if (action === 'pulsePump') {
    const pump = parseInt(url.searchParams.get('pump'), 10) === 2 ? 2 : 1;
    const seconds = clamp(parseInt(url.searchParams.get('seconds'), 10) || 5, 1, 30);
    startPump(pump, seconds, 'manual pulse');
  } else if (action === 'toggleRelay') {
    const ch = parseInt(url.searchParams.get('ch'), 10);
    if (state.autoMode) {
      state.autoMode = false;
      log('Mode switched to MANUAL override by relay click');
    }

    if (ch === 1) {
      state.relays.peltier = !state.relays.peltier;
      if (state.relays.peltier) {
        state.relays.fan = true;
        log('Manual: Peltier ON + fan interlocked ON');
      } else {
        log('Manual: Peltier OFF');
      }
    } else if (ch === 2) {
      state.relays.fan = !state.relays.fan;
      if (!state.relays.fan && state.relays.peltier) {
        state.relays.peltier = false;
        log('Safety interlock: Peltier forced OFF because fan was turned OFF');
      } else {
        log(`Manual: fan ${state.relays.fan ? 'ON' : 'OFF'}`);
      }
    } else if (ch === 3) {
      state.relays.spare = !state.relays.spare;
      log(`Manual: spare relay ${state.relays.spare ? 'ON' : 'OFF'}`);
    } else if (ch === 4) {
      state.relays.growLight = !state.relays.growLight;
      log(`Manual: grow light ${state.relays.growLight ? 'ON' : 'OFF'}`);
    } else if (ch === 5) {
      state.relays.ledStrip = !state.relays.ledStrip;
      log(`Manual: LED strip ${state.relays.ledStrip ? 'ON' : 'OFF'}`);
    } else if (ch === 6) {
      if (state.relays.pump1) stopPump(1, 'manual');
      else startPump(1, 30, 'manual toggle');
    } else if (ch === 7) {
      if (state.relays.pump2) stopPump(2, 'manual');
      else startPump(2, 30, 'manual toggle');
    }
  } else if (action === 'mode') {
    state.autoMode = url.searchParams.get('val') === 'auto';
    log(`Control mode: ${state.autoMode ? 'AUTOMATIC' : 'MANUAL'}`);
  } else if (action === 'phase') {
    state.phase = parseInt(url.searchParams.get('val'), 10) === 1 ? 1 : 2;
    log(`Growth phase: ${state.phase === 1 ? 'germination blackout' : 'autotrophic light'}`);
  } else if (action === 'day') {
    state.day = clamp(parseInt(url.searchParams.get('val'), 10) || 1, 1, 10);
    state.phase = state.day <= 3 ? 1 : 2;
    log(`Cultivation day set to ${state.day}`);
  } else if (action === 'setThreshold') {
    const param = url.searchParams.get('param');
    const val = parseFloat(url.searchParams.get('val'));
    if (Object.prototype.hasOwnProperty.call(state.thresholds, param) && Number.isFinite(val)) {
      const limits = {
        tempMin: [5, 35], tempMax: [5, 40],
        rhMin: [10, 95], rhMax: [10, 95],
        soilMin: [5, 95], soilMax: [5, 95]
      };
      if (limits[param]) {
        state.thresholds[param] = clamp(val, limits[param][0], limits[param][1]);
        log(`Threshold ${param} set to ${state.thresholds[param]}`);
      }
    }
  } else if (action === 'calibrateSoil') {
    const sensor = parseInt(url.searchParams.get('sensor'), 10) === 2 ? 2 : 1;
    const type = url.searchParams.get('type');
    const adc = sensor === 2 ? state.adc2 : state.adc1;
    if (type === 'dry') state.thresholds[`dry${sensor}`] = adc;
    else if (type === 'wet') state.thresholds[`wet${sensor}`] = adc;
    log(`Soil ${sensor} ${type} calibration stored at ADC ${adc}`);
  } else if (action === 'refill') {
    const tank = parseInt(url.searchParams.get('tank'), 10) === 2 ? 2 : 1;
    if (tank === 1) state.tank1Vol = 1000;
    else state.tank2Vol = 1000;
    log(`Tank ${tank} estimate reset to 1000 mL`);
  } else if (action === 'reinitSensors') {
    state.health.dhtOk = true;
    state.health.soil1Ok = true;
    state.health.soil2Ok = true;
    state.health.dhtFails = 0;
    state.health.soil1Fails = 0;
    state.health.soil2Fails = 0;
    state.health.dhtReinits += 1;
    log('Sensor bus re-initialized (simulation)');
  } else if (action === 'reconnectWiFi') {
    state.health.wifiConnected = true;
    state.health.apFallback = false;
    state.health.wifiReconnects += 1;
    state.health.wifiRssi = -48;
    log('WiFi reconnect requested (simulation)');
  } else if (action === 'allOff') {
    state.autoMode = false;
    Object.keys(state.relays).forEach((key) => { state.relays[key] = false; });
    [1, 2].forEach((pump) => {
      if (pumpTimers[pump]) clearTimeout(pumpTimers[pump]);
      pumpTimers[pump] = null;
    });
    log('Manual: ALL outputs OFF');
  } else if (action === 'restart') {
    Object.keys(state.relays).forEach((key) => { state.relays[key] = false; });
    state.uptimeSec = 0;
    state.autoMode = true;
    state.health.wifiConnected = true;
    log('Controller restart requested (simulation reset)');
  } else {
    res.writeHead(400, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({ success: false, error: 'unknown action' }));
    return;
  }

  res.writeHead(200, { 'Content-Type': 'application/json' });
  res.end(JSON.stringify({ success: true, ts: nowTs }));
}

const server = http.createServer((req, res) => {
  res.setHeader('Access-Control-Allow-Origin', '*');
  res.setHeader('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
  res.setHeader('Access-Control-Allow-Headers', 'Content-Type, Origin, Authorization, Accept');

  if (req.method === 'OPTIONS') {
    res.writeHead(204);
    res.end();
    return;
  }

  const url = new URL(req.url, `http://${req.headers.host || `127.0.0.1:${PORT}`}`);
  const pathname = url.pathname;

  if (pathname === '/api/telemetry') {
    telemetryTick();
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify(payload()));
    return;
  }

  if (pathname === '/api/control') {
    handleControl(url, res);
    return;
  }

  // Standalone (waifu) dashboard, same file that is embedded in the ESP32 firmware.
  if (pathname === '/dashboard.html' || pathname === '/dashboard' || pathname === '/waifu') {
    serveStatic(req, res, DASHBOARD_FILE);
    return;
  }

  // OTA page preview in simulation. Uploading is hardware-only.
  if (pathname === '/update') {
    serveStatic(req, res, path.join(__dirname, 'ota.html'));
    return;
  }

  // Main modern console from public/.
  let safePath = path.normalize(pathname).replace(/^(\.\.[/\\])+/, '');
  if (safePath === '/' || safePath === '\\') safePath = '/index.html';
  serveStatic(req, res, path.join(PUBLIC_DIR, safePath));
});

server.listen(PORT, '127.0.0.1', () => {
  console.log(`Biosphere server running:`);
  console.log(`  Main console : http://127.0.0.1:${PORT}/`);
  console.log(`  Waifu console: http://127.0.0.1:${PORT}/dashboard.html`);
  console.log(`  API          : http://127.0.0.1:${PORT}/api/telemetry`);
});
