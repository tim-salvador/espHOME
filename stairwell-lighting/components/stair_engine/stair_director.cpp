#include "stair_director.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>

namespace stair {

const char *phase_name(Phase p) {
  switch (p) {
    case Phase::IDLE:
      return "idle";
    case Phase::PREVIEW:
      return "preview";
    case Phase::PREVIEW_OUT:
      return "preview_out";
    case Phase::ENTER:
      return "enter";
    case Phase::HOLD:
      return "hold";
    case Phase::RESTORE:
      return "restore";
    case Phase::GRACE:
      return "grace";
    case Phase::EXIT:
      return "exit";
    case Phase::INCIDENTAL:
      return "incidental";
  }
  return "unknown";
}

// ------------------------------------------------------------------ queue

void Director::Queue::push(uint32_t v) {
  if (this->n == CAP) this->pop();  // drop the oldest rather than the newest
  this->t[this->n++] = v;
}

void Director::Queue::pop() {
  if (this->n == 0) return;
  for (uint8_t i = 1; i < this->n; i++) this->t[i - 1] = this->t[i];
  this->n--;
}

// ------------------------------------------------------------------ setup

void Director::attach(const Geometry *geo) {
  this->geo_ = geo;
  this->from_.assign(geo->num_pixels(), 0.0f);
  this->last_.assign(geo->num_pixels(), 0.0f);
  this->tmp_.assign(geo->num_pixels(), 0.0f);
  this->sub_cache_.clear();
  const EffectRegistry &reg = effects();
  if (this->s_.fx_up < 0) this->s_.fx_up = static_cast<int16_t>(reg.first(EffectKind::TRAVERSAL));
  if (this->s_.fx_down < 0) this->s_.fx_down = static_cast<int16_t>(reg.first(EffectKind::TRAVERSAL));
  if (this->s_.fx_incidental < 0) this->s_.fx_incidental = static_cast<int16_t>(reg.first(EffectKind::INCIDENTAL));
}

bool Director::select_effect(Slot slot, const char *name, size_t len) {
  const EffectKind kind = slot == Slot::INCIDENTAL ? EffectKind::INCIDENTAL : EffectKind::TRAVERSAL;
  const int idx = effects().find(kind, name, len);
  if (idx < 0) return false;
  switch (slot) {
    case Slot::UP:
      this->s_.fx_up = static_cast<int16_t>(idx);
      break;
    case Slot::DOWN:
      this->s_.fx_down = static_cast<int16_t>(idx);
      break;
    case Slot::INCIDENTAL:
      this->s_.fx_incidental = static_cast<int16_t>(idx);
      break;
  }
  return true;
}

bool Director::select_section_effect(uint8_t section, const char *name, size_t len) {
  if (section >= MAX_SECTIONS) return false;
  static const char FOLLOW[] = "Follow Action";
  if (len == 0 || (len == sizeof(FOLLOW) - 1 && strncmp(name, FOLLOW, len) == 0)) {
    this->s_.section_fx[section] = -1;
    return true;
  }
  const int idx = effects().find(EffectKind::TRAVERSAL, name, len);
  if (idx < 0) return false;
  this->s_.section_fx[section] = static_cast<int16_t>(idx);
  return true;
}

void Director::begin(uint32_t now) {
  this->started_ = true;
  this->boot_ms_ = now;
  this->last_activity_ = now;
  this->seed_ ^= now * 2654435761u;
  this->note_("ready");
}

void Director::note_(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vsnprintf(this->event_, sizeof(this->event_), fmt, args);
  va_end(args);
  this->event_seq_++;
}

bool Director::lit_() const {
  switch (this->phase_) {
    case Phase::ENTER:
    case Phase::HOLD:
    case Phase::RESTORE:
    case Phase::GRACE:
    case Phase::EXIT:
    case Phase::INCIDENTAL:
      return true;
    default:
      return false;
  }
}

bool Director::booting_(uint32_t now) const {
  return !this->started_ || (now - this->boot_ms_) < this->s_.boot_ignore_ms;
}

bool Director::gated_() const { return this->s_.gating && !std::isnan(this->lux_) && this->lux_ >= this->s_.lux_threshold; }

// ------------------------------------------------------------------ inputs

void Director::sensor(End e, bool active, uint32_t now) {
  const uint8_t i = static_cast<uint8_t>(e);
  if (active == this->active_[i]) return;
  this->active_[i] = active;

  if (!active) {
    this->had_fall_[i] = true;
    this->last_fall_[i] = now;
    if (!this->fault_[i]) this->last_activity_ = now;
    return;
  }

  this->last_rise_[i] = now;
  this->active_since_[i] = now;
  if (this->booting_(now) && !this->sim_fast_) {
    this->note_("%s sensor ignored (boot settle)", end_name(e));
    return;
  }

  // A faulted sensor may still wake the stairs from dark (a real person must never be left
  // in the dark), but while lit it can neither extend the hold nor add phantom entries.
  if (this->fault_[i] && this->lit_()) return;
  this->last_activity_ = now;

  // A re-trigger shortly after this sensor cleared is the same presence (PIR chatter
  // or someone lingering), not a new arrival. Only meaningful while lit.
  const bool same_presence = this->had_fall_[i] && (now - this->last_fall_[i]) < this->s_.rearm_ms;
  if (same_presence && this->lit_()) {
    if (this->phase_ == Phase::GRACE || this->phase_ == Phase::EXIT || this->phase_ == Phase::INCIDENTAL) {
      this->note_("%s re-trigger, restoring", end_name(e));
      this->restore_(now);
    }
    return;
  }
  this->arrival_(e, now);
}

void Director::arrival_(End e, uint32_t now) {
  if (!this->lit_() && this->gated_() && !this->sim_fast_) {
    this->note_("%s ignored: ambient %.0f lx >= %.0f", end_name(e), this->lux_, this->s_.lux_threshold);
    return;
  }

  // Entries heading TOWARD this end are the ones this arrival could complete.
  const Dir toward = (e == End::UPPER) ? Dir::UP : Dir::DOWN;
  Queue &comp = this->q_[qi(toward)];
  const bool comp_trusted = !this->fault_[static_cast<uint8_t>(far_end_of(e))];
  bool completion = false;
  if (comp.n > 0 && comp_trusted && (now - comp.front()) >= this->s_.min_traversal_ms) {
    const uint32_t sample = now - comp.front();
    comp.pop();
    this->last_exit_dir_ = toward;
    this->record_sample_(toward, sample);
    completion = true;
    this->note_("%s complete in %.1fs (%u/%u pending)", dir_name(toward), static_cast<float>(sample) / 1000.0f, this->q_[0].n,
                this->q_[1].n);
  } else {
    const Dir away = travel_dir_from(e);
    this->q_[qi(away)].push(now);
    this->note_("%s entry at %s (%u/%u pending)", dir_name(away), end_name(e), this->q_[0].n, this->q_[1].n);
    // Chatter detection: many entries from one end, none of them ever completed.
    const uint8_t ei = static_cast<uint8_t>(e);
    this->recent_[ei].push(now);
    if (!this->fault_[ei] && !this->sim_fast_ &&
        this->recent_[ei].within(now, this->s_.fault_window_ms) >= this->s_.fault_entries) {
      this->set_fault_(e, true, "repeated entries, none completed", now);
    }
  }
  if (completion) this->recent_[static_cast<uint8_t>(far_end_of(e))].clear();  // those entries were real

  switch (this->phase_) {
    case Phase::IDLE:
    case Phase::PREVIEW:
    case Phase::PREVIEW_OUT: {
      if (!completion) {
        this->start_traversal_(Phase::ENTER, true, now, travel_dir_from(e), e);
      }
      break;
    }
    case Phase::GRACE:
    case Phase::EXIT:
    case Phase::INCIDENTAL:
      this->restore_(now);
      break;
    case Phase::ENTER:
    case Phase::HOLD:
    case Phase::RESTORE:
      break;  // already lit
  }
}

void Director::report_incidental(End e, uint32_t now) {
  // Only short-circuit when the sole pending traversal is the one this end just started.
  const Dir away = travel_dir_from(e);
  Queue &q = this->q_[qi(away)];
  const Queue &other = this->q_[qi(away == Dir::UP ? Dir::DOWN : Dir::UP)];
  if (q.n != 1 || other.n != 0) return;
  if (this->phase_ != Phase::ENTER && this->phase_ != Phase::HOLD) return;
  q.pop();
  this->note_("%s classified incidental by sensor", end_name(e));
  this->start_(Phase::INCIDENTAL, effects().at(this->s_.fx_incidental).enter, now, away, e);
}

void Director::preview(uint8_t section, uint32_t now) {
  if (this->geo_ == nullptr || section >= this->geo_->num_sections()) return;
  if (this->phase_ != Phase::IDLE && this->phase_ != Phase::PREVIEW && this->phase_ != Phase::PREVIEW_OUT) return;
  this->preview_until_ = now + this->s_.preview_ms;
  if (this->phase_ == Phase::PREVIEW && static_cast<uint8_t>(this->param_) == section) return;  // extend only
  this->note_("preview %s", this->geo_->section(section).name);
  this->start_(Phase::PREVIEW, &preview_in_effect(), now, Dir::UP, End::LOWER, static_cast<float>(section));
}

bool Director::simulate(SimAction a, uint32_t now) {
  if (this->phase_ != Phase::IDLE && this->phase_ != Phase::PREVIEW && this->phase_ != Phase::PREVIEW_OUT) return false;
  this->sim_.clear();
  this->sim_t0_ = now;
  this->sim_fast_ = true;
  switch (a) {
    case SimAction::UP:
      this->sim_ = {{0, End::LOWER, true}, {1200, End::LOWER, false}, {3200, End::UPPER, true}, {4400, End::UPPER, false}};
      break;
    case SimAction::DOWN:
      this->sim_ = {{0, End::UPPER, true}, {1200, End::UPPER, false}, {3200, End::LOWER, true}, {4400, End::LOWER, false}};
      break;
    case SimAction::INCIDENTAL:
      this->sim_ = {{0, End::LOWER, true}, {900, End::LOWER, false}};
      break;
  }
  // Start from a clean sensor slate so the scripted edges are not deduplicated or rearm-filtered.
  this->active_[0] = this->active_[1] = false;
  this->had_fall_[0] = this->had_fall_[1] = false;
  this->note_("simulating %s", a == SimAction::UP ? "up" : (a == SimAction::DOWN ? "down" : "incidental"));
  return true;
}

// ------------------------------------------------------------------ phases

void Director::begin_phase_(Phase p, uint32_t now, Dir dir, End origin, float param) {
  // Capture exactly what is on the stairs so the new effect starts seamlessly.
  this->from_ = this->last_;
  this->phase_ = p;
  this->phase_start_ = now;
  this->dir_ = dir;
  this->origin_ = origin;
  this->param_ = param;
  this->seed_ = this->seed_ * 1664525u + 1013904223u;
  this->plan_.clear();
  this->plan_dur_ = 0;
}

EffectCtx Director::ctx_(const Geometry *g, uint32_t t, size_t seg) const {
  return EffectCtx{g,      this->dir_,         this->origin_, t, this->s_.speed, this->from_.data(),
                   this->seed_ ^ static_cast<uint32_t>(seg * 0x9E3779B9u), this->param_};
}

void Director::start_(Phase p, const Effect *fx, uint32_t now, Dir dir, End origin, float param) {
  this->begin_phase_(p, now, dir, origin, param);
  this->plan_.push_back({this->geo_, fx, 0});
  this->plan_dur_ = fx->duration(this->ctx_(this->geo_, 0, 0));
}

const Geometry *Director::subset_(uint8_t first, uint8_t count) {
  if (first == 0 && count == this->geo_->num_sections()) return this->geo_;
  for (const SubGeo &sg : this->sub_cache_)
    if (sg.first == first && sg.count == count) return sg.geo.get();
  std::unique_ptr<Geometry> g(new Geometry());
  g->make_subset(*this->geo_, first, count);
  this->sub_cache_.push_back({first, count, std::move(g)});
  return this->sub_cache_.back().geo.get();
}

void Director::start_traversal_(Phase p, bool enter, uint32_t now, Dir dir, End origin) {
  this->begin_phase_(p, now, dir, origin, 0.0f);
  const EffectRegistry &reg = effects();
  const int16_t main = this->traversal_fx_(dir);

  // Group consecutive sections (bottom to top) that resolve to the same effect.
  struct Run {
    uint8_t first, count;
    int16_t fx;
  } runs[MAX_SECTIONS];
  uint8_t nruns = 0;
  for (uint8_t i = 0; i < this->geo_->num_sections(); i++) {
    int16_t fx = this->s_.section_fx[i];
    if (fx < 0 || static_cast<size_t>(fx) >= reg.size() || reg.at(fx).kind != EffectKind::TRAVERSAL) fx = main;
    if (nruns > 0 && runs[nruns - 1].fx == fx) {
      runs[nruns - 1].count++;
    } else {
      runs[nruns++] = {i, 1, fx};
    }
  }

  // Relay in the direction of travel: each run starts when the previous one hands off.
  uint32_t start = 0;
  for (uint8_t r = 0; r < nruns; r++) {
    const Run &run = runs[dir == Dir::UP ? r : nruns - 1 - r];
    const EffectEntry &entry = reg.at(run.fx);
    const Effect *fx = enter ? entry.enter : entry.exit;
    const Geometry *g = this->subset_(run.first, run.count);
    const size_t seg = this->plan_.size();
    this->plan_.push_back({g, fx, start});
    const EffectCtx c = this->ctx_(g, 0, seg);
    const uint32_t end = start + fx->duration(c);
    if (end > this->plan_dur_) this->plan_dur_ = end;
    start += fx->handoff(c);
  }
}

void Director::restore_(uint32_t now) {
  this->last_activity_ = now;
  this->start_(Phase::RESTORE, &restore_effect(), now, this->dir_, this->origin_);
}

void Director::to_idle_(uint32_t now) {
  this->phase_ = Phase::IDLE;
  this->plan_.clear();
  this->plan_dur_ = 0;
  this->phase_start_ = now;
  this->q_[0].clear();
  this->q_[1].clear();
  if (this->sim_.empty()) this->sim_fast_ = false;
}

bool Director::oldest_pending_(Dir *d, End *origin) const {
  const Queue &up = this->q_[0], &down = this->q_[1];
  if (up.n == 0 && down.n == 0) return false;
  bool pick_up;
  if (up.n == 0) {
    pick_up = false;
  } else if (down.n == 0) {
    pick_up = true;
  } else {
    // Older entry wins; unsigned subtraction keeps this wrap-safe within ~24 days.
    pick_up = static_cast<int32_t>(up.front() - down.front()) <= 0;
  }
  *d = pick_up ? Dir::UP : Dir::DOWN;
  *origin = entry_end_of(*d);
  return true;
}

uint32_t Director::backstop_ms_() const {
  if (this->sim_fast_) return 3000;
  Dir d;
  End o;
  if (!this->oldest_pending_(&d, &o)) return this->hold_ms_for(this->last_exit_dir_);
  if (this->fault_[static_cast<uint8_t>(o)]) return this->s_.hold_min_ms;  // likely another false trigger
  return this->hold_ms_for(d);
}

uint32_t Director::hold_ms_for(Dir d) const {
  const uint8_t i = qi(d);
  const uint8_t n = this->learn_.count[i];
  uint32_t hold = this->s_.hold_max_ms;
  if (n >= 3) {
    uint16_t buf[LearnBlob::N];
    for (uint8_t k = 0; k < n; k++) buf[k] = this->learn_.sample_cs[i][k];
    std::sort(buf, buf + n);
    const uint8_t idx = static_cast<uint8_t>((n * 9 + 9) / 10 - 1);  // ceil(0.9 n) - 1
    const float p90_ms = static_cast<float>(buf[idx]) * 10.0f;
    hold = static_cast<uint32_t>(p90_ms * this->s_.hold_factor);
  }
  if (hold < this->s_.hold_min_ms) hold = this->s_.hold_min_ms;
  if (hold > this->s_.hold_max_ms) hold = this->s_.hold_max_ms;
  return hold;
}

void Director::tick(uint32_t now) {
  // Deliver scripted simulation edges.
  while (!this->sim_.empty() && (now - this->sim_t0_) >= this->sim_.front().at) {
    const SimEvent ev = this->sim_.front();
    this->sim_.erase(this->sim_.begin());
    this->sensor(ev.end, ev.active, now);
  }

  if (this->geo_ == nullptr || !this->geo_->valid()) return;

  for (uint8_t i = 0; i < 2; i++) {
    const End e = static_cast<End>(i);
    if (!this->fault_[i] && this->active_[i] && !this->booting_(now) &&
        (now - this->active_since_[i]) >= this->s_.stuck_ms) {
      this->set_fault_(e, true, "stuck active", now);
    } else if (this->fault_[i] && !this->active_[i] && (now - this->last_rise_[i]) >= this->s_.fault_clear_ms) {
      this->set_fault_(e, false, "quiet, auto-cleared", now);
    }
  }
  const bool any_active = this->effective_active_();
  const uint32_t elapsed = now - this->phase_start_;

  auto fx_done = [&]() -> bool { return this->plan_.empty() || elapsed >= this->plan_dur_; };

  switch (this->phase_) {
    case Phase::IDLE:
      break;

    case Phase::PREVIEW:
      if (static_cast<int32_t>(now - this->preview_until_) >= 0)
        this->start_(Phase::PREVIEW_OUT, &fade_out_effect(), now, this->dir_, this->origin_);
      break;

    case Phase::PREVIEW_OUT:
      if (fx_done()) this->to_idle_(now);
      break;

    case Phase::ENTER:
    case Phase::RESTORE:
      if (fx_done()) {
        this->phase_ = Phase::HOLD;
        this->plan_.clear();
        this->plan_dur_ = 0;
        this->phase_start_ = now;
      }
      break;

    case Phase::HOLD: {
      if (any_active) {
        this->last_activity_ = now;  // continuous presence keeps the lights up
        break;
      }
      const uint32_t quiet = now - this->last_activity_;
      if (this->q_[0].n == 0 && this->q_[1].n == 0) {
        if (quiet >= this->s_.exit_delay_ms) {
          this->note_("exit %s", dir_name(this->last_exit_dir_));
          this->start_traversal_(Phase::EXIT, false, now, this->last_exit_dir_, entry_end_of(this->last_exit_dir_));
        }
      } else {
        const uint32_t backstop = this->backstop_ms_();
        if (quiet >= backstop) {
          this->note_("unresolved after %.0fs, grace", static_cast<float>(backstop) / 1000.0f);
          this->start_(Phase::GRACE, &grace_effect(), now, this->dir_, this->origin_, this->s_.grace_level);
        }
      }
      break;
    }

    case Phase::GRACE: {
      const uint32_t grace = this->sim_fast_ ? 1500 : this->s_.grace_ms;
      if (elapsed >= grace) {
        Dir d = this->dir_;
        End o = this->origin_;
        this->oldest_pending_(&d, &o);
        const EffectEntry &inc = effects().at(this->s_.fx_incidental);
        this->note_("incidental (%s)", inc.name);
        this->start_(Phase::INCIDENTAL, inc.enter, now, d, o);
      }
      break;
    }

    case Phase::EXIT:
    case Phase::INCIDENTAL:
      if (fx_done()) {
        this->note_("idle");
        this->to_idle_(now);
      }
      break;
  }
}

void Director::render(uint32_t now, float *levels) {
  const uint16_t n = this->geo_ == nullptr ? 0 : this->geo_->num_pixels();
  if (n == 0) return;
  switch (this->phase_) {
    case Phase::IDLE:
      std::fill(levels, levels + n, 0.0f);
      break;
    case Phase::HOLD:
      for (uint16_t px = 0; px < n; px++) levels[px] = this->geo_->mapped(px) ? 1.0f : 0.0f;
      break;
    default: {
      const uint32_t elapsed = now - this->phase_start_;
      if (this->plan_.empty()) {
        std::fill(levels, levels + n, 0.0f);
        break;
      }
      const bool single = this->plan_.size() == 1 && this->plan_[0].geo == this->geo_;
      if (!single) std::fill(levels, levels + n, 0.0f);
      for (size_t i = 0; i < this->plan_.size(); i++) {
        const Segment &seg = this->plan_[i];
        // Before its hand-off a segment renders at t=0, which is its captured level.
        uint32_t t = elapsed > seg.start ? elapsed - seg.start : 0;
        EffectCtx c = this->ctx_(seg.geo, t, i);
        // Clamp to the effect end so a late tick never renders past the final frame.
        const uint32_t dur = seg.fx->duration(c);
        if (c.t > dur) c.t = dur;
        if (single) {
          seg.fx->render(c, levels);
        } else {
          seg.fx->render(c, this->tmp_.data());
          for (uint16_t px = 0; px < n; px++)
            if (seg.geo->mapped(px)) levels[px] = this->tmp_[px];
        }
      }
      break;
    }
  }
  std::copy(levels, levels + n, this->last_.begin());
}

// ------------------------------------------------------------------ faults

void Director::set_fault_(End e, bool on, const char *why, uint32_t now) {
  const uint8_t i = static_cast<uint8_t>(e);
  if (this->fault_[i] == on) return;
  this->fault_[i] = on;
  this->recent_[i].clear();
  if (!on) this->active_since_[i] = now;
  this->note_("%s sensor fault %s: %s", end_name(e), on ? "SET" : "cleared", why);
}

void Director::clear_faults(uint32_t now) {
  this->set_fault_(End::LOWER, false, "manual", now);
  this->set_fault_(End::UPPER, false, "manual", now);
}

// ------------------------------------------------------------------ learning

void Director::record_sample_(Dir d, uint32_t sample_ms) {
  if (this->sim_fast_) return;                         // never learn from simulations
  if (sample_ms > this->s_.hold_max_ms) return;        // lingering, not walking
  const uint8_t i = qi(d);
  uint32_t cs = sample_ms / 10;
  if (cs > 0xFFFF) cs = 0xFFFF;
  LearnBlob &b = this->learn_;
  b.magic = LearnBlob::MAGIC;
  b.sample_cs[i][b.head[i]] = static_cast<uint16_t>(cs);
  b.head[i] = static_cast<uint8_t>((b.head[i] + 1) % LearnBlob::N);
  if (b.count[i] < LearnBlob::N) b.count[i]++;
  this->learn_dirty_ = true;
}

void Director::load_learning(const LearnBlob &b) {
  if (b.magic != LearnBlob::MAGIC) return;
  for (int i = 0; i < 2; i++) {
    if (b.count[i] > LearnBlob::N || b.head[i] >= LearnBlob::N) return;  // corrupt, ignore
  }
  this->learn_ = b;
}

void Director::reset_learning() {
  this->learn_ = LearnBlob{};
  this->learn_.magic = LearnBlob::MAGIC;
  this->learn_dirty_ = true;
  this->note_("learning reset");
}

}  // namespace stair
