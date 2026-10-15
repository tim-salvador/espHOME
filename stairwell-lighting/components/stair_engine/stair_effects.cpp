#include "stair_effects.h"

#include <cmath>

namespace stair {

namespace {

constexpr float TWO_PI = 6.28318530718f;

inline float clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
inline float smooth(float x) {
  x = clamp01(x);
  return x * x * (3.0f - 2.0f * x);
}
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
inline float ms(float base, float speed) { return base / (speed > 0.05f ? speed : 0.05f); }

// Deterministic per-pixel random in [0,1).
inline float rand01(uint32_t seed, uint32_t px, uint32_t salt) {
  uint32_t h = seed ^ (px * 0x9E3779B1u) ^ (salt * 0x85EBCA77u);
  h ^= h >> 16;
  h *= 0x7FEB352Du;
  h ^= h >> 15;
  h *= 0x846CA68Bu;
  h ^= h >> 16;
  return static_cast<float>(h & 0x00FFFFFFu) / 16777216.0f;
}

inline uint16_t n_px(const EffectCtx &c) { return c.geo->num_pixels(); }
inline float n_steps(const EffectCtx &c) { return static_cast<float>(c.geo->num_steps()); }

// ---------------------------------------------------------------- traversal

// Steps light one after another along the direction of travel, each with a soft fade.
class Cascade final : public Effect {
 public:
  explicit Cascade(bool on) : on_(on) {}
  const char *name() const override { return "Cascade"; }
  uint32_t duration(const EffectCtx &c) const override {
    return static_cast<uint32_t>((n_steps(c) - 1.0f) * interval(c) + fade(c));
  }
  void render(const EffectCtx &c, float *out) const override {
    const float t = static_cast<float>(c.t);
    for (uint16_t px = 0; px < n_px(c); px++) {
      if (!c.geo->mapped(px)) {
        out[px] = 0.0f;
        continue;
      }
      const float k = static_cast<float>(c.geo->step_along(px, c.dir));
      const float p = smooth((t - k * interval(c)) / fade(c));
      out[px] = this->on_ ? lerp(c.from[px], 1.0f, p) : c.from[px] * (1.0f - p);
    }
  }

 private:
  float interval(const EffectCtx &c) const { return ms(110.0f, c.speed); }
  float fade(const EffectCtx &c) const { return ms(350.0f, c.speed); }
  bool on_;
};

// A soft-edged front sweeps continuously along the direction of travel.
class SoftWipe final : public Effect {
 public:
  explicit SoftWipe(bool on) : on_(on) {}
  const char *name() const override { return "Soft Wipe"; }
  uint32_t duration(const EffectCtx &c) const override { return static_cast<uint32_t>(total(c)); }
  void render(const EffectCtx &c, float *out) const override {
    const float n = n_steps(c);
    const float front = (static_cast<float>(c.t) / total(c)) * (n + EDGE);
    for (uint16_t px = 0; px < n_px(c); px++) {
      if (!c.geo->mapped(px)) {
        out[px] = 0.0f;
        continue;
      }
      const float p = smooth((front - c.geo->along(px, c.dir)) / EDGE);
      out[px] = this->on_ ? lerp(c.from[px], 1.0f, p) : c.from[px] * (1.0f - p);
    }
  }

 private:
  static constexpr float EDGE = 2.0f;  // steps
  float total(const EffectCtx &c) const {
    float base = n_steps(c) * 90.0f;
    return ms(base < 600.0f ? 600.0f : base, c.speed);
  }
  bool on_;
};

// Enter: a bright head runs ahead, the trail settles from dim to full.
// Exit: a fast fade follows the head along the direction of travel.
class Comet final : public Effect {
 public:
  explicit Comet(bool on) : on_(on) {}
  const char *name() const override { return "Comet"; }
  uint32_t duration(const EffectCtx &c) const override {
    return static_cast<uint32_t>(n_steps(c) * head(c) + (this->on_ ? flash(c) + settle(c) : tail(c)));
  }
  void render(const EffectCtx &c, float *out) const override {
    const float t = static_cast<float>(c.t);
    for (uint16_t px = 0; px < n_px(c); px++) {
      if (!c.geo->mapped(px)) {
        out[px] = 0.0f;
        continue;
      }
      const float tp = t - c.geo->along(px, c.dir) * head(c);  // ms since the head passed this pixel
      if (this->on_) {
        float v;
        if (tp < 0.0f) {
          v = 0.0f;
        } else if (tp < flash(c)) {
          v = 1.0f;
        } else {
          v = 0.3f + 0.7f * smooth((tp - flash(c)) / settle(c));
        }
        out[px] = c.from[px] > v ? c.from[px] : v;
      } else {
        out[px] = tp < 0.0f ? c.from[px] : c.from[px] * (1.0f - smooth(tp / tail(c)));
      }
    }
  }

 private:
  float head(const EffectCtx &c) const { return ms(70.0f, c.speed); }
  float flash(const EffectCtx &c) const { return ms(90.0f, c.speed); }
  float settle(const EffectCtx &c) const { return ms(700.0f, c.speed); }
  float tail(const EffectCtx &c) const { return ms(300.0f, c.speed); }
  bool on_;
};

// Whole staircase fades together.
class Fade final : public Effect {
 public:
  explicit Fade(bool on) : on_(on) {}
  const char *name() const override { return "Fade"; }
  uint32_t duration(const EffectCtx &c) const override { return static_cast<uint32_t>(ms(800.0f, c.speed)); }
  void render(const EffectCtx &c, float *out) const override {
    const float p = smooth(static_cast<float>(c.t) / ms(800.0f, c.speed));
    for (uint16_t px = 0; px < n_px(c); px++) {
      if (!c.geo->mapped(px)) {
        out[px] = 0.0f;
        continue;
      }
      out[px] = this->on_ ? lerp(c.from[px], 1.0f, p) : c.from[px] * (1.0f - p);
    }
  }

 private:
  bool on_;
};

// --------------------------------------------------------------- incidental

// "Curious glance": a brief shimmer, then the light withdraws toward the end
// where the trigger came from and disappears.
class Retreat final : public Effect {
 public:
  const char *name() const override { return "Retreat"; }
  uint32_t duration(const EffectCtx &c) const override { return static_cast<uint32_t>(glance(c) + travel(c)); }
  void render(const EffectCtx &c, float *out) const override {
    const float t = static_cast<float>(c.t);
    const float n = n_steps(c);
    for (uint16_t px = 0; px < n_px(c); px++) {
      if (!c.geo->mapped(px)) {
        out[px] = 0.0f;
        continue;
      }
      if (t < glance(c)) {
        // Two soft dips: "looking around". cos(0)=1 so it starts exactly at the captured level.
        const float w = 0.82f + 0.18f * cosf(TWO_PI * 2.0f * t / glance(c));
        out[px] = c.from[px] * w;
        continue;
      }
      const float tt = t - glance(c);
      const float front = (n + EDGE) - (tt / travel(c)) * (n + 2.0f * EDGE);
      out[px] = c.from[px] * smooth((front - c.geo->pos_from(px, c.origin)) / EDGE);
    }
  }

 private:
  static constexpr float EDGE = 1.5f;
  float glance(const EffectCtx &c) const { return ms(500.0f, c.speed); }
  float travel(const EffectCtx &c) const {
    float base = n_steps(c) * 80.0f;
    return ms(base < 900.0f ? 900.0f : base, c.speed);
  }
};

// Pixels drift into a twinkle and wink out one by one, like fireflies.
class Firefly final : public Effect {
 public:
  const char *name() const override { return "Firefly"; }
  uint32_t duration(const EffectCtx &c) const override { return static_cast<uint32_t>(total(c)); }
  void render(const EffectCtx &c, float *out) const override {
    const float t = static_cast<float>(c.t);
    const float tw_in = smooth(t / ms(300.0f, c.speed));
    for (uint16_t px = 0; px < n_px(c); px++) {
      if (!c.geo->mapped(px)) {
        out[px] = 0.0f;
        continue;
      }
      const float t_off = rand01(c.seed, px, 1) * total(c) * 0.8f;
      const float period = ms(180.0f + 260.0f * rand01(c.seed, px, 2), c.speed);
      const float phase = rand01(c.seed, px, 3);
      const float tw = 0.55f + 0.45f * (0.5f + 0.5f * sinf(TWO_PI * (t / period + phase)));
      const float env = t < t_off ? 1.0f : 1.0f - smooth((t - t_off) / ms(260.0f, c.speed));
      out[px] = c.from[px] * env * (1.0f - tw_in * (1.0f - tw));
    }
  }

 private:
  float total(const EffectCtx &c) const { return ms(2400.0f, c.speed); }
};

// A ripple rolls away from the trigger end, leaving decaying waves that fade out.
class Ripple final : public Effect {
 public:
  const char *name() const override { return "Ripple"; }
  uint32_t duration(const EffectCtx &c) const override {
    return static_cast<uint32_t>((n_steps(c) + TAIL) * per_step(c));
  }
  void render(const EffectCtx &c, float *out) const override {
    const float front = static_cast<float>(c.t) / per_step(c);
    for (uint16_t px = 0; px < n_px(c); px++) {
      if (!c.geo->mapped(px)) {
        out[px] = 0.0f;
        continue;
      }
      const float d = front - c.geo->pos_from(px, c.origin);  // steps behind the front
      if (d <= 0.0f) {
        out[px] = c.from[px];
      } else {
        out[px] = c.from[px] * expf(-d / 1.6f) * (0.5f + 0.5f * cosf(2.4f * d));
      }
    }
  }

 private:
  static constexpr float TAIL = 7.0f;  // steps of decay after the far end
  float per_step(const EffectCtx &c) const { return ms(95.0f, c.speed); }
};

// The whole staircase exhales once, takes a small breath, and fades away.
class Breath final : public Effect {
 public:
  const char *name() const override { return "Breath"; }
  uint32_t duration(const EffectCtx &c) const override { return static_cast<uint32_t>(ms(2900.0f, c.speed)); }
  void render(const EffectCtx &c, float *out) const override {
    const float t = static_cast<float>(c.t);
    const float a = ms(650.0f, c.speed), b = ms(1350.0f, c.speed), e = ms(2900.0f, c.speed);
    float k;
    if (t < a) {
      k = lerp(1.0f, 0.4f, smooth(t / a));
    } else if (t < b) {
      k = lerp(0.4f, 0.85f, smooth((t - a) / (b - a)));
    } else {
      k = lerp(0.85f, 0.0f, smooth((t - b) / (e - b)));
    }
    for (uint16_t px = 0; px < n_px(c); px++) out[px] = c.geo->mapped(px) ? c.from[px] * k : 0.0f;
  }
};

// ------------------------------------------------------------------ utility

class Restore final : public Effect {
 public:
  const char *name() const override { return "Restore"; }
  uint32_t duration(const EffectCtx &) const override { return 350; }
  void render(const EffectCtx &c, float *out) const override {
    const float p = smooth(static_cast<float>(c.t) / 350.0f);
    for (uint16_t px = 0; px < n_px(c); px++) out[px] = c.geo->mapped(px) ? lerp(c.from[px], 1.0f, p) : 0.0f;
  }
};

class Grace final : public Effect {
 public:
  const char *name() const override { return "Grace"; }
  uint32_t duration(const EffectCtx &) const override { return 900; }
  void render(const EffectCtx &c, float *out) const override {
    const float p = smooth(static_cast<float>(c.t) / 900.0f);
    for (uint16_t px = 0; px < n_px(c); px++)
      out[px] = c.geo->mapped(px) ? c.from[px] * lerp(1.0f, clamp01(c.param), p) : 0.0f;
  }
};

class PreviewIn final : public Effect {
 public:
  const char *name() const override { return "Preview"; }
  uint32_t duration(const EffectCtx &) const override { return 200; }
  void render(const EffectCtx &c, float *out) const override {
    const float p = smooth(static_cast<float>(c.t) / 200.0f);
    const uint8_t target = static_cast<uint8_t>(c.param);
    for (uint16_t px = 0; px < n_px(c); px++) {
      out[px] = c.geo->section_of(px) == target ? lerp(c.from[px], 1.0f, p) : c.from[px] * (1.0f - p);
    }
  }
};

class FadeOut final : public Effect {
 public:
  const char *name() const override { return "Fade Out"; }
  uint32_t duration(const EffectCtx &) const override { return 600; }
  void render(const EffectCtx &c, float *out) const override {
    const float p = smooth(static_cast<float>(c.t) / 600.0f);
    for (uint16_t px = 0; px < n_px(c); px++) out[px] = c.from[px] * (1.0f - p);
  }
};

const Cascade CASCADE_ON(true), CASCADE_OFF(false);
const SoftWipe WIPE_ON(true), WIPE_OFF(false);
const Comet COMET_ON(true), COMET_OFF(false);
const Fade FADE_ON(true), FADE_OFF(false);
const Retreat RETREAT;
const Firefly FIREFLY;
const Ripple RIPPLE;
const Breath BREATH;
const Restore RESTORE;
const Grace GRACE;
const PreviewIn PREVIEW_IN;
const FadeOut FADE_OUT;

}  // namespace

const Effect &enter_effect(Style s) {
  switch (s) {
    case Style::SOFT_WIPE:
      return WIPE_ON;
    case Style::COMET:
      return COMET_ON;
    case Style::FADE:
      return FADE_ON;
    case Style::CASCADE:
    default:
      return CASCADE_ON;
  }
}

const Effect &exit_effect(Style s) {
  switch (s) {
    case Style::SOFT_WIPE:
      return WIPE_OFF;
    case Style::COMET:
      return COMET_OFF;
    case Style::FADE:
      return FADE_OFF;
    case Style::CASCADE:
    default:
      return CASCADE_OFF;
  }
}

const Effect &incidental_effect(IncidentalStyle s) {
  switch (s) {
    case IncidentalStyle::FIREFLY:
      return FIREFLY;
    case IncidentalStyle::RIPPLE:
      return RIPPLE;
    case IncidentalStyle::BREATH:
      return BREATH;
    case IncidentalStyle::RETREAT:
    default:
      return RETREAT;
  }
}

const Effect &restore_effect() { return RESTORE; }
const Effect &grace_effect() { return GRACE; }
const Effect &preview_in_effect() { return PREVIEW_IN; }
const Effect &fade_out_effect() { return FADE_OUT; }

const char *style_name(Style s) { return enter_effect(s).name(); }
const char *incidental_name(IncidentalStyle s) { return incidental_effect(s).name(); }

}  // namespace stair
