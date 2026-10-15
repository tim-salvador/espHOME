#pragma once
// ESPHome glue for the framework-free stair core.
//
// Deliberately thin: inputs arrive through YAML lambdas (binary sensor on_state,
// number/select on_value, light on_state), output leaves through one persistent
// addressable_lambda effect that calls render(). This keeps the ESPHome API
// surface this file depends on to: Component, millis(), preferences, and
// AddressableLight pixel writes.

#include <string>
#include <vector>

#include "esphome/components/light/addressable_light.h"
#include "esphome/core/color.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/preferences.h"

#include "stair_compose.h"
#include "stair_director.h"
#include "stair_geometry.h"

namespace esphome {
namespace stair_engine {

class StairEngine : public Component {
 public:
  // ---- code generation
  void set_num_pixels(uint16_t n) { this->geo_.set_num_pixels(n); }
  void add_section(const char *name, uint16_t first_pixel, uint16_t pixels, uint16_t pixels_per_step,
                   bool reversed) {
    this->geo_.add_section(name, first_pixel, pixels, pixels_per_step, reversed);
  }

  // ---- component
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  // ---- sensors
  void set_lower_sensor(bool active) { this->dir_.sensor(stair::End::LOWER, active, millis()); }
  void set_upper_sensor(bool active) { this->dir_.sensor(stair::End::UPPER, active, millis()); }
  void set_ambient_lux(float lux) { this->dir_.set_ambient_lux(lux); }

  // ---- look (r,g,b normalized 0..1 as reported by an RGB light's remote values)
  void set_section_color(uint8_t section, float r, float g, float b, float brightness, bool on);
  void set_master_brightness_pct(float pct) { this->master_ = clamp_(pct / 100.0f, 0.0f, 1.0f); }
  void set_up_effect(size_t index);
  void set_down_effect(size_t index);
  void set_incidental_effect(size_t index);
  void set_speed_pct(float pct) { this->dir_.settings().speed = clamp_(pct / 100.0f, 0.25f, 4.0f); }

  // ---- behavior
  void set_hold_min_s(float s) { this->dir_.settings().hold_min_ms = to_ms_(s); }
  void set_hold_max_s(float s) { this->dir_.settings().hold_max_ms = to_ms_(s); }
  void set_grace_s(float s) { this->dir_.settings().grace_ms = to_ms_(s); }
  void set_grace_level_pct(float pct) { this->dir_.settings().grace_level = clamp_(pct / 100.0f, 0.0f, 1.0f); }
  void set_exit_delay_s(float s) { this->dir_.settings().exit_delay_ms = to_ms_(s); }
  void set_gating(bool on) { this->dir_.settings().gating = on; }
  void set_lux_threshold(float lux) { this->dir_.settings().lux_threshold = lux; }

  // ---- actions
  void simulate_up() { this->simulate_(stair::SimAction::UP); }
  void simulate_down() { this->simulate_(stair::SimAction::DOWN); }
  void simulate_incidental() { this->simulate_(stair::SimAction::INCIDENTAL); }
  void reset_learning();

  // ---- output: call from an addressable_lambda effect
  void render(light::AddressableLight &it);

  // ---- status (template sensors)
  std::string state_text() const { return this->dir_.phase_text(); }
  std::string last_event_text() const { return this->dir_.last_event(); }
  float learned_hold_s(bool up) const {
    return static_cast<float>(this->dir_.hold_ms_for(up ? stair::Dir::UP : stair::Dir::DOWN)) / 1000.0f;
  }
  float learned_samples(bool up) const {
    return static_cast<float>(this->dir_.learned_count(up ? stair::Dir::UP : stair::Dir::DOWN));
  }

 protected:
  static float clamp_(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
  static uint32_t to_ms_(float s) { return s <= 0.0f ? 0u : static_cast<uint32_t>(s * 1000.0f); }
  void simulate_(stair::SimAction a);

  stair::Geometry geo_;
  stair::Director dir_;
  stair::SectionLook looks_[stair::MAX_SECTIONS];
  float master_{1.0f};

  std::vector<float> levels_;
  std::vector<uint8_t> rgb_;

  ESPPreferenceObject pref_;
  uint32_t last_save_ms_{0};
  uint32_t setup_ms_{0};
  uint32_t last_render_ms_{0};
  uint32_t logged_seq_{0};
  bool ready_{false};
  bool size_warned_{false};
  bool render_warned_{false};
};

}  // namespace stair_engine
}  // namespace esphome
