// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include <graphscore/core/graphscore_core.hpp>
#include <graphscore/domain/project.hpp>

namespace graphscore {

enum class DiagnosticSeverity : std::uint8_t {
  kError = 0,
  kWarning,
};

enum class DiagnosticCode : std::uint8_t {
  kNilUuid = 0,
  kDuplicateUuid,
  kRhythmIncomplete,
  kMissingTrackLane,
  kUnexpectedTrackLane,
  kUnexpectedStave,
  kMissingStave,
  kMissingClefLane,
  kUnexpectedClefLane,
  kClefDefaultMismatch,
  kIllegalTimeSignature,
  kIllegalKeySignature,
  kIllegalClefChange,
  kIllegalTempoLane,
  kIllegalPickdown,
  kDanglingStartNode,
  kDanglingGraphDestination,
  kDanglingEventBinding,
  kMissingEventListener,
  kEventListenerTypeMismatch,
  kInvalidListenerCapacity,
  kDuplicateEventName,
  kExportDestinationRequired,
  kVerticalEventRequired,
  kInvalidRandomWeightTotal,
  kDanglingTie,
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
};

// The variant preserves the strong type of every heterogeneous stable entity
// identity. A diagnostic never reduces an id to an untyped UUID or string.
using DiagnosticEntity = std::variant<ProjectId, TrackId, StaveId, NodeId,
                                      ConnectorId, EventId, NotationEntityId>;

// Cache ownership for one diagnostic occurrence. `entity` is the strongest
// useful subject/context id; `node` distinguishes node-local occurrences and
// `track` distinguishes lane/layout occurrences in malformed duplicate
// namespaces.
struct DiagnosticScope {
  DiagnosticEntity       entity;
  std::optional<NodeId>  node;
  std::optional<TrackId> track;

  [[nodiscard]] bool operator==(const DiagnosticScope&) const = default;
};

struct Diagnostic {
  DiagnosticEntity       entity;
  std::optional<NodeId>  node;
  std::optional<TrackId> track;
  DiagnosticSeverity     severity = DiagnosticSeverity::kError;
  DiagnosticCode         code;
  std::string            text;

  [[nodiscard]] bool operator==(const Diagnostic&) const = default;
};

// Canonical production ordering for diagnostics. Exposed so caches and tests
// can preserve exactly the same deterministic order as ValidationService.
struct DiagnosticLess {
  [[nodiscard]] bool operator()(const Diagnostic& lhs,
                                const Diagnostic& rhs) const;
};

// Canonical cache-invalidation match for one changed stable identity. In
// addition to the diagnostic subject, NodeId and TrackId changes match their
// typed occurrence context. Variant equality prevents UUID bytes shared by a
// different ID kind from matching.
[[nodiscard]] bool diagnostic_matches_invalidation(
    const Diagnostic& diagnostic, const DiagnosticEntity& invalidated_entity);

struct UnknownValidationChange {
  [[nodiscard]] bool operator==(const UnknownValidationChange&) const = default;
};

// Scoped connector/notation changes identify the node that owned the edited
// occurrence immediately before the edit. The prior node is selected together
// with every current owner and current cross-entity dependent. This preserves
// positional and aggregate semantics that cannot be reconstructed from a
// surviving duplicate ID after one occurrence is removed. The prior node must
// still exist; otherwise validation safely falls back to a complete pass.
// Callers that did not capture ownership before editing must use the bare ID
// alternatives below, which always request the safe complete fallback.
class ConnectorOccurrenceChange {
 public:
  explicit ConnectorOccurrenceChange(ConnectorId id, NodeId prior_node)
      : id_(id), prior_node_(prior_node) {}

  [[nodiscard]] ConnectorId id() const noexcept { return id_; }

  [[nodiscard]] NodeId prior_node() const noexcept { return prior_node_; }

  [[nodiscard]] bool operator==(const ConnectorOccurrenceChange&) const =
      default;

 private:
  ConnectorId id_;
  NodeId      prior_node_;
};

class NotationOccurrenceChange {
 public:
  explicit NotationOccurrenceChange(NotationEntityId id, NodeId prior_node)
      : id_(id), prior_node_(prior_node) {}

  [[nodiscard]] NotationEntityId id() const noexcept { return id_; }

  [[nodiscard]] NodeId prior_node() const noexcept { return prior_node_; }

  [[nodiscard]] bool operator==(const NotationOccurrenceChange&) const =
      default;

 private:
  NotationEntityId id_;
  NodeId           prior_node_;
};

// ProjectId and UnknownValidationChange have global impact. Track, stave, and
// event changes may also expand globally. Resolved NodeId changes and scoped
// occurrence changes expand to owning/dependent nodes plus the project-wide
// UUID namespace; they do not run every unrelated node's semantic checks.
// Bare ConnectorId and NotationEntityId changes are intentionally complete
// fallbacks because post-edit state cannot reveal a removed occurrence's
// former owner when another occurrence with the same ID survives.
using ValidationChange =
    std::variant<UnknownValidationChange, ProjectId, TrackId, StaveId, NodeId,
                 ConnectorId, EventId, NotationEntityId,
                 ConnectorOccurrenceChange, NotationOccurrenceChange>;

struct ValidationReport {
  // Always sorted by severity, code, entity kind/UUID, node/track context,
  // then text, with exact duplicate findings removed.
  std::vector<Diagnostic>      diagnostics;
  std::vector<DiagnosticScope> affected_scopes;
  // Entity-wide cache invalidation for changed stable IDs. Before inserting
  // diagnostics, a caller applying an incremental report erases cached
  // diagnostics whose exact scope is in affected_scopes OR for which
  // diagnostic_matches_invalidation returns true for an entry in
  // invalidated_entities. This removes stale subjects and stale node/track
  // context occurrences whose former ownership no longer exists without
  // broadening resolved local validation to the whole project.
  std::vector<DiagnosticEntity> invalidated_entities;
  bool                          complete = false;

  [[nodiscard]] bool operator==(const ValidationReport&) const = default;
};

class ValidationService {
 public:
  [[nodiscard]] ValidationReport validate_complete(
      const Project& project) const;

  // Returns exactly the current findings for affected_scopes after
  // conservative dependency expansion. Callers first erase cached findings
  // matching affected_scopes or invalidated_entities, then insert diagnostics
  // and order them with DiagnosticLess. A complete report replaces the whole
  // cache. Empty, unknown, unresolved, or global-impact changes fall back to
  // complete validation. Bare connector/notation IDs are safe complete
  // fallbacks. Their occurrence-change forms use a scoped path plus typed
  // subject/context invalidation for the changed stable ID. Cache replacement
  // therefore erases the former and current selected scopes before inserting
  // the report, even when a duplicate ID survives in a different node.
  [[nodiscard]] ValidationReport validate_incremental(
      const Project& project, std::span<const ValidationChange> changes) const;
};

}  // namespace graphscore
