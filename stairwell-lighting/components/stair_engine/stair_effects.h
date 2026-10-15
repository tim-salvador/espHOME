#pragma once
// Framework-free core. No ESPHome includes in this file.
//
// Effects produce a per-pixel INTENSITY in [0,1]. They cannot produce color:
// color comes only from the section palette in the compositor. This enforces
// the "no colors inside effects" rule by construction.
//
// Effects are stateless: output is a pure function of the context and time, so
// completion is simply t >= duration(). Randomness comes from ctx.seed.

#include <cstdint>
#include "stair_geometry.h"

namespace stair {

struct EffectCtx {
  const Geometry *geo;
  Dir dir;            // direction of travel
  End origin;         // end where the triggering activity began
  uint32_t t;         // ms since the effect started
  float speed;        // > 0, 1.0 = nominal
  const float *from;  // per-pixel levels captured when the effect started
  uint32_t seed;      // per-run random seed
  float param;        // effect-specific parameter (grace level, preview section, ...)
};

class Effect {
 public:
  virtual ~Effect() = default;
  virtual const char *name() const = 0;
  virtual uint32_t duration(const EffectCtx &c) const = 0;
  virtual void render(const EffectCtx &c, float *out) const = 0;
};

// Traversal styles: each has an enter (light up) and exit (turn off) variant.
// ORDER MUST MATCH the "Up Effect" / "Down Effect" select options in YAML.
enum class Style : uint8_t { CASCADE = 0, SOFT_WIPE, COMET, FADE, COUNT };

// Incidental styles: start from whatever is lit and resolve to dark.
// ORDER MUST MATCH the "Incidental Effect" select options in YAML.
enum class IncidentalStyle : uint8_t { RETREAT = 0, FIREFLY, RIPPLE, BREATH, COUNT };

const Effect &enter_effect(Style s);
const Effect &exit_effect(Style s);
const Effect &incidental_effect(IncidentalStyle s);

const Effect &restore_effect();      // current -> full
const Effect &grace_effect();        // current -> param (dim level), then holds
const Effect &preview_in_effect();   // section == param fades in, others dark
const Effect &fade_out_effect();     // current -> dark

const char *style_name(Style s);
const char *incidental_name(IncidentalStyle s);

}  // namespace stair
