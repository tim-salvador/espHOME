#include "stair_geometry.h"

namespace stair {

void Geometry::add_section(const char *name, uint16_t first_pixel, uint16_t pixels, uint16_t pixels_per_step,
                           bool reversed) {
  SectionDef s{};
  s.name = name;
  s.first_pixel = first_pixel;
  s.pixels = pixels;
  s.pixels_per_step = pixels_per_step;
  s.reversed = reversed;
  this->sections_.push_back(s);
  this->valid_ = false;
}

bool Geometry::finalize() {
  this->valid_ = false;
  if (this->num_pixels_ == 0 || this->num_pixels_ > MAX_PIXELS) {
    this->error_ = "num_pixels out of range";
    return false;
  }
  if (this->sections_.empty() || this->sections_.size() > MAX_SECTIONS) {
    this->error_ = "section count out of range";
    return false;
  }

  this->px_section_.assign(this->num_pixels_, NO_SECTION);
  this->px_step_.assign(this->num_pixels_, 0);
  this->px_pos_.assign(this->num_pixels_, 0.0f);

  uint16_t step_cursor = 0;
  for (uint8_t si = 0; si < this->sections_.size(); si++) {
    SectionDef &s = this->sections_[si];
    if (s.pixels == 0 || s.pixels_per_step == 0) {
      this->error_ = "section has zero pixels or zero pixels_per_step";
      return false;
    }
    if (s.pixels % s.pixels_per_step != 0) {
      this->error_ = "section pixels must be a multiple of pixels_per_step";
      return false;
    }
    if (static_cast<uint32_t>(s.first_pixel) + s.pixels > this->num_pixels_) {
      this->error_ = "section extends past num_pixels";
      return false;
    }
    s.first_step = step_cursor;
    s.steps = s.pixels / s.pixels_per_step;
    step_cursor += s.steps;

    for (uint16_t k = 0; k < s.pixels; k++) {
      uint16_t px = s.first_pixel + k;
      if (this->px_section_[px] != NO_SECTION) {
        this->error_ = "sections overlap";
        return false;
      }
      // k_up: index counted upward from the bottom of this section.
      uint16_t k_up = s.reversed ? static_cast<uint16_t>(s.pixels - 1 - k) : k;
      this->px_section_[px] = si;
      this->px_step_[px] = s.first_step + k_up / s.pixels_per_step;
      this->px_pos_[px] =
          static_cast<float>(s.first_step) + (static_cast<float>(k_up) + 0.5f) / static_cast<float>(s.pixels_per_step);
    }
  }
  this->num_steps_ = step_cursor;
  if (this->num_steps_ == 0) {
    this->error_ = "no steps";
    return false;
  }
  this->error_ = "";
  this->valid_ = true;
  return true;
}

bool Geometry::make_subset(const Geometry &full, uint8_t first, uint8_t count) {
  this->sections_.clear();
  this->num_pixels_ = full.num_pixels();
  for (uint8_t i = first; i < first + count && i < full.num_sections(); i++) {
    const SectionDef &s = full.section(i);
    this->add_section(s.name, s.first_pixel, s.pixels, s.pixels_per_step, s.reversed);
  }
  return this->finalize();
}

}  // namespace stair
