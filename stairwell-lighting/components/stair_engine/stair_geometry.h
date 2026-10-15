#pragma once
// Framework-free core. No ESPHome includes in this file.
//
// Geometry maps physical pixels onto a single staircase axis measured in
// "steps" from the bottom (0) to the top (num_steps). Sections are listed
// bottom to top. A landing is just a section whose steps are step-equivalents.

#include <cstdint>
#include <vector>

namespace stair {

enum class End : uint8_t { LOWER = 0, UPPER = 1 };
enum class Dir : int8_t { UP = 1, DOWN = -1 };

inline Dir travel_dir_from(End entry) { return entry == End::LOWER ? Dir::UP : Dir::DOWN; }
inline End far_end_of(End e) { return e == End::LOWER ? End::UPPER : End::LOWER; }
inline End entry_end_of(Dir d) { return d == Dir::UP ? End::LOWER : End::UPPER; }
inline const char *end_name(End e) { return e == End::LOWER ? "lower" : "upper"; }
inline const char *dir_name(Dir d) { return d == Dir::UP ? "up" : "down"; }

static constexpr uint8_t NO_SECTION = 0xFF;
static constexpr uint8_t MAX_SECTIONS = 8;
static constexpr uint16_t MAX_PIXELS = 2048;

struct SectionDef {
  const char *name;
  uint16_t first_pixel;
  uint16_t pixels;
  uint16_t pixels_per_step;
  bool reversed;        // true when pixel index increases toward the BOTTOM within this section
  uint16_t first_step;  // computed by finalize()
  uint16_t steps;       // computed by finalize()
};

class Geometry {
 public:
  void set_num_pixels(uint16_t n) { this->num_pixels_ = n; }
  void add_section(const char *name, uint16_t first_pixel, uint16_t pixels, uint16_t pixels_per_step,
                   bool reversed);

  // Validates the layout and builds lookup tables. Returns false and sets error() on failure.
  bool finalize();
  // Builds this geometry as sections [first, first+count) of `full`, steps renumbered from 0.
  // Pixel indices stay global; pixels outside the subset are unmapped. Used for relays.
  bool make_subset(const Geometry &full, uint8_t first, uint8_t count);

  bool valid() const { return this->valid_; }
  const char *error() const { return this->error_; }

  uint16_t num_pixels() const { return this->num_pixels_; }
  uint16_t num_steps() const { return this->num_steps_; }
  uint8_t num_sections() const { return static_cast<uint8_t>(this->sections_.size()); }
  const SectionDef &section(uint8_t i) const { return this->sections_[i]; }

  uint8_t section_of(uint16_t px) const { return this->px_section_[px]; }
  bool mapped(uint16_t px) const { return this->px_section_[px] != NO_SECTION; }

  // Continuous position of the pixel center along the stair axis, in steps. 0 = bottom edge.
  float pos(uint16_t px) const { return this->px_pos_[px]; }
  // Distance from the given end, in steps (0 at that end).
  float pos_from(uint16_t px, End e) const {
    return e == End::LOWER ? this->px_pos_[px] : static_cast<float>(this->num_steps_) - this->px_pos_[px];
  }
  // Distance along the direction of travel (0 where travel starts).
  float along(uint16_t px, Dir d) const { return this->pos_from(px, entry_end_of(d)); }
  // Integer step index counted along the direction of travel.
  uint16_t step_along(uint16_t px, Dir d) const {
    uint16_t s = this->px_step_[px];
    return d == Dir::UP ? s : static_cast<uint16_t>(this->num_steps_ - 1 - s);
  }

 private:
  uint16_t num_pixels_{0};
  uint16_t num_steps_{0};
  bool valid_{false};
  const char *error_{"not finalized"};
  std::vector<SectionDef> sections_;
  std::vector<uint8_t> px_section_;
  std::vector<uint16_t> px_step_;
  std::vector<float> px_pos_;
};

}  // namespace stair
