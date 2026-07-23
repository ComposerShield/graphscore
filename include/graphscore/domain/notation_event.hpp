// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <variant>
#include <vector>

#include <graphscore/core/graphscore_core.hpp>
#include <graphscore/domain/articulation.hpp>  // StemDirection

namespace graphscore {

// A single sounding note: a spelling, a duration, and whether it ties into
// the immediately following voice event's matching pitch. A tie never
// stores an explicit target reference: voice content is an ordered
// sequence occupying contiguous time, so "the following event" is always
// unambiguous. validate_ties() below checks that a tied note's pitch
// actually matches something in that event. `articulations` may hold
// several markings at once (e.g. accent + staccato); mutually-exclusive
// duration articulations are flagged by the referential validator, not
// prevented here. `stem` is a manual auto/up/down override.
struct Note {
  NotationEntityId          id;
  SpelledPitch              pitch;
  Duration                  duration;
  bool                      tied_to_next = false;
  std::vector<Articulation> articulations;
  StemDirection             stem = StemDirection::kAuto;

  [[nodiscard]] bool operator==(const Note&) const = default;
};

// One notehead within a Chord: its spelling and its own tie state. A chord
// ties note-by-note, so two noteheads in the same chord may have different
// tie states.
struct ChordNote {
  SpelledPitch pitch;
  bool         tied_to_next = false;

  [[nodiscard]] bool operator==(const ChordNote&) const = default;
};

// Two or more simultaneous noteheads sharing one duration. Articulations
// and the stem override apply to the whole chord, matching how they are
// notated: one marking/stem per chord column, not per notehead.
struct Chord {
  NotationEntityId          id;
  Duration                  duration;
  std::vector<ChordNote>    notes;
  std::vector<Articulation> articulations;
  StemDirection             stem = StemDirection::kAuto;

  [[nodiscard]] bool operator==(const Chord&) const = default;
};

struct Rest {
  NotationEntityId id;
  Duration         duration;

  [[nodiscard]] bool operator==(const Rest&) const = default;
};

// A voice's content is an ordered sequence of these three event kinds.
using VoiceEvent = std::variant<Note, Chord, Rest>;

[[nodiscard]] Note make_note(SpelledPitch pitch, Duration duration,
                             bool                      tied_to_next  = false,
                             std::vector<Articulation> articulations = {},
                             StemDirection stem = StemDirection::kAuto);

// Precondition: notes.size() >= 2. Chord's own invariant ("2+ noteheads")
// is enforced where a Chord is added to a VoiceContent, not at this plain
// aggregate's construction, so it can still be freely copied/mutated like
// the other event kinds.
[[nodiscard]] Chord make_chord(Duration duration, std::vector<ChordNote> notes,
                               std::vector<Articulation> articulations = {},
                               StemDirection stem = StemDirection::kAuto);

[[nodiscard]] Rest make_rest(Duration duration);

[[nodiscard]] const Duration& event_duration(const VoiceEvent& event);

[[nodiscard]] NotationEntityId event_id(const VoiceEvent& event);

// True if `event` sounds `pitch`: a Note with a matching pitch, or a Chord
// with a matching notehead. Always false for a Rest.
[[nodiscard]] bool event_sounds_pitch(const VoiceEvent&   event,
                                      const SpelledPitch& pitch);

// The event's articulation set: a Note's or Chord's `articulations`, or
// nullptr for a Rest (which carries none).
[[nodiscard]] const std::vector<Articulation>* event_articulations(
    const VoiceEvent& event);

// The event's stem override: a Note's or Chord's `stem`, or
// StemDirection::kAuto for a Rest (which has no stem to override).
[[nodiscard]] StemDirection event_stem(const VoiceEvent& event);

// True if `event` is a Note or Chord whose Duration base value is eighth
// note or shorter. Rests are never considered beamable here: this model
// scopes manual beam overrides to sounding events only.
[[nodiscard]] bool event_is_beamable(const VoiceEvent& event);

// Structural, intra-voice tie check: every event whose tied_to_next flag
// (Note) or per-notehead tied_to_next flag (Chord) is set must be
// immediately followed by an event that sounds a matching pitch. A tied
// note/notehead with no following event, or whose following event does not
// sound the same pitch, is representable but flagged invalid here — the
// full cross-entity referential validator (slurs, hairpins, spans) is a
// later increment; this check covers only the tie link intrinsic to the
// rhythm model itself.
[[nodiscard]] Result validate_ties(const std::vector<VoiceEvent>& events);

}  // namespace graphscore
