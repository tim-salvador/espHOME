#pragma once
// Framework-free helpers shared by effect implementations.

#include <cmath>
#include <cstdint>
#include "stair_effects.h"

namespace stair {
namespace fx {

// Not TWO_PI: the ESP8266 Arduino core #defines TWO_PI, which would break this declaration.
constexpr float kTwoPi = 6.28318530718f;

inline float clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
// Smoothstep: eased 0..1 ramp, clamped.
inline float smooth(float x) {
  x = clamp01(x);
  return x * x * (3.0f - 2.0f * x);
}
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
// Scale a nominal duration by the Effect Speed setting.
inline float ms(float base, float speed) { return base / (speed > 0.05f ? speed : 0.05f); }

// Deterministic random in [0,1) from (seed, index, salt). Use the pixel or step
// index as `index` and a distinct `salt` per random property. For flicker, use a
// time bucket such as c.t / 40 as part of the salt.
inline float rand01(uint32_t seed, uint32_t index, uint32_t salt) {
  uint32_t h = seed ^ (index * 0x9E3779B1u) ^ (salt * 0x85EBCA77u);
  h ^= h >> 16;
  h *= 0x7FEB352Du;
  h ^= h >> 15;
  h *= 0x846CA68Bu;
  h ^= h >> 16;
  return static_cast<float>(h & 0x00FFFFFFu) / 16777216.0f;
}

inline uint16_t n_px(const EffectCtx &c) { return c.geo->num_pixels(); }
inline float n_steps(const EffectCtx &c) { return static_cast<float>(c.geo->num_steps()); }

// Common output helper: blend from the captured level toward full (on) or dark (off).
inline float toward(const EffectCtx &c, uint16_t px, bool on, float p) {
  return on ? lerp(c.from[px], 1.0f, p) : c.from[px] * (1.0f - p);
}

}  // namespace fx
}  // namespace stair
