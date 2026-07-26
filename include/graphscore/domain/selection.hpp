// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <graphscore/core/graphscore_core.hpp>
#include <graphscore/domain/node_timeline.hpp>

namespace graphscore {

class Project;

// ---- item types for each selection arm ----

struct NoteheadItem {
  NodeId           node;
  TrackId          track;
  StaveId          stave;
  Voice            voice;
  NotationEntityId entity;

  [[nodiscard]] bool operator==(const NoteheadItem&) const = default;
};

struct ChordItem {
  NodeId           node;
  TrackId          track;
  StaveId          stave;
  Voice            voice;
  NotationEntityId entity;

  [[nodiscard]] bool operator==(const ChordItem&) const = default;
};

struct FullMeasureItem {
  NodeId      node;
  TrackId     track;
  StaveId     stave;
  std::size_t measure_index = 0;

  [[nodiscard]] bool operator==(const FullMeasureItem&) const = default;
};

struct ArbitraryRangeItem {
  NodeId      node;
  TrackId     track;
  StaveId     stave;
  Voice       voice;
  MusicalSpan span;

  [[nodiscard]] bool operator==(const ArbitraryRangeItem&) const = default;
};

struct NodeItem {
  NodeId node;

  [[nodiscard]] bool operator==(const NodeItem&) const = default;
};

struct ConnectorItem {
  NodeId      node;
  ConnectorId connector;

  [[nodiscard]] bool operator==(const ConnectorItem&) const = default;
};

struct InsertionCaretItem {
  NodeId   node;
  TrackId  track;
  StaveId  stave;
  Voice    voice;
  Rational position;

  [[nodiscard]] bool operator==(const InsertionCaretItem&) const = default;
};

// ---- set types: each is non-empty, deduplicated ----

class NoteheadSet {
 public:
  NoteheadSet() = delete;

  [[nodiscard]] static std::optional<NoteheadSet> create(
      std::vector<NoteheadItem> items);

  [[nodiscard]] const std::vector<NoteheadItem>& items() const noexcept {
    return items_;
  }

  [[nodiscard]] bool operator==(const NoteheadSet&) const = default;

 private:
  explicit NoteheadSet(std::vector<NoteheadItem> items)
      : items_(std::move(items)) {}

  std::vector<NoteheadItem> items_;
};

class ChordSet {
 public:
  ChordSet() = delete;

  [[nodiscard]] static std::optional<ChordSet> create(
      std::vector<ChordItem> items);

  [[nodiscard]] const std::vector<ChordItem>& items() const noexcept {
    return items_;
  }

  [[nodiscard]] bool operator==(const ChordSet&) const = default;

 private:
  explicit ChordSet(std::vector<ChordItem> items) : items_(std::move(items)) {}

  std::vector<ChordItem> items_;
};

class FullMeasureSet {
 public:
  FullMeasureSet() = delete;

  [[nodiscard]] static std::optional<FullMeasureSet> create(
      std::vector<FullMeasureItem> items);

  [[nodiscard]] const std::vector<FullMeasureItem>& items() const noexcept {
    return items_;
  }

  [[nodiscard]] bool operator==(const FullMeasureSet&) const = default;

 private:
  explicit FullMeasureSet(std::vector<FullMeasureItem> items)
      : items_(std::move(items)) {}

  std::vector<FullMeasureItem> items_;
};

class ArbitraryRangeSet {
 public:
  ArbitraryRangeSet() = delete;

  // Each item's span must satisfy start <= end (intrinsic); empty
  // vectors and duplicate items are also rejected.
  [[nodiscard]] static std::optional<ArbitraryRangeSet> create(
      std::vector<ArbitraryRangeItem> items);

  [[nodiscard]] const std::vector<ArbitraryRangeItem>& items() const noexcept {
    return items_;
  }

  [[nodiscard]] bool operator==(const ArbitraryRangeSet&) const = default;

 private:
  explicit ArbitraryRangeSet(std::vector<ArbitraryRangeItem> items)
      : items_(std::move(items)) {}

  std::vector<ArbitraryRangeItem> items_;
};

class NodeSet {
 public:
  NodeSet() = delete;

  [[nodiscard]] static std::optional<NodeSet> create(
      std::vector<NodeItem> items);

  [[nodiscard]] const std::vector<NodeItem>& items() const noexcept {
    return items_;
  }

  [[nodiscard]] bool operator==(const NodeSet&) const = default;

 private:
  explicit NodeSet(std::vector<NodeItem> items) : items_(std::move(items)) {}

  std::vector<NodeItem> items_;
};

class ConnectorSet {
 public:
  ConnectorSet() = delete;

  [[nodiscard]] static std::optional<ConnectorSet> create(
      std::vector<ConnectorItem> items);

  [[nodiscard]] const std::vector<ConnectorItem>& items() const noexcept {
    return items_;
  }

  [[nodiscard]] bool operator==(const ConnectorSet&) const = default;

 private:
  explicit ConnectorSet(std::vector<ConnectorItem> items)
      : items_(std::move(items)) {}

  std::vector<ConnectorItem> items_;
};

class InsertionCaretSet {
 public:
  InsertionCaretSet() = delete;

  [[nodiscard]] static std::optional<InsertionCaretSet> create(
      std::vector<InsertionCaretItem> items);

  [[nodiscard]] const std::vector<InsertionCaretItem>& items() const noexcept {
    return items_;
  }

  [[nodiscard]] bool operator==(const InsertionCaretSet&) const = default;

 private:
  explicit InsertionCaretSet(std::vector<InsertionCaretItem> items)
      : items_(std::move(items)) {}

  std::vector<InsertionCaretItem> items_;
};

// The selection: exactly one homogeneous set of scoped items.  The variant
// arm enforces homogeneity; each set object enforces non-emptiness and
// deduplication.  This selection is a snapshot value — it validates only
// intrinsic structure.  Cross-reference validation (entity kind, active-
// track membership, measure bounds, connector ownership, caret legality) is
// the separate free function validate_selection below.
//
// Measure-index selections are invalidated by measure insertion/deletion
// (Phase 8h); consumers must revalidate after timeline edits.
//
// M03 persistence obligation: ChordNote::id and GraceNote::id (8f-i) are
// load-bearing — dropping or regenerating them silently breaks every
// notehead/chord selection and clipboard identity remapping.
using Selection =
    std::variant<NoteheadSet, ChordSet, FullMeasureSet, ArbitraryRangeSet,
                 NodeSet, ConnectorSet, InsertionCaretSet>;

// ---- validation ----

enum class SelectionDiagnosticCode : std::uint8_t {
  kNodeNotFound = 0,
  kTrackNotFound,
  kTrackArchived,
  kStaveNotFound,
  kLaneNotFound,
  kEntityNotFound,
  kWrongEntityKind,
  kMeasureIndexOutOfRange,
  kMeasureIndexInPickdown,
  kNoTimeline,
  kConnectorNotFound,
  kCaretPositionNotBoundary,
};

struct SelectionDiagnostic {
  std::size_t             item_index;
  SelectionDiagnosticCode code;
  std::string             message;

  [[nodiscard]] bool operator==(const SelectionDiagnostic&) const = default;
};

// Validates every item in `selection` against `project`.  Returns one
// diagnostic per failing item, or an empty vector if every item is valid.
// This function is deterministic and non-mutating: it reads project state
// only, never modifies it.
//
// Checks performed per arm:
//   notehead      — node/track/stave/lane exists; track is active;
//                   entity resolves to Note, ChordNote, or GraceNote
//   chord         — node/track/stave/lane exists; track is active;
//                   entity resolves to top-level Chord (not ChordNote,
//                   Rest, GraceNote, or non-chord entity)
//   full measure  — node/track/stave exists; track is active; lane
//                   exists; node has timeline; measure_index <
//                   measure_count() (kNoTimeline otherwise); pickdown
//                   indices rejected structurally (kMeasureIndexInPickdown)
//   range         — node/track/stave/lane exists; track is active;
//                   node has timeline; classify(span) invoked (pickdown
//                   ranges are accepted; MusicalSpan validity is
//                   intrinsic and enforced by ArbitraryRangeSet::create)
//   node          — node_id exists in project
//   connector     — node exists; connector is an input or output on
//                   that node (kConnectorNotFound otherwise)
//   caret         — node/track/stave/lane exists; track is active;
//                   position 0 is always valid; other positions must
//                   be an event boundary or TrackLane::total_length()
[[nodiscard]] std::vector<SelectionDiagnostic> validate_selection(
    const Project& project, const Selection& selection);

}  // namespace graphscore
