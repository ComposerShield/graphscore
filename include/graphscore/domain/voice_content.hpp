// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <optional>
#include <vector>

#include <graphscore/core/graphscore_core.hpp>
#include <graphscore/domain/notation_event.hpp>

namespace graphscore {

// One voice's ordered, contiguous-time content: a sequence of notes,
// chords, and rests. VoiceContent does not itself know the node's total
// musical length; normalize() and check_complete() take that length as an
// explicit Rational parameter (typically NodeTimeline::node_end()) so this
// type never duplicates timeline state.
class VoiceContent {
 public:
  VoiceContent() = default;

  [[nodiscard]] const std::vector<VoiceEvent>& events() const noexcept {
    return events_;
  }

  // Appends `event` to the end of the voice. Fails, leaving the voice
  // unchanged, if `event` holds a Chord with fewer than two notes.
  [[nodiscard]] Result append(VoiceEvent event);

  void clear() noexcept { events_.clear(); }

  // The exact whole-note sum of every event's resolved duration.
  [[nodiscard]] Rational total_length() const;

  // Succeeds only if total_length() exactly equals `target_length`; a
  // kInvalidArgument diagnostic otherwise covers both under-fill and
  // over-fill.
  [[nodiscard]] Result check_complete(Rational target_length) const;

  // Fills the gap between total_length() and `target_length` with
  // automatically generated, non-tuplet Rests (see decompose_rest), so the
  // voice exactly tiles [0, target_length) with no gaps or overhangs. A
  // no-op if the voice already exactly fills `target_length`. Fails,
  // leaving the voice unchanged, if the voice already exceeds
  // `target_length`, or if the gap cannot be expressed exactly as plain
  // dotted rests within the whole-through-sixty-fourth duration range.
  [[nodiscard]] Result normalize(Rational target_length);

  // Structural, intra-voice tie check; see validate_ties in
  // notation_event.hpp.
  [[nodiscard]] Result validate() const;

  [[nodiscard]] bool operator==(const VoiceContent&) const = default;

 private:
  std::vector<VoiceEvent> events_;
};

// Decomposes a strictly positive whole-note `length` into the fewest plain
// (non-tuplet) Rests, greedily choosing the largest available base-value
// and dot-count combination that fits the remainder at each step, so
// automatic rests read as idiomatic notation rather than one arbitrary
// fraction. Fails if `length` is not strictly positive, or if some
// remainder is finer than the smallest representable unit (a plain
// sixty-fourth note) — e.g. a gap left by a tuplet whose remainder is not
// dyadic.
[[nodiscard]] std::optional<std::vector<Rest>> decompose_rest(Rational length);

}  // namespace graphscore
