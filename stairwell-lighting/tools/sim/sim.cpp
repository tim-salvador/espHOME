// Host-side regression tests and visualizer for the framework-free stair core.
//
//   make            # build + run all scenarios
//   ./sim --show    # also print ASCII frames of every effect
//
// The geometry mirrors stairwell.yaml: 58 px; lower 16 px (2/step), landing
// 36 px (9/step), upper 6 px (2/step) -> 8 + 4 + 3 = 15 steps.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "stair_compose.h"
#include "stair_director.h"
#include "stair_effects.h"
#include "stair_geometry.h"

using namespace stair;

static int g_fail = 0, g_pass = 0;
static bool g_show = false;

#define CHECK(cond, ...)                                  \
  do {                                                    \
    if (cond) {                                           \
      g_pass++;                                           \
    } else {                                              \
      g_fail++;                                           \
      printf("    FAIL %s:%d  ", __FILE__, __LINE__);     \
      printf(__VA_ARGS__);                                \
      printf("\n");                                       \
    }                                                     \
  } while (0)

static Geometry make_geo() {
  Geometry g;
  g.set_num_pixels(58);
  g.add_section("Lower Flight", 0, 16, 2, false);
  g.add_section("Landing", 16, 36, 9, false);
  g.add_section("Upper Flight", 52, 6, 2, false);
  if (!g.finalize()) {
    printf("geometry invalid: %s\n", g.error());
    exit(2);
  }
  return g;
}

// Drives a Director with a 10 ms tick + 20 ms render cadence, like the device.
struct Rig {
  Geometry geo = make_geo();
  Director d;
  std::vector<float> lv;
  uint32_t now;
  uint32_t t0;
  std::vector<std::string> phases;  // phase transition log

  explicit Rig(uint32_t start = 0) : now(start), t0(start) {
    d.attach(&geo);
    lv.assign(geo.num_pixels(), 0.0f);
    d.begin(now);
    phases.push_back(phase_name(d.phase()));
    advance(3500);  // past boot settle
  }
  uint32_t rel() const { return now - t0; }
  void step() {
    now += 10;
    d.tick(now);
    if ((now / 10) % 2 == 0) d.render(now, lv.data());
    const char *p = phase_name(d.phase());
    if (phases.back() != p) phases.push_back(p);
  }
  void advance(uint32_t ms) {
    for (uint32_t i = 0; i < ms / 10; i++) step();
  }
  void until(uint32_t rel_ms) {
    while (static_cast<int32_t>(rel() - rel_ms) < 0) step();
  }
  void sensor(End e, bool a) { d.sensor(e, a, now); }
  float level_sum() const {
    float s = 0;
    for (float v : lv) s += v;
    return s;
  }
  bool saw(const char *p) const {
    for (auto &x : phases)
      if (x == p) return true;
    return false;
  }
  std::string trace() const {
    std::string s;
    for (auto &x : phases) s += x + " ";
    return s;
  }
  // Run until a phase is reached; returns ms elapsed or UINT32_MAX on timeout.
  uint32_t run_until_phase(Phase p, uint32_t max_ms) {
    uint32_t start = now;
    while (d.phase() != p) {
      if (now - start > max_ms) return UINT32_MAX;
      step();
    }
    return now - start;
  }
};

// ----------------------------------------------------------------- scenarios

static void t_up_traversal() {
  printf("up traversal\n");
  Rig r;
  uint32_t b = r.rel();
  r.sensor(End::LOWER, true);
  CHECK(r.d.phase() == Phase::ENTER, "expected ENTER, got %s", r.d.phase_text());
  CHECK(r.d.pending(Dir::UP) == 1, "pending up = %u", r.d.pending(Dir::UP));
  r.until(b + 2000);
  r.sensor(End::LOWER, false);
  r.until(b + 6000);
  CHECK(r.d.phase() == Phase::HOLD, "should hold mid-climb, got %s", r.d.phase_text());
  r.sensor(End::UPPER, true);
  CHECK(r.d.pending(Dir::UP) == 0, "completion should clear pending");
  r.until(b + 8000);
  r.sensor(End::UPPER, false);
  r.until(b + 9400);
  CHECK(r.d.phase() == Phase::HOLD, "exit delay not yet elapsed, got %s", r.d.phase_text());
  r.until(b + 9600);
  CHECK(r.d.phase() == Phase::EXIT, "expected EXIT after exit delay, got %s", r.d.phase_text());
  uint32_t ms = r.run_until_phase(Phase::IDLE, 10000);
  CHECK(ms != UINT32_MAX, "exit never finished");
  r.advance(100);
  CHECK(r.level_sum() == 0.0f, "dark at idle, sum=%f", r.level_sum());
  CHECK(r.d.learned_count(Dir::UP) == 1, "learned one up sample");
}

static void t_down_traversal() {
  printf("down traversal\n");
  Rig r;
  uint32_t b = r.rel();
  r.sensor(End::UPPER, true);
  CHECK(r.d.phase() == Phase::ENTER, "expected ENTER");
  r.until(b + 1500);
  r.sensor(End::UPPER, false);
  r.until(b + 5000);
  r.sensor(End::LOWER, true);
  r.until(b + 6000);
  r.sensor(End::LOWER, false);
  CHECK(r.run_until_phase(Phase::EXIT, 3000) != UINT32_MAX, "no exit");
  CHECK(r.run_until_phase(Phase::IDLE, 10000) != UINT32_MAX, "no idle");
  CHECK(r.d.learned_count(Dir::DOWN) == 1, "learned one down sample");
}

static void t_incidental_unlearned() {
  printf("incidental, no learning (backstop = hold_max)\n");
  Rig r;
  uint32_t b = r.rel();
  r.sensor(End::LOWER, true);
  r.until(b + 2500);
  r.sensor(End::LOWER, false);
  const uint32_t hold = r.d.settings().hold_max_ms;
  r.until(b + 2500 + hold - 100);
  CHECK(r.d.phase() == Phase::HOLD, "must still hold just before backstop, got %s", r.d.phase_text());
  r.until(b + 2500 + hold + 100);
  CHECK(r.d.phase() == Phase::GRACE, "expected GRACE, got %s", r.d.phase_text());
  r.until(b + 2500 + hold + 1500);
  float dim = r.lv[0];
  CHECK(std::fabs(dim - r.d.settings().grace_level) < 0.02f, "grace level %.3f", dim);
  r.until(b + 2500 + hold + r.d.settings().grace_ms + 100);
  CHECK(r.d.phase() == Phase::INCIDENTAL, "expected INCIDENTAL, got %s", r.d.phase_text());
  CHECK(r.run_until_phase(Phase::IDLE, 10000) != UINT32_MAX, "incidental never finished");
  CHECK(r.d.pending(Dir::UP) == 0, "queues cleared at idle");
}

static void t_slow_climber() {
  printf("slow climber (old firmware turned lights off here)\n");
  Rig r;
  uint32_t b = r.rel();
  r.sensor(End::LOWER, true);
  r.until(b + 3000);
  r.sensor(End::LOWER, false);
  r.until(b + 25000);
  CHECK(r.d.phase() == Phase::HOLD, "must be fully lit at 25 s, got %s", r.d.phase_text());
  CHECK(r.lv[30] > 0.99f, "landing fully lit, %.3f", r.lv[30]);
  r.sensor(End::UPPER, true);
  r.until(b + 27000);
  r.sensor(End::UPPER, false);
  CHECK(r.run_until_phase(Phase::EXIT, 3000) != UINT32_MAX, "exit after completion");
}

static void t_learned_hold() {
  printf("learned hold\n");
  Rig r;
  for (int i = 0; i < 6; i++) {
    r.sensor(End::LOWER, true);
    r.advance(1500);
    r.sensor(End::LOWER, false);
    r.advance(4000 + 100 * i);
    r.sensor(End::UPPER, true);
    r.advance(1500);
    r.sensor(End::UPPER, false);
    r.run_until_phase(Phase::IDLE, 20000);
    r.advance(2000);
  }
  CHECK(r.d.learned_count(Dir::UP) == 6, "6 samples, got %u", r.d.learned_count(Dir::UP));
  // p90 ~ 6.0 s, x2 = 12 s, floored to hold_min 20 s.
  CHECK(r.d.hold_ms_for(Dir::UP) == r.d.settings().hold_min_ms, "hold %u", r.d.hold_ms_for(Dir::UP));
  r.d.settings().hold_min_ms = 5000;
  uint32_t h = r.d.hold_ms_for(Dir::UP);
  CHECK(h >= 11800 && h <= 12200, "p90 x factor ~12 s, got %u", h);
  CHECK(r.d.hold_ms_for(Dir::DOWN) == r.d.settings().hold_max_ms, "down unlearned -> hold_max");
}

static void t_grace_restore() {
  printf("activity during grace restores\n");
  Rig r;
  r.d.settings().hold_max_ms = 10000;
  uint32_t b = r.rel();
  r.sensor(End::LOWER, true);
  r.until(b + 2000);
  r.sensor(End::LOWER, false);
  r.until(b + 12500);
  CHECK(r.d.phase() == Phase::GRACE, "in grace, got %s", r.d.phase_text());
  r.sensor(End::UPPER, true);  // the slow climber finally reaches the top
  CHECK(r.d.phase() == Phase::RESTORE, "restore on activity, got %s", r.d.phase_text());
  CHECK(r.d.pending(Dir::UP) == 0, "that was a completion");
  r.advance(500);
  CHECK(r.d.phase() == Phase::HOLD, "back to hold");
  CHECK(r.lv[0] > 0.99f, "full again %.3f", r.lv[0]);
}

static void t_boot_guard() {
  printf("boot settle ignores early triggers\n");
  Geometry g = make_geo();
  Director d;
  d.attach(&g);
  d.sensor(End::LOWER, true, 0);  // before begin(): GPIO sensors publish first
  d.begin(100);
  d.sensor(End::LOWER, false, 500);
  d.sensor(End::LOWER, true, 1000);
  CHECK(d.phase() == Phase::IDLE, "ignored during settle, got %s", d.phase_text());
  d.sensor(End::LOWER, false, 2000);
  d.sensor(End::LOWER, true, 5000);
  CHECK(d.phase() == Phase::ENTER, "accepted after settle, got %s", d.phase_text());
}

static void t_gating() {
  printf("ambient gating\n");
  Rig r;
  r.d.settings().gating = true;
  r.d.settings().lux_threshold = 75;
  r.d.set_ambient_lux(200);
  r.sensor(End::LOWER, true);
  CHECK(r.d.phase() == Phase::IDLE, "bright: ignored");
  CHECK(r.d.pending(Dir::UP) == 0, "no phantom entry");
  r.advance(2000);
  r.sensor(End::LOWER, false);
  r.advance(2000);
  r.d.set_ambient_lux(NAN);
  r.sensor(End::LOWER, true);
  CHECK(r.d.phase() == Phase::ENTER, "unknown lux fails open");
}

static void t_chatter() {
  printf("PIR chatter inside rearm window\n");
  Rig r;
  r.sensor(End::LOWER, true);
  r.advance(500);
  r.sensor(End::LOWER, false);
  r.advance(400);
  r.sensor(End::LOWER, true);
  CHECK(r.d.pending(Dir::UP) == 1, "still one entry, got %u", r.d.pending(Dir::UP));
}

static void t_two_people_same_dir() {
  printf("two people going up, spaced\n");
  Rig r;
  uint32_t b = r.rel();
  r.sensor(End::LOWER, true);
  r.until(b + 2000);
  r.sensor(End::LOWER, false);
  r.until(b + 4000);
  r.sensor(End::LOWER, true);
  CHECK(r.d.pending(Dir::UP) == 2, "two pending, got %u", r.d.pending(Dir::UP));
  r.until(b + 6000);
  r.sensor(End::LOWER, false);
  r.until(b + 7000);
  r.sensor(End::UPPER, true);
  r.until(b + 8000);
  r.sensor(End::UPPER, false);
  r.until(b + 10500);
  CHECK(r.d.phase() == Phase::HOLD, "second person still on stairs, got %s", r.d.phase_text());
  r.sensor(End::UPPER, true);
  r.until(b + 12500);
  r.sensor(End::UPPER, false);
  CHECK(r.run_until_phase(Phase::EXIT, 3000) != UINT32_MAX, "exit after both done");
}

static void t_crossing() {
  printf("crossing traffic never exits early\n");
  Rig r;
  uint32_t b = r.rel();
  r.sensor(End::LOWER, true);
  r.until(b + 400);
  r.sensor(End::LOWER, false);
  r.until(b + 600);
  r.sensor(End::UPPER, true);  // someone at the top within min_traversal: a new DOWN entry
  CHECK(r.d.pending(Dir::UP) == 1 && r.d.pending(Dir::DOWN) == 1, "up=%u down=%u", r.d.pending(Dir::UP),
        r.d.pending(Dir::DOWN));
  r.until(b + 3000);
  r.sensor(End::UPPER, false);
  r.until(b + 9000);
  CHECK(r.d.phase() == Phase::HOLD, "hold while traversals open, got %s", r.d.phase_text());
}

static void t_ambiguous_crossing() {
  printf("ambiguous crossing (documented two-sensor limit): relights on far-end arrival\n");
  Rig r;
  uint32_t b = r.rel();
  r.sensor(End::LOWER, true);  // A starts up
  r.until(b + 1500);
  r.sensor(End::LOWER, false);
  r.until(b + 2000);
  r.sensor(End::UPPER, true);  // B starts down; looks exactly like A finishing fast
  r.until(b + 3000);
  r.sensor(End::UPPER, false);
  r.until(b + 6000);
  // B reaches the bottom; whatever phase we are in, lights must come back up immediately.
  r.sensor(End::LOWER, true);
  r.advance(400);
  CHECK(r.lv[0] > 0.6f, "bottom relit within 400 ms (%.2f), phase %s", r.lv[0], r.d.phase_text());
}

static void t_chatter_fault_from_log() {
  printf("false-triggering lower PIR (pattern from the field log)\n");
  Rig r;
  // Intervals between lower rises observed 05:55:09 -> 05:58:12, 2 s AM312 pulses, nobody on the stairs.
  const uint32_t gaps[] = {0, 6000, 5000, 12000, 47000, 20000, 10000, 6000, 22000, 12000, 43000};
  bool reached_idle_while_chattering = false;
  for (uint32_t g : gaps) {
    for (uint32_t k = 0; k < g / 10; k++) {
      r.step();
      if (r.d.sensor_fault(End::LOWER) && r.d.phase() == Phase::IDLE) reached_idle_while_chattering = true;
    }
    r.sensor(End::LOWER, true);
    r.advance(2000);
    r.sensor(End::LOWER, false);
  }
  CHECK(r.d.sensor_fault(End::LOWER), "lower flagged as faulted");
  CHECK(!r.d.sensor_fault(End::UPPER), "upper not flagged");
  CHECK(r.d.pending(Dir::UP) <= 2, "faulted rises stop piling up entries (%u)", r.d.pending(Dir::UP));
  CHECK(reached_idle_while_chattering, "lights actually turned off despite chatter");
  uint32_t ms = r.run_until_phase(Phase::IDLE, 60000);
  CHECK(ms != UINT32_MAX, "settles to idle after the last false trigger");
  r.advance(600000 + 1000);
  CHECK(!r.d.sensor_fault(End::LOWER), "auto-clears after 10 quiet minutes");
}

static void t_stuck_sensor() {
  printf("stuck-high sensor cannot hold the lights forever\n");
  Rig r;
  r.sensor(End::LOWER, true);  // never releases
  r.advance(r.d.settings().stuck_ms + 100);
  CHECK(r.d.sensor_fault(End::LOWER), "stuck fault set");
  CHECK(r.run_until_phase(Phase::IDLE, 120000) != UINT32_MAX, "goes dark despite stuck sensor");
  r.sensor(End::LOWER, false);
  r.advance(r.d.settings().fault_clear_ms + 1000);
  CHECK(!r.d.sensor_fault(End::LOWER), "clears once released and quiet");
}

static void t_faulted_sensor_still_wakes() {
  printf("faulted sensor still wakes dark stairs; good sensor still holds them\n");
  Rig r;
  for (int i = 0; i < 5; i++) {  // drive lower into fault
    r.sensor(End::LOWER, true);
    r.advance(2000);
    r.sensor(End::LOWER, false);
    r.advance(8000);
  }
  CHECK(r.d.sensor_fault(End::LOWER), "precondition: lower faulted");
  r.run_until_phase(Phase::IDLE, 120000);
  r.advance(3000);
  uint32_t b = r.rel();
  r.sensor(End::LOWER, true);  // a real person
  CHECK(r.d.phase() == Phase::ENTER, "faulted sensor wakes from idle, got %s", r.d.phase_text());
  r.until(b + 2000);
  r.sensor(End::LOWER, false);
  r.until(b + 7000);
  r.sensor(End::UPPER, true);  // reaches the top: upper is healthy
  CHECK(r.d.pending(Dir::UP) == 1, "untrusted entry not counted as completed");
  r.until(b + 9000);
  r.sensor(End::UPPER, false);
  r.until(b + 9000 + r.d.settings().hold_min_ms - 500);
  CHECK(r.d.phase() == Phase::HOLD, "still lit for hold_min after real activity, got %s", r.d.phase_text());
  CHECK(r.run_until_phase(Phase::IDLE, 60000) != UINT32_MAX, "then resolves to idle");
  r.d.clear_faults(r.now);
  CHECK(!r.d.sensor_fault(End::LOWER), "manual clear");
}

static void t_preview() {
  printf("color preview\n");
  Rig r;
  r.d.preview(1, r.now);
  CHECK(r.d.phase() == Phase::PREVIEW, "preview");
  r.advance(400);
  CHECK(r.lv[20] > 0.99f && r.lv[0] < 0.01f && r.lv[55] < 0.01f, "only landing lit (%.2f %.2f %.2f)", r.lv[20],
        r.lv[0], r.lv[55]);
  r.advance(3000);
  r.d.preview(1, r.now);  // user keeps dragging the wheel: extends
  r.advance(3000);
  CHECK(r.d.phase() == Phase::PREVIEW, "extended, got %s", r.d.phase_text());
  CHECK(r.run_until_phase(Phase::IDLE, 4000) != UINT32_MAX, "preview times out");
  r.d.preview(0, r.now);
  r.advance(100);
  r.sensor(End::LOWER, true);
  CHECK(r.d.phase() == Phase::ENTER, "real trigger preempts preview");
}

static void t_simulation() {
  printf("simulate buttons\n");
  const SimAction acts[] = {SimAction::UP, SimAction::DOWN, SimAction::INCIDENTAL};
  const char *names[] = {"up", "down", "incidental"};
  for (int i = 0; i < 3; i++) {
    Rig r;
    CHECK(r.d.simulate(acts[i], r.now), "sim accepted");
    r.step();
    CHECK(r.d.phase() == Phase::ENTER, "sim starts immediately, got %s", r.d.phase_text());
    uint32_t ms = r.run_until_phase(Phase::IDLE, 30000);
    r.advance(100);
    CHECK(ms != UINT32_MAX, "sim %s finished", names[i]);
    printf("    sim %-10s -> %s (%.1f s)\n", names[i], r.trace().c_str(), static_cast<double>(ms) / 1000.0);
    CHECK(r.d.learned_count(Dir::UP) == 0 && r.d.learned_count(Dir::DOWN) == 0, "sim must not learn");
    if (acts[i] == SimAction::INCIDENTAL) CHECK(r.saw("grace") && r.saw("incidental"), "incidental path");
    else CHECK(r.saw("exit"), "exit path");
  }
}

static void t_wraparound() {
  printf("millis() wraparound\n");
  Rig r(0xFFFFF000u);
  r.sensor(End::LOWER, true);
  r.advance(2000);
  r.sensor(End::LOWER, false);
  r.advance(3000);  // crosses 2^32 here
  r.sensor(End::UPPER, true);
  r.advance(1000);
  r.sensor(End::UPPER, false);
  CHECK(r.run_until_phase(Phase::EXIT, 3000) != UINT32_MAX, "exit across wrap");
  CHECK(r.d.learned_count(Dir::UP) == 1, "sample across wrap");
  uint32_t h = r.d.hold_ms_for(Dir::UP);
  CHECK(h == r.d.settings().hold_max_ms, "1 sample -> hold_max, got %u", h);
}

static void t_effect_math() {
  printf("effect math: bounds, continuity, endpoints\n");
  Geometry g = make_geo();
  const uint16_t n = g.num_pixels();
  std::vector<float> from_on(n, 1.0f), from_off(n, 0.0f), out(n);
  for (float spd : {0.5f, 1.0f, 2.0f}) {
    for (int s = 0; s < static_cast<int>(Style::COUNT); s++) {
      for (Dir d : {Dir::UP, Dir::DOWN}) {
        for (int on = 0; on < 2; on++) {
          const Effect &fx = on ? enter_effect(static_cast<Style>(s)) : exit_effect(static_cast<Style>(s));
          EffectCtx c{&g, d, entry_end_of(d), 0, spd, on ? from_off.data() : from_on.data(), 42, 0};
          uint32_t dur = fx.duration(c);
          bool ok = true;
          for (uint32_t t = 0; t <= dur; t += 10) {
            c.t = t;
            fx.render(c, out.data());
            for (float v : out) ok &= (v >= 0.0f && v <= 1.0f && !std::isnan(v));
          }
          c.t = 0;
          fx.render(c, out.data());
          float start = 0;
          for (float v : out) start += v;
          c.t = dur;
          fx.render(c, out.data());
          float end = 0;
          for (float v : out) end += v;
          CHECK(ok, "%s bounds", fx.name());
          if (on) {
            CHECK(start < 0.6f * n, "%s %s on starts mostly dark (%.1f)", fx.name(), dir_name(d), start);
            CHECK(end > 0.999f * n, "%s %s on ends full (%.2f/%u)", fx.name(), dir_name(d), end, n);
          } else {
            CHECK(start > 0.95f * n, "%s %s off starts full (%.2f)", fx.name(), dir_name(d), start);
            CHECK(end < 0.001f * n, "%s %s off ends dark (%.3f)", fx.name(), dir_name(d), end);
          }
        }
      }
    }
    for (int s = 0; s < static_cast<int>(IncidentalStyle::COUNT); s++) {
      const Effect &fx = incidental_effect(static_cast<IncidentalStyle>(s));
      EffectCtx c{&g, Dir::UP, End::LOWER, 0, spd, from_on.data(), 7, 0};
      uint32_t dur = fx.duration(c);
      bool ok = true;
      for (uint32_t t = 0; t <= dur; t += 10) {
        c.t = t;
        fx.render(c, out.data());
        for (float v : out) ok &= (v >= 0.0f && v <= 1.0f && !std::isnan(v));
      }
      c.t = 0;
      fx.render(c, out.data());
      float start = 0;
      for (float v : out) start += v;
      c.t = dur;
      fx.render(c, out.data());
      float end = 0;
      for (float v : out) end += v;
      CHECK(ok, "%s bounds", fx.name());
      CHECK(start > 0.95f * n, "%s continuous with lit stairs (%.2f)", fx.name(), start);
      CHECK(end < 0.02f * n, "%s ends dark (%.3f)", fx.name(), end);
    }
  }
}

static void t_geometry() {
  printf("geometry\n");
  Geometry g = make_geo();
  CHECK(g.num_steps() == 15, "15 steps, got %u", g.num_steps());
  CHECK(g.section(1).first_step == 8 && g.section(2).first_step == 12, "step offsets");
  CHECK(g.step_along(0, Dir::UP) == 0 && g.step_along(57, Dir::UP) == 14, "up order");
  CHECK(g.step_along(57, Dir::DOWN) == 0, "down order");
  Geometry bad;
  bad.set_num_pixels(10);
  bad.add_section("a", 0, 6, 4, false);
  CHECK(!bad.finalize(), "6 not multiple of 4 rejected");
  Geometry ov;
  ov.set_num_pixels(10);
  ov.add_section("a", 0, 6, 2, false);
  ov.add_section("b", 4, 6, 2, false);
  CHECK(!ov.finalize(), "overlap rejected");
  Geometry rev;
  rev.set_num_pixels(4);
  rev.add_section("r", 0, 4, 1, true);
  rev.finalize();
  CHECK(rev.step_along(0, Dir::UP) == 3, "reversed section maps pixel 0 to top");
}

static void t_compose() {
  printf("compositor\n");
  Geometry g = make_geo();
  std::vector<float> lv(g.num_pixels(), 1.0f);
  SectionLook looks[3];
  looks[0] = {1.0f, 0.5f, 0.0f, 1.0f, true};
  looks[1] = {0.0f, 0.0f, 1.0f, 0.5f, true};
  looks[2] = {1.0f, 1.0f, 1.0f, 1.0f, false};
  std::vector<uint8_t> rgb(g.num_pixels() * 3);
  compose(g, lv.data(), looks, 1.0f, rgb.data());
  CHECK(rgb[0] == 255 && rgb[1] == 128 && rgb[2] == 0, "lower color");
  CHECK(rgb[3 * 20 + 2] == 128, "landing at 50%% brightness");
  CHECK(rgb[3 * 55] == 0 && rgb[3 * 55 + 1] == 0, "disabled section dark");
}

// ------------------------------------------------------------------ visual

static void show(const char *title, const Effect &fx, Dir d, End o, bool from_full) {
  Geometry g = make_geo();
  std::vector<float> from(g.num_pixels(), from_full ? 1.0f : 0.0f), out(g.num_pixels());
  EffectCtx c{&g, d, o, 0, 1.0f, from.data(), 99, 0};
  uint32_t dur = fx.duration(c);
  printf("\n%s (%s, %u ms)   bottom -> top\n", title, dir_name(d), dur);
  const char *ramp = " .:-=+*#%@";
  for (uint32_t t = 0; t <= dur + 99; t += dur / 14 + 1) {
    c.t = t > dur ? dur : t;
    fx.render(c, out.data());
    printf("  %5u |", c.t);
    for (uint16_t px = 0; px < g.num_pixels(); px++) {
      if (px == 16 || px == 52) printf("|");
      int k = static_cast<int>(out[px] * 9.0f + 0.5f);
      putchar(ramp[k < 0 ? 0 : (k > 9 ? 9 : k)]);
    }
    printf("|\n");
  }
}

int main(int argc, char **argv) {
  for (int i = 1; i < argc; i++)
    if (strcmp(argv[i], "--show") == 0) g_show = true;

  t_geometry();
  t_compose();
  t_effect_math();
  t_up_traversal();
  t_down_traversal();
  t_incidental_unlearned();
  t_slow_climber();
  t_learned_hold();
  t_grace_restore();
  t_boot_guard();
  t_gating();
  t_chatter();
  t_two_people_same_dir();
  t_crossing();
  t_ambiguous_crossing();
  t_chatter_fault_from_log();
  t_stuck_sensor();
  t_faulted_sensor_still_wakes();
  t_preview();
  t_simulation();
  t_wraparound();

  if (g_show) {
    for (int s = 0; s < static_cast<int>(Style::COUNT); s++) {
      show(enter_effect(static_cast<Style>(s)).name(), enter_effect(static_cast<Style>(s)), Dir::UP, End::LOWER, false);
      show("  exit", exit_effect(static_cast<Style>(s)), Dir::UP, End::LOWER, true);
    }
    for (int s = 0; s < static_cast<int>(IncidentalStyle::COUNT); s++)
      show(incidental_effect(static_cast<IncidentalStyle>(s)).name(),
           incidental_effect(static_cast<IncidentalStyle>(s)), Dir::UP, End::LOWER, true);
  }

  printf("\n%d passed, %d failed\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}
