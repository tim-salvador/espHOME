#pragma once
// Framework-free core. No ESPHome includes in this file.
//
// The Director turns raw end-sensor edges into lighting phases.
//
// Model (two endpoint sensors, no mid-stair sensing):
//   * A new arrival at an end is either the COMPLETION of a traversal that
//     started at the other end (oldest pending entry heading toward it), or a
//     NEW ENTRY heading away from it.
//   * Lights turn off with a directional exit only when every entry has been
//     matched by a completion and both sensors are clear.
//   * Unmatched entries (incidental triggers, turn-backs, someone still on the
//     stairs) are resolved by a learned backstop: HOLD -> GRACE (dim) ->
//     INCIDENTAL effect -> IDLE. Any sensor activity during GRACE restores.
//   * The backstop never undercuts hold_min; with fewer than 3 learned
//     traversals it uses hold_max.

#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>
#include "stair_effects.h"
#include "stair_geometry.h"

namespace stair {

enum class Phase : uint8_t { IDLE, PREVIEW, PREVIEW_OUT, ENTER, HOLD, RESTORE, GRACE, EXIT, INCIDENTAL };
enum class SimAction : uint8_t { UP, DOWN, INCIDENTAL };

const char *phase_name(Phase p);

struct Settings {
  // Hold / backstop
  uint32_t hold_min_ms{20000};
  uint32_t hold_max_ms{60000};
  float hold_factor{2.0f};          // learned p90 traversal x factor
  uint32_t grace_ms{8000};          // time spent dimmed before the incidental effect
  float grace_level{0.35f};         // dim level during grace (0..1, pre-gamma)
  uint32_t exit_delay_ms{1500};     // after last sensor clears with all traversals resolved
  // Sensor interpretation
  uint32_t rearm_ms{1500};          // re-trigger closer than this to the last clear = same presence
  uint32_t min_traversal_ms{800};   // a far-end trigger sooner than this is not a completion
  uint32_t boot_ignore_ms{3000};    // ignore arrivals right after boot (PIR settle)
  // Sensor fault detection (false-triggering or stuck sensors must not hold the lights on)
  uint8_t fault_entries{5};         // this many new entries at one end...
  uint32_t fault_window_ms{180000}; // ...within this window, none completed = fault
  uint32_t fault_clear_ms{600000};  // auto-clear after this long with no rises from that end
  uint32_t stuck_ms{300000};        // continuously active this long = stuck fault
  // Look
  float speed{1.0f};
  // Effect choices are indices into effects() (the registry). Set them by name
  // through Director::select_effect(); attach() initializes them to the defaults.
  int16_t fx_up{-1};
  int16_t fx_down{-1};
  int16_t fx_incidental{-1};
  // Per-section traversal override for relays; -1 = follow the action's effect.
  int16_t section_fx[MAX_SECTIONS]{-1, -1, -1, -1, -1, -1, -1, -1};
  // Preview
  uint32_t preview_ms{5000};
  // Ambient gating (blocks NEW activations only)
  bool gating{false};
  float lux_threshold{75.0f};
};

// Persisted learning data. Trivially copyable on purpose (stored raw in flash).
struct LearnBlob {
  static constexpr uint32_t MAGIC = 0x53544C31;  // "STL1"
  static constexpr uint8_t N = 16;
  uint32_t magic;
  uint8_t count[2];             // [0]=UP, [1]=DOWN
  uint8_t head[2];
  uint16_t sample_cs[2][N];     // traversal time in centiseconds (max 655 s)
};

class Director {
 public:
  void attach(const Geometry *geo);
  void begin(uint32_t now);
  Settings &settings() { return this->s_; }
  const Settings &settings() const { return this->s_; }

  // ---- inputs
  void sensor(End e, bool active, uint32_t now);
  // For sensors that can classify a non-person on their own (future ToF/mmWave nodes).
  void report_incidental(End e, uint32_t now);
  void preview(uint8_t section, uint32_t now);
  bool simulate(SimAction a, uint32_t now);
  void set_ambient_lux(float lux) { this->lux_ = lux; }

  // ---- effect selection by registry name. Returns false (and changes nothing) if
  // the name is not a registered effect of the right kind.
  enum class Slot : uint8_t { UP, DOWN, INCIDENTAL };
  bool select_effect(Slot slot, const char *name, size_t len);
  // Section override: "Follow Action" (or empty) clears it.
  bool select_section_effect(uint8_t section, const char *name, size_t len);

  // ---- runtime
  void tick(uint32_t now);
  // Writes per-pixel intensity [0..1] for every pixel. Also captures the frame
  // so the next phase can start from exactly what is on the stairs.
  void render(uint32_t now, float *levels);

  // ---- status
  Phase phase() const { return this->phase_; }
  const char *phase_text() const { return phase_name(this->phase_); }
  const char *last_event() const { return this->event_; }
  uint32_t event_seq() const { return this->event_seq_; }
  uint8_t pending(Dir d) const { return this->q_[qi(d)].n; }
  uint32_t hold_ms_for(Dir d) const;
  uint8_t learned_count(Dir d) const { return this->learn_.count[qi(d)]; }
  bool sensor_fault(End e) const { return this->fault_[static_cast<uint8_t>(e)]; }
  void clear_faults(uint32_t now);

  // ---- learning persistence
  void load_learning(const LearnBlob &b);
  const LearnBlob &learning() const { return this->learn_; }
  bool learning_dirty() const { return this->learn_dirty_; }
  void clear_learning_dirty() { this->learn_dirty_ = false; }
  void reset_learning();

 private:
  struct Queue {
    static constexpr uint8_t CAP = 4;
    uint32_t t[CAP];
    uint8_t n{0};
    void push(uint32_t v);
    uint32_t front() const { return this->t[0]; }
    void pop();
    void clear() { this->n = 0; }
  };
  struct Recent {
    static constexpr uint8_t CAP = 8;
    uint32_t t[CAP];
    uint8_t n{0};
    void push(uint32_t v) {
      if (this->n == CAP) {
        for (uint8_t i = 1; i < CAP; i++) this->t[i - 1] = this->t[i];
        this->n--;
      }
      this->t[this->n++] = v;
    }
    uint8_t within(uint32_t now, uint32_t window) const {
      uint8_t c = 0;
      for (uint8_t i = 0; i < this->n; i++)
        if (now - this->t[i] < window) c++;
      return c;
    }
    void clear() { this->n = 0; }
  };
  struct SimEvent {
    uint32_t at;
    End end;
    bool active;
  };

  static uint8_t qi(Dir d) { return d == Dir::UP ? 0 : 1; }
  bool lit_() const;
  bool booting_(uint32_t now) const;
  bool gated_() const;
  void arrival_(End e, uint32_t now);
  // One effect over the whole staircase.
  void start_(Phase p, const Effect *fx, uint32_t now, Dir dir, End origin, float param = 0.0f);
  // Traversal (enter or exit) as a relay across sections, honoring section overrides.
  void start_traversal_(Phase p, bool enter, uint32_t now, Dir dir, End origin);
  void begin_phase_(Phase p, uint32_t now, Dir dir, End origin, float param);
  EffectCtx ctx_(const Geometry *g, uint32_t t, size_t seg) const;
  const Geometry *subset_(uint8_t first, uint8_t count);
  int16_t traversal_fx_(Dir d) const { return d == Dir::UP ? this->s_.fx_up : this->s_.fx_down; }
  void to_idle_(uint32_t now);
  void restore_(uint32_t now);
  void record_sample_(Dir d, uint32_t sample_ms);
  uint32_t backstop_ms_() const;
  bool oldest_pending_(Dir *d, End *origin) const;
  void set_fault_(End e, bool on, const char *why, uint32_t now);
  bool effective_active_() const {
    return (this->active_[0] && !this->fault_[0]) || (this->active_[1] && !this->fault_[1]);
  }
  void note_(const char *fmt, ...);

  const Geometry *geo_{nullptr};
  Settings s_;
  Phase phase_{Phase::IDLE};
  struct Segment {
    const Geometry *geo;
    const Effect *fx;
    uint32_t start;
  };
  std::vector<Segment> plan_;  // empty = no effect (idle/hold)
  uint32_t plan_dur_{0};
  struct SubGeo {
    uint8_t first, count;
    std::unique_ptr<Geometry> geo;
  };
  std::vector<SubGeo> sub_cache_;
  std::vector<float> tmp_;
  uint32_t phase_start_{0};
  Dir dir_{Dir::UP};
  End origin_{End::LOWER};
  float param_{0.0f};
  uint32_t seed_{0x1234567u};

  bool started_{false};
  uint32_t boot_ms_{0};
  bool active_[2]{false, false};
  bool had_fall_[2]{false, false};
  uint32_t last_fall_[2]{0, 0};
  uint32_t last_activity_{0};
  uint32_t last_rise_[2]{0, 0};
  uint32_t active_since_[2]{0, 0};
  bool fault_[2]{false, false};
  Recent recent_[2];
  Dir last_exit_dir_{Dir::UP};
  Queue q_[2];  // pending traversals: [0]=UP (entered at lower), [1]=DOWN (entered at upper)
  float lux_{NAN};

  uint32_t preview_until_{0};

  std::vector<float> from_;
  std::vector<float> last_;

  std::vector<SimEvent> sim_;
  uint32_t sim_t0_{0};
  bool sim_fast_{false};

  LearnBlob learn_{};
  bool learn_dirty_{false};

  char event_[72]{"boot"};
  uint32_t event_seq_{0};
};

}  // namespace stair
