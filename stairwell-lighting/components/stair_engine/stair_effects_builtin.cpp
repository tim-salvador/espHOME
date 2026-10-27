// Built-in effects and the effect registry.
// Add your own effects in stair_effects_custom.cpp, not here.

#include <cstring>

#include "stair_effect_util.h"
#include "stair_effects.h"

namespace stair {

using namespace fx;

// ------------------------------------------------------------------ registry

void EffectRegistry::add_traversal(const char *name, const Effect *enter, const Effect *exit) {
  this->entries_.push_back({name, EffectKind::TRAVERSAL, enter, exit});
}

void EffectRegistry::add_incidental(const char *name, const Effect *effect) {
  this->entries_.push_back({name, EffectKind::INCIDENTAL, effect, nullptr});
}

void EffectRegistry::add_ambient(const char *name, const Effect *effect) {
  this->entries_.push_back({name, EffectKind::AMBIENT, effect, nullptr});
}

int EffectRegistry::find(EffectKind kind, const char *name, size_t len) const {
  if (name == nullptr) return -1;
  for (size_t i = 0; i < this->entries_.size(); i++) {
    const EffectEntry &e = this->entries_[i];
    if (e.kind == kind && strlen(e.name) == len && strncmp(e.name, name, len) == 0) return static_cast<int>(i);
  }
  return -1;
}

int EffectRegistry::find(EffectKind kind, const char *name) const {
  return name == nullptr ? -1 : this->find(kind, name, strlen(name));
}

int EffectRegistry::first(EffectKind kind) const {
  for (size_t i = 0; i < this->entries_.size(); i++)
    if (this->entries_[i].kind == kind) return static_cast<int>(i);
  return -1;
}

const EffectRegistry &effects() {
  static EffectRegistry reg = [] {
    EffectRegistry r;
    register_builtin_effects(r);
    register_ambient_effects(r);
    register_custom_effects(r);
    return r;
  }();
  return reg;
}

namespace {

// ---------------------------------------------------------------- traversal

// Steps light one after another along the direction of travel, each with a soft fade.
class Cascade final : public Effect {
 public:
  explicit Cascade(bool on) : on_(on) {}
  const char *name() const override { return "Cascade"; }
  uint32_t duration(const EffectCtx &c) const override {
    return static_cast<uint32_t>((n_steps(c) - 1.0f) * interval(c) + fade(c));
  }
  uint32_t handoff(const EffectCtx &c) const override { return static_cast<uint32_t>(n_steps(c) * interval(c)); }
  void render(const EffectCtx &c, float *out) const override {
    const float t = static_cast<float>(c.t);
    for (uint16_t px = 0; px < n_px(c); px++) {
      if (!c.geo->mapped(px)) {
        out[px] = 0.0f;
        continue;
      }
      const float k = static_cast<float>(c.geo->step_along(px, c.dir));
      out[px] = toward(c, px, this->on_, smooth((t - k * interval(c)) / fade(c)));
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
  uint32_t handoff(const EffectCtx &c) const override {
    return static_cast<uint32_t>(total(c) * n_steps(c) / (n_steps(c) + EDGE));
  }
  void render(const EffectCtx &c, float *out) const override {
    const float front = (static_cast<float>(c.t) / total(c)) * (n_steps(c) + EDGE);
    for (uint16_t px = 0; px < n_px(c); px++) {
      if (!c.geo->mapped(px)) {
        out[px] = 0.0f;
        continue;
      }
      out[px] = toward(c, px, this->on_, smooth((front - c.geo->along(px, c.dir)) / EDGE));
    }
  }

 private:
  static constexpr float EDGE = 2.0f;  // steps
  float total(const EffectCtx &c) const {
    // Speed is per step, with a floor so short sections (a relayed landing) do not flash past.
    float base = n_steps(c) * 90.0f + EDGE * 90.0f;
    return ms(base < 450.0f ? 450.0f : base, c.speed);
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
  uint32_t handoff(const EffectCtx &c) const override { return static_cast<uint32_t>(n_steps(c) * head(c)); }
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

// Whole range fades together.
class Fade final : public Effect {
 public:
  explicit Fade(bool on) : on_(on) {}
  const char *name() const override { return "Fade"; }
  uint32_t duration(const EffectCtx &c) const override { return static_cast<uint32_t>(ms(800.0f, c.speed)); }
  uint32_t handoff(const EffectCtx &c) const override { return this->duration(c) / 2; }
  void render(const EffectCtx &c, float *out) const override {
    const float p = smooth(static_cast<float>(c.t) / ms(800.0f, c.speed));
    for (uint16_t px = 0; px < n_px(c); px++) out[px] = c.geo->mapped(px) ? toward(c, px, this->on_, p) : 0.0f;
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
        out[px] = c.from[px] * (0.82f + 0.18f * cosf(kTwoPi * 2.0f * t / glance(c)));
        continue;
      }
      const float front = (n + EDGE) - ((t - glance(c)) / travel(c)) * (n + 2.0f * EDGE);
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
      const float tw = 0.55f + 0.45f * (0.5f + 0.5f * sinf(kTwoPi * (t / period + rand01(c.seed, px, 3))));
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
      const float d = front - c.geo->pos_from(px, c.origin);
      out[px] = d <= 0.0f ? c.from[px] : c.from[px] * expf(-d / 1.6f) * (0.5f + 0.5f * cosf(2.4f * d));
    }
  }

 private:
  static constexpr float TAIL = 7.0f;
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
    for (uint16_t px = 0; px < n_px(c); px++)
      out[px] = c.geo->section_of(px) == target ? lerp(c.from[px], 1.0f, p) : c.from[px] * (1.0f - p);
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

void register_builtin_effects(EffectRegistry &reg) {
  // The first TRAVERSAL and first INCIDENTAL entries are the defaults.
  reg.add_traversal("Cascade", &CASCADE_ON, &CASCADE_OFF);
  reg.add_traversal("Soft Wipe", &WIPE_ON, &WIPE_OFF);
  reg.add_traversal("Comet", &COMET_ON, &COMET_OFF);
  reg.add_traversal("Fade", &FADE_ON, &FADE_OFF);
  reg.add_incidental("Retreat", &RETREAT);
  reg.add_incidental("Firefly", &FIREFLY);
  reg.add_incidental("Ripple", &RIPPLE);
  reg.add_incidental("Breath", &BREATH);
}

const Effect &restore_effect() { return RESTORE; }
const Effect &grace_effect() { return GRACE; }
const Effect &preview_in_effect() { return PREVIEW_IN; }
const Effect &fade_out_effect() { return FADE_OUT; }

}  // namespace stair
