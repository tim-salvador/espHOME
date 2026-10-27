# Stairwell Lighting (stair_engine)

Direction-aware, sectioned LED stair lighting for ESPHome and Home Assistant.

Walk up and the stairs light ahead of you from the bottom. Walk down and they
light from the top. When you reach the other end they turn off behind you, in
the direction you walked. A ball rolling past a sensor gets its own short
"incidental" animation instead of a full light show. Every section has its own
color, every action has its own effect, and the hold time learns how long your
household actually takes on the stairs.

All behavior lives in a small C++ engine that compiles and runs on a PC for
testing (3,100 automated checks), then runs unchanged on an ESP8266 or ESP32.

![version](https://img.shields.io/badge/version-3.4.0-blue) ![esphome](https://img.shields.io/badge/ESPHome-%E2%89%A52026.9.0-black) ![boards](https://img.shields.io/badge/boards-ESP8266%20%7C%20ESP32-green)

---

## Contents

- [Features](#features)
- [How it works](#how-it-works)
- [Hardware](#hardware)
- [Wiring and pinouts](#wiring-and-pinouts)
- [Storage room light](#storage-room-light)
- [Build and flash](#build-and-flash)
- [Home Assistant / Web UI settings](#home-assistant--web-ui-settings)
- [Effects](#effects)
- [Relay (landing effect)](#relay-landing-effect)
- [Using it](#using-it)
- [Why it is built this way](#why-it-is-built-this-way)
- [Troubleshooting](#troubleshooting)
- [Testing](#testing)
- [Project files](#project-files)
- [Extending](#extending)
- [Ambient mode](#ambient-mode)
- [Future ideas](#future-ideas)
- [Known limits](#known-limits)
- [Roadmap](#roadmap)
- [Changelog](#changelog)

---

## Features

- **Three actions, handled differently:** going up, going down, and incidental
  triggers (pets, a ball, a sensor glitch).
- **Sections:** the strip is split into independently colored sections. This
  install uses three: lower flight, landing, upper flight. Up to 8 are supported.
- **Live per-section color** from a Home Assistant color wheel. Changing a color
  lights just that section for 5 seconds so you can see it in place.
- **Effect per action** from a dropdown: 13 traversal effects (Cascade, Soft
  Wipe, Comet, Fade, Glitch, Stagger, Scatter, Typewriter, Bloom, Bounce, Sparkle
  Fill, Curtain, Lead Glow) and 8 incidental effects (Retreat, Firefly, Ripple,
  Breath, Peek, Sonar, Shrug, Ember). Effects never contain colors; they only
  shape brightness, and color always comes from the section.
- **Relay landing effect:** the landing can run its own effect. The action's
  effect carries you to the landing, hands off, and takes back over for the
  next flight, in your direction of travel.
- **Custom effects in one file:** effects register by name, so adding one means
  editing `stair_effects_custom.cpp` and adding its name to a dropdown.
- **Hold until confirmed:** lights stay on until the person is seen arriving at
  the other end. No fixed timer turns the lights off on a slow climber.
- **Learned backstop:** for trips that never complete (turn-backs, false
  triggers), the timeout adapts to how long real trips take in your home.
- **Grace dim:** before giving up, the stairs dim instead of snapping off. Any
  motion brings them straight back.
- **Sensor fault detection:** a false-triggering or stuck PIR is flagged in Home
  Assistant and can no longer hold the lights on forever.
- **Ambient mode:** instead of going dark when idle, the stairs can run a slow,
  dim ambient effect (Glow, Guide, Landing Beacon, Breathe, Tide, Starfield,
  Candle, Ember Bed). Motion takes over seamlessly and ambient fades back after.
- **Daylight gating:** optional suppression of activations from any HA lux sensor.
- **Simulate buttons** to demo or test every action without walking the stairs.
- **Dual target:** one YAML file builds for ESP8266 or ESP32.
- **Future storage room light:** a PIR-controlled 12 V white strip on the same
  controller, enabled with one line in the config.

---

## How it works

### Architecture

```mermaid
flowchart LR
    subgraph Inputs
        PL[Lower PIR]
        PU[Upper PIR]
        LX[HA lux sensor]
        UI[HA / Web UI<br/>colors, effects, timing]
    end
    subgraph Engine["stair_engine (C++)"]
        DIR[Director<br/>state machine<br/>trip tracking<br/>learning, faults]
        REG[Effect registry<br/>built-in + custom]
        FX[Effects via relay<br/>brightness only]
        CMP[Compositor<br/>brightness x section color]
    end
    PL --> DIR
    PU --> DIR
    LX --> DIR
    UI --> DIR
    UI --> CMP
    REG --> FX
    DIR --> FX --> CMP
    CMP --> DRV[LED driver<br/>RMT or NeoPixelBus]
    DRV --> LS[74AHCT125<br/>3.3 V to 5 V]
    LS --> STRIP[WS2811 strip<br/>58 px]
```

### Lighting phases

```mermaid
stateDiagram-v2
    [*] --> idle
    idle --> enter: new entry at an end
    idle --> preview: color changed
    preview --> idle: 5 s then fade out
    preview --> enter: motion
    enter --> hold: effect done
    hold --> exit: all trips done and sensors clear
    hold --> grace: trips still open after backstop
    grace --> restore: any motion
    grace --> incidental: grace period over
    exit --> restore: motion
    incidental --> restore: motion
    restore --> hold
    exit --> idle: effect done
    incidental --> idle: effect done
```

### Trip tracking

With only two sensors there is no way to see what happens mid-stairs, so the
engine keeps a short queue of open trips per direction:

- A trigger at the bottom with no open "down" trip is a **new up trip**.
- A trigger at the top while an "up" trip is open (and at least 0.8 s old) is
  that trip **completing**. The time it took is recorded for learning.
- Lights exit only when **every** trip has completed and both sensors are clear.

Two people, someone turning back, or a ball all leave trips open. Those are
resolved by the backstop: hold, then grace dim, then the incidental effect.

### Learned backstop

Each completed trip time is stored (the last 16 per direction, saved to flash at
most once a minute). The backstop is:

```
backstop = 2 x (90th percentile trip time), clamped to [Hold Minimum, Hold Maximum]
```

Until 3 trips have been learned in a direction, Hold Maximum is used. Simulated
trips never train it. Example: if your trips up take 4 to 6 s, the backstop is
about 12 s, raised to the 20 s Hold Minimum.

### Sensor faults

A PIR that keeps firing with nobody there would otherwise hold the lights on
indefinitely. The engine flags a sensor as faulted when either:

- it produces **5 new entries within 3 minutes, none of which complete**, or
- it stays **continuously active for 5 minutes**.

While faulted, that sensor can still **wake dark stairs** (a real person is
never left in the dark), but it cannot hold them on, add phantom trips, or
complete trips. Trips started by it time out at Hold Minimum. The fault clears
automatically after 10 quiet minutes, or with the Clear Sensor Faults button.

---

## Hardware

### Installed controller

| Part | Notes |
|---|---|
| HiLetgo NodeMCU v1.0 (ESP8266MOD, ESP-12E, CP2102) | 4 MB flash. ESPHome board `nodemcuv2` |
| NodeMCU screw-terminal carrier board | with a perfboard area |
| SN74AHCT125N | 3.3 V to 5 V level shifter for LED data, powered from 5 V |
| Buck converter module, 12 V to 5 V | feeds NodeMCU VIN and the 74AHCT125 |
| 12 V power supply | LED strip and buck input |
| WS2811 12 V addressable strip | 58 pixels (driver ICs), BRG order |
| 2x AM312 mini PIR | 3.3 V, about 15 uA, fixed ~2 s output pulse |
| 4-core cable per sensor | red +, white -, green signal, blue ground guard |
| WAGO 221 lever connectors | power distribution |
| Enclosure | |

### Recommended additions

| Part | Qty | Where | Why |
|---|---|---|---|
| 100 nF ceramic capacitor | 1 | 74AHCT125 pin 14 to pin 7 | local decoupling for the shifter |
| 100 ohm resistor | 1 per PIR | in series with PIR + at the sensor | RC filter (see below) |
| 100 uF electrolytic capacitor | 1 per PIR | PIR VCC to GND at the sensor | RC filter |
| 100 nF ceramic capacitor | 1 per PIR | PIR VCC to GND at the sensor | RC filter, high frequency |
| 10 kohm resistor | 1 | 74AHCT125 pin 5 to GND | keeps the storage gate low while the ESP boots |

### Optional ESP32 build

ESP32 WROOM-32 (`esp32dev`), ESP-IDF, RMT LED driver. Same engine and features,
a higher frame rate, and internal pulldowns on the PIR pins. Set `board: esp32`.

---

## Wiring and pinouts

### System overview

```mermaid
flowchart TB
    PSU[12 V PSU] --> W12[WAGO 12 V +]
    PSU --> WG[WAGO GND]
    W12 --> BUCK[Buck 12 V to 5 V]
    WG --> BUCK
    W12 --> STRIP[WS2811 strip +12 V]
    WG --> STRIPG[WS2811 strip GND]
    BUCK -- 5 V --> VIN[NodeMCU VIN]
    BUCK -- 5 V --> SHV[74AHCT125 VCC pins 10, 13, 14]
    BUCK -- GND --> NG[NodeMCU GND bottom left]

    RX[NodeMCU RX GPIO3, DMA] --> S2[74AHCT125 pin 2, 1A]
    S3[74AHCT125 pin 3, 1Y] --> DATA[WS2811 data in]

    D1[NodeMCU D1 GPIO5] --> S5[74AHCT125 pin 5, 2A]
    S6[74AHCT125 pin 6, 2Y] --> MOS[MOSFET gate, storage strip]

    LP[Lower AM312] -- signal --> D7[NodeMCU D7 GPIO13]
    UP[Upper AM312] -- signal --> D6[NodeMCU D6 GPIO12]
    SP[Storage AM312] -. future .-> D5[NodeMCU D5 GPIO14]
```

### NodeMCU carrier terminals (as installed)

Right-hand block, top to bottom:

| Terminal | GPIO | Connection | Notes |
|---|---|---|---|
| D0 | 16 | none | tied to the onboard LED, avoid for inputs |
| D1 | 5 | 74AHCT125 pin 5 (2A) | storage strip control (future). Driven LOW by firmware |
| D2 | 4 | none | was stair strip data before v3.3.1 |
| D3 | 0 | none (old temp sensor harness removed) | boot pin, keep free |
| D4 | 2 | none | boot pin, avoid |
| 3V3 | | lower PIR + (red) | through the RC filter at the sensor |
| GND | | lower PIR - (white) and lower guard (blue) | guard grounded here only |
| D5 | 14 | none | reserved for the storage PIR (future) |
| D6 | 12 | upper PIR signal (green) | |
| D7 | 13 | lower PIR signal (green) | |
| D8 | 15 | none | boot pin, avoid |
| RX | 3 | 74AHCT125 pin 2 (1A) | stair strip data, DMA (see below) |
| TX | 1 | none | serial log output |
| GND | | upper PIR - (white) | |
| 3V3 | | none | |

Left-hand block:

| Terminal | Connection |
|---|---|
| VIN | 5 V from the buck converter |
| 3V3 | upper PIR + (red), through the RC filter at the sensor |
| GND (top) | upper guard (blue) |
| GND (bottom) | GND from the buck converter |

All GND terminals are the same net. Each guard conductor is grounded **at the
controller end only**; leave the far end unconnected.

### 74AHCT125 level shifter

```
               +---\/---+
 1OE   GND    -|1     14|- VCC  5 V
 1A    RX     -|2     13|- 4OE  5 V   (gate 4 disabled)
 1Y    strip  -|3     12|- 4A   GND
 2OE   GND    -|4     11|- 4Y   n/c
 2A    D1     -|5     10|- 3OE  5 V   (gate 3 disabled)
 2Y    MOSFET -|6      9|- 3A   GND
 GND          -|7      8|- 3Y   n/c
               +--------+
```

| Gate | Input | Output | State |
|---|---|---|---|
| 1 | RX / GPIO3 | WS2811 data | active, DMA |
| 2 | D1 / GPIO5 | storage MOSFET gate | active, PWM for fades |
| 3 | GND | n/c | disabled (OE high) |
| 4 | GND | n/c | disabled (OE high) |

OE is active LOW: a gate is enabled when its OE pin is grounded.

### Why the stair data is on RX (GPIO3)

On the ESP8266, PWM (used to fade the storage light) is generated in a
**non-maskable interrupt**. The default NeoPixelBus driver bit-bangs WS2811 data
with normal interrupts disabled, but it cannot block an NMI, so every PWM edge
stretches a data bit and the strip shows random RGB twinkles. The `esp8266_dma`
method sends the data from the I2S hardware instead, which interrupts cannot
disturb, and it only exists on GPIO3. It also removes the ~1.8 ms per frame of
blocked interrupts that bit-banging caused, which is better for Wi-Fi.

GPIO3 is also the USB serial RX line. Flashing over USB still works; the strip
may flicker while flashing, which is harmless. ESPHome's logger only transmits,
so logs over USB keep working. If the strip stays dark after switching, set
`logger: hardware_uart: UART1` so the logger leaves UART0 alone (USB logs then
stop; API and web logs still work).

### PIR sensor wiring with RC filter

The AM312 is known to false-trigger from supply ripple, and the NodeMCU's 3.3 V
regulator also feeds the ESP8266, whose Wi-Fi bursts pull hundreds of mA. The
AM312 draws only about 15 uA, so a series resistor plus capacitors **at the
sensor** filters the ripple with a negligible 1.5 mV drop:

```
 controller end                          sensor end
 3V3  ------ red ------[ 100 ohm ]---+------+------ AM312 VCC
                                     |      |
                                  100 uF  100 nF
                                     |      |
 GND  ------ white ------------------+------+------ AM312 GND
 GPIO ------ green -------------------------------- AM312 OUT
 GND  ------ blue (guard) ------------------- x     (far end NOT connected)
```

The filter corner is about 16 Hz.

---

## Storage room light

A plain 12 V white strip (two wires, + and -) only needs on and off with a fade,
so it does not need an addressable strip. A logic-level N-channel MOSFET
switches its negative side. The existing 74AHCT125 gate 2 provides a 5 V gate
drive, which fully turns on common logic-level MOSFETs. PWM from the ESP gives
smooth fades.

### Parts

| Part | Example | Notes |
|---|---|---|
| Logic-level N-MOSFET, TO-220 | IRLZ44N (used here) or IRLB8721PbF | do **not** use IRF520 modules; they are not logic-level |
| Gate resistor | 100 ohm | limits ringing |
| Gate pulldown | 10 kohm, gate to source | the MOSFET stays off if the drive is lost |
| Boot pulldown | 10 kohm, 74AHCT125 pin 5 to GND | gate 2 input stays LOW while GPIO5 floats during boot |
| AM312 and RC filter | as above | wired to D5 |
| 12 V white strip | | check its current draw against the PSU |

An IRLB8721 runs cool with no heatsink up to several amps. LED strips need no
flyback diode.

### Storage wiring

```mermaid
flowchart LR
    V12[12 V +] --> SP[White strip +]
    SN[White strip -] --> DR[MOSFET drain]
    SRC[MOSFET source] --> GND[GND]
    Y2[74AHCT125 pin 6, 2Y] --> RG[100 ohm] --> G[MOSFET gate]
    G --> RPD[10 kohm] --> SRC
    D1[NodeMCU D1 GPIO5] --> A2[74AHCT125 pin 5, 2A]
    A2 --> RBP[10 kohm] --> GND
    SPIR[Storage AM312 signal] --> D5[NodeMCU D5 GPIO14]
```

```
 12 V + -------------------------------- strip +
                                         strip - ---- D (drain)
 74AHCT125 pin 6 ---[100 ohm]---+------------------- G (gate)
                                |
                             [10 kohm]
                                |
 GND ---------------------------+------------------- S (source)

 IRLB8721 / IRLZ44N, TO-220, facing the label: G D S (left to right)
```

### Enabling it

1. Wire the MOSFET, strip, and PIR as above.
2. In `stairwell-lighting.yaml`, set `storage_installed: true`.
3. Flash. New entities appear: **Storage Light**, **Storage Motion**,
   **Storage Off Delay**, **Storage Motion Control**.

Behavior: motion fades the light up over 1 s to the brightness last set on
Storage Light (set it once from Home Assistant or the web page; motion reuses it). When motion
clears, a restartable timer waits Storage Off Delay, then fades out over 2 s.
New motion during the wait cancels the timer. Turn off Storage Motion Control
to run the light manually from Home Assistant.

Until `storage_installed` is true, the firmware still drives GPIO5 LOW from
boot, so gate 2 never sees a floating input.

Leave Storage Motion Control **off** until the storage PIR is wired: an
unconnected D5 floats and can trigger the light by itself.

**Measuring the strip voltage.** Below 100 % the MOSFET is switched at 1 kHz,
and a DC multimeter shows the average, lowered further by gamma correction
(2.8): about 8.9 V at 90 %, 1.7 V at 50 % on a 12 V supply. At 100 % the output
is held solidly on, so the strip should read close to the supply voltage. If it
does not, check gate to source (about 5 V) and drain to source (under 0.1 V);
a higher drain-source voltage means the MOSFET is not fully on.

| Board | Storage LED pin | Storage PIR pin | PWM |
|---|---|---|---|
| esp8266 | GPIO5 (D1) | GPIO14 (D5) | `esp8266_pwm`, 1 kHz |
| esp32 | GPIO19 | GPIO25 | `ledc`, 1 kHz |

---

## Build and flash

### Requirements

- ESPHome **2026.9.0 or newer** (the board selection uses Jinja substitutions
  with dictionary values).
- `components/` next to the YAML file (local external component).

### Configure

Edit the top of `stairwell-lighting.yaml`:

```yaml
substitutions:
  board: esp8266           # esp8266 | esp32
  storage_installed: false # true once the storage hardware is wired
  name: stairwell
  friendly_name: Stairwell
  lux_entity: sensor.hue_motion_sensor_1_illuminance
```

Secrets:

```bash
cp secrets.example.yaml secrets.yaml
openssl rand -base64 32    # paste as api_encryption_key
```

### Build

```bash
esphome config  stairwell-lighting.yaml   # validate
esphome compile stairwell-lighting.yaml   # build
esphome run     stairwell-lighting.yaml   # flash and follow logs
```

### Updating without losing your settings

Do not paste the full `stairwell-lighting.yaml` over your device config: every
release ships with default settings (`storage_installed: false`,
`name: stairwell`), so pasting it wipes your choices. Instead, keep your settings
in a small device file that includes the full config.

ESPHome config folder (`/config/esphome` in Home Assistant):

```
secrets.yaml
stairwell-lighting.yaml            <- your device file (copy of example-device.yaml)
stairwell-lighting/
  stairwell-lighting.yaml          <- full config from the release
components/
  stair_engine/                    <- engine source from the release
```

Your device file ([`example-device.yaml`](example-device.yaml)):

```yaml
substitutions:
  name: stairwell-lighting
  friendly_name: Stairwell
  board: esp8266
  storage_installed: true
  lux_entity: sensor.hue_motion_sensor_1_illuminance

packages:
  stairwell: !include stairwell-lighting/stairwell-lighting.yaml
```

Settings in the device file override the defaults in the full config. To
update: replace `stairwell-lighting/stairwell-lighting.yaml`, and
`components/stair_engine/` when the release notes say the engine changed. The
dashboard cannot create folders; use the File Editor, Samba, or SSH add-on.

### First flash: use USB

The previous firmware on this NodeMCU was built for a 1 MB layout (`esp01_1m`),
so its OTA updater sees too little free space for v3. Flash once over USB.
After that, OTA works normally. Settings from v2 do not carry over, and v2
entities can be removed from Home Assistant.

### Security defaults

- API traffic is encrypted with `api_encryption_key`.
- OTA uploads use the same key (`ota: encryption: {}`); there is no OTA password.
- The web server's plaintext `/update` endpoint is disabled.

---

## Home Assistant / Web UI settings

Everything below appears in Home Assistant and on the device's built-in web
page (`http://stairwell.local`).

### Colors

| Entity | Type | What it does |
|---|---|---|
| Lower Flight Color | light | color wheel = section color, brightness = section level, off = section disabled |
| Landing Color | light | same, for the landing |
| Upper Flight Color | light | same, for the upper flight |

Examples:
- Warm white stairs with a blue landing: set both flights to a warm white and the
  landing to blue. Each change previews that section for 5 s.
- A dimmer landing: drop Landing Color brightness to 30%.
- Skip the landing entirely: turn Landing Color off.

### Effects

| Entity | Options | Example |
|---|---|---|
| Up Effect | Cascade, Soft Wipe, Comet, Fade, Glitch, Stagger, Scatter, Typewriter, Bloom, Bounce, Sparkle Fill, Curtain, Lead Glow | Comet for a playful climb |
| Down Effect | same as Up | Soft Wipe for a calm descent |
| Incidental Effect | Retreat, Firefly, Ripple, Breath, Peek, Sonar, Shrug, Ember | Peek for the cat's 3 a.m. visits |
| Landing Effect | Follow Action, or any Up/Down effect | Bloom on the landing, Cascade on the flights |
| Effect Speed | 25 to 200 % | 150 % makes every effect 1.5x faster |

Examples:
- Cascade up the flights with a Glitch landing: Up Effect = Cascade, Landing
  Effect = Glitch. Climbing, the cascade reaches the landing, the landing
  glitches on, and the cascade resumes up the top flight.
- Same look in both directions: set Up and Down to the same effect and leave
  Landing Effect on Follow Action.

### Ambient

| Entity | Default | Range | What it does |
|---|---|---|---|
| Ambient Mode | off | switch | off = idle is dark; on = idle runs the Ambient Effect |
| Ambient Effect | Glow | Glow, Guide, Landing Beacon, Breathe, Tide, Starfield, Candle, Ember Bed | the idle look |
| Ambient Level | 30 % | 10 to 60 % | brightness ceiling for ambient, in perceived brightness (see [Ambient mode](#ambient-mode)) |

Examples:
- A night light that marks the stair edges: Ambient Mode on, Ambient Effect =
  Guide, Ambient Level = 30 %, scheduled from sunset to sunrise (see the
  automation in [Ambient mode](#ambient-mode)).
- A gentle glow on the landing only: Ambient Effect = Landing Beacon.

### Timing and behavior

| Entity | Default | Range | What it does |
|---|---|---|---|
| Master Brightness | 100 % | 5 to 100 % | scales everything; lower it to cap power or for a dimmer default |
| Hold Minimum | 20 s | 5 to 120 s | the backstop never goes below this |
| Hold Maximum | 60 s | 10 to 300 s | backstop ceiling, and the backstop until 3 trips are learned. Wins if it conflicts with Hold Minimum |
| Grace Period | 8 s | 2 to 30 s | how long the stairs stay dimmed before the incidental effect |
| Grace Level | 35 % | 5 to 80 % | brightness during grace |
| Exit Delay | 1.5 s | 0 to 10 s | pause after the last sensor clears before the exit effect |
| Daylight Gating | off | switch | when on, new activations are blocked while lux is at or above the threshold |
| Daylight Threshold | 75 lx | 0 to 500 lx | daylight level for gating |

Examples:
- Someone in the house is slow on the stairs: raise Hold Minimum to 45 s.
- Lights linger too long after a ball rolls by: lower Hold Maximum to 30 s.
  This also sets the timeout before enough real trips have been learned.
- Stop the stairs lighting on a sunny afternoon: enable Daylight Gating, then set
  Daylight Threshold just below your typical daytime reading. If the lux sensor
  goes offline, gating fails open so the stairs still work.

### Actions

| Button | What it does |
|---|---|
| Simulate Up / Down | runs a full scripted trip (about 8 s) with the current effects |
| Simulate Incidental | runs the incidental path with a short backstop (about 7 s) |
| Reset Learned Timing | clears trip history; the backstop returns to Hold Maximum |
| Clear Sensor Faults | clears both fault flags immediately |
| Restart | reboots the controller |

Simulations are refused while the stairs are in use and never train learning.

### Status and diagnostics

| Entity | What it shows |
|---|---|
| State | idle, preview, enter, hold, grace, exit, incidental, restore |
| Last Event | the engine's latest decision and why, e.g. `up complete in 5.2s (0/0 pending)` |
| Lower Motion / Upper Motion | raw PIR state |
| Lower / Upper Sensor Fault | problem flag (see [Sensor faults](#sensor-faults)) |
| Learned Hold Up / Down | current backstop per direction, in seconds |
| Learned Samples Up / Down | how many trips have been learned (max 16) |
| WiFi Signal, Uptime | health |

---

## Effects

Effects output brightness only. Color is applied afterward from each section,
so every effect works with any color scheme. Durations are at 100 % speed on
this 15-step layout; Effect Speed scales them.

### Up / Down (traversal)

Each traversal effect has an **enter** (light up in your direction of travel)
and a matching **exit** (turn off behind you, in the same direction).

| Effect | Look | Duration |
|---|---|---|
| Cascade | steps light one after another, each with a soft fade | 1.9 s |
| Soft Wipe | a smooth, soft-edged front sweeps along the stairs | 1.5 s |
| Comet | a bright head runs ahead; the trail settles from dim to full | 1.8 s (exit 1.4 s) |
| Fade | the whole staircase fades together | 0.8 s |
| Glitch | each step flickers like a bad signal, faster as it "locks on", then snaps solid; now and then a block of steps tears (flashes) together. Exit stutters and drops out | 2.1 s |
| Stagger | every other step lights first, like footprints, then a second wave fills the gaps. Exit turns off in the same two waves | 2.2 s |
| Scatter | steps light out of order with random delays, but the light still flows your way; pixels within a step sparkle in slightly apart | 1.8 s |
| Typewriter | pixels land one at a time in your direction, each with a bright tick before settling dim; then everything rises to full. Exit clicks pixels off one by one. The cadence adapts so long strips still finish in about 1.8 s | 2.0 s (exit 1.7 s) |
| Bloom | steps light in your direction, each opening from its center outward. Made for wide steps such as the 9-pixel landing; on 2-pixel steps it reads like a soft Cascade. Exit closes each step from its edges inward | 2.0 s |
| Bounce | a soft front races to the far end at about half brightness, bounces, and a second front comes back filling everything to full. Exit: the first front dims, the return front turns it off | 1.8 s |
| Sparkle Fill | random pixels pop on with a bright flash and settle, denser near you first, until the stairs are full. Exit winks pixels off at random, nearest first | 1.7 s (exit 1.4 s) |
| Curtain | light closes in from both ends and meets in the middle; exit opens a dark gap in the middle that spreads to both ends. Ignores direction | 1.1 s |
| Lead Glow | a fast, dim front scouts the path ahead, then the full light follows. Exit: a fast front dims everything, then a slower one turns it off | 1.8 s |

### Incidental

Incidental effects start from whatever is lit (normally the dimmed grace level)
and end dark.

| Effect | Look | Duration |
|---|---|---|
| Retreat | a brief "looking around" shimmer, then the light withdraws toward the trigger end | 1.7 s |
| Firefly | pixels drift into a twinkle and wink out one by one | 2.4 s |
| Ripple | a decaying wave rolls away from the trigger end | 2.1 s |
| Breath | the staircase exhales, takes a small breath, and fades | 2.9 s |
| Peek | the stairs fall away except a small glow near the trigger end, which pulses softly twice and fades. Subtle; good for pets | 2.4 s |
| Sonar | the stairs fade while one soft pulse travels from the trigger end to the far end, weakening as it goes | 2.0 s |
| Shrug | two quick dim blinks, then a fade out | 1.5 s |
| Ember | the stairs smoulder with slow, uneven flicker like dying coals, each pixel burning out at its own pace | 3.2 s |

Run `make show` in `tools/sim` to see ASCII frames of every effect.

---

## Relay (landing effect)

The landing can run its own effect while the flights use the action's effect.
The effects pass the light along like a relay baton, in the direction you walk:

```mermaid
flowchart LR
    A[Lower flight<br/>action effect] -- hand-off --> B[Landing<br/>Landing Effect] -- hand-off --> C[Upper flight<br/>action effect]
```

Going down, the order reverses: upper flight, landing, lower flight. Exits relay
the same way, so the off-wave also changes character as it crosses the landing.

**Hand-off timing.** Each effect reports when it should pass the baton. Effects
with a moving front (Cascade, Soft Wipe, Comet, Glitch, Scatter, Typewriter,
Bloom, Bounce, Lead Glow) hand off when the front reaches the far edge of their
section, so the motion continues without a gap. Stagger hands off when its first
wave reaches the far edge, Sparkle Fill about 60% of the way through its fill,
and the non-directional Fade and Curtain halfway through. Before its turn, a section simply holds its current
level.

**Why the landing.** Its steps are 9 pixels wide versus 2 on the flights, so
effects with texture inside a step (Bloom, Typewriter, Sparkle Fill, Glitch's
dropout, Scatter's sparkle) read best there.

**Rules.**
- Landing Effect = Follow Action means no relay: the action's effect runs over
  the whole staircase in one continuous sweep.
- Choosing the same effect as the action is identical to Follow Action.
- Incidental effects always run across the whole staircase (no relay), because
  they start from whatever is lit, not from dark.
- The engine supports an override on any section. Only the landing is exposed
  in the YAML; see [Extending](#extending) to add others.

---

## Using it

1. **Walk the stairs normally** for a few days. After 3 trips each way the
   backstop starts adapting (watch Learned Samples).
2. **Pick colors** with the three color entities. Each change previews in place.
3. **Pick effects** per action. Use the Simulate buttons to compare them without
   walking the stairs.
4. **Tune** Hold Minimum if anyone is slow on the stairs, and Hold Maximum if
   incidental triggers linger.
5. **Watch Last Event** when something looks wrong. It records each decision
   and the reason.

Example Home Assistant automation, dimmer stairs at night:

```yaml
automation:
  - alias: Stairs night brightness
    triggers:
      - trigger: time
        at: "22:30:00"
    actions:
      - action: number.set_value
        target:
          entity_id: number.stairwell_master_brightness
        data:
          value: 40
  - alias: Stairs day brightness
    triggers:
      - trigger: time
        at: "06:30:00"
    actions:
      - action: number.set_value
        target:
          entity_id: number.stairwell_master_brightness
        data:
          value: 100
```

Example, get notified when a sensor misbehaves:

```yaml
automation:
  - alias: Stair sensor fault
    triggers:
      - trigger: state
        entity_id:
          - binary_sensor.stairwell_lower_sensor_fault
          - binary_sensor.stairwell_upper_sensor_fault
        to: "on"
    actions:
      - action: notify.notify
        data:
          message: "Stairwell PIR fault: {{ trigger.to_state.name }}"
```

Entity IDs depend on your device name; check them in Home Assistant.

---

## Why it is built this way

**C++ engine instead of YAML lambdas.** v2 kept effects in YAML lambdas that
coordinated through shared globals (`effect_finished`, `system_state`), which
caused race conditions: lights stuck on, and lights turning off on a slow
climber about 9 s after they left the bottom sensor. The engine has a single
state machine, compiles on a PC for testing, and only its glue layer touches
ESPHome APIs, so it can move to a bare ESP-IDF firmware later.

**Effects cannot produce color.** Effects return brightness per pixel; color is
applied in one place. A hard-coded color inside an effect is impossible, not
just discouraged.

**One permanent render effect.** The strip runs a single "Engine" effect and
the engine draws every frame. No effect switching, no handshakes between
effects.

**Hold until confirmed, not a timer.** A fixed timeout is the main way stair
lights fail people: too short and the lights go out mid-climb, too long and
they waste power. Waiting for the far end, with a learned backstop for the
unresolved cases, gets both right.

**Faulted sensors can wake, but not hold.** Safety first: a real person must
never be left in the dark, but a broken sensor must not keep the lights on all
night.

**Virtual color lights.** Each section's color is a normal HA light entity
that drives no hardware, so you get a native color wheel and brightness slider.
The engine reads its target values directly.

**Pin choices.** On ESP32, GPIO12 is a strapping pin that selects 1.8 V flash
when held high at boot (a PIR can do that), so the PIRs use GPIO32/33. On
ESP8266, GPIO0, 2 and 15 are boot pins and are kept off sensors.

**Level shifter.** The ESP outputs 3.3 V logic; WS2811 data and a MOSFET gate
both want 5 V. The 74AHCT125 is fast enough for 800 kHz LED data and accepts
3.3 V inputs at a 5 V supply.

**RC filter on PIR power.** Cheaper and more effective than a separate
regulator, because the AM312 draws almost nothing.

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| Last Event repeats `up entry at lower` with nobody there | AM312 false triggers from supply ripple or RF | add the RC filter at the sensor; ground the guard at the controller; keep wires off the ESP antenna; test with `output_power: 13dB` under `wifi:` |
| A Sensor Fault turns on | the above, or a stuck sensor | fix the cause; the flag auto-clears after 10 quiet minutes |
| Lights do not react at all | daylight gating active | check Daylight Gating and Daylight Threshold against the lux reading |
| Ambient Mode is on but the stairs look dark | Ambient Level too low, or the section colors are very dark | raise Ambient Level (about 10 % or less outputs almost nothing after gamma); check the section colors and that sections are on |
| Animated ambient effects look steppy | 8-bit LEDs have few real brightness steps at low levels | raise Ambient Level to 40 % or more, or use a static effect (Glow, Guide, Landing Beacon) |
| Board will not boot | a sensor or harness on GPIO0, GPIO2 or GPIO15 | disconnect it; see the pin tables |
| Stair strip shows random RGB twinkles when the storage light is on or fading | ESP8266 PWM runs in an NMI that corrupts bit-banged LED data | stair data must be on RX (GPIO3) with `method: esp8266_dma` (the default since v3.3.1) |
| Wi-Fi or API drops on ESP8266 | heavy frame load | raise the Engine `update_interval` to 40 ms |
| Weak Wi-Fi (around -70 dBm) | wires or copper over the module antenna | reroute so nothing sits within 15 mm of the antenna |
| OTA fails with "not enough space" | first update from v2's 1 MB layout | flash once over USB |
| Log says `Lit but no frames rendered` | strip light not running the Engine effect | reboot; the effect is started at boot |
| Colors look wrong | channel order | the stair strip is BRG; adjust `type:` or `channel_colors:` |
| Log warns `Unknown traversal effect` | a select option does not match a registered name | compare with the effect list printed in the boot log (exact, case-sensitive) |

To tell a bad GPIO from a bad sensor, swap the two PIR signal wires at the
board. If the problem follows the wire, it is the sensor or cable. If it stays
on the same input, it is the pin.

---

## Testing

The core engine has no ESPHome dependencies and is tested on a PC:

```bash
cd tools/sim
make          # build with -Wall -Wextra -Wconversion -Werror and run all checks
make show     # also print ASCII frames of every effect
```

3,100 checks cover: up and down trips, the slow climber, the incidental path,
learned hold, grace restore, boot settle, daylight gating, PIR chatter, two
people, crossing traffic, the false-trigger pattern from a real field log,
stuck sensors, faulted sensors still waking dark stairs, color preview,
simulation, `millis()` rollover, per-pixel travel order, the effect registry and name selection, relay
order and hand-off in both directions, and every registered effect (on the
whole staircase and on the landing alone, both directions, 0.5x to 2x speed)
against the effect rules: levels within 0 to 1, unmapped pixels dark, a
seamless start, and an exact final frame. Ambient effects are checked for
bounds, visibility, seamless looping at the period wrap, and smooth frame to
frame motion; ambient mode for fade in and out, seamless takeover by motion,
return after exit, live level and effect changes, preview, simulation, and
daylight gating.

Validation status of this release:

| Check | Status |
|---|---|
| Host tests | 3,100 / 3,100 pass |
| `esphome config` (ESP8266 and ESP32, storage on and off) | valid on 2026.9.1 |
| Glue code against ESPHome 2026.9.1 headers | compiles (syntax check) |
| Full firmware compile | run `esphome compile` locally |

---

## Project files

```
.
├── stairwell-lighting.yaml         full device config (ESP8266 or ESP32)
├── example-device.yaml            your settings + include (see Updating)
├── secrets.example.yaml           template for secrets.yaml
├── README.md
├── .gitignore
├── components/
│   └── stair_engine/
│       ├── __init__.py            ESPHome schema, layout validation, code generation
│       ├── stair_engine.h/.cpp    ESPHome glue (the only ESPHome-dependent code)
│       ├── stair_director.h/.cpp  state machine, trip tracking, relay, learning, faults, simulation
│       ├── stair_effects.h        effect interface and registry
│       ├── stair_effect_util.h    helpers for writing effects (smooth, lerp, rand01, ms)
│       ├── stair_effects_builtin.cpp  built-in effects and the registry
│       ├── stair_effects_ambient.cpp  ambient (idle) effects
│       ├── stair_effects_custom.cpp   your effects: Glitch, Stagger, Scatter, Peek, Sonar, ...
│       ├── stair_geometry.h/.cpp  sections, steps, pixel positions
│       └── stair_compose.h        brightness x section color
└── tools/
    └── sim/
        ├── sim.cpp                host tests and ASCII effect viewer
        └── Makefile
```

---

## Extending

### Change the layout

Sections are listed bottom to top under `stair_engine:`:

```yaml
stair_engine:
  id: stairs
  num_pixels: 58
  sections:
    - name: Lower Flight
      first_pixel: 0
      pixels: 16
      pixels_per_step: 2
    - name: Landing
      first_pixel: 16
      pixels: 36
      pixels_per_step: 9      # a landing is just a section of step-equivalents
    - name: Upper Flight
      first_pixel: 52
      pixels: 6
      pixels_per_step: 2
      # reversed: true         # if pixel index increases toward the bottom here
```

Rules, checked at compile time: `pixels` must be a multiple of
`pixels_per_step`, sections must not overlap or run past `num_pixels`, and at
most 8 sections. Each section needs a color light whose
`set_section_color(N, ...)` index matches its position in the list.

### Relay overrides for other sections

The engine accepts an override for any section. To expose one, copy the
Landing Effect select and change the section index (0 = first section listed):

```yaml
  - platform: template
    name: Upper Flight Effect
    options: [Follow Action, Cascade, Soft Wipe, Comet, Fade, Glitch, Stagger, Scatter]
    initial_option: Follow Action
    optimistic: true
    restore_value: true
    on_value:
      - lambda: id(stairs).set_section_effect(2, x.c_str(), x.size());
```

### Add an effect

Effects live in two files. Leave `stair_effects_builtin.cpp` alone; put yours in
**`stair_effects_custom.cpp`**. Effects are looked up by name, so there is no
enum or ordering to keep in sync.

**1. Write the class.** Traversal effects take an `on` flag so one class
provides both enter and exit:

```cpp
// Stagger: every other step lights first, then a second wave fills the gaps.
class Stagger final : public Effect {
 public:
  explicit Stagger(bool on) : on_(on) {}
  const char *name() const override { return "Stagger"; }

  uint32_t duration(const EffectCtx &c) const override {
    return static_cast<uint32_t>(2.0f * half(c) * gap(c) + fade(c));
  }
  // Relay hand-off: when the first wave reaches the far edge.
  uint32_t handoff(const EffectCtx &c) const override {
    return static_cast<uint32_t>(half(c) * gap(c));
  }

  void render(const EffectCtx &c, float *out) const override {
    const float t = static_cast<float>(c.t);
    for (uint16_t px = 0; px < n_px(c); px++) {
      if (!c.geo->mapped(px)) { out[px] = 0.0f; continue; }
      const uint16_t k = c.geo->step_along(px, c.dir);  // step count in travel order
      const float start = (k % 2 == 0 ? 0.0f : half(c) * gap(c)) + static_cast<float>(k / 2) * gap(c);
      out[px] = toward(c, px, this->on_, smooth((t - start) / fade(c)));
    }
  }

 private:
  float half(const EffectCtx &c) const { return std::ceil(n_steps(c) / 2.0f); }
  float gap(const EffectCtx &c) const { return ms(120.0f, c.speed); }   // scales with Effect Speed
  float fade(const EffectCtx &c) const { return ms(300.0f, c.speed); }
  bool on_;
};

const Stagger STAGGER_ON(true), STAGGER_OFF(false);
```

**2. Register it** in `register_custom_effects()` at the bottom of the file:

```cpp
reg.add_traversal("Stagger", &STAGGER_ON, &STAGGER_OFF);   // Up/Down/Landing effect
reg.add_incidental("Sonar", &SONAR);                       // Incidental effect
```

**3. Add the name to the YAML** select options, spelled exactly as registered:
Up Effect, Down Effect and Landing Effect for traversal effects; Incidental
Effect for incidental ones; Ambient Effect for ambient ones. Order does not
matter.

**Ambient effects** register with `reg.add_ambient("Name", &EFFECT)` and follow
different rules: they must override `period()` and repeat exactly every
`period()` ms (the engine passes `t` modulo the period), they never end, they
ignore `c.from`, and they output a full-scale pattern that the engine scales by
Ambient Level. For looping randomness, take time buckets modulo
`period / bucket` so the last bucket blends into the first (see
`loop_noise()` in `stair_effects_ambient.cpp`).

**4. Test it.** `make` in `tools/sim` runs the rule checks on every registered
effect automatically, with no test changes. `make show` prints its frames.

**5. Build and flash.** The boot log lists every registered effect and which
ones are selected.

#### What an effect gets (`EffectCtx`)

| Field | Meaning |
|---|---|
| `geo` | geometry: pixels, steps, positions. In a relay this is one section only |
| `dir` | direction of travel (`Dir::UP` / `Dir::DOWN`) |
| `origin` | end where the activity started (incidental effects grow from here) |
| `t` | ms since this effect started |
| `speed` | Effect Speed as a multiplier; use `ms(base, c.speed)` |
| `from` | levels on the stairs when the effect started (start from these) |
| `seed` | random seed for this run; use `rand01(c.seed, index, salt)` |

#### Useful geometry calls

| Call | Returns |
|---|---|
| `c.geo->mapped(px)` | whether the pixel belongs to this effect's range |
| `c.geo->step_along(px, c.dir)` | step number in travel order, 0 = first step reached |
| `c.geo->along(px, c.dir)` | continuous position in steps along the travel direction |
| `c.geo->rank_along(px, c.dir)` | unique pixel order along the travel direction, 0 = first pixel reached |
| `c.geo->pos(px)` | position in steps from the bottom; `pos - floor(pos)` is the place within a step |
| `c.geo->num_mapped()` | pixels in this effect's range |
| `c.geo->pos_from(px, c.origin)` | distance in steps from the trigger end |
| `n_steps(c)`, `n_px(c)` | steps in range, total pixels |

#### Rules (enforced by the tests)

- Write a level from 0 to 1 for **every** pixel; unmapped pixels get 0.
- Start from `c.from` so transitions are seamless. `toward(c, px, on, p)`
  blends from the captured level toward full (enter) or dark (exit).
- Enter ends at exactly 1.0; exit and incidental end at exactly 0.0.
- No state between calls: compute everything from `c.t` and `c.seed`. For
  flicker, bucket time (`c.t / 40`) and use it as the random salt.
- Count steps with `c.geo`; in a relay it covers one section only.
- Implement `handoff()` if the effect has a moving front, so relays flow.

---

## Known limits

These come from having only two endpoint PIR sensors.

- **Incidental triggers resolve slowly.** A ball and a slow climber look the
  same until the backstop expires, so incidental plays after hold plus grace,
  not instantly. `Director::report_incidental()` is ready for sensors that can
  classify on their own (ToF, mmWave).
- **Crossing traffic can be ambiguous.** Two people crossing so that it looks
  like one fast completed trip can cause a brief exit. The stairs relight the
  moment the far end triggers. A mid-stair presence sensor removes this.
- **A faulted sensor degrades gracefully, not perfectly.** Trips it starts
  cannot be confirmed by the other end, so they time out at Hold Minimum.

---

## Ambient mode

With **Ambient Mode** off, idle means dark. With it on, idle runs a slow, dim
**Ambient Effect** instead. Motion takes over instantly, and when the stairs go
idle again the ambient effect fades back in.

```mermaid
stateDiagram-v2
    [*] --> ambient: Ambient Mode on
    [*] --> dark: Ambient Mode off
    ambient --> enter: motion
    dark --> enter: motion
    enter --> hold
    hold --> exit
    hold --> grace: unresolved
    grace --> incidental
    exit --> ambient: fades in over 2 s
    incidental --> ambient: fades in over 2 s
    exit --> dark: Ambient Mode off
    incidental --> dark: Ambient Mode off
```

**How it behaves**

- **Seamless takeover.** Every effect starts from the levels already on the
  stairs, so the enter effect grows out of the ambient glow with no jump. Steps
  the effect has not reached yet keep glowing at the ambient level.
- **Fades.** Turning Ambient Mode on or off fades over 2 s. After an exit or
  incidental effect (which end dark), ambient fades back in over 2 s.
- **Previews** work the same from ambient; ambient resumes afterward.
- **Unchanged:** trip tracking, sensor faults, learning, simulations, and
  daylight gating. Daylight gating blocks new activations; it does not turn
  ambient off.
- **State** shows `ambient` while idle with Ambient Mode on.
- The pattern uses the section colors and section brightness like everything
  else; a section that is turned off stays dark.

### Ambient effects

| Effect | Look | Motion |
|---|---|---|
| Glow | a steady, even night light | static |
| Guide | only the first and last step of each section glow, marking the edges | static |
| Landing Beacon | only the middle section glows (the landing here); with one or two sections, everything glows | static |
| Breathe | a very slow rise and fall, never fully dark | 7 s per breath |
| Tide | a soft swell drifts from the bottom to the top and back | 16 s round trip |
| Starfield | a dim field where scattered pixels twinkle in and out | random, 10 min loop |
| Candle | each step flickers gently and irregularly | random, 10 min loop |
| Ember Bed | random pixels glow up and fade slowly at different rates | random, 10 min loop |

The random effects repeat exactly every 10 minutes, so they loop without a seam.
They use a fixed seed, so the pattern is the same every night.

### Ambient Level and brightness

Ambient Level is **perceived** brightness, before gamma correction. The strip
applies gamma 2.8 afterward, so low levels are much dimmer in real output:

| Ambient Level | Real output (Glow) | Brightness steps available to animated effects |
|---|---|---|
| 20 % | about 1 % | about 4 |
| 30 % (default) | about 3.5 % | about 9 |
| 40 % | about 8 % | about 19 |
| 50 % | about 15 % | about 34 |

8-bit LEDs have very few distinct levels near black. Static effects look fine at
any level. Animated ones (Breathe, Tide, Candle, Ember Bed, Starfield) look
noticeably smoother at 40 % and above.

### Scheduling

Ambient Mode is a normal switch, so Home Assistant decides when it runs. A night
light from sunset to sunrise:

```yaml
automation:
  - alias: Stair ambient at night
    triggers:
      - trigger: sun
        event: sunset
        id: "on"
      - trigger: sun
        event: sunrise
        id: "off"
    actions:
      - action: "switch.turn_{{ trigger.id }}"
        target:
          entity_id: switch.stairwell_ambient_mode
```

Power: ambient keeps LEDs lit all night. At 30 % on this 58-pixel strip the real
output is about 3.5 %, so the draw is small but not zero.

---

## Future ideas

- Temporal dithering for smoother animated ambient at very low levels.
- An option for exit effects to finish at the ambient level instead of dark.
- Separate ambient brightness per section.

---

## Roadmap

Product direction for this design:

- ToF (VL53L1X) end sensors with two detection zones: direction plus incidental
  classification within about 100 ms, instead of after the backstop.
- Smart sensor nodes on an RS-485 bus, so mid-stair or landing sensors become
  plug-in additions.
- ESP32-S3 controller with PSRAM, a custom PCB, and a 24 V LED rail with
  switched power for true zero standby draw.
- RGBW and RGB+CCT strip support through a per-output color mapper.
- Ambient mode follow-ups (see [Future ideas](#future-ideas)).
- Optional Matter support (per-section lights, stair sensors as occupancy).
- Speed-adaptive cascades that keep pace with the person.

---

## Changelog

### v3.4.0
- **Ambient mode:** idle can run a slow, dim effect instead of going dark. New
  entities: Ambient Mode (switch), Ambient Effect (select), Ambient Level
  (number, 10 to 60 %, default 30 %).
- Eight ambient effects: Glow, Guide, Landing Beacon, Breathe, Tide, Starfield,
  Candle, Ember Bed (new `stair_effects_ambient.cpp`).
- **Renamed** Ambient Gating to **Daylight Gating** and Ambient Threshold to
  **Daylight Threshold** so they are not confused with ambient mode. Their
  entity IDs change (for example `switch.stairwell_ambient_gating` becomes
  `switch.stairwell_daylight_gating`); update any automations that use them and
  set the values again.
- 3,100 host checks.
- Files changed: `components/stair_engine/` (new file plus engine changes),
  `stairwell-lighting.yaml`, README.

### v3.3.3
- `example-device.yaml`: keep your settings (name, board, storage) in a small
  device file that includes the full config, so updates no longer reset them.
- `storage_installed` accepts `true`/`false` with or without quotes (a quoted
  `"false"` previously counted as true).
- Files changed: `stairwell-lighting.yaml`, `example-device.yaml`, README.

### v3.3.2
- Storage light: removed Storage Brightness (it only applied to motion-triggered
  turn-on, which was confusing). Motion now reuses the Storage Light's last-set
  brightness.
- README: storage strip voltage measurement notes (PWM averaging, gamma).

### v3.3.1
- ESP8266: stair strip moved to RX (GPIO3) with `method: esp8266_dma`. Fixes
  random RGB twinkles on the stairs while the storage light is on or fading
  (ESP8266 PWM runs in a non-maskable interrupt that corrupted bit-banged data).
  **Rewire:** level shifter pin 2 from D2 to RX.

### v3.3.0
- New traversal effects: Typewriter, Bloom, Bounce, Sparkle Fill, Curtain, Lead
  Glow. New incidental effects: Shrug, Ember. 13 traversal and 8 incidental
  effects in total.
- Geometry: per-pixel travel order (`rank_along`, `num_mapped`) for effects that
  work pixel by pixel.
- 2,820 host checks.

### v3.2.2
- Device config renamed `stairwell-2boards.yaml` to `stairwell-lighting.yaml`.
  It is now the only YAML; earlier `stairwell.yaml` and `stairwell-2boards.yaml`
  history remains in the git log and the `stairwell-v*` tags.

### v3.2.1
- Fix ESP8266 build: renamed the `TWO_PI` constant to `kTwoPi` (the ESP8266
  Arduino core defines `TWO_PI` as a macro). Core identifiers were checked
  against the macros in the ESP8266 core and SDK headers; no other collisions.

### v3.2
- New traversal effects: Glitch, Stagger, Scatter. New incidental effects:
  Peek, Sonar.
- Relay: per-section traversal overrides with direction-aware hand-off. New
  Landing Effect select.
- Effect registry: effects are selected by name, so YAML option order no longer
  matters. Custom effects live in `stair_effects_custom.cpp`; built-ins in
  `stair_effects_builtin.cpp`; helpers in `stair_effect_util.h`.
- Boot log lists every registered effect and the current selections.
- Tests iterate the registry: every effect is checked on the full staircase and
  on a single section (1,658 checks).
- Soft Wipe timing adjusted so short relayed sections do not flash past.

### v3.1
- Sensor fault detection: chatter (5 unconfirmed entries in 3 min) and stuck
  (5 min active). Faulted sensors wake but cannot hold. New fault entities and
  a Clear Sensor Faults button.
- Lower PIR moved to GPIO13 (D7). GPIO14 (D5) reserved for the storage PIR.
- GPIO5 driven LOW from boot so the level shifter's gate 2 never floats.
- Future storage room light behind `storage_installed`.
- ESP8266 board set to `nodemcuv2` (matches the installed NodeMCU v1.0).
- Default board is now `esp8266`.
- `stairwell.yaml` (ESP32-only) retired; `stairwell-2boards.yaml` covers both.

### v3.0
- Rewrite: C++ `stair_engine` with trip tracking, learned backstop, grace dim,
  per-section colors, effects per action, simulations, and host tests.
- Dual-target YAML. Encrypted OTA, web OTA disabled.
- Removed: storage light on the old GPIO2 wiring, DHT11, `sun:`, and effects
  with hard-coded colors.

### v2.0
- ESPHome YAML with lambdas on ESP8266 (`esp01_1m`), two color presets.
