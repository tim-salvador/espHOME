// Ambient effects: what the stairs show while idle when Ambient Mode is on.
//
// Ambient effects differ from the others:
//   * They never end. The engine passes t modulo period(), so the motion must
//     repeat exactly every period() ms to loop seamlessly.
//   * They output a pattern in [0,1]; the engine scales it by Ambient Level and
//     fades it in and out. Design them at full scale.
//   * They do not start from c.from: there is nothing to continue from. Any
//     transition into or out of ambient is handled by the engine.
//
// Randomness that must loop uses time buckets taken modulo (period / bucket),
// so the last bucket blends back into the first.

#include "stair_effect_util.h"
#include "stair_effects.h"

namespace stair {

using namespace fx;

namespace {

constexpr uint32_t LONG_PERIOD = 600000;  // 10 min loop for random effects

// Looping value noise: smooth blend between per-bucket random values.
inline float loop_noise(uint32_t seed, uint32_t index, uint32_t salt, float t, float bucket_ms, uint32_t buckets) {
  const uint32_t b = static_cast<uint32_t>(t / bucket_ms);
  const float f = smooth((t - static_cast<float>(b) * bucket_ms) / bucket_ms);
  const float r0 = rand01(seed, index, salt + (b % buckets));
  const float r1 = rand01(seed, index, salt + ((b + 1) % buckets));
  return lerp(r0, r1, f);
}

// Glow: a steady, even night light.
class Glow final : public Effect {
 public:
  const char *name() const override { return "Glow"; }
  uint32_t duration(const EffectCtx &) const override { return 0; }
  uint32_t period() const override { return 1000; }
  void render(const EffectCtx &c, float *out) const override {
    for (uint16_t px = 0; px < n_px(c); px++) out[px] = c.geo->mapped(px) ? 1.0f : 0.0f;
  }
};

// Guide: only the first and last step of every section glow, marking the edges.
class Guide final : public Effect {
 public:
  const char *name() const override { return "Guide"; }
  uint32_t duration(const EffectCtx &) const override { return 0; }
  uint32_t period() const override { return 1000; }
  void render(const EffectCtx &c, float *out) const override {
    for (uint16_t px = 0; px < n_px(c); px++) {
      if (!c.geo->mapped(px)) {
        out[px] = 0.0f;
        continue;
      }
      const SectionDef &s = c.geo->section(c.geo->section_of(px));
      const uint16_t st = c.geo->step(px);
      out[px] = (st == s.first_step || st == s.first_step + s.steps - 1) ? 1.0f : 0.0f;
    }
  }
};

// Landing Beacon: only the middle section(s) glow (the landing on this install).
// With one or two sections, everything glows.
class LandingBeacon final : public Effect {
 public:
  const char *name() const override { return "Landing Beacon"; }
  uint32_t duration(const EffectCtx &) const override { return 0; }
  uint32_t period() const override { return 1000; }
  void render(const EffectCtx &c, float *out) const override {
    const uint8_t n = c.geo->num_sections();
    for (uint16_t px = 0; px < n_px(c); px++) {
      if (!c.geo->mapped(px)) {
        out[px] = 0.0f;
        continue;
      }
      const uint8_t s = c.geo->section_of(px);
      out[px] = (n <= 2 || (s > 0 && s + 1 < n)) ? 1.0f : 0.0f;
    }
  }
};

// Breathe: a very slow rise and fall, never fully dark.
class Breathe final : public Effect {
 public:
  const char *name() const override { return "Breathe"; }
  uint32_t duration(const EffectCtx &) const override { return 0; }
  uint32_t period() const override { return 7000; }
  void render(const EffectCtx &c, float *out) const override {
    const float k = 0.45f + 0.55f * (0.5f - 0.5f * cosf(kTwoPi * static_cast<float>(c.t) / 7000.0f));
    for (uint16_t px = 0; px < n_px(c); px++) out[px] = c.geo->mapped(px) ? k : 0.0f;
  }
};

// Tide: a soft swell drifts slowly from the bottom to the top and back.
class Tide final : public Effect {
 public:
  const char *name() const override { return "Tide"; }
  uint32_t duration(const EffectCtx &) const override { return 0; }
  uint32_t period() const override { return 16000; }
  void render(const EffectCtx &c, float *out) const override {
    const float n = n_steps(c);
    const float crest = n * (0.5f - 0.5f * cosf(kTwoPi * static_cast<float>(c.t) / 16000.0f));
    for (uint16_t px = 0; px < n_px(c); px++) {
      if (!c.geo->mapped(px)) {
        out[px] = 0.0f;
        continue;
      }
      const float d = c.geo->pos(px) - crest;
      out[px] = 0.3f + 0.7f * expf(-(d * d) / (2.0f * 1.6f * 1.6f));
    }
  }
};

// Starfield: a dim field where scattered pixels twinkle in and out.
class Starfield final : public Effect {
 public:
  const char *name() const override { return "Starfield"; }
  uint32_t duration(const EffectCtx &) const override { return 0; }
  uint32_t period() const override { return LONG_PERIOD; }
  void render(const EffectCtx &c, float *out) const override {
    const float t = static_cast<float>(c.t);
    for (uint16_t px = 0; px < n_px(c); px++) {
      if (!c.geo->mapped(px)) {
        out[px] = 0.0f;
        continue;
      }
      // Each pixel cycles a whole number of times per LONG_PERIOD (2.5 to 6 s per
      // cycle), so the loop is seamless. About a third of cycles twinkle.
      const uint32_t cycles = 100 + static_cast<uint32_t>(rand01(c.seed, px, 51) * 140.0f);
      const float pp = static_cast<float>(LONG_PERIOD) / static_cast<float>(cycles);
      const float x = t / pp + rand01(c.seed, px, 52);
      const uint32_t k = static_cast<uint32_t>(x) % cycles;
      const float frac = x - floorf(x);
      const bool active = rand01(c.seed, px, 1000 + k) < 0.35f;
      // Peak mid-cycle and zero at the cycle edges, where `active` may change.
      const float bump = powf(fmaxf(0.0f, -cosf(kTwoPi * frac)), 10.0f);
      out[px] = 0.12f + 0.88f * (active ? bump : 0.0f);
    }
  }
};

// Candle: each step flickers gently and irregularly, like candlelight.
class Candle final : public Effect {
 public:
  const char *name() const override { return "Candle"; }
  uint32_t duration(const EffectCtx &) const override { return 0; }
  uint32_t period() const override { return LONG_PERIOD; }
  void render(const EffectCtx &c, float *out) const override {
    const float t = static_cast<float>(c.t);
    for (uint16_t px = 0; px < n_px(c); px++) {
      if (!c.geo->mapped(px)) {
        out[px] = 0.0f;
        continue;
      }
      const uint32_t st = c.geo->step(px);
      const float fast = loop_noise(c.seed, st, 20000, t, 100.0f, LONG_PERIOD / 100);
      const float slow = loop_noise(c.seed, st, 40000, t, 1000.0f, LONG_PERIOD / 1000);
      out[px] = clamp01(0.55f + 0.3f * fast + 0.15f * slow);
    }
  }
};

// Ember Bed: random pixels glow up and fade slowly at different rates.
class EmberBed final : public Effect {
 public:
  const char *name() const override { return "Ember Bed"; }
  uint32_t duration(const EffectCtx &) const override { return 0; }
  uint32_t period() const override { return LONG_PERIOD; }
  void render(const EffectCtx &c, float *out) const override {
    const float t = static_cast<float>(c.t);
    for (uint16_t px = 0; px < n_px(c); px++) {
      if (!c.geo->mapped(px)) {
        out[px] = 0.0f;
        continue;
      }
      const float n = loop_noise(c.seed, px, 60000, t, 750.0f, LONG_PERIOD / 750);
      out[px] = 0.08f + 0.92f * n * n;  // squared: mostly dim, occasional glow
    }
  }
};

const Glow GLOW;
const Guide GUIDE;
const LandingBeacon BEACON;
const Breathe BREATHE;
const Tide TIDE;
const Starfield STARFIELD;
const Candle CANDLE;
const EmberBed EMBER_BED;

}  // namespace

void register_ambient_effects(EffectRegistry &reg) {
  // The first AMBIENT entry is the default.
  reg.add_ambient("Glow", &GLOW);
  reg.add_ambient("Guide", &GUIDE);
  reg.add_ambient("Landing Beacon", &BEACON);
  reg.add_ambient("Breathe", &BREATHE);
  reg.add_ambient("Tide", &TIDE);
  reg.add_ambient("Starfield", &STARFIELD);
  reg.add_ambient("Candle", &CANDLE);
  reg.add_ambient("Ember Bed", &EMBER_BED);
}

}  // namespace stair
