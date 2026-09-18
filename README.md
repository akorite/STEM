# Kebun Biosfer — Broccoli Biosphere Cabin 🥦🌱

Automated climate-controlled plant chamber firmware and high-density admin telemetry console.

## Architecture

The project is decoupled into two clean tiers:

```
├── BroccoliBiosphereCabin.ino   # Headless ESP32 C++ Firmware (REST API & Control Engine)
├── server.js                    # Node.js local development simulation & static server
├── index.html                   # Web root entry redirecting to public/
└── public/                      # Standalone Admin Web Component (Zero Dependencies)
    ├── index.html               # Unscrollable single-page viewport structure with Brocco companion
    ├── style.css                # Modern pastel botanical sanctuary styling & responsive design
    └── app.js                   # Telemetry poller, pastel SVG charts, interactive mascot, GPIO controls
```

### 1. Headless ESP32 REST API (`BroccoliBiosphereCabin.ino`)
- Pure embedded C++ firmware with zero embedded HTML overhead (2,100+ lines of HTML removed).
- Built-in CORS support (`Access-Control-Allow-Origin: *`, `OPTIONS` preflight handlers) allowing external web frontends to query the ESP32 directly from any browser or origin.
- Non-blocking closed-loop control: Tetens VPD calculation, Plant Comfort Score (PCS), thermal safety interlock (Peltier cooler auto-locks heatsink fan), anti-fungal purge cycles, and dual-tank automated misting.
- Endpoints:
  - `GET /`: API status and discovery JSON
  - `GET /api/telemetry`: Real-time sensory data, relay states, 20-point history, and logs
  - `POST /api/control`: Parameter setpoints, relay overrides, servo angles, and purge commands

### 2. Standalone Modern Pastel Telemetry Console (`public/`)
- **Ergonomic 100vh Viewport**: Engineered to fit standard desktop displays (`100vh`) without vertical scrolling, with graceful mobile/tablet responsive flow.
- **Modern Pastel Botanical Aesthetics**:
  - **Plant & Soil Sanctuary**: Real-time KPI readouts for Air Temp, Relative Humidity, Soil Moisture, Tetens VPD, and Plant Comfort Score (PCS), paired with an interactive animated **Brocco Sprout** seedling companion.
  - **Tactile Setpoint Steppers**: Dedicated `−` and `+` adjustment steppers with real-time range preview and dirty-state feedback (`SAVE TARGETS ✨`).
  - **Pastel Fluidics Vials**: Dual reservoir test tubes (Water & Nutrients) with graduated levels, floating animated bubbles, and one-tap refill/switch actions.
  - **Pastel Strip-Chart**: Rolling 20-sample historical telemetry graph with mint & sky curves, target comfort band (18.0 - 22.0°C), and live statistics.
  - **Bouncy Actuator Switchboard**: 6-channel tactile relay toggles (Peltier cooler, heatsink fan, blower, grow LED, dual ultrasonic sprayers), dual servomotor flow faders, and a soft coral Emergency Flush button.
  - **Live Activity Audit Feed**: Chronological event log with clean category tags (`[AUTO]`, `[RELAY]`, `[SYS]`, `[LOCK]`, `[CRIT]`).
  - **Dual-Target Connectivity**: Seamlessly switch between local mock simulation (`http://127.0.0.1:8080`) and physical ESP32 IP.

---

## Getting Started

### Local Development (Simulation)
1. Start the simulation server:
   ```bash
   node server.js
   ```
2. Open your browser to:
   ```
   http://127.0.0.1:8080/
   ```

### Connecting to Physical Hardware
1. Flash `BroccoliBiosphereCabin.ino` to your ESP32 using the Arduino IDE.
2. Note the IP assigned to your ESP32 in the Serial Monitor (or default fallback AP `192.168.4.1`).
3. Open the web dashboard and enter the ESP32 URL in the **TARGET** field at the top right, then click **CONNECT**.
