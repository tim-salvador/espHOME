// Custom effects. This is the file to edit when adding your own.
//
// Recipe (see README "Add an effect"):
//   1. Write a class deriving from Effect: name(), duration(), render(), and
//      optionally handoff() for smooth relays between sections.
//   2. Register it in register_custom_effects() below. The name you register is
//      exactly what must appear in the YAML select options.
//   3. Run `make` in tools/sim: every registered effect is tested automatically.
//
// Rules every effect must follow (the tests enforce them):
//   * Write a level in [0,1] for EVERY pixel; unmapped pixels get 0.
//   * Start from c.from (what is already lit) so transitions are seamless.
//   * Traversal enter ends at exactly 1.0; exit and incidental end at 0.0.
//   * No state between calls: derive everything from c.t and c.seed.
//   * Scale times with ms(base, c.speed) so the Effect Speed setting applies.
//   * Index pixels globally, but count steps with c.geo (it may be one section
//     of a relay, so num_steps() can be smaller than the whole staircase).

#include "stair_effect_util.h"
#include "stair_effects.h"

namespace stair {

using namespace fx;

namespace {

// Glitch: steps "lock on" along the direction of travel. Before locking, a step
// flickers like a bad signal, denser as the lock approaches; occasionally a
// block of steps tears (flashes) together. Exit is the reverse: stutter, then drop.
class Glitch final : public Effect {
 public:
  explicit Glitch(bool on) : on_(on) {}
  const char *name() const override { return "Glitch"; }
  uint32_t duration(const EffectCtx &c) const override {
    return static_cast<uint32_t>((n_steps(c) - 1.0f) * gap(c) + jitter(c) + win(c) + settle(c));
  }
  uint32_t handoff(const EffectCtx &c) const override { return static_cast<uint32_t>(n_steps(c) * gap(c)); }
  void render(const EffectCtx &c, float *out) const override {
    const float t = static_cast<float>(c.t);
    const float dur = static_cast<float>(this->duration(c));
    const uint32_t bucket = static_cast<uint32_t>(t / ms(40.0f, c.speed));
    const uint32_t steps = c.geo->num_steps();

    // Tear: one block of 3 steps, this bucket only, never near the start or end.
    int tear_from = -1;
    if (t > ms(150.0f, c.speed) && t < dur - ms(150.0f, c.speed) && rand01(c.seed, 0, 9000 + bucket) < 0.08f)
      tear_from = static_cast<int>(rand01(c.seed, 1, 9000 + bucket) * static_cast<float>(steps));

    for (uint16_t px = 0; px < n_px(c); px++) {
      if (!c.geo->mapped(px)) {
        out[px] = 0.0f;
        continue;
      }
      const uint16_t k = c.geo->step_along(px, c.dir);
      const float lock = static_cast<float>(k) * gap(c) + rand01(c.seed, k, 11) * jitter(c) + win(c);
      float v;
      if (t >= lock) {
        const float p = smooth((t - lock) / settle(c));
        v = this->on_ ? lerp(0.7f, 1.0f, p) : lerp(c.from[px] * 0.3f, 0.0f, p);
      } else if (t < lock - win(c)) {
        v = c.from[px];
      } else {
        const float q = (t - (lock - win(c))) / win(c);  // 0..1 approaching the lock
        const bool flip = rand01(c.seed, k, 100 + bucket) < powf(q, 1.3f) &&
                          rand01(c.seed, px, 300 + bucket) >= 0.25f;  // per-pixel dropout texture
        if (this->on_) {
          v = flip ? 0.55f + 0.45f * rand01(c.seed, k, 500 + bucket) : c.from[px];
        } else {
          v = flip ? c.from[px] * 0.1f : c.from[px];
        }
      }
      if (tear_from >= 0 && k >= tear_from && k < tear_from + 3 && t < lock) v = this->on_ ? 1.0f : 0.0f;
      out[px] = clamp01(v);
    }
  }

 private:
  float gap(const EffectCtx &c) const { return ms(110.0f, c.speed); }
  float win(const EffectCtx &c) const { return ms(450.0f, c.speed); }
  float jitter(const EffectCtx &c) const { return ms(60.0f, c.speed); }
  float settle(const EffectCtx &c) const { return ms(60.0f, c.speed); }
  bool on_;
};

// Stagger: every other step lights in sequence (footprints), then a second wave
// fills the gaps. Exit turns off in the same two-wave pattern.
class Stagger final : public Effect {
 public:
  explicit Stagger(bool on) : on_(on) {}
  const char *name() const override { return "Stagger"; }
  uint32_t duration(const EffectCtx &c) const override {
    return static_cast<uint32_t>(2.0f * half(c) * gap(c) + fade(c));
  }
  // Hand off once the first wave reaches the far edge.
  uint32_t handoff(const EffectCtx &c) const override { return static_cast<uint32_t>(half(c) * gap(c)); }
  void render(const EffectCtx &c, float *out) const override {
    const float t = static_cast<float>(c.t);
    for (uint16_t px = 0; px < n_px(c); px++) {
      if (!c.geo->mapped(px)) {
        out[px] = 0.0f;
        continue;
      }
      const uint16_t k = c.geo->step_along(px, c.dir);
      const float start = (k % 2 == 0 ? 0.0f : half(c) * gap(c)) + static_cast<float>(k / 2) * gap(c);
      out[px] = toward(c, px, this->on_, smooth((t - start) / fade(c)));
    }
  }

 private:
  float half(const EffectCtx &c) const { return std::ceil(n_steps(c) / 2.0f); }
  float gap(const EffectCtx &c) const { return ms(120.0f, c.speed); }
  float fade(const EffectCtx &c) const { return ms(300.0f, c.speed); }
  bool on_;
};

// Scatter: steps light out of order with random delays, biased so the light
// still flows in your direction. Pixels within a step sparkle in slightly apart.
class Scatter final : public Effect {
 public:
  explicit Scatter(bool on) : on_(on) {}
  const char *name() const override { return "Scatter"; }
  uint32_t duration(const EffectCtx &c) const override {
    return static_cast<uint32_t>((n_steps(c) - 1.0f) * gap(c) + spread(c) + sparkle(c) + fade(c));
  }
  uint32_t handoff(const EffectCtx &c) const override {
    return static_cast<uint32_t>(n_steps(c) * gap(c) + spread(c) * 0.5f);
  }
  void render(const EffectCtx &c, float *out) const override {
    const float t = static_cast<float>(c.t);
    for (uint16_t px = 0; px < n_px(c); px++) {
      if (!c.geo->mapped(px)) {
        out[px] = 0.0f;
        continue;
      }
      const uint16_t k = c.geo->step_along(px, c.dir);
      const float start = static_cast<float>(k) * gap(c) + rand01(c.seed, k, 21) * spread(c) +
                          rand01(c.seed, px, 22) * sparkle(c);
      out[px] = toward(c, px, this->on_, smooth((t - start) / fade(c)));
    }
  }

 private:
  float gap(const EffectCtx &c) const { return ms(70.0f, c.speed); }
  float spread(const EffectCtx &c) const { return ms(420.0f, c.speed); }  // about 6 steps of shuffle
  float sparkle(const EffectCtx &c) const { return ms(80.0f, c.speed); }
  float fade(const EffectCtx &c) const { return ms(320.0f, c.speed); }
  bool on_;
};

// Peek (incidental): the stairs fall away except a small glow at the trigger
// end, which lingers for a moment and fades. Subtle; good for pets.
class Peek final : public Effect {
 public:
  const char *name() const override { return "Peek"; }
  uint32_t duration(const EffectCtx &c) const override { return static_cast<uint32_t>(c_end(c)); }
  void render(const EffectCtx &c, float *out) const override {
    const float t = static_cast<float>(c.t);
    const float a = a_end(c), b = b_end(c), e = c_end(c);
    for (uint16_t px = 0; px < n_px(c); px++) {
      if (!c.geo->mapped(px)) {
        out[px] = 0.0f;
        continue;
      }
      // 1 within ~2 steps of the trigger end, soft edge out to ~3.5 steps.
      const float w = 1.0f - smooth((c.geo->pos_from(px, c.origin) - 2.0f) / 1.5f);
      float v;
      if (t < a) {
        const float s = smooth(t / a);
        v = lerp(c.from[px] * (1.0f - s), lerp(c.from[px], PEAK, s), w);
      } else if (t < b) {
        v = w * PEAK * (0.9f + 0.1f * cosf(kTwoPi * 2.0f * (t - a) / (b - a)));  // two soft pulses
      } else {
        v = w * PEAK * (1.0f - smooth((t - b) / (e - b)));
      }
      out[px] = clamp01(v);
    }
  }

 private:
  static constexpr float PEAK = 0.85f;
  float a_end(const EffectCtx &c) const { return ms(600.0f, c.speed); }
  float b_end(const EffectCtx &c) const { return ms(1500.0f, c.speed); }
  float c_end(const EffectCtx &c) const { return ms(2400.0f, c.speed); }
};

// Sonar (incidental): the stairs fade while one soft pulse travels from the
// trigger end to the far end, weakening as it goes, and is gone.
class Sonar final : public Effect {
 public:
  const char *name() const override { return "Sonar"; }
  uint32_t duration(const EffectCtx &c) const override {
    return static_cast<uint32_t>((n_steps(c) + 3.0f) * per_step(c));
  }
  void render(const EffectCtx &c, float *out) const override {
    const float t = static_cast<float>(c.t);
    const float n = n_steps(c);
    const float pos = t / per_step(c);
    float amp = smooth(t / ms(150.0f, c.speed)) * (1.0f - pos / (n + 2.0f));
    if (amp < 0.0f) amp = 0.0f;
    const float base_k = 1.0f - smooth(t / ms(350.0f, c.speed));
    for (uint16_t px = 0; px < n_px(c); px++) {
      if (!c.geo->mapped(px)) {
        out[px] = 0.0f;
        continue;
      }
      const float d = c.geo->pos_from(px, c.origin) - pos;
      const float pulse = amp * expf(-(d * d) / (2.0f * 0.8f * 0.8f));
      const float base = c.from[px] * base_k;
      out[px] = clamp01(base > pulse ? base : pulse);
    }
  }

 private:
  float per_step(const EffectCtx &c) const { return ms(110.0f, c.speed); }
};

const Glitch GLITCH_ON(true), GLITCH_OFF(false);
const Stagger STAGGER_ON(true), STAGGER_OFF(false);
const Scatter SCATTER_ON(true), SCATTER_OFF(false);
const Peek PEEK;
const Sonar SONAR;

}  // namespace

void register_custom_effects(EffectRegistry &reg) {
  reg.add_traversal("Glitch", &GLITCH_ON, &GLITCH_OFF);
  reg.add_traversal("Stagger", &STAGGER_ON, &STAGGER_OFF);
  reg.add_traversal("Scatter", &SCATTER_ON, &SCATTER_OFF);
  reg.add_incidental("Peek", &PEEK);
  reg.add_incidental("Sonar", &SONAR);
}

}  // namespace stair
