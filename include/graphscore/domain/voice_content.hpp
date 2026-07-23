// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <optional>
#include <utility>
#include <vector>

#include <graphscore/core/graphscore_core.hpp>
#include <graphscore/domain/notation_event.hpp>
#include <graphscore/domain/notation_markings.hpp>

namespace graphscore {

// One voice's ordered, contiguous-time content: a sequence of notes,
// chords, and rests, plus the dynamics/hairpins/slurs/beam overrides/grace
// groups attached within it. VoiceContent does not itself know the node's
// total musical length; normalize() and check_complete() take that length
// as an explicit Rational parameter (typically NodeTimeline::node_end())
// so this type never duplicates timeline state. Markings are appended
// without validating their entity-id references; validate_voice_references
// (notation_validation.hpp) is the focused referential check that runs
// after edits.
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

  [[nodiscard]] const std::vector<DynamicMarking>& dynamics() const noexcept {
    return dynamics_;
  }

  void add_dynamic(DynamicMarking marking) { dynamics_.push_back(marking); }

  [[nodiscard]] const std::vector<Hairpin>& hairpins() const noexcept {
    return hairpins_;
  }

  void add_hairpin(Hairpin hairpin) { hairpins_.push_back(hairpin); }

  [[nodiscard]] const std::vector<Slur>& slurs() const noexcept {
    return slurs_;
  }

  void add_slur(Slur slur) { slurs_.push_back(slur); }

  [[nodiscard]] const std::vector<BeamOverride>& beam_overrides()
      const noexcept {
    return beam_overrides_;
  }

  void add_beam_override(BeamOverride override) {
    beam_overrides_.push_back(std::move(override));
  }

  [[nodiscard]] const std::vector<GraceGroup>& grace_groups() const noexcept {
    return grace_groups_;
  }

  void add_grace_group(GraceGroup group) {
    grace_groups_.push_back(std::move(group));
  }

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
  std::vector<VoiceEvent>     events_;
  std::vector<DynamicMarking> dynamics_;
  std::vector<Hairpin>        hairpins_;
  std::vector<Slur>           slurs_;
  std::vector<BeamOverride>   beam_overrides_;
  std::vector<GraceGroup>     grace_groups_;
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
