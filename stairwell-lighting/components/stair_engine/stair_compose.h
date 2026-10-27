#pragma once
// Framework-free core. No ESPHome includes in this file.
//
// The only place color enters the pipeline. Output is pre-gamma 8-bit RGB;
// the LED driver applies gamma and global brightness afterward.

#include <cstdint>
#include "stair_geometry.h"

namespace stair {

struct SectionLook {
  float r{1.0f}, g{1.0f}, b{1.0f};  // normalized color (max channel 1.0)
  float brightness{1.0f};           // 0..1
  bool enabled{true};
};

inline uint8_t to_u8(float v) {
  if (v <= 0.0f) return 0;
  if (v >= 1.0f) return 255;
  return static_cast<uint8_t>(v * 255.0f + 0.5f);
}

// rgb_out: 3 bytes per pixel. looks: one per section.
inline void compose(const Geometry &geo, const float *levels, const SectionLook *looks, float master,
                    uint8_t *rgb_out) {
  for (uint16_t px = 0; px < geo.num_pixels(); px++) {
    const uint8_t s = geo.section_of(px);
    uint8_t *o = rgb_out + 3 * px;
    if (s == NO_SECTION || !looks[s].enabled) {
      o[0] = o[1] = o[2] = 0;
      continue;
    }
    const float k = levels[px] * looks[s].brightness * master;
    o[0] = to_u8(looks[s].r * k);
    o[1] = to_u8(looks[s].g * k);
    o[2] = to_u8(looks[s].b * k);
  }
}

// Like compose(), but each section's color is blended toward `alt` by `mix`
// (0 = section colors, 1 = alt everywhere). Used for the ambient color override:
// colors (including brightness) are mixed first, then scaled by the level, so a
// takeover crossfades hue without changing brightness. A disabled section stays
// dark either way.
inline void compose_mixed(const Geometry &geo, const float *levels, const SectionLook *looks,
                          const SectionLook &alt, float mix, float master, uint8_t *rgb_out) {
  if (mix <= 0.0f) {
    compose(geo, levels, looks, master, rgb_out);
    return;
  }
  const float m = mix > 1.0f ? 1.0f : mix;
  for (uint16_t px = 0; px < geo.num_pixels(); px++) {
    const uint8_t s = geo.section_of(px);
    uint8_t *o = rgb_out + 3 * px;
    if (s == NO_SECTION || !looks[s].enabled) {
      o[0] = o[1] = o[2] = 0;
      continue;
    }
    const SectionLook &a = looks[s];
    const float k = levels[px] * master;
    const float r = (a.r * a.brightness) * (1.0f - m) + (alt.r * alt.brightness) * m;
    const float g = (a.g * a.brightness) * (1.0f - m) + (alt.g * alt.brightness) * m;
    const float b = (a.b * a.brightness) * (1.0f - m) + (alt.b * alt.brightness) * m;
    o[0] = to_u8(r * k);
    o[1] = to_u8(g * k);
    o[2] = to_u8(b * k);
  }
}

}  // namespace stair
