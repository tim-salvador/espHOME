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

// Typewriter: pixels land one at a time in travel order, each with a bright
// tick, settling dim; the whole staircase then rises to full. Exit clicks pixels
// off one by one. Cadence adapts so long strips still finish in about 1.8 s.
class Typewriter final : public Effect {
 public:
  explicit Typewriter(bool on) : on_(on) {}
  const char *name() const override { return "Typewriter"; }
  uint32_t duration(const EffectCtx &c) const override {
    const float n = static_cast<float>(c.geo->num_mapped());
    return static_cast<uint32_t>((n - 1.0f) * per_px(c) + tick(c) + (this->on_ ? ramp(c) : 0.0f));
  }
  uint32_t handoff(const EffectCtx &c) const override {
    return static_cast<uint32_t>(static_cast<float>(c.geo->num_mapped()) * per_px(c));
  }
  void render(const EffectCtx &c, float *out) const override {
    const float t = static_cast<float>(c.t);
    const float all_landed = (static_cast<float>(c.geo->num_mapped()) - 1.0f) * per_px(c) + tick(c);
    const float rise = smooth((t - all_landed) / ramp(c));
    for (uint16_t px = 0; px < n_px(c); px++) {
      if (!c.geo->mapped(px)) {
        out[px] = 0.0f;
        continue;
      }
      const float tp = t - static_cast<float>(c.geo->rank_along(px, c.dir)) * per_px(c);
      float v;
      if (this->on_) {
        v = tp < 0.0f ? c.from[px] : (tp < tick(c) ? 1.0f : 0.7f);
        v = lerp(v, 1.0f, rise);
      } else {
        v = tp < 0.0f ? c.from[px] : (tp < tick(c) ? c.from[px] * 0.35f : 0.0f);
      }
      out[px] = clamp01(v);
    }
  }

 private:
  float per_px(const EffectCtx &c) const {
    const float n = static_cast<float>(c.geo->num_mapped());
    const float base = 1800.0f / (n > 1.0f ? n : 1.0f);
    return ms(base < 28.0f ? base : 28.0f, c.speed);
  }
  float tick(const EffectCtx &c) const { return ms(70.0f, c.speed); }
  float ramp(const EffectCtx &c) const { return ms(300.0f, c.speed); }
  bool on_;
};

// Bloom: steps light in travel order, each opening from its center outward.
// Made for wide steps such as the 9-pixel landing. Exit closes each step from
// its edges inward.
class Bloom final : public Effect {
 public:
  explicit Bloom(bool on) : on_(on) {}
  const char *name() const override { return "Bloom"; }
  uint32_t duration(const EffectCtx &c) const override {
    return static_cast<uint32_t>((n_steps(c) - 1.0f) * gap(c) + open(c) + fade(c));
  }
  uint32_t handoff(const EffectCtx &c) const override { return static_cast<uint32_t>(n_steps(c) * gap(c)); }
  void render(const EffectCtx &c, float *out) const override {
    const float t = static_cast<float>(c.t);
    for (uint16_t px = 0; px < n_px(c); px++) {
      if (!c.geo->mapped(px)) {
        out[px] = 0.0f;
        continue;
      }
      const float pos = c.geo->pos(px);
      const float d = fabsf((pos - floorf(pos)) - 0.5f) * 2.0f;  // 0 at step center, 1 at its edge
      const float k = static_cast<float>(c.geo->step_along(px, c.dir));
      const float start = k * gap(c) + (this->on_ ? d : 1.0f - d) * open(c);
      out[px] = toward(c, px, this->on_, smooth((t - start) / fade(c)));
    }
  }

 private:
  float gap(const EffectCtx &c) const { return ms(110.0f, c.speed); }
  float open(const EffectCtx &c) const { return ms(260.0f, c.speed); }
  float fade(const EffectCtx &c) const { return ms(200.0f, c.speed); }
  bool on_;
};

// Bounce: a soft front races to the far end at partial brightness, bounces,
// and a second front returns to fill everything to full. Exit: the first front
// dims, the return front turns everything off.
class Bounce final : public Effect {
 public:
  explicit Bounce(bool on) : on_(on) {}
  const char *name() const override { return "Bounce"; }
  uint32_t duration(const EffectCtx &c) const override { return static_cast<uint32_t>(2.0f * leg(c)); }
  uint32_t handoff(const EffectCtx &c) const override {
    return static_cast<uint32_t>(leg(c) * n_steps(c) / (n_steps(c) + EDGE));
  }
  void render(const EffectCtx &c, float *out) const override {
    const float t = static_cast<float>(c.t);
    const float n = n_steps(c);
    const float f1 = (t / leg(c)) * (n + EDGE);                 // outbound front
    const float f2 = ((t - leg(c)) / leg(c)) * (n + EDGE);      // return front, measured from the far end
    for (uint16_t px = 0; px < n_px(c); px++) {
      if (!c.geo->mapped(px)) {
        out[px] = 0.0f;
        continue;
      }
      const float a = c.geo->along(px, c.dir);
      const float s1 = smooth((f1 - a) / EDGE);
      const float s2 = t < leg(c) ? 0.0f : smooth((f2 - (n - a)) / EDGE);
      if (this->on_) {
        const float first = lerp(c.from[px], 0.55f, s1);
        out[px] = clamp01(lerp(c.from[px] > first ? c.from[px] : first, 1.0f, s2));
      } else {
        out[px] = clamp01(c.from[px] * lerp(1.0f, 0.45f, s1) * (1.0f - s2));
      }
    }
  }

 private:
  static constexpr float EDGE = 1.5f;
  float leg(const EffectCtx &c) const {
    const float base = n_steps(c) * 60.0f;
    return ms(base < 450.0f ? 450.0f : base, c.speed);
  }
  bool on_;
};

// Sparkle Fill: random pixels pop on with a bright flash and settle, denser
// near you first, until the stairs are full. Exit winks pixels off at random,
// nearest first.
class SparkleFill final : public Effect {
 public:
  explicit SparkleFill(bool on) : on_(on) {}
  const char *name() const override { return "Sparkle Fill"; }
  uint32_t duration(const EffectCtx &c) const override {
    return static_cast<uint32_t>(fill(c) + flash(c) + (this->on_ ? ramp(c) : 0.0f));
  }
  uint32_t handoff(const EffectCtx &c) const override { return static_cast<uint32_t>(fill(c) * 0.6f); }
  void render(const EffectCtx &c, float *out) const override {
    const float t = static_cast<float>(c.t);
    const float n = n_steps(c);
    const float rise = smooth((t - fill(c) - flash(c)) / ramp(c));
    for (uint16_t px = 0; px < n_px(c); px++) {
      if (!c.geo->mapped(px)) {
        out[px] = 0.0f;
        continue;
      }
      const float bias = c.geo->along(px, c.dir) / (n > 0.0f ? n : 1.0f);  // 0 near you, 1 far
      const float start = (0.55f * bias + 0.45f * rand01(c.seed, px, 41)) * fill(c);
      const float tp = t - start;
      float v;
      if (this->on_) {
        v = tp < 0.0f ? c.from[px] : (tp < flash(c) ? 1.0f : 0.75f);
        v = lerp(v, 1.0f, rise);
      } else {
        v = tp < 0.0f ? c.from[px] : c.from[px] * (1.0f - smooth(tp / flash(c)));
      }
      out[px] = clamp01(v);
    }
  }

 private:
  float fill(const EffectCtx &c) const {
    const float base = n_steps(c) * 90.0f;
    return ms(base < 700.0f ? 700.0f : base, c.speed);
  }
  float flash(const EffectCtx &c) const { return ms(80.0f, c.speed); }
  float ramp(const EffectCtx &c) const { return ms(300.0f, c.speed); }
  bool on_;
};

// Curtain: light closes in from both ends and meets in the middle; exit opens a
// dark gap in the middle that spreads to both ends. Ignores direction.
class Curtain final : public Effect {
 public:
  explicit Curtain(bool on) : on_(on) {}
  const char *name() const override { return "Curtain"; }
  uint32_t duration(const EffectCtx &c) const override { return static_cast<uint32_t>(total(c)); }
  uint32_t handoff(const EffectCtx &c) const override { return static_cast<uint32_t>(total(c) * 0.5f); }
  void render(const EffectCtx &c, float *out) const override {
    const float half = n_steps(c) / 2.0f;
    const float front = (static_cast<float>(c.t) / total(c)) * (half + EDGE);
    for (uint16_t px = 0; px < n_px(c); px++) {
      if (!c.geo->mapped(px)) {
        out[px] = 0.0f;
        continue;
      }
      const float p = c.geo->pos(px);
      const float to_end = p < n_steps(c) - p ? p : n_steps(c) - p;  // distance to the nearest end
      const float d = this->on_ ? to_end : half - to_end;              // exit grows from the middle
      out[px] = toward(c, px, this->on_, smooth((front - d) / EDGE));
    }
  }

 private:
  static constexpr float EDGE = 1.5f;
  float total(const EffectCtx &c) const {
    const float base = (n_steps(c) / 2.0f + EDGE) * 120.0f;
    return ms(base < 500.0f ? 500.0f : base, c.speed);
  }
  bool on_;
};

// Lead Glow: a fast, dim front scouts the path ahead, then the full light
// follows. Exit: a fast front dims everything, then a slower one turns it off.
class LeadGlow final : public Effect {
 public:
  explicit LeadGlow(bool on) : on_(on) {}
  const char *name() const override { return "Lead Glow"; }
  uint32_t duration(const EffectCtx &c) const override { return static_cast<uint32_t>(slow(c)); }
  uint32_t handoff(const EffectCtx &c) const override {
    return static_cast<uint32_t>(slow(c) * n_steps(c) / (n_steps(c) + EDGE));
  }
  void render(const EffectCtx &c, float *out) const override {
    const float t = static_cast<float>(c.t);
    const float span = n_steps(c) + EDGE;
    const float ff = (t / fast(c)) * span, fs = (t / slow(c)) * span;
    for (uint16_t px = 0; px < n_px(c); px++) {
      if (!c.geo->mapped(px)) {
        out[px] = 0.0f;
        continue;
      }
      const float a = c.geo->along(px, c.dir);
      const float s1 = smooth((ff - a) / EDGE), s2 = smooth((fs - a) / EDGE);
      if (this->on_) {
        const float scout = lerp(c.from[px], 0.35f, s1);
        out[px] = clamp01(lerp(c.from[px] > scout ? c.from[px] : scout, 1.0f, s2));
      } else {
        out[px] = clamp01(c.from[px] * lerp(1.0f, 0.35f, s1) * (1.0f - s2));
      }
    }
  }

 private:
  static constexpr float EDGE = 2.0f;
  float fast(const EffectCtx &c) const { return ms((n_steps(c) + EDGE) * 40.0f, c.speed); }
  float slow(const EffectCtx &c) const {
    const float base = (n_steps(c) + EDGE) * 105.0f;
    return ms(base < 600.0f ? 600.0f : base, c.speed);
  }
  bool on_;
};

// Shrug (incidental): two quick dim blinks, then a fade out.
class Shrug final : public Effect {
 public:
  const char *name() const override { return "Shrug"; }
  uint32_t duration(const EffectCtx &c) const override { return static_cast<uint32_t>(ms(1500.0f, c.speed)); }
  void render(const EffectCtx &c, float *out) const override {
    const float t = static_cast<float>(c.t);
    const float b = ms(120.0f, c.speed);  // each half-blink
    float k;
    if (t < 4.0f * b) {
      const float phase = fmodf(t, 2.0f * b);
      k = phase < b ? lerp(1.0f, 0.2f, smooth(phase / b)) : lerp(0.2f, 1.0f, smooth((phase - b) / b));
    } else {
      k = 1.0f - smooth((t - 4.0f * b) / (ms(1500.0f, c.speed) - 4.0f * b));
    }
    for (uint16_t px = 0; px < n_px(c); px++) out[px] = c.geo->mapped(px) ? c.from[px] * k : 0.0f;
  }
};

// Ember (incidental): the stairs smoulder with slow, uneven flicker like dying
// coals, each pixel burning out at its own pace.
class Ember final : public Effect {
 public:
  const char *name() const override { return "Ember"; }
  uint32_t duration(const EffectCtx &c) const override { return static_cast<uint32_t>(life(c)); }
  void render(const EffectCtx &c, float *out) const override {
    const float t = static_cast<float>(c.t);
    const float in = smooth(t / ms(300.0f, c.speed));
    const float bucket_ms = ms(180.0f, c.speed);
    const uint32_t b = static_cast<uint32_t>(t / bucket_ms);
    const float frac = smooth((t - static_cast<float>(b) * bucket_ms) / bucket_ms);
    for (uint16_t px = 0; px < n_px(c); px++) {
      if (!c.geo->mapped(px)) {
        out[px] = 0.0f;
        continue;
      }
      // Smooth value noise: blend this bucket's random level into the next one's.
      const float noise = lerp(rand01(c.seed, px, 600 + b), rand01(c.seed, px, 601 + b), frac);
      const float flicker = 0.6f + 0.7f * noise;
      const float own_life = life(c) * (0.6f + 0.4f * rand01(c.seed, px, 42));
      const float env = 1.0f - smooth(t / own_life);
      out[px] = clamp01(c.from[px] * env * lerp(1.0f, flicker, in));
    }
  }

 private:
  float life(const EffectCtx &c) const { return ms(3200.0f, c.speed); }
};

const Glitch GLITCH_ON(true), GLITCH_OFF(false);
const Stagger STAGGER_ON(true), STAGGER_OFF(false);
const Scatter SCATTER_ON(true), SCATTER_OFF(false);
const Peek PEEK;
const Sonar SONAR;
const Typewriter TYPEWRITER_ON(true), TYPEWRITER_OFF(false);
const Bloom BLOOM_ON(true), BLOOM_OFF(false);
const Bounce BOUNCE_ON(true), BOUNCE_OFF(false);
const SparkleFill SPARKLE_ON(true), SPARKLE_OFF(false);
const Curtain CURTAIN_ON(true), CURTAIN_OFF(false);
const LeadGlow LEAD_ON(true), LEAD_OFF(false);
const Shrug SHRUG;
const Ember EMBER;

}  // namespace

void register_custom_effects(EffectRegistry &reg) {
  reg.add_traversal("Glitch", &GLITCH_ON, &GLITCH_OFF);
  reg.add_traversal("Stagger", &STAGGER_ON, &STAGGER_OFF);
  reg.add_traversal("Scatter", &SCATTER_ON, &SCATTER_OFF);
  reg.add_traversal("Typewriter", &TYPEWRITER_ON, &TYPEWRITER_OFF);
  reg.add_traversal("Bloom", &BLOOM_ON, &BLOOM_OFF);
  reg.add_traversal("Bounce", &BOUNCE_ON, &BOUNCE_OFF);
  reg.add_traversal("Sparkle Fill", &SPARKLE_ON, &SPARKLE_OFF);
  reg.add_traversal("Curtain", &CURTAIN_ON, &CURTAIN_OFF);
  reg.add_traversal("Lead Glow", &LEAD_ON, &LEAD_OFF);
  reg.add_incidental("Peek", &PEEK);
  reg.add_incidental("Sonar", &SONAR);
  reg.add_incidental("Shrug", &SHRUG);
  reg.add_incidental("Ember", &EMBER);
}

}  // namespace stair
