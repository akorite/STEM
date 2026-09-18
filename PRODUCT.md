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
- Must fit within 100vh viewport without vertical scrollbar (unscrollable dashboard).
- High information density with instant scanability.
- No mascot / virtual pet ("talking tom") elements.
- Real-time telemetry: Temperature, Relative Humidity, Soil Moisture, VPD (Tetens), Plant Comfort Score (PCS), Dual Reservoirs (Tank 1 & 2).
- Real-time 20-point historical telemetry graph with optimal agronomic target envelopes.
- Closed-loop parameter setpoint tuning and soil ADC calibration.
- 6-channel GPIO relay matrix (CH1 Peltier with CH2 Heatsink Fan thermal safety interlock, CH3 Blower, CH4 Grow Light, CH5 Spray T1, CH6 Spray T2) and 2 servomotor flow valves.
- Real-time event and safety diagnostics terminal log.
- Dual-target connection: Local Node simulation (`127.0.0.1:8080`) or physical ESP32 IP over LAN.

## Brand Commitments
Kebun Biosfer // Biosphere Cabin Telemetry & Control Engine. Industrial agronomy and precision aerospace telemetry aesthetic.

## Evidence on Hand
- `BroccoliBiosphereCabin.ino`: ESP32 C++ firmware with non-blocking closed-loop climate control and REST API.
- `server.js`: Node.js simulation server with realistic dynamic physics and CORS support.
- `public/`: Web frontend implementation.

## Product Principles
1. Clarity over clutter: High information density without visual chaos.
2. Safety first: Hardware interlocks and status alerts must be immediately glanceable.
3. Precision instrument design: Numbers, gauges, and actuators feel tangible, responsive, and trustworthy.
4. Zero fluff: Pure operational utility, no ornamental distractions.
