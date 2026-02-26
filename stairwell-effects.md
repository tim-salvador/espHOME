# Stairwell Effects — ESPHome Configuration

Addressable LED stairwell lighting with PIR-triggered directional fade effects, easter eggs, color presets, and full Home Assistant integration. Built for an ESP8266 (ESP-01 1M) driving WS2811 NeoPixel strips.

---

## Table of Contents

- [Hardware](#hardware)
- [LED Layout](#led-layout)
- [How It Works](#how-it-works)
  - [Normal Operation](#normal-operation)
  - [State Machine](#state-machine)
  - [Easter Eggs](#easter-eggs)
- [New Entities in HA / Web UI](#new-entities-in-ha--web-ui)
  - [Color Presets](#color-presets)
  - [Tunable Numbers](#tunable-numbers)
  - [Switches](#switches)
  - [Sensors](#sensors)
- [All Light Effects](#all-light-effects)
- [Configuration Reference](#configuration-reference)
  - [Global Variables](#global-variables)
  - [GPIO Pin Assignments](#gpio-pin-assignments)
  - [Secrets Required](#secrets-required)
- [Version 2.0 Changes](#version-20-changes)
  - [Bug Fixes](#bug-fixes)
  - [New Features](#new-features)
- [Known Limitations & Warnings](#known-limitations--warnings)
- [Tuning Guide](#tuning-guide)

---

## Hardware

| Component | Details |
|---|---|
| Microcontroller | ESP8266 ESP-01 (1MB flash, 96KB RAM) |
| LED Strip (Stairs) | WS2811, 58 LEDs, BRG color order, GPIO4 |
| LED Strip (Storage) | WS2811, 60 LEDs, GRB color order, GPIO5 |
| Upper PIR Sensor | GPIO12 — detects motion at top of stairs |
| Lower PIR Sensor | GPIO14 — detects motion at bottom of stairs |
| Storage PIR Sensor | GPIO13 — controls storage area light independently |
| Storage Light MOSFET | GPIO2 (PWM) — IRF520/IRLZ44N/IRF540/IRLB8721 recommended |
| Temperature Sensor | DHT11, GPIO0 ⚠️ *(see warning below)* |
| Illuminance (Remote) | Hue Motion Sensor via Home Assistant entity |

### MOSFET Wiring (Storage Light)
- **Gate** → ESP8266 GPIO2
- **Source** → Common GND (shared with ESP8266 and 12V PSU negative)
- **Drain** → Negative (−) wire of LED strip
- **12V PSU positive** → LED strip positive (+) directly

---

## LED Layout

The 58-LED stair strip is divided into three logical sections used by the `Fade Control V2` effect. Each section can have an independent color.

```
LED Index:  0                15 16                              51 52    57
            |--- Section 1 ---|--|------- Section 2+3 ----------||- Sec4-|
            |   16 LEDs       ||         36 LEDs                ||6 LEDs |
            | (Stair Color)   ||      (Platform Color)          ||(Stair)|
            |  Bottom stairs  ||  Landing / mid-stair platform  || Top   |
```

- **Section 1** (LEDs 0–15): Lower stair run, uses *Stair Color Preset*
- **Section 2+3** (LEDs 16–51): Landing/platform area, uses *Platform Color Preset* (independent color)
- **Section 4** (LEDs 52–57): Upper stair run, uses *Stair Color Preset*

---

## How It Works

### Normal Operation

The system detects which end of the stairs a person enters from and triggers a directional fade-on effect. When the person clears the opposite PIR, the lights fade off in the same direction they traveled, then turn off after a configurable dwell period.

**Walking UP (lower PIR triggers first):**
1. Lower PIR fires → `Fade Control V2` fades LEDs on from LED 0 → 57 (bottom to top)
2. System enters `up_active` state
3. Upper PIR releases (person has passed) → configurable delay → fade off from LED 0 → 57
4. Dwell period → lights off

**Walking DOWN (upper PIR triggers first):**
1. Upper PIR fires → `Fade Control V2` fades LEDs on from LED 57 → 0 (top to bottom)
2. System enters `down_active` state
3. Lower PIR releases → configurable delay → fade off from LED 57 → 0
4. Dwell period → lights off

**Turned around halfway:**
If the person re-triggers the same PIR that started the sequence while lights are fully on, the system fades off in reverse (back the way they came) and returns to idle.

### State Machine

All state is tracked by a single `system_state` integer global (v2.0 consolidated from separate booleans):

| Value | Meaning |
|---|---|
| `0` | Idle — no active sequence, lights off |
| `1` | Up active — person walking up, lights on |
| `2` | Down active — person walking down, lights on |

A safety timeout (configurable, default 60 seconds) forces `system_state` back to `0` and turns lights off if they've been on too long with no PIR activity — a stuck-state recovery.

### Easter Eggs

Three hidden effects triggered by specific physical interactions:

| Easter Egg | Trigger | Effect | Duration |
|---|---|---|---|
| **Disco Mode** | Activate both PIR sensors simultaneously 3× within 5 seconds | Rainbow color cycle across all LEDs | 10 seconds |
| **Secret Knock** | Trigger the lower PIR 5× within 3 seconds | Fire Flicker warm candlelight glow | 15 seconds |
| **Moonlight** | Hold both PIRs active continuously for 10 seconds | Soft pulsing blue moonlight | 30 seconds |

> **Note:** A `special_effect_active` mutex prevents two easter eggs from triggering simultaneously. All easter eggs reset state and release the mutex on completion.

---

## New Entities in HA / Web UI

After flashing, the following entities will appear in Home Assistant and the built-in ESPHome web UI (`http://<device-ip>`).

### Color Presets

Two `select` dropdown entities control the colors used by all fade effects. Changes apply immediately to the next effect run — no reflash needed.

#### Stair Color Preset
Controls LEDs in **Section 1** (0–15) and **Section 4** (52–57) — the stair runs.

| Option | Hue | Saturation | Brightness | Description |
|---|---|---|---|---|
| Warm White | 30 | 60 | 220 | Soft warm white, easy on the eyes at night |
| Cool Blue | 160 | 200 | 200 | Clean blue-white |
| Sunset Orange | 15 | 240 | 200 | Warm amber-orange |
| Forest Green | 95 | 200 | 150 | Muted natural green |
| Purple Haze | 200 | 220 | 180 | Soft purple/violet |
| Custom | — | — | — | Uses the Custom Hue/Saturation/Brightness sliders |

#### Platform Color Preset
Controls LEDs in **Section 2+3** (16–51) — the landing/platform area. Has all the same options as Stair Color Preset, plus:

| Option | Description |
|---|---|
| Match Primary | Instantly syncs platform color to whatever Stair Color Preset is currently set to |

> **Default after flash:** Warm White stairs + Cool Blue platform — matches the original hardcoded values.

#### Custom Color Sliders
Three `number` entities that are active when either preset is set to "Custom". All values are HSV (0–255 range):

| Entity | Range | Description |
|---|---|---|
| Custom Color Hue | 0–255 | Color wheel position (0=red, 85=green, 170=blue) |
| Custom Color Saturation | 0–255 | 0 = white/grey, 255 = fully saturated color |
| Custom Color Brightness | 0–255 | LED intensity |

---

### Tunable Numbers

All previously hardcoded timing and layout values are now exposed as HA number entities. Values persist across reboots.

| Entity | Default | Range | Description |
|---|---|---|---|
| LED Group Size | 2 | 1–20 | How many LEDs light up per step in group fade effects. Larger = faster sweep. |
| LED Platform Group Size | 9 | 1–36 | Group size for the platform section (LEDs 16–51). Must divide evenly into 36 for clean results: 1, 2, 3, 4, 6, 9, 12, 18, 36. |
| Effect Speed | 1 | 1–5 | Frame-skip multiplier. 1 = full speed, 5 = slowest. Applied at runtime with no reflash. |
| Illuminance Threshold | 75 lux | 0–500 | Lux level below which PIR triggers are allowed (when Illuminance Gating is ON). |
| Light Timeout | 60 sec | 30–300 | How long lights stay on before the safety auto-off timeout triggers. |
| Fade Off Delay | 500 ms | 0–5000 | Milliseconds to wait after a PIR clears before the fade-off effect begins. Increase this if lights start fading while someone is still on the stairs. |
| Dwell Time | 6 sec | 0–30 | How long the lights remain on (fully lit) after the fade-off animation completes before `light.turn_off` is called. |

---

### Switches

| Entity | Default | Description |
|---|---|---|
| Illuminance Gating | OFF | When ON, PIR triggers are ignored if the Hue illuminance sensor reads above the Illuminance Threshold. Prevents lights activating in daylight. |

---

### Sensors

| Entity | Update | Description |
|---|---|---|
| System State | 1s | Current state machine value: 0=idle, 1=up active, 2=down active |
| Last PIR Triggered | 1s | Which PIR last fired: 0=none, 1=lower, 2=upper |
| Light Status | On demand | Human-readable illuminance string, e.g. `42.0 lux (light is off)` |
| Stair Light Enclosure Temperature | 60s | DHT11 reading in °F (with +8° calibration offset) |
| Stair Light Enclosure Humidity | 60s | DHT11 relative humidity % |

---

## All Light Effects

### Directional Fade Effects (used by automations)

| Effect Name | Direction | Action | Used By |
|---|---|---|---|
| `Fade Control V2` | Up or Down (via `fade_direction_up` global) | Section-aware fade on/off | Primary trigger effect |
| `Fade on UP` | Bottom → Top | Single-LED fade on | Legacy / manual |
| `Fade off UP` | Bottom → Top | Single-LED fade off | Legacy / manual |
| `Fade on DOWN` | Top → Bottom | Single-LED fade on | Legacy / manual |
| `Fade off DOWN` | Top → Bottom | Single-LED fade off | Legacy / manual |
| `Group Fade on UP` | Bottom → Top | Multi-LED group fade on | Legacy |
| `Group Fade off UP` | Bottom → Top | Multi-LED group fade off | Turn-around (going up) |
| `Group Fade on DOWN` | Top → Bottom | Multi-LED group fade on | Legacy |
| `Group Fade off DOWN` / `Group Fade off Down` | Top → Bottom | Multi-LED group fade off | Turn-around (going down) |
| `Fade Control` | Up or Down | Legacy unified single-LED version | Legacy |

### Easter Egg Effects

| Effect Name | Trigger | Description |
|---|---|---|
| `Disco Mode` | Both PIRs 3× in 5s | Rotating rainbow hue cycle, shifts every 200ms |
| `Fire Flicker` | Lower PIR 5× in 3s | Random brightness variation in warm orange/amber |
| `Moonlight` | Both PIRs held 10s | Slow breathing pulse in soft blue |

### Decorative / Demo Effects

| Effect Name | Description |
|---|---|
| `Rainbow Effect With Custom Values` | Full strip rainbow, speed 10, width 50 |
| `forward wipe` | White gradient wipe forward |
| `rev wipe` | White gradient wipe reverse |
| `Pulse` | Full-strip brightness pulse |
| `Breathing Trail` | Single LED "lead" with trailing dim LEDs, breathes at the front |
| `Step Highlight` | Groups of 10 LEDs fade in/out to mark each step |
| `Wave Runner` | Single pixel bounces back and forth |
| `Step Forward-Back Pattern` | Progressive on/off pattern building from LED 0 |

---

## Configuration Reference

### Global Variables

| ID | Type | Persisted | Description |
|---|---|---|---|
| `system_state` | `int` | No | 0=idle, 1=up_active, 2=down_active |
| `last_pir` | `int` | No | 0=none, 1=lower, 2=upper |
| `effect_finished` | `bool` | No | Set `true` by effects on completion; cleared by automation |
| `fade_direction_up` | `bool` | No | `true` = fade from LED 0 upward |
| `fade_on` | `bool` | No | `true` = fade to color, `false` = fade to black |
| `special_effect_active` | `bool` | No | Mutex: prevents simultaneous easter eggs |
| `last_trigger_time` | `uint32_t` | No | `millis()` timestamp of last PIR trigger |
| `last_pir_cleared_time` | `uint32_t` | No | `millis()` timestamp of last PIR release |
| `preset_h/s/v` | `uint8_t` | Yes | Active stair color in HSV |
| `platform_h/s/v` | `uint8_t` | Yes | Active platform color in HSV |
| `custom_h/s/v` | `uint8_t` | Yes | Custom preset HSV values |
| `led_group_size` | `int` | Yes | LEDs per step in group fade effects |
| `led_platform_group_size` | `int` | Yes | LEDs per step for platform section |
| `effect_speed` | `int` | Yes | Frame-skip divisor (1–5) |
| `light_timeout_ms` | `uint32_t` | Yes | Safety timeout in ms |
| `fade_off_delay_ms` | `uint32_t` | Yes | Post-PIR-clear delay before fade-off |
| `dwell_time_ms` | `uint32_t` | Yes | Post-fade-off dwell before `turn_off` |
| `illuminance_threshold` | `float` | Yes | Lux cutoff for gating |
| `illuminance_gating_enabled` | `bool` | Yes | Whether illuminance gating is active |

### GPIO Pin Assignments

| GPIO | Function | Notes |
|---|---|---|
| GPIO0 | DHT11 Temperature/Humidity | ⚠️ Boot pin — see warning |
| GPIO2 | Storage MOSFET (PWM output) | |
| GPIO4 | Stair WS2811 data | 58 LEDs, BRG |
| GPIO5 | Storage WS2811 data | 60 LEDs, GRB |
| GPIO12 | Upper PIR sensor input | |
| GPIO13 | Storage PIR sensor input | |
| GPIO14 | Lower PIR sensor input | |

### Secrets Required

The following entries must exist in your `secrets.yaml`:

```yaml
wifi_ssid: "YourNetworkName"
wifi_password: "YourWiFiPassword"
sw_effect_ap_pass: "FallbackAPPassword"
api_encryption_key: "your-32-byte-base64-key"
sw_effect_ota: "YourOTAPassword"
```

---

## Version 2.0 Changes

### Bug Fixes

**`uint32_t` for all time globals** — Previously declared as `long`. Since `millis()` returns `unsigned long`, signed comparison would produce incorrect results after ~25 days of continuous uptime when the counter wraps. All time-tracking globals are now `uint32_t`.

**`on_release` fade-off logic** — Both PIR `on_release` handlers previously contained `wait_until: binary_sensor.is_off: [sensor]` which is a no-op because the sensor is already off when `on_release` fires. Replaced with a configurable `fade_off_delay_ms` delay.

**`system_state` set incorrectly after fade-off** — The upper PIR `on_release` handler was setting `system_state = 1` (up_active) after turning the lights off. Now correctly sets `system_state = 0` (idle).

**Unified state management** — Removed the `up_sequence_active`, `down_sequence_active`, and `light_state` boolean/int globals that duplicated the information in `system_state`. All logic now reads `system_state` exclusively, eliminating a class of drift bugs where the booleans and the integer got out of sync.

**Easter egg mutex** — Disco Mode, Fire Flicker, and Moonlight could all activate simultaneously. Added `special_effect_active` boolean that acts as a mutex: only one special effect can run at a time, and it is cleared on effect completion.

**`Color` type fix** — `Step Forward-Back Pattern` used `ESPColor(r,g,b)` which does not exist in the ESPHome addressable LED API. Corrected to `Color(r,g,b)`.

### New Features

- **Color preset selects** — Two `select` entities for stair and platform colors. Six named presets plus a fully custom HSV option. Changes are live with no reflash.
- **Custom color sliders** — Three `number` entities (Hue, Saturation, Brightness) for the Custom preset option.
- **Effect speed control** — Runtime frame-skip multiplier, 1–5. Works with all updated fade effects.
- **Illuminance gating** — `switch` + `number` threshold. When enabled, PIR triggers are ignored above the lux threshold. Uses the existing Hue illuminance sensor that was already being pulled but not acted upon.
- **Configurable light timeout** — `number` entity, 30–300 seconds, persisted across reboots.
- **Configurable fade-off delay** — `number` entity, 0–5000 ms.
- **Configurable dwell time** — `number` entity, 0–30 seconds.
- **Platform group size exposed** — `LED Platform Group Size` is now a `number` entity; was previously globals-only.

---

## Known Limitations & Warnings

### ⚠️ GPIO0 — DHT11 Boot Pin Risk
GPIO0 is the ESP8266 programming/boot mode pin. If the DHT11 data line pulls GPIO0 LOW during power-on, the ESP8266 will enter flash mode instead of normal boot and the device will not start. This is a hardware design issue.

**Mitigation options:**
1. Add a 10kΩ pull-up resistor between GPIO0 and 3.3V to ensure it stays HIGH at boot.
2. Move the DHT11 to a safe pin (GPIO3/RX is usable if you don't need serial logging, or GPIO16) and update the `pin:` field in the `dht:` sensor config.

### ESP-01 Memory Pressure
The ESP-01 has 1MB flash and only 96KB RAM. The number of `addressable_lambda` effects is high. If you experience OTA failures, random reboots, or heap panics, consider migrating to a D1 Mini or ESP32 which have 4MB flash and significantly more RAM.

### `millis()` Rollover
All time comparisons use `uint32_t` as of v2.0. `millis()` rolls over after ~49.7 days. Subtractive comparisons (`millis() - id(last_time) > threshold`) are rollover-safe by design as long as both values are `uint32_t`.

### `wait_until` and Parallel Automation Triggers
ESPHome executes automation actions sequentially within a single trigger but multiple triggers can run concurrently. The `wait_until: lambda: return id(effect_finished)` blocks within a trigger are not re-entrant. Rapid double-triggering of a PIR within the same `wait_until` window could cause unexpected behavior. The `system_state` check at the start of each trigger guards against most cases.

---

## Tuning Guide

**Lights sweep too fast/slow:**
Adjust `Effect Speed` in HA (1–5). For finer control, increase `LED Group Size` (larger groups = faster sweep for the same speed setting) or modify `update_interval` in the effect definition (requires reflash).

**Lights turn off before I reach the bottom:**
Increase `Fade Off Delay` and/or `Dwell Time` in HA.

**Lights keep triggering during the day:**
Enable `Illuminance Gating` switch and set `Illuminance Threshold` to a lux value below your daytime ambient level. Check the `Light Status` sensor to see current lux readings.

**Platform section looks wrong color:**
Set `Platform Color Preset` to `Match Primary` to sync it with the stair color, or choose an independent color.

**Platform section sweeps unevenly:**
`LED Platform Group Size` must divide evenly into 36 (the platform section size). Valid values: 1, 2, 3, 4, 6, 9, 12, 18, 36.

**Easter eggs accidentally triggering:**
The disco mode requires 3 simultaneous-PIR activations in 5 seconds. If your PIR sensors are mounted close together or have wide detection angles, they may both be active during normal stair use. Tighten PIR sensitivity or angle them to reduce overlap.
