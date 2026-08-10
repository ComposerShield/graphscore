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
  kSlurAttachedToRest,
  kPedalSpanNotOrdered,
  kPedalSpanOutOfRange,
  kIncompleteTupletGroup,
  kInvalidBeamOverride,
  kDynamicDanglingReference,
  kGraceGroupPrincipalNotSounding,
  kDuplicateArticulation,
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
[[nodiscard]] std::vector<NotationDiagnostic> validate_voice_references(
    const VoiceContent& voice);

// Focused referential validator for one stave's pedal spans: each span's
// start must strictly precede its end, and both must fall within the
// stave's node timeline range [0, node_end).
[[nodiscard]] std::vector<NotationDiagnostic> validate_pedal_spans(
    const std::vector<PedalSpan>& spans, Rational node_end);

// Runs validate_voice_references over every voice of every stave in
// `lane`, and validate_pedal_spans over every stave's pedal spans.
[[nodiscard]] std::vector<NotationDiagnostic> validate_lane_references(
    const TrackLane& lane, Rational node_end);

// Domain-owned validation state for consumers of VoiceDelta. Validation and
// diagnostic ordering remain exactly equivalent to validate_voice_references;
// validation bookkeeping is not part of the engraving-rebuild locality
// guarantee and may scan retained voice content.
class VoiceValidationState {
 public:
  VoiceValidationState() = default;

  // Full rebuild from a voice's complete state.  Scans all events and
  // references.  The returned diagnostics are equivalent to
  // validate_voice_references(voice).
  [[nodiscard]] std::vector<NotationDiagnostic> rebuild(
      const VoiceContent& voice);

  // Incremental apply.  `voice` is the current VoiceContent (after
  // mutations).  `delta` is the delta since the last rebuild or apply.
  // Returns current diagnostics and IDs of semantic event/reference records
  // directly revalidated. visited_ids is not a complete CPU-work counter.
  struct ApplyResult {
    std::vector<NotationDiagnostic> diagnostics;
    std::vector<NotationEntityId>   visited_ids;
  };

  [[nodiscard]] ApplyResult apply(const VoiceContent& voice,
                                  const VoiceDelta&   delta);

  // The complete diagnostics retained from the last rebuild or apply.
  [[nodiscard]] const std::vector<NotationDiagnostic>& diagnostics() const {
    return diagnostics_;
  }

 private:
  std::vector<NotationDiagnostic> diagnostics_;
};

}  // namespace graphscore
