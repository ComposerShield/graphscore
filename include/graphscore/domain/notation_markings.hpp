// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <vector>

#include <graphscore/core/graphscore_core.hpp>

namespace graphscore {

// A point dynamic marking (e.g. "mf") attached to one event in a voice.
// Dynamics-to-note-on-velocity mapping is
// graphscore/core/playback_mapping.hpp's velocity_for_dynamic(); this is
// structure only.
struct DynamicMarking {
  NotationEntityId id;
  NotationEntityId at_event;
  Dynamic          value = Dynamic::kMf;

  [[nodiscard]] bool operator==(const DynamicMarking&) const = default;
};

enum class HairpinDirection : std::uint8_t {
  kCrescendo = 0,
  kDiminuendo,
};

// A crescendo/diminuendo span from one event to a later event in the same
// voice, referencing both endpoints by NotationEntityId.
// Hairpin-to-note-on-velocity mapping is
// graphscore/core/playback_mapping.hpp's interpolate_hairpin_velocity();
// this is structure only.
struct Hairpin {
  NotationEntityId id;
  NotationEntityId start_event;
  NotationEntityId end_event;
  HairpinDirection direction = HairpinDirection::kCrescendo;

  [[nodiscard]] bool operator==(const Hairpin&) const = default;
};

// A legato span from one note/chord to a later note/chord in the same
// voice, referencing both endpoints by NotationEntityId. Slur-to-legato-
// overlap MIDI behavior is graphscore/core/playback_mapping.hpp's
// legato_sounded_duration(); this is structure only.
struct Slur {
  NotationEntityId id;
  NotationEntityId start_event;
  NotationEntityId end_event;

  [[nodiscard]] bool operator==(const Slur&) const = default;
};

// A manual override of automatic beaming across a run of beamable events
// (eighth note and shorter; see event_is_beamable in notation_event.hpp),
// referenced by NotationEntityId in voice order. kBreak forces a beam
// break that automatic beaming would not otherwise make; kJoin forces
// events that automatic beaming would not otherwise share a beam to join
// one. Beam geometry/rendering is out of scope; this records only which
// events the override affects.
struct BeamOverride {
  enum class Kind : std::uint8_t {
    kBreak = 0,
    kJoin,
  };

  NotationEntityId              id;
  Kind                          kind = Kind::kBreak;
  std::vector<NotationEntityId> events;

  [[nodiscard]] bool operator==(const BeamOverride&) const = default;
};

// A sustain-pedal (CC64) span over an exact whole-note [start, end) range.
// Scoped per stave, never per voice (see TrackLane::add_pedal_span): the
// pedal applies to everything sounding on that staff. CC64 emission and
// logical-OR ownership across overlapping spans/tails is Phase 6/7; this
// is structure only.
struct PedalSpan {
  NotationEntityId id;
  Rational         start;
  Rational         end;

  [[nodiscard]] bool operator==(const PedalSpan&) const = default;
};

// One grace note within a GraceGroup: its spelling, its notated (unstolen)
// duration, its acciaccatura/appoggiatura kind, and whether it is drawn
// slashed.
struct GraceNote {
  SpelledPitch  pitch;
  Duration      duration;
  GraceNoteType type    = GraceNoteType::kAppoggiatura;
  bool          slashed = false;

  [[nodiscard]] bool operator==(const GraceNote&) const = default;
};

// An ordered group of grace notes attached to a principal event
// (`principal_event`, a NotationEntityId of a Note or Chord in the same
// voice), conceptually stealing playback time from the sounded note that
// precedes the principal event. This models the attachment and note
// ordering only.
//
// The steal fraction/limits, multi-note division of the stolen time across
// `notes`, behavior when no sounded note precedes `principal_event`, and
// interaction with articulations are normative playback rules specified
// and implemented by graphscore/core/playback_mapping.hpp's
// grace_steal_durations(), and wired to this structure by
// graphscore/domain/notation_playback.hpp; this type itself remains
// structure only.
struct GraceGroup {
  NotationEntityId       id;
  NotationEntityId       principal_event;
  std::vector<GraceNote> notes;

  [[nodiscard]] bool operator==(const GraceGroup&) const = default;
};

[[nodiscard]] DynamicMarking make_dynamic_marking(NotationEntityId at_event,
                                                  Dynamic          value);

[[nodiscard]] Hairpin make_hairpin(NotationEntityId start_event,
                                   NotationEntityId end_event,
                                   HairpinDirection direction);

[[nodiscard]] Slur make_slur(NotationEntityId start_event,
                             NotationEntityId end_event);

[[nodiscard]] BeamOverride make_beam_override(
    BeamOverride::Kind kind, std::vector<NotationEntityId> events);

[[nodiscard]] PedalSpan make_pedal_span(Rational start, Rational end);

[[nodiscard]] GraceGroup make_grace_group(NotationEntityId principal_event,
                                          std::vector<GraceNote> notes);

}  // namespace graphscore
