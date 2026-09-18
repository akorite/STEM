# Design System: Kebun Biosfer Modern Pastel Biosphere Console

<!-- impeccable:design-schema 1 -->

## Direction

- **Thesis:** A modern, pastel, delightfully cute yet highly functional environmental chamber console. Transforms cold, dark industrial telemetry into an inviting, warm botanical sanctuary where scientific precision meets gentle, cheerful plant-care aesthetics.
- **Form:** Modern Pastel Botanical Chamber Console & Living Plant Companion.
- **Mode:** Operate.
- **Viewport:** Ergonomic 100vh desktop dashboard (`overflow: hidden` on spacious viewports with graceful adaptive flow).

## Color System

### Canvas & Surfaces
- `--canvas-bg`: `#f8fafc` (Dreamy soft cloud porcelain)
- `--card-bg`: `#ffffff` (Clean milky card surface)
- `--card-bg-elevated`: `#ffffff` (Elevated controls and modal pods)
- `--card-bg-sunken`: `#f1f5f9` (Soft inset wells and track grounds)

### Pastel Borders & Soft Shadows
- `--border-subtle`: `rgba(226, 232, 240, 0.9)` (Delicate card hairline)
- `--border-mint`: `rgba(52, 211, 153, 0.4)` (Fresh sprout green border)
- `--border-sky`: `rgba(56, 189, 248, 0.4)` (Air and humidity border)
- `--border-peach`: `rgba(251, 113, 133, 0.35)` (Warm botanical border)
- `--border-lavender`: `rgba(167, 139, 250, 0.35)` (Nutrient and fluid border)
- `--shadow-soft`: `0 8px 25px -4px rgba(148, 163, 184, 0.12), 0 3px 8px -2px rgba(148, 163, 184, 0.06)`
- `--shadow-hover`: `0 12px 32px -4px rgba(148, 163, 184, 0.18), 0 4px 10px -2px rgba(148, 163, 184, 0.08)`
- `--shadow-active`: `0 2px 6px -1px rgba(148, 163, 184, 0.16)`

### Fresh Pastel Functional Spectrum
- `--mint-green`: `#059669` / bg `#ecfdf5` / border `#a7f3d0` (Nominal status, active regulation, healthy growth)
- `--sky-blue`: `#0284c7` / bg `#f0f9ff` / border `#bae6fd` (Relative humidity, Tank 1 pure water, cool mist)
- `--honey-amber`: `#d97706` / bg `#fffbeb` / border `#fde68a` (Day progression, stomatal warnings, heat lamps)
- `--sakura-rose`: `#e11d48` / bg `#fff1f2` / border `#fecdd3` (Critical alarms, emergency flush, pythium alert)
- `--lilac-violet`: `#7c3aed` / bg `#f5f3ff` / border `#ddd6fe` (Tank 2 micronutrients, servo valves, VPD metric)

### Typography & Readability (WCAG AA Compliant)
- `--text-heading`: `#0f172a` (Deep slate heading text for maximum clarity)
- `--text-body`: `#334155` (Crisp readable charcoal slate)
- `--text-muted`: `#64748b` (Soft supportive text)
- `--text-faint`: `#94a3b8` (Subtle metadata)

- **System Rounded Sans:** `-apple-system, BlinkMacSystemFont, "Plus Jakarta Sans", "Quicksand", "Inter", "Segoe UI", Roboto, sans-serif`
  - System Header: 15px / 800 weight
  - Card & Section Titles: 12px–13px / 700 weight
  - Telemetric Large Numbers: 24px–28px / 800 weight
  - Status Pills & Badges: 10px–11px / 700 weight
- **Tabular Numerals:** `ui-monospace, "SF Mono", "JetBrains Mono", Menlo, Consolas, monospace`
  - Clocks, Setpoints, Calibration ADC: 11px / 700 weight
  - Audit Trail Entries: 11px / 500 weight

## Ergonomic Viewport Architecture

- **Root:** `100vw × 100vh`, `max-height: 100vh` on desktop displays.
- **Top System Bar:** Fixed `52px` height, featuring rounded pill controls and live heartbeat lamp.
- **Workspace Grid:** 3 harmonious decks with generous 10px spacing:
  - **Sensory Pods (Upper):** Atmospheric thermodynamics pod + Plant companion & soil pod + Fluidics vials.
  - **Recorder & Setpoints (Middle):** Pastel SVG strip-chart with soft gradients + Tactile +/- setpoint steppers.
  - **Operations & Audit (Lower):** 6-channel cute pastel switchboard with flow valves + Clean live activity log.
