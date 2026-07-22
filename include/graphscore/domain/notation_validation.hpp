// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <graphscore/core/graphscore_core.hpp>
#include <graphscore/domain/track.hpp>
#include <graphscore/domain/voice_content.hpp>

namespace graphscore {

// Machine-readable classification of a notation referential validation
// finding. This focused validator only covers the checks the "Notation
// model" deliverable calls out (ties, slurs, hairpins, tuplets, beams,
// pedal spans, articulation conflicts); the general incremental/complete
// ValidationService with severities is a later phase.
enum class NotationDiagnosticCode : std::uint8_t {
  kDanglingTie = 0,
  kTiePitchMismatch,
  kConflictingDurationArticulation,
  kHairpinDanglingEndpoint,
  kHairpinNotOrdered,
  kSlurDanglingEndpoint,
  kSlurNotOrdered,
  kPedalSpanNotOrdered,
  kPedalSpanOutOfRange,
  kIncompleteTupletGroup,
  kInvalidBeamOverride,
};

// One referential validation finding: the offending NotationEntityId (the
// tied note/notehead's owning event, the span's own id, or the first event
// of an incomplete tuplet run), a machine-readable code, and a short
// human-facing message.
struct NotationDiagnostic {
  NotationEntityId       entity_id;
  NotationDiagnosticCode code;
  std::string            message;

  [[nodiscard]] bool operator==(const NotationDiagnostic&) const = default;
};

// Focused referential validator over a single voice's events, dynamics,
// hairpins, slurs, beam overrides, and grace groups. Does not check
// rhythmic completeness (see VoiceContent::check_complete) and does not
// check pedal spans (they are stave-scoped; see validate_pedal_spans).
//
// Checks performed:
//   - Ties: every tied Note/ChordNote is immediately followed by an event
//     sounding the same pitch (see validate_ties in notation_event.hpp;
//     this reports one diagnostic per offending tie instead of a single
//     Result).
//   - Articulation conflicts: at most one of staccato/staccatissimo/tenuto
//     per event (see is_duration_articulation).
//   - Slurs/hairpins: both endpoints must resolve to events in this voice,
//     and the start event must strictly precede the end event.
//   - Tuplets: for every maximal run of adjacent events sharing the exact
//     same TupletRatio, the run's summed resolved (already tuplet-scaled)
//     length must be an exact whole-number multiple of the plain,
//     un-tupleted, undotted length of the run's first event's base note
//     value. This is exactly the tuplet's own "N in the time of M"
//     contract: e.g. three tuplet eighths (ratio 3:2) resolve to 1/4,
//     which is exactly 2 plain eighths (1/8 each) — a whole multiple. Two
//     tuplet eighths alone resolve to 1/6, which is not a whole multiple
//     of a plain eighth, so the run is flagged as an incomplete/truncated
//     tuplet group.
//   - Beam overrides: every referenced event must exist in this voice, be
//     beamable (event_is_beamable), and the full set of referenced events
//     must occupy a contiguous, adjacent run in voice order.
[[nodiscard]] std::vector<NotationDiagnostic> validate_voice_references(
    const VoiceContent& voice);

// Focused referential validator for one stave's pedal spans: each span's
// start must strictly precede its end, and both must fall within the
// stave's node timeline range [0, node_end).
[[nodiscard]] std::vector<NotationDiagnostic> validate_pedal_spans(
    const std::vector<PedalSpan>& spans, Rational node_end);

// Runs validate_voice_references over every voice of every stave in
// `lane`, and validate_pedal_spans over every stave's pedal spans (bounded
// by `node_end`), concatenating every diagnostic produced. Voice rhythmic
// completeness is intentionally out of scope (see
// VoiceContent::check_complete), as is anything not enumerated on
// validate_voice_references/validate_pedal_spans above.
[[nodiscard]] std::vector<NotationDiagnostic> validate_lane_references(
    const TrackLane& lane, Rational node_end);

}  // namespace graphscore
