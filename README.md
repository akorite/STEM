# Kebun Biosfer — SchoolSTEM ESP32 Biosphere Cabin 🥦🌱

Automated climate and dual-zone watering controller for a broccoli microgreen cabin, with an ESP32 REST API and two web consoles.

This tree merges the original [`akorite/STEM`](https://github.com/akorite/STEM) firmware/console with the standalone `dashboard.html` work from PR #2, then remaps the firmware to the SchoolSTEM relay and sensor wiring.

## Hardware profile

### Relays

| Function | Relay board input | ESP32 pin | Wire colour | Notes |
|---|---|---|---|---|
| Peltier 12 V | 4-CH **IN1** | **GPIO 25** | Hitam | Cooling element |
| Spare actuator | 4-CH **IN2** | **GPIO 26** | Putih | Free / future use |
| Fan (outside + inside, parallel) | 4-CH **IN3** | **GPIO 27** | Abu-abu | Interlocked with Peltier |
| HPL grow light | 4-CH **IN4** | **GPIO 14** | Ungu | 16 h / 8 h photoperiod in phase 2 |
| LED strip / room light | 2-CH **IN1** | **GPIO 13** | Cokelat | Alternate: GPIO 12 |
| DC pump 1 / zone A | 2-CH **IN2** | **GPIO 18** | Putih / abu | Alternate: GPIO 19 |
| DC pump 2 / zone B | 1-CH **IN** | **GPIO 16** | Ungu / biru | Alternate: GPIO 17 |

### Sensors

| Sensor | Signal | ESP32 pin | Wire colour | Notes |
|---|---|---|---|---|
| Soil moisture 1 (capacitive V2.0) | AOUT | **GPIO 34** | Kuning | ADC1, input-only |
| Soil moisture 2 (capacitive V2.0) | AOUT | **GPIO 35** | Kuning | ADC1, input-only |
| DHT11 | DATA | **GPIO 32** | Hijau / oranye | Alternate: GPIO 4 |
| Status LED | — | **GPIO 2** | — | Onboard LED on most DevKit boards |

The firmware uses the **safer pin from each “A / B” pair** in the wiring table:

- GPIO 13 instead of 12 (GPIO 12 is a boot-strapping pin; a relay pulling it low can block boot).
- GPIO 18 instead of 19 (both are fine; 18 avoids the USB D− pin).
- GPIO 16 instead of 17 (both are fine).
- GPIO 32 instead of 4 (GPIO 4 is also a boot-strapping pin).

If your harness is already crimped for the alternate pin, change the matching `#define` at the top of `BroccoliBiosphereCabin.ino`. All pin choices are reported live in `/api/telemetry` under `pins`.

### Relay polarity

The firmware assumes **active-LOW** relay boards (`IN = LOW` turns the relay on), which is the common blue 4-CH/2-CH/1-CH module. If your board is active-HIGH, set:

```cpp
#define RELAY_ACTIVE_LOW 0
```

Power each relay board from its rated coil voltage (usually **5 V**, from the ESP32 `5V`/`VIN` pin or a 5 V buck), and power the pumps/Peltier from a separate **12 V** supply. Connect all grounds together (ESP32 GND, relay board GND, 12 V supply GND). Never feed 12 V into the relay board's 5 V VCC pin, and do not run the 12 V loads from the ESP32 5 V/VIN pin.

## Firmware behaviour

- **Non-blocking loop** — no `delay()` in the control path.
- **Thermal interlock** — the Peltier can never run without the combined fan. Turning the fan off while cooling forces the Peltier off.
- **Dual-zone watering** — each soil probe controls its own pump. A pump runs for at most **8 s** per automatic pulse, then waits **60 s** before re-checking. Manual pump runs are hard-capped at **30 s**.
- **Emergency flush** — both pumps run for 5 s.
- **Safe mode** — three consecutive failed DHT11 reads force the fan on and cut the Peltier, grow light, LED strip, spare relay, and both pumps. Control resumes automatically when the sensor recovers.
- **Photoperiod** — phase 1 (days 1–3) keeps the grow light and LED strip off; phase 2 (days 4–10) runs a 16 h on / 8 h off window.
- **Wi-Fi fallback** — if the configured router is unavailable, the board starts `BroccoliCabin_AP` (password `biosphere123`) at `http://192.168.4.1/`.
- **mDNS** — on a normal LAN the console is also available at `http://biosphere.local/`.
- **Embedded dashboard** — the ESP32 serves the standalone waifu console at `/` and `/dashboard`, so no laptop is needed for a demo. The API is at `/api/telemetry` and `/api/control`.

Set your Wi-Fi credentials at the top of `BroccoliBiosphereCabin.ino`:

```cpp
const char* WIFI_SSID     = "Tryout_SA_0509";
const char* WIFI_PASSWORD = "SAujian1234";
```

## Repository layout

```
├── BroccoliBiosphereCabin.ino   # ESP32 firmware (REST API + embedded dashboard)
├── dashboard_html.h             # AUTO-GENERATED from dashboard.html
├── dashboard.html               # Standalone compact console (served by ESP32 and Node)
├── server.js                    # Local simulation server + static file server
├── index.html                   # Redirect helper for the public console
├── public/                      # Main modern console (HTML/CSS/JS, zero dependencies)
├── tools/embed-dashboard.js     # Regenerates dashboard_html.h
├── package.json                 # npm start / npm run embed / checks
└── docs/                        # Design and planning notes
```

## Run locally (no hardware)

```bash
npm start
```

- Main console: <http://127.0.0.1:8080/>
- Standalone console: <http://127.0.0.1:8080/dashboard.html>
- API: <http://127.0.0.1:8080/api/telemetry>

The Node server simulates temperature, humidity, both soil probes, VPD, PCS, relays, pump pulses, and the automatic control loop. Use the target field in the main console to point it at a physical ESP32 instead.

## Flash the ESP32

1. Install the Arduino IDE with ESP32 board support (`esp32` by Espressif).
2. Install **DHT sensor library** by Adafruit (Library Manager). It also needs **Adafruit Unified Sensor**.
3. Open `BroccoliBiosphereCabin.ino`.
4. Select **ESP32 Dev Module** and the correct serial port.
5. Upload, then open Serial Monitor at **115200 baud** to see the IP address / fallback AP.

If you edit `dashboard.html`, regenerate the embedded copy before uploading:

```bash
npm run embed
```

`npm run check:embed` fails if `dashboard_html.h` is out of date.

## REST API

### `GET /api/telemetry`

Key fields:

```json
{
  "temp": 20.4, "rh": 57.6,
  "soil1": 56.4, "soil2": 48.2, "soil": 52.3,
  "adc1": 2240, "adc2": 2600,
  "vpd": 0.62, "pcs": 97.4,
  "status": "OPTIMAL", "autoMode": true, "phase": 2, "day": 5,
  "relays": {
    "peltier": false, "fan": false, "spare": false,
    "growLight": true, "ledStrip": true,
    "pump1": false, "pump2": false
  },
  "thresholds": {
    "tempMin": 18, "tempMax": 22,
    "rhMin": 50, "rhMax": 65,
    "soilMin": 45, "soilMax": 70,
    "dry1": 3200, "wet1": 1400,
    "dry2": 3200, "wet2": 1400
  },
  "pins": {
    "dht": 32, "soil1": 34, "soil2": 35,
    "peltier": 25, "spare": 26, "fan": 27, "growLight": 14,
    "ledStrip": 13, "pump1": 18, "pump2": 16, "statusLed": 2
  }
}
```

Also included: `history` (20 samples of temp/RH/soil1/soil2), `logs`, `vpSat`, `vpAct`, `tank1Vol`, `tank2Vol`, fault flags, and 3.3 V ADC voltages.

### `POST /api/control`

| Action | Parameters | Effect |
|---|---|---|
| `mode` | `val=auto\|manual` | Automatic climate control on/off |
| `phase` | `val=1\|2` | Germination blackout / autotrophic light |
| `day` | `val=1..10` | Set cultivation day and sync phase |
| `toggleRelay` | `ch=1..7` | Toggle relay (switches to manual mode) |
| `flush` | — | Both pumps on for 5 s |
| `pulsePump` | `pump=1\|2&seconds=1..30` | Timed single-pump run |
| `setThreshold` | `param=tempMin...&val=` | Update a setpoint |
| `calibrateSoil` | `sensor=1\|2&type=dry\|wet` | Store current ADC as calibration point |
| `refill` | `tank=1\|2` | Reset the estimated reservoir volume |

Relay channels: `1` Peltier, `2` Fan, `3` Spare, `4` Grow light, `5` LED strip, `6` Pump 1, `7` Pump 2.

Example:

```bash
curl -X POST "http://192.168.1.50/api/control?action=toggleRelay&ch=6"
```

## Soil calibration

1. Put probe 1 in dry air, wait for a stable reading, then click **S1 DRY AIR** (or call `action=calibrateSoil&sensor=1&type=dry`).
2. Submerge probe 1 in water, wait, then click **S1 WATER**.
3. Repeat for probe 2 with the S2 buttons.
4. Defaults are 3200 (dry) and 1400 (wet). Calibration is kept in RAM and resets on reboot; change the `SOIL*_DRY_RAW` / `SOIL*_WET_RAW` defines to persist your values.

## Notes and troubleshooting

- **DHT11 reads NaN** — check the data wire on GPIO 32, 3.3 V supply, and a 10 kΩ pull-up to 3.3 V if the module does not already include one. The firmware needs three consecutive good frames after an error to leave safe mode.
- **Relay clicks at boot** — many ESP32 pins float low before `setup()`. The sketch preloads every relay OFF before setting `pinMode`, but a brief hardware click can still happen on some boards. Use a relay board that already has input pull-ups, or add a 10 kΩ pull-up from each IN pin to the relay board's own logic voltage (check whether it is 3.3 V or 5 V).
- **Pumps do not run in auto** — check that the matching soil channel is not flagged as a fault and the measured moisture is below `soilMin`. Automatic pulses have a 60 s cooldown.
- **Only one dashboard is needed** — the ESP32 serves `dashboard.html` directly. `server.js` is only for local development without hardware and serves both consoles.
- **ADC readings** — GPIO 34/35 are input-only and belong to ADC1, which keeps working while Wi-Fi is active.

## Legacy notes

The original project used six relays, two servo valves, a DHT22, and one soil probe. Those parts are no longer required. The old `public/` console was kept and updated to the new relay map; the PR #2 standalone console is kept as the self-hosted dashboard.
