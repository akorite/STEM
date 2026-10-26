# Product

<!-- impeccable:product-schema 1 -->

## Platform

web

## Users
Agronomists, student researchers, and automated biosphere operators monitoring and controlling an enclosed microgreen chamber.

## Product Purpose
Real-time environmental monitoring, closed-loop climate supervision, and hardware actuation for cultivating broccoli microgreens in an automated biosphere cabin.

## Positioning
A dedicated zero-dependency, headless ESP32-backed telemetry and control engine featuring real-time Tetens VPD calculation, Plant Comfort Score (PCS), and multi-channel hardware interlock management.

## Operating Context
Benchtop laboratory environment, running continuously on desktop/laptop displays as an unscrollable mission-control dashboard, communicating with an ESP32 microcontroller over Wi-Fi REST API or local Node.js simulation.

## Capabilities and Constraints
- Must fit within 100vh viewport without vertical scrollbar (unscrollable main dashboard).
- High information density with instant scanability.
- Two consoles share one REST contract: the modern `public/` console and the compact self-hosted `dashboard.html` with the Brocco companion.
- Real-time telemetry: Temperature, Relative Humidity, dual-zone Soil Moisture (GPIO 34/35), VPD (Tetens), Plant Comfort Score (PCS), estimated reservoir volumes.
- Real-time 20-point historical telemetry graph with optimal agronomic target envelopes.
- Closed-loop parameter setpoint tuning and per-probe soil ADC calibration.
- 7-channel GPIO relay matrix: Peltier (GPIO 25) interlocked to the combined fan (GPIO 27), spare (GPIO 26), grow light (GPIO 14), LED strip (GPIO 13), pump 1 (GPIO 18), pump 2 (GPIO 16).
- Dual-zone automatic watering with 8 s pulses, 60 s cooldown, and a 30 s manual hard limit.
- Real-time event and safety diagnostics terminal log.
- Dual-target connection: Local Node simulation (`127.0.0.1:8080`) or physical ESP32 IP / `biosphere.local` over LAN.

## Brand Commitments
Kebun Biosfer // Biosphere Cabin Telemetry & Control Engine. Industrial agronomy and precision aerospace telemetry aesthetic.

## Evidence on Hand
- `BroccoliBiosphereCabin.ino`: ESP32 C++ firmware with non-blocking climate control, dual-zone watering, and REST API.
- `dashboard.html` + `dashboard_html.h`: standalone self-hosted console embedded in the ESP32 firmware.
- `server.js`: Node.js simulation server with realistic dynamic physics and CORS support.
- `public/`: Main web frontend implementation.

## Product Principles
1. Clarity over clutter: High information density without visual chaos.
2. Safety first: Hardware interlocks and status alerts must be immediately glanceable.
3. Precision instrument design: Numbers, gauges, and actuators feel tangible, responsive, and trustworthy.
4. Zero fluff: Pure operational utility, no ornamental distractions.
