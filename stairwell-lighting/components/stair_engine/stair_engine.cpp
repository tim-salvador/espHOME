#include "stair_engine.h"

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome {
namespace stair_engine {

static const char *const TAG = "stair_engine";

// Preview is suppressed this long after setup so restored colors do not light the stairs at boot.
static constexpr uint32_t PREVIEW_BOOT_HOLDOFF_MS = 5000;
// Learning is persisted at most this often (NVS commits are batched by ESPHome as well).
static constexpr uint32_t SAVE_INTERVAL_MS = 60000;
// If the strip effect stops calling render() while lit, something turned the strip off.
static constexpr uint32_t RENDER_STALL_MS = 2000;

void StairEngine::setup() {
  if (!this->geo_.finalize()) {
    ESP_LOGE(TAG, "Invalid stair layout: %s", this->geo_.error());
    this->mark_failed();
    return;
  }
  this->dir_.attach(&this->geo_);
  this->levels_.assign(this->geo_.num_pixels(), 0.0f);
  this->rgb_.assign(static_cast<size_t>(this->geo_.num_pixels()) * 3, 0);

  this->pref_ = global_preferences->make_preference<stair::LearnBlob>(fnv1_hash("stair_engine_learning_v1"), true);
  stair::LearnBlob blob{};
  if (this->pref_.load(&blob)) {
    this->dir_.load_learning(blob);
    ESP_LOGI(TAG, "Restored learning: up=%u down=%u samples", this->dir_.learned_count(stair::Dir::UP),
             this->dir_.learned_count(stair::Dir::DOWN));
  }

  const uint32_t now = millis();
  this->setup_ms_ = now;
  this->last_render_ms_ = now;
  this->dir_.begin(now);
  this->ready_ = true;
}

void StairEngine::loop() {
  if (!this->ready_) return;
  const uint32_t now = millis();
  this->dir_.tick(now);

  if (this->dir_.event_seq() != this->logged_seq_) {
    this->logged_seq_ = this->dir_.event_seq();
    ESP_LOGD(TAG, "[%s] %s", this->dir_.phase_text(), this->dir_.last_event());
  }

  if (this->dir_.learning_dirty() && (now - this->last_save_ms_) >= SAVE_INTERVAL_MS) {
    const stair::LearnBlob &b = this->dir_.learning();
    if (this->pref_.save(&b)) {
      this->dir_.clear_learning_dirty();
      this->last_save_ms_ = now;
      ESP_LOGD(TAG, "Learning saved");
    }
  }

  const bool lit = this->dir_.phase() != stair::Phase::IDLE;
  if (lit && (now - this->last_render_ms_) > RENDER_STALL_MS) {
    if (!this->render_warned_) {
      ESP_LOGW(TAG, "Lit but no frames rendered for %ums: is the strip's \"Engine\" effect running?",
               static_cast<unsigned>(now - this->last_render_ms_));
      this->render_warned_ = true;
    }
  } else {
    this->render_warned_ = false;
  }
}

void StairEngine::render(light::AddressableLight &it) {
  const uint32_t now = millis();
  this->last_render_ms_ = now;
  const int32_t size = it.size();

  if (!this->ready_) {
    for (int32_t i = 0; i < size; i++) it[i] = Color::BLACK;
    return;
  }
  if (size != static_cast<int32_t>(this->geo_.num_pixels()) && !this->size_warned_) {
    ESP_LOGW(TAG, "Strip has %d LEDs but stair_engine num_pixels is %u; extra pixels stay dark",
             static_cast<int>(size), this->geo_.num_pixels());
    this->size_warned_ = true;
  }

  this->dir_.render(now, this->levels_.data());
  stair::compose(this->geo_, this->levels_.data(), this->looks_, this->master_, this->rgb_.data());

  const int32_t n = static_cast<int32_t>(this->geo_.num_pixels());
  for (int32_t i = 0; i < size; i++) {
    if (i < n) {
      const uint8_t *p = &this->rgb_[static_cast<size_t>(i) * 3];
      it[i] = Color(p[0], p[1], p[2]);
    } else {
      it[i] = Color::BLACK;
    }
  }
}

void StairEngine::set_section_color(uint8_t section, float r, float g, float b, float brightness, bool on) {
  if (section >= stair::MAX_SECTIONS) return;
  stair::SectionLook &l = this->looks_[section];
  const bool changed = l.r != r || l.g != g || l.b != b || l.brightness != brightness || l.enabled != on;
  l.r = r;
  l.g = g;
  l.b = b;
  l.brightness = brightness;
  l.enabled = on;
  if (!changed || !this->ready_ || section >= this->geo_.num_sections()) return;
  if ((millis() - this->setup_ms_) < PREVIEW_BOOT_HOLDOFF_MS) return;
  this->dir_.preview(section, millis());
}

void StairEngine::set_up_effect(size_t index) {
  if (index < static_cast<size_t>(stair::Style::COUNT)) this->dir_.settings().style_up = static_cast<stair::Style>(index);
}

void StairEngine::set_down_effect(size_t index) {
  if (index < static_cast<size_t>(stair::Style::COUNT))
    this->dir_.settings().style_down = static_cast<stair::Style>(index);
}

void StairEngine::set_incidental_effect(size_t index) {
  if (index < static_cast<size_t>(stair::IncidentalStyle::COUNT))
    this->dir_.settings().incidental = static_cast<stair::IncidentalStyle>(index);
}

void StairEngine::simulate_(stair::SimAction a) {
  if (!this->ready_) return;
  if (!this->dir_.simulate(a, millis())) ESP_LOGW(TAG, "Simulation refused: stairs are in use");
}

void StairEngine::reset_learning() {
  this->dir_.reset_learning();
  this->last_save_ms_ = millis() - SAVE_INTERVAL_MS;  // persist on the next loop
}

void StairEngine::dump_config() {
  ESP_LOGCONFIG(TAG, "Stair Engine:");
  if (!this->geo_.valid()) {
    ESP_LOGCONFIG(TAG, "  Layout INVALID: %s", this->geo_.error());
    return;
  }
  ESP_LOGCONFIG(TAG, "  Pixels: %u  Steps: %u  Sections: %u", this->geo_.num_pixels(), this->geo_.num_steps(),
                this->geo_.num_sections());
  for (uint8_t i = 0; i < this->geo_.num_sections(); i++) {
    const stair::SectionDef &s = this->geo_.section(i);
    ESP_LOGCONFIG(TAG, "  [%u] %-14s px %3u-%3u  %2u px/step  steps %2u-%2u%s", i, s.name, s.first_pixel,
                  s.first_pixel + s.pixels - 1, s.pixels_per_step, s.first_step, s.first_step + s.steps - 1,
                  s.reversed ? "  reversed" : "");
  }
  ESP_LOGCONFIG(TAG, "  Learned samples: up=%u down=%u", this->dir_.learned_count(stair::Dir::UP),
                this->dir_.learned_count(stair::Dir::DOWN));
}

}  // namespace stair_engine
}  // namespace esphome
