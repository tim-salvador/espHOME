#pragma once
// Framework-free core. No ESPHome includes in this file.
//
// Effects produce a per-pixel INTENSITY in [0,1]. They cannot produce color:
// color comes only from the section palette in the compositor. This enforces
// the "no colors inside effects" rule by construction.
//
// Effects are stateless: output is a pure function of the context and time, so
// completion is simply t >= duration(). Randomness comes from ctx.seed.
//
// Effects are found BY NAME through the registry. The names are what appear in
// the Home Assistant selects, so option order in YAML does not matter.
//
//   stair_effects_builtin.cpp  built-in effects + register_builtin_effects()
//   stair_effects_custom.cpp   your effects     + register_custom_effects()

#include <cstddef>
#include <cstdint>
#include <vector>
#include "stair_geometry.h"

namespace stair {

struct EffectCtx {
  const Geometry *geo;  // may be a section subset (relay); always index pixels globally
  Dir dir;              // direction of travel
  End origin;           // end where the triggering activity began
  uint32_t t;           // ms since the effect started
  float speed;          // > 0, 1.0 = nominal
  const float *from;    // per-pixel levels captured when the effect started (global indices)
  uint32_t seed;        // per-run random seed
  float param;          // effect-specific parameter (grace level, preview section, ...)
};

class Effect {
 public:
  virtual ~Effect() = default;
  virtual const char *name() const = 0;
  virtual uint32_t duration(const EffectCtx &c) const = 0;
  // Must write every pixel. Unmapped pixels (c.geo->mapped(px) == false) get 0.
  virtual void render(const EffectCtx &c, float *out) const = 0;
  // Relay hand-off: ms after start when the next section's effect should begin.
  // Effects with a moving front return the time the front reaches the far edge.
  virtual uint32_t handoff(const EffectCtx &c) const { return this->duration(c); }
};

enum class EffectKind : uint8_t { TRAVERSAL, INCIDENTAL };

struct EffectEntry {
  const char *name;
  EffectKind kind;
  const Effect *enter;  // TRAVERSAL: light up. INCIDENTAL: the effect itself.
  const Effect *exit;   // TRAVERSAL: turn off. INCIDENTAL: nullptr.
};

class EffectRegistry {
 public:
  void add_traversal(const char *name, const Effect *enter, const Effect *exit);
  void add_incidental(const char *name, const Effect *fx);

  // Exact, case-sensitive match on the display name. -1 when not found.
  int find(EffectKind kind, const char *name, size_t len) const;
  int find(EffectKind kind, const char *name) const;
  int first(EffectKind kind) const;
  const EffectEntry &at(int i) const { return this->entries_[static_cast<size_t>(i)]; }
  size_t size() const { return this->entries_.size(); }

 private:
  std::vector<EffectEntry> entries_;
};

// Built-ins first, then custom. Populated once on first call.
const EffectRegistry &effects();

void register_builtin_effects(EffectRegistry &reg);
void register_custom_effects(EffectRegistry &reg);

// Engine utility effects (not user-selectable).
const Effect &restore_effect();      // current -> full
const Effect &grace_effect();        // current -> param (dim level), then holds
const Effect &preview_in_effect();   // section == param fades in, others dark
const Effect &fade_out_effect();     // current -> dark

}  // namespace stair
