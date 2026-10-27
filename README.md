# espHOME

ESPHome device configurations for my Home Assistant setup: stair lighting,
landscape lights, an arcade-style LED matrix, a voice assistant satellite, and
reusable snippets. Each project lives in its own folder with its own
documentation.

![ESPHome](https://img.shields.io/badge/ESPHome-2026.9-black) ![Home Assistant](https://img.shields.io/badge/Home%20Assistant-integrated-41BDF5)

---

## Projects

| Project | Board | What it does | Status |
|---|---|---|---|
| [stairwell-lighting](stairwell-lighting/) | ESP8266 NodeMCU or ESP32 | Direction-aware, sectioned LED stair lighting. Lights the way up or down, turns off behind you, learns how long trips take, and runs a dim ambient effect when idle. 13 traversal, 8 incidental and 9 ambient effects, relay effects on the landing, a storage room light, and host tests. Custom C++ engine (`stair_engine`). | Active, v3.5.0. [Full README](stairwell-lighting/README.md) |
| [landscape-lights](landscape-lights/) | ESP32 (esp32dev) | Front yard landscape lights on WS2811 pixels. Per-light effects (pulse, strobe, candle flicker, color cycle), combined effects (rainbow, alternating colors), and holiday presets (Valentine's Day, St. Patrick's Day, Easter, Independence Day, Halloween, Thanksgiving, Christmas, New Year) chosen automatically by date. On at 7:00 PM, off at 11:00 PM. A Home Assistant service sets a preset on demand. | Working; see [Known issues](#known-issues) |
| [arcade-led-matrix](arcade-led-matrix/) | ESP8266 (ESP-01) | A 64x16 addressable LED matrix that scrolls text from a Home Assistant `input_text`, shows the time and the weather forecast from Home Assistant, and plays images and animations. Includes a relay and a confirm button. | Needs updates for current ESPHome; see [Known issues](#known-issues) |
| [voice-satellite](voice-satellite/) | ESP32-WROVER (PSRAM) | A Home Assistant voice assistant satellite: INMP441 microphone and MAX98357A speaker over I2S, media player with announcements, optional wake word, and a 35-LED status strip that shows each phase (idle, listening, thinking, replying, muted, error). | Missing an audio asset; see [Known issues](#known-issues) |
| [code-snippets](code-snippets/) | any | Reusable configuration examples: binary sensors, lights, outputs. | Reference |

---

## Repository layout

```
espHOME/
├── README.md                  this file
├── LICENSE
├── .gitignore                 secrets.yaml, build output, .DS_Store
├── stairwell-lighting/        stair lighting (own README, C++ component, tests)
├── landscape-lights/          front yard landscape lights
├── arcade-led-matrix/         64x16 LED matrix display
├── voice-satellite/           voice assistant satellite
└── code-snippets/             reusable examples
```

---

## Using a project

1. Copy the project's YAML (and any folders it uses, such as
   `stairwell-lighting/components/`) into your ESPHome config folder, for example
   `/config/esphome` in Home Assistant.
2. Add the secrets it references to your `secrets.yaml`:

   | Project | Secrets |
   |---|---|
   | stairwell-lighting | `wifi_ssid`, `wifi_password`, `fallback_ap_password`, `api_encryption_key` |
   | landscape-lights | `wifi_ssid`, `wifi_password`, `local_domain`, `ap_fallback_password`, `api_encryption_key`, `ota_password` |
   | arcade-led-matrix | `wifi_ssid`, `wifi_password`, `led_matrix_address`, `ap_fb_password`, `arcade_led_matrix_enc_key`, `ota_password` |
   | voice-satellite | `wifi_ssid`, `wifi_password`, `local_domain`, `wrover-api`, `wrover-ota` |

   Generate API encryption keys with `openssl rand -base64 32`.
3. Review the pins, LED counts and any network settings (static IPs,
   `use_address`) for your hardware, then validate and flash:

   ```bash
   esphome config  <project>.yaml
   esphome run     <project>.yaml
   ```

Projects with their own README (stairwell-lighting) have more detailed setup,
wiring and update steps there.

---

## Conventions

- **One folder per project**, each with its configuration and, where it helps,
  a README.
- **No secrets in the repo.** Every credential is a `!secret` reference;
  `secrets.yaml` is ignored by `.gitignore`.
- **Versions are tags, prefixed by project**, for example `stairwell-v3.5.0`.
  Browse any project as it was at a release from the Tags list, or with
  `git log` / `git show <tag>`.
- **History is kept across renames.** Use `git log --follow <file>` to see a
  file's history from before the move into its project folder.

---

## Known issues

Checked with `esphome config` on ESPHome 2026.9.1.

| Project | Issue | Fix |
|---|---|---|
| stairwell-lighting | none | |
| landscape-lights | `rgb_order` is deprecated (removed in 2027.3.0) | use `channel_colors: GRB` |
| landscape-lights | OTA uses a password | replace with `encryption: {}` under `ota:` (uses the API key) |
| arcade-led-matrix | fails validation: `image:` entries now need a `type:` and the old `image:` format is deprecated (removed in 2027.1.0) | convert `image:` to the platform format and add `type:` to each image |
| arcade-led-matrix | assets referenced but not in the repo: `img/genesis.png`, `ttf/lexis.ttf` | add the files or document where to get them |
| voice-satellite | fails validation: `beep.wav` is referenced but not in the repo | add the file (48 kHz, 16-bit) next to the YAML |
| voice-satellite | GPIO12 (mic WS) is a strapping pin | works, but a pin that is not strapping is safer |

---

## License

MIT. See [LICENSE](LICENSE).

---

## Author

**Tim Salvador**
[timsalvador.com](https://timsalvador.com) ·
[GitHub](https://github.com/tim-salvador) ·
[LinkedIn](https://www.linkedin.com/in/tim-salvador)
