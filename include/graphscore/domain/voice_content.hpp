// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
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
  // unchanged, if `event` holds a Chord with fewer than two notes, or
  // if its NotationEntityId duplicates an id already present in the voice.
  [[nodiscard]] Result append(VoiceEvent event);

  // Returns the index of the event whose start position exactly equals
  // `position`, or std::nullopt if no event starts there. An empty voice
  // returns std::nullopt for every position including Rational(0).
  [[nodiscard]] std::optional<std::size_t> find_event_index_at(
      Rational position) const;

  // Inserts `event` at `position`, which must be an exact event boundary
  // (the start of an existing event), or total_length() (an append).
  //
  // Instead of shifting later events right, the insertion consumes
  // exactly `event`'s duration from the *contiguous Rest coverage*
  // beginning at `position` — consuming whole rests and decomposing the
  // final Rest remainder as needed.  Insertion before a sounding event
  // (Note/Chord) is rejected.  Insertion at total_length() is a plain
  // append (no consumption).  All later sounding-event onsets are
  // preserved.
  //
  // Fails, leaving the voice unchanged, if `position` is not a boundary,
  // if the boundary event is sounding, if contiguous Rest coverage is
  // insufficient to cover `event`'s duration, if `event` holds a Chord
  // with fewer than two notes, if its NotationEntityId duplicates an id
  // already present in the voice, if the new total length would exceed
  // `target_length`, or if normalization fails.
  [[nodiscard]] Result insert_event(Rational position, VoiceEvent event,
                                    Rational target_length);

  // Removes the event starting at `position`: replaces it with normalized
  // Rests of the same duration at the same position, preserving every
  // later event's onset.  The voice is then normalized to `target_length`.
  // Fails, leaving the voice unchanged, if no event starts at `position`
  // or if normalization fails.
  [[nodiscard]] Result remove_event(Rational position, Rational target_length);

  // Replaces the event starting at `position` with `event` and normalizes
  // the voice to `target_length`.
  //
  // Duration contraction (new_dur < old_dur): inserts `old_dur - new_dur`
  // worth of normalized Rests immediately after the replacement, so every
  // later event's onset is preserved.
  //
  // Duration expansion (new_dur > old_dur): consumes immediately
  // following Rest coverage greedily, splitting the final consumed Rest's
  // remainder if needed.  The original Rest ID is preserved on surviving
  // remainders when possible.
  //
  // Fails, leaving the voice unchanged, if no event starts at `position`,
  // if `event` holds a Chord with fewer than two notes, if its
  // NotationEntityId duplicates an id held by another event (reusing the
  // target event's own id is permitted), if the new total length would
  // exceed `target_length`, or if normalization fails.
  [[nodiscard]] Result replace_event(Rational position, VoiceEvent event,
                                     Rational target_length);

  void clear() noexcept { events_.clear(); }

  [[nodiscard]] const std::vector<DynamicMarking>& dynamics() const noexcept {
    return dynamics_;
  }

  [[nodiscard]] Result add_dynamic(DynamicMarking marking);

  [[nodiscard]] Result remove_dynamic(NotationEntityId id);

  [[nodiscard]] const std::vector<Hairpin>& hairpins() const noexcept {
    return hairpins_;
  }

  [[nodiscard]] Result add_hairpin(Hairpin hairpin);

  [[nodiscard]] Result remove_hairpin(NotationEntityId id);

  [[nodiscard]] const std::vector<Slur>& slurs() const noexcept {
    return slurs_;
  }

  [[nodiscard]] Result add_slur(Slur slur);

  [[nodiscard]] Result remove_slur(NotationEntityId id);

  [[nodiscard]] const std::vector<BeamOverride>& beam_overrides()
      const noexcept {
    return beam_overrides_;
  }

  [[nodiscard]] Result add_beam_override(BeamOverride override);

  [[nodiscard]] Result remove_beam_override(NotationEntityId id);

  [[nodiscard]] const std::vector<GraceGroup>& grace_groups() const noexcept {
    return grace_groups_;
  }

  [[nodiscard]] Result add_grace_group(GraceGroup group);

  [[nodiscard]] Result remove_grace_group(NotationEntityId id);

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
  //
  // Transactional: builds a complete temp vector and swaps only on
  // success; on failure events_ is unchanged.
  [[nodiscard]] Result normalize(Rational target_length);

  // Structural, intra-voice tie check; see validate_ties in
  // notation_event.hpp.
  [[nodiscard]] Result validate() const;

  [[nodiscard]] bool operator==(const VoiceContent&) const = default;

 private:
  // True if `id` appears as an event id or in any marking collection.
  [[nodiscard]] bool marking_id_exists(NotationEntityId id) const;

  // True if `id` appears in any marking collection (dynamics, hairpins,
  // slurs, beam overrides, grace groups), ignoring events.  Used by
  // replace_event so that self-id replacement is still rejected when
  // the target event's id collides with a marking.
  [[nodiscard]] bool marking_only_id_exists(NotationEntityId id) const;

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
