# Stairwell Lighting (stair_engine) v3.0

Direction-aware, sectioned stair lighting for ESPHome. All behavior lives in a
framework-free C++ core (`components/stair_engine`) that compiles on the host
for testing and is portable to a bare ESP-IDF product firmware later.

| Item | Value |
|---|---|
| MCU | ESP32 WROOM-32 (`esp32dev`), ESP-IDF toolchain |
| ESPHome | >= 2026.9.0 (validated against 2026.9.1) |
| LEDs | WS2811 12 V, BRG, 58 pixels, one data line |
| Sensors | PIR lower + PIR upper; ambient lux from Home Assistant |

## Install

```bash
cp secrets.example.yaml secrets.yaml        # fill in; api key: openssl rand -base64 32
esphome config stairwell.yaml               # validate
esphome run stairwell.yaml                  # first flash over USB, OTA afterwards
```

`components/` must sit next to `stairwell.yaml` (local external component). OTA
uses the API encryption key; the web server's plaintext `/update` is disabled.

## Wiring

| Function | GPIO | Notes |
|---|---|---|
| LED data | 18 | 74AHCT125 level shifter recommended (3.3 V -> 5 V logic) |
| Upper PIR | 32 | internal pulldown enabled |
| Lower PIR | 33 | internal pulldown enabled |

Do not reuse the old ESP8266 pins: GPIO12 is an ESP32 strapping pin (a PIR
holding it high at boot selects 1.8 V flash and boot-loops). Avoid 0, 2, 5,
12, 15 (strapping) and 6-11 (flash). Common ground between the 12 V supply
and the ESP32 is required.

## Layout

Sections are declared bottom to top in `stair_engine:`. A landing is a
section whose steps are step-equivalents. Rules (validated at compile time):
`pixels` must be a multiple of `pixels_per_step`, sections must not overlap
or exceed `num_pixels`. Set `reversed: true` if a section's pixel index
increases toward the bottom. Max 8 sections.

```
Lower Flight  px  0-15   2 px/step   steps  0-7
Landing       px 16-51   9 px/step   steps  8-11
Upper Flight  px 52-57   2 px/step   steps 12-14
```

Adding a section: add it under `stair_engine: sections:` and add a matching
color light whose `set_section_color(N, ...)` index equals its position.

## Behavior

| Phase | Meaning |
|---|---|
| idle | dark |
| enter | traversal effect lights the stairs in the direction of travel |
| hold | fully lit until every entry is matched by an arrival at the far end |
| exit | traversal effect turns off following the direction of travel |
| grace | unresolved past the backstop: dims, any motion restores |
| incidental | creative off effect after grace |
| restore | motion during grace/exit/incidental brings lights back up |
| preview | a color change lights only that section for 5 s |

**Traversal tracking.** An arrival at an end completes the oldest open trip
heading toward it, otherwise it opens a new trip. Lights exit only when no
trips are open and both sensors are clear for Exit Delay. Two people, a
turn-back, or a ball all keep trips open, so the backstop decides.

**Learned backstop.** Each confirmed trip time is stored (16 per direction,
persisted to flash at most once a minute). Backstop = 2 x p90 trip time,
clamped to Hold Minimum..Hold Maximum (Maximum wins if they conflict). Fewer
than 3 samples uses Hold Maximum. Simulations never train it.

**Chatter.** A re-trigger within 1.5 s of that sensor clearing is the same
presence. Arrivals in the first 3 s after boot are ignored (PIR settle).

**Ambient gating.** When enabled, blocks only NEW activations while lux >=
threshold. Unknown lux (HA offline) fails open: lights still work.

## Home Assistant entities

| Entity | Purpose |
|---|---|
| Lower Flight / Landing / Upper Flight Color | color = hue, brightness = section level, off = section disabled |
| Up Effect / Down Effect | Cascade, Soft Wipe, Comet, Fade |
| Incidental Effect | Retreat, Firefly, Ripple, Breath |
| Effect Speed, Master Brightness | look |
| Hold Minimum / Maximum, Grace Period / Level, Exit Delay | timing |
| Ambient Gating, Ambient Threshold | daylight suppression |
| Simulate Up / Down / Incidental | demo or test without walking the stairs |
| Reset Learned Timing | clears trip history |
| State, Last Event | live phase and the latest decision, with reason |
| Learned Hold Up/Down, Learned Samples Up/Down | diagnostics |
| Lower Motion, Upper Motion | raw PIR state |

Select option order is a contract with the `Style` / `IncidentalStyle` enums
in `stair_effects.h`. Add new effects to both, in the same position.

## Host tests

```bash
cd tools/sim
make          # builds with -Wall -Wextra -Wconversion -Werror and runs 256 checks
make show     # ASCII frames of every effect
```

Covers traversals, slow climber, incidental path, learned hold, grace
restore, boot settle, gating, chatter, two people, crossing, preview,
simulation, millis() rollover, and effect bounds/continuity at 0.5x-2x speed.

## Architecture

```
PIR / lux / HA entities --(YAML lambdas)--> StairEngine (ESPHome glue)
                                               |
                       Director: state machine, trip queues, learning
                       Effects:  intensity per pixel (no color possible)
                       Compose:  intensity x section color x brightness
                                               |
                         addressable_lambda "Engine" --> RMT --> WS2811
```

Only `stair_engine.h/.cpp` touch ESPHome APIs (Component, millis, preferences,
AddressableLight writes). Everything else is plain C++20.

## Known limits (two PIR endpoints)

- A ball and a slow climber look identical until the backstop, so incidental
  resolves after hold + grace, not instantly. `Director::report_incidental()`
  is ready for ToF/mmWave nodes that classify on their own.
- A crossing within ~0.8 s is detected; a crossing that looks like a fast
  completed trip can briefly exit, then relights the moment the far end
  triggers. A mid-stair presence sensor removes this.

## Removed from v2

Storage light/PIR, DHT11, the GPIO5 "Platform Color Picker" strip, `sun:`,
and all effects with hard-coded colors.
