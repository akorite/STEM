const http = require('http');
const fs = require('fs');

function getIndexHtml() {
  try {
    if (fs.existsSync('dashboard.html')) return fs.readFileSync('dashboard.html', 'utf8');
  } catch (e) {}
  const code = fs.readFileSync('BroccoliBiosphereCabin.ino', 'utf8');
  const startMarker = 'const char INDEX_HTML[] PROGMEM = R"rawliteral(';
  const endMarker = ')rawliteral";';
  const startIdx = code.indexOf(startMarker);
  const endIdx = code.indexOf(endMarker);

  if (startIdx === -1 || endIdx === -1) {
    throw new Error('Failed to locate INDEX_HTML markers in BroccoliBiosphereCabin.ino');
  }
  return code.substring(startIdx + startMarker.length, endIdx);
}

let state = {
  ip: "127.0.0.1:8080",
  uptimeSec: 620,
  status: "OPTIMAL",
  autoMode: true,
  phase: 2,
  day: 5,
  activeTank: 1,
  temp: 20.4,
  rh: 57.6,
  soil: 56.4,
  vpd: 0.62,
  vpSat: 2.34,
  vpAct: 1.40,
  rawSoilAdc: 2240,
  soilVoltage: 1.81,
  pcs: 97.4,
  tank1Vol: 885.0,
  tank2Vol: 920.0,
  relays: {
    peltier: false,
    fan: false,
    blower: false,
    light: true,
    spray1: false,
    spray2: false
  },
  valves: {
    v1: 0,
    v2: 0
  },
  thresholds: {
    tempMin: 18.0,
    tempMax: 22.0,
    rhMin: 50.0,
    rhMax: 65.0,
    soilMin: 45.0,
    soilMax: 70.0
  },
  pins: {
    dht: 14,
    soil: 34,
    peltier: 19,
    fan: 18,
    blower: 5,
    light: 17,
    spray1: 16,
    spray2: 4,
    servo1: 25,
    servo2: 26,
    led: 27
  },
  history: {
    temp: [20.1, 20.2, 20.1, 20.3, 20.5, 20.4, 20.3, 20.5, 20.3, 20.2, 20.4, 20.3, 20.3, 20.4, 20.5, 20.4, 20.3, 20.4, 20.3, 20.4],
    rh: [58.0, 58.5, 59.0, 58.8, 57.9, 58.1, 58.3, 58.0, 57.8, 58.2, 58.4, 58.1, 58.0, 57.9, 58.2, 58.0, 58.3, 58.1, 58.0, 57.6]
  },
  logs: [
    { t: "00:10:15", m: "Hardware GPIO Matriks Interaktif Aktif (Port 8080)" },
    { t: "00:09:00", m: "Brocco Maskot Berjalan Aktif di Pet Studio 🌸" },
    { t: "00:07:30", m: "Kondisi Prima: Suhu 20.4°C, RH 57.6%, VPD 0.62 kPa ✨" },
    { t: "00:05:42", m: "Siklus Otomatis Aktif - Skor Kenyamanan 97.4%" },
    { t: "00:00:00", m: "Inisialisasi Pengontrol Kabin Biosfer Brokoli..." }
  ]
};

const server = http.createServer((req, res) => {
  const url = new URL(req.url, `http://${req.headers.host}`);

  if (url.pathname === '/' || url.pathname === '/index.html') {
    try {
      const html = getIndexHtml();
      res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' });
      res.end(html);
    } catch (err) {
      res.writeHead(500, { 'Content-Type': 'text/plain' });
      res.end('Error loading dashboard: ' + err.message);
    }
  } else if (url.pathname === '/api/telemetry') {
    state.uptimeSec += 1;
    const h = String(Math.floor(state.uptimeSec / 3600)).padStart(2, '0');
    const m = String(Math.floor((state.uptimeSec % 3600) / 60)).padStart(2, '0');
    const s = String(state.uptimeSec % 60).padStart(2, '0');

    // Natural gentle breathing variation in temperature and humidity
    state.temp = +(20.3 + Math.sin(state.uptimeSec * 0.15) * 0.25).toFixed(1);
    state.rh = +(57.6 + Math.cos(state.uptimeSec * 0.15) * 0.4).toFixed(1);

    // Dynamic Tetens VPD computation
    const vpSat = 0.61078 * Math.exp((17.27 * state.temp) / (state.temp + 237.3));
    const vpAct = vpSat * (state.rh / 100.0);
    state.vpSat = +vpSat.toFixed(2);
    state.vpAct = +vpAct.toFixed(2);
    state.vpd = +(vpSat - vpAct).toFixed(2);

    // Soil ADC raw variation
    state.rawSoilAdc = Math.round(3200 - (state.soil / 100) * (3200 - 1400));
    state.soilVoltage = +(state.rawSoilAdc * (3.3 / 4095)).toFixed(2);

    if (state.uptimeSec % 3 === 0) {
      state.history.temp.shift();
      state.history.temp.push(state.temp);
      state.history.rh.shift();
      state.history.rh.push(state.rh);
    }

    const payload = {
      ip: state.ip,
      uptime: `${h}:${m}:${s}`,
      status: state.status,
      autoMode: state.autoMode,
      phase: state.phase,
      day: state.day,
      activeTank: state.activeTank,
      temp: state.temp,
      rh: state.rh,
      soil: state.soil,
      vpd: state.vpd,
      vpSat: state.vpSat,
      vpAct: state.vpAct,
      rawSoilAdc: state.rawSoilAdc,
      soilVoltage: state.soilVoltage,
      pcs: state.pcs,
      tank1Vol: state.tank1Vol,
      tank2Vol: state.tank2Vol,
      relays: state.relays,
      valves: state.valves,
      thresholds: state.thresholds,
      pins: state.pins,
      history: state.history,
      logs: state.logs
    };

    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify(payload));
  } else if (url.pathname === '/api/control') {
    const action = url.searchParams.get('action');
    const nowTs = new Date().toTimeString().split(' ')[0];

    if (action === 'flush') {
      state.relays.spray1 = true;
      state.valves.v1 = 90;
      state.tank1Vol = Math.max(0, +(state.tank1Vol - 4.0).toFixed(1));
      state.logs.unshift({ t: nowTs, m: "Aksi Pengasuhan: Beri Minum (Semprot Air Baku Tangki 1 GPIO 16 5s)" });
      setTimeout(() => {
        state.relays.spray1 = false;
        state.valves.v1 = 0;
      }, 5000);
    } else if (action === 'toggleRelay') {
      const ch = url.searchParams.get('ch');
      if (state.autoMode) {
        state.autoMode = false; // Auto switch to manual override on direct hardware click
        state.logs.unshift({ t: nowTs, m: "Mode Otomatis dialihkan ke MANUAL OVERRIDE oleh klik relay" });
      }

      if (ch === '1') {
        state.relays.peltier = !state.relays.peltier;
        if (state.relays.peltier) {
          state.relays.fan = true; // Thermal safety interlock
          state.logs.unshift({ t: nowTs, m: "Manual: Peltier (GPIO 19) ON + Heatsink Fan (GPIO 18) Auto-Locked ON 🔒" });
        } else {
          state.logs.unshift({ t: nowTs, m: "Manual: Peltier (GPIO 19) OFF" });
        }
      } else if (ch === '2') {
        state.relays.fan = !state.relays.fan;
        if (!state.relays.fan && state.relays.peltier) {
          state.relays.peltier = false; // Thermal safety interlock
          state.logs.unshift({ t: nowTs, m: "Proteksi Interlock: Peltier (GPIO 19) dipadamkan karena Heatsink Fan (GPIO 18) mati!" });
        } else {
          state.logs.unshift({ t: nowTs, m: `Manual: Heatsink Fan (GPIO 18) ${state.relays.fan ? 'ON' : 'OFF'}` });
        }
      } else if (ch === '3') {
        state.relays.blower = !state.relays.blower;
        state.logs.unshift({ t: nowTs, m: `Manual: DC Blower (GPIO 5) ${state.relays.blower ? 'ON' : 'OFF'}` });
      } else if (ch === '4') {
        state.relays.light = !state.relays.light;
        state.logs.unshift({ t: nowTs, m: `Manual: Grow Light (GPIO 17) ${state.relays.light ? 'ON' : 'OFF'}` });
      } else if (ch === '5') {
        state.relays.spray1 = !state.relays.spray1;
        state.valves.v1 = state.relays.spray1 ? 90 : 0;
        state.logs.unshift({ t: nowTs, m: `Manual: Spray T1 (GPIO 16) ${state.relays.spray1 ? 'ON (Katup 1 90°)' : 'OFF (Katup 1 0°)'}` });
      } else if (ch === '6') {
        state.relays.spray2 = !state.relays.spray2;
        state.valves.v2 = state.relays.spray2 ? 90 : 0;
        state.logs.unshift({ t: nowTs, m: `Manual: Spray T2 (GPIO 4) ${state.relays.spray2 ? 'ON (Katup 2 90°)' : 'OFF (Katup 2 0°)'}` });
      }
    } else if (action === 'setServo') {
      const s = url.searchParams.get('servo');
      const angle = parseInt(url.searchParams.get('angle'), 10) || 0;
      if (s === '1') {
        state.valves.v1 = angle;
        state.logs.unshift({ t: nowTs, m: `Manual Servo 1 (GPIO 25): Putar ke ${angle}°` });
      } else if (s === '2') {
        state.valves.v2 = angle;
        state.logs.unshift({ t: nowTs, m: `Manual Servo 2 (GPIO 26): Putar ke ${angle}°` });
      }
    } else if (action === 'setThreshold') {
      const param = url.searchParams.get('param');
      const val = parseFloat(url.searchParams.get('val'));
      if (state.thresholds[param] !== undefined) {
        state.thresholds[param] = val;
        state.logs.unshift({ t: nowTs, m: `Ambang Batas ${param} disetel ke ${val}` });
      }
    } else if (action === 'calibrateSoil') {
      const type = url.searchParams.get('type');
      state.logs.unshift({ t: nowTs, m: `Kalibrasi Titik ${type.toUpperCase()}: ADC ${state.rawSoilAdc} tersimpan` });
    } else if (action === 'mode') {
      state.autoMode = (url.searchParams.get('val') === 'auto');
      state.logs.unshift({ t: nowTs, m: `Mode Kontrol: ${state.autoMode ? 'Otomatis' : 'Manual Override'}` });
    } else if (action === 'phase') {
      state.phase = parseInt(url.searchParams.get('val'), 10) || 1;
      state.logs.unshift({ t: nowTs, m: `Fase Tumbuh: ${state.phase === 1 ? 'Perkecambahan' : 'Autotrofik'}` });
    } else if (action === 'day') {
      state.day = parseInt(url.searchParams.get('val'), 10) || 1;
      state.logs.unshift({ t: nowTs, m: `Penyetel Hari: Hari ke-${state.day}` });
    } else if (action === 'refill') {
      const tank = url.searchParams.get('tank');
      if (tank === '1') {
        state.tank1Vol = 1000.0;
        state.logs.unshift({ t: nowTs, m: "Tangki 1 (Air Baku) Diisi Ulang ke 1000 mL" });
      }
      if (tank === '2') {
        state.tank2Vol = 1000.0;
        state.logs.unshift({ t: nowTs, m: "Tangki 2 (Nutrisi Mikro) Diisi Ulang ke 1000 mL" });
      }
    }

    if (state.logs.length > 12) state.logs.pop();

    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({ success: true }));
  } else {
    res.writeHead(404);
    res.end('Not Found');
  }
});

server.listen(8080, '127.0.0.1', () => {
  console.log('Kebun Biosfer Web Server running at http://127.0.0.1:8080/');
});
