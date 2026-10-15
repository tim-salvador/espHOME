#include "stair_director.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>

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
    this->last_activity_ = now;
    return;
  }

  this->last_activity_ = now;
  if (this->booting_(now) && !this->sim_fast_) {
    this->note_("%s sensor ignored (boot settle)", end_name(e));
    return;
  }

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
  bool completion = false;
  if (comp.n > 0 && (now - comp.front()) >= this->s_.min_traversal_ms) {
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
  }

  switch (this->phase_) {
    case Phase::IDLE:
    case Phase::PREVIEW:
    case Phase::PREVIEW_OUT: {
      if (!completion) {
        const Dir d = travel_dir_from(e);
        const Style st = d == Dir::UP ? this->s_.style_up : this->s_.style_down;
        this->start_(Phase::ENTER, &enter_effect(st), now, d, e);
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
  this->start_(Phase::INCIDENTAL, &incidental_effect(this->s_.incidental), now, away, e);
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

void Director::start_(Phase p, const Effect *fx, uint32_t now, Dir dir, End origin, float param) {
  // Capture exactly what is on the stairs so the new effect starts seamlessly.
  this->from_ = this->last_;
  this->phase_ = p;
  this->fx_ = fx;
  this->phase_start_ = now;
  this->dir_ = dir;
  this->origin_ = origin;
  this->param_ = param;
  this->seed_ = this->seed_ * 1664525u + 1013904223u;
}

void Director::restore_(uint32_t now) {
  this->last_activity_ = now;
  this->start_(Phase::RESTORE, &restore_effect(), now, this->dir_, this->origin_);
}

void Director::to_idle_(uint32_t now) {
  this->phase_ = Phase::IDLE;
  this->fx_ = nullptr;
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
  if (!this->oldest_pending_(&d, &o)) d = this->last_exit_dir_;
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
  const bool any_active = this->active_[0] || this->active_[1];
  const uint32_t elapsed = now - this->phase_start_;

  auto fx_done = [&]() -> bool {
    if (this->fx_ == nullptr) return true;
    EffectCtx c{this->geo_, this->dir_, this->origin_, elapsed, this->s_.speed, this->from_.data(), this->seed_,
                this->param_};
    return elapsed >= this->fx_->duration(c);
  };

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
        this->fx_ = nullptr;
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
          const Style st = this->last_exit_dir_ == Dir::UP ? this->s_.style_up : this->s_.style_down;
          this->note_("exit %s", dir_name(this->last_exit_dir_));
          this->start_(Phase::EXIT, &exit_effect(st), now, this->last_exit_dir_, entry_end_of(this->last_exit_dir_));
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
        this->note_("incidental (%s)", incidental_name(this->s_.incidental));
        this->start_(Phase::INCIDENTAL, &incidental_effect(this->s_.incidental), now, d, o);
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
      EffectCtx c{this->geo_, this->dir_, this->origin_, elapsed, this->s_.speed, this->from_.data(), this->seed_,
                  this->param_};
      // Clamp time to the effect end so a late tick never renders past the final frame.
      const uint32_t dur = this->fx_->duration(c);
      if (c.t > dur) c.t = dur;
      this->fx_->render(c, levels);
      break;
    }
  }
  std::copy(levels, levels + n, this->last_.begin());
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
