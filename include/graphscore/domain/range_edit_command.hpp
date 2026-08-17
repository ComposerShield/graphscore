// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include <graphscore/domain/command.hpp>
#include <graphscore/domain/selection.hpp>
#include <graphscore/domain/track.hpp>

namespace graphscore {

class Project;

// A range transpose is expressed in staff steps or semitones. Diatonic
// transposition keeps each notehead's accidental while moving its letter;
// chromatic transposition chooses a deterministic spelling for the resulting
// MIDI pitch.
enum class RangeTransposeKind : std::uint8_t {
  kDiatonic = 0,
  kChromatic,
};

// Pure precondition query shared by notation factories and range commands.
// It rejects mixed nodes/spans, duplicate addressed voices, invalid bounds,
// and multi-measure full-range selections before the first lane is touched.
[[nodiscard]] bool is_valid_range_edit_selection(const Project&   project,
                                                 const Selection& selection);

// Deletes or transposes the musical events addressed by a full-measure or
// arbitrary range selection. Delete replaces the selected time with
// normalized rests, using the same boundary reconnection policy as cut. A
// transpose is pitch-only: durations, rests, tuplets, identities, markings,
// and event order remain unchanged. A tied notehead is moved with its whole
// connected chain, including a chain member just outside the selected range,
// so the edit cannot create an invalid tie.
class RangeEditCommand final : public Command {
 public:
  static RangeEditCommand make_delete(Selection selection) {
    return RangeEditCommand(std::move(selection), std::nullopt, 0);
  }

  static RangeEditCommand make_transpose(Selection          selection,
                                         RangeTransposeKind kind,
                                         std::int32_t       amount) {
    return RangeEditCommand(std::move(selection), kind, amount);
  }

  RangeEditCommand(Selection selection, std::optional<RangeTransposeKind> kind,
                   std::int32_t amount)
      : selection_(std::move(selection)), kind_(kind), amount_(amount) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

 private:
  Selection                                                 selection_;
  std::optional<RangeTransposeKind>                         kind_;
  std::int32_t                                              amount_ = 0;
  std::optional<NodeId>                                     node_id_;
  std::optional<std::vector<std::pair<TrackId, TrackLane>>> pre_snapshot_;
  std::optional<std::vector<std::pair<TrackId, TrackLane>>> post_snapshot_;
  State state_ = State::kFresh;
};

}  // namespace graphscore
