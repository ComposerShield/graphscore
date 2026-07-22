// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

#include <graphscore/core/graphscore_core.hpp>

namespace graphscore {

// One stave within a track's fixed layout: a stable identity plus the clef
// it is notated in by default.
struct StaveDefinition {
  StaveId id;
  Clef    default_clef = Clef::kTreble;

  [[nodiscard]] bool operator==(const StaveDefinition&) const = default;
};

// A track's fixed staff layout: one or more staves, set once at track
// creation and never changed afterward (product decision: "a track has one
// or more fixed pitched staves").
class StaffLayout {
 public:
  StaffLayout() = delete;

  // Fails only if `staves` is empty; a track always has at least one stave.
  [[nodiscard]] static std::optional<StaffLayout> create(
      std::vector<StaveDefinition> staves);

  // A single pitched staff notated in `clef` — the common case for
  // one-staff instruments.
  [[nodiscard]] static StaffLayout single_staff(Clef clef = Clef::kTreble);

  // The standard two-staff grand-staff configuration: treble over bass.
  [[nodiscard]] static StaffLayout grand_staff();

  [[nodiscard]] const std::vector<StaveDefinition>& staves() const noexcept {
    return staves_;
  }

  [[nodiscard]] std::size_t stave_count() const noexcept {
    return staves_.size();
  }

  [[nodiscard]] bool operator==(const StaffLayout&) const = default;

 private:
  explicit StaffLayout(std::vector<StaveDefinition> staves)
      : staves_(std::move(staves)) {}

  std::vector<StaveDefinition> staves_;
};

}  // namespace graphscore
