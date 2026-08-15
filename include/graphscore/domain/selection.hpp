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

struct RestItem {
  NodeId           node;
  TrackId          track;
  StaveId          stave;
  Voice            voice;
  NotationEntityId entity;

  [[nodiscard]] bool operator==(const RestItem&) const = default;
};

// Which visible marking a MarkingItem names.  Every marking a reader can
// see is independently selectable and deletable, so this covers the four
// marking records that carry their own NotationEntityId (dynamic, hairpin,
// slur, pedal span) as well as the three that do not exist as records at
// all (articulation, tie, tuplet number) and are addressed by composite
// key instead — see MarkingItem.
//
// BeamOverride is deliberately absent: it has an id, but the engraver
// emits no hit geometry for it (beams are bare LineCommands), so it cannot
// be resolved from a pointer position.  Adding it here without engraving
// support would make a selection that the editor can never produce or
// indicate.
enum class MarkingKind : std::uint8_t {
  kDynamic = 0,
  kHairpin,
  kSlur,
  kPedalSpan,
  kArticulation,
  kTie,
  kTuplet,
};

// One selected marking, addressed by an *anchor entity id plus a kind
// discriminator* rather than by an id of its own.
//
// Articulations (a plain Articulation in Note/Chord::articulations), ties
// (a plain bool tied_to_next on Note/ChordNote) are values, not identified
// records. Tuplets carry a stable TupletGroupId on every member event, but the
// selected visible bracket remains anchored to the group's first event. The
// composite selection key is unambiguous: an event cannot carry the same
// articulation twice (enforced by validate_voice_references), a note has at
// most one tie to the following event, and a tuplet group has exactly one
// first event.
//
// Anchor semantics per kind:
//   kDynamic, kHairpin, kSlur — the marking record's own id, in the
//     addressed voice's dynamics()/hairpins()/slurs().
//   kPedalSpan               — the PedalSpan's own id.  Pedal is scoped
//     per stave, never per voice (TrackLane::add_pedal_span), so `voice`
//     is disengaged for this kind and engaged for every other one; that
//     keeps one pedal span from having four equally valid addresses, which
//     would defeat MarkingSet's deduplication.
//   kArticulation            — the *parent event's* id (a top-level Note
//     or Chord; articulations belong to the chord column, not to a single
//     ChordNote), plus `articulation` naming which one.  `articulation` is
//     engaged for this kind and disengaged for every other one.
//   kTie                     — the id of the Note or ChordNote whose
//     tied_to_next is set.  A tie is drawn from its origin notehead, and a
//     chord ties note-by-note, so the origin is the only stable anchor.
//     This admits an origin whose tie is never painted (target missing or
//     pitch mismatch), which is safe: validate_voice_references already
//     flags that project state, and the Phase 2 resolver works from
//     geometry, so it can never produce such an item.
//   kTuplet                  — the id of the *first event of the stable
//     tuplet group*, which is where the bracket and number are drawn.
//
// The shape rules above (`voice`/`articulation` engaged exactly when the
// kind requires it) are intrinsic and enforced by MarkingSet::create, like
// ArbitraryRangeItem's span ordering; validate_selection then checks the
// anchor against the project.
struct MarkingItem {
  NodeId  node;
  TrackId track;
  StaveId stave;
  // Engaged for every kind except kPedalSpan (stave-scoped).
  std::optional<Voice> voice;
  MarkingKind          kind = MarkingKind::kDynamic;
  NotationEntityId     anchor;
  // Engaged if and only if kind == kArticulation.
  std::optional<Articulation> articulation;

  [[nodiscard]] bool operator==(const MarkingItem&) const = default;
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

class RestSet {
 public:
  RestSet() = delete;

  [[nodiscard]] static std::optional<RestSet> create(
      std::vector<RestItem> items);

  [[nodiscard]] const std::vector<RestItem>& items() const noexcept {
    return items_;
  }

  [[nodiscard]] bool operator==(const RestSet&) const = default;

 private:
  explicit RestSet(std::vector<RestItem> items) : items_(std::move(items)) {}

  std::vector<RestItem> items_;
};

class MarkingSet {
 public:
  MarkingSet() = delete;

  // Each item's discriminator fields must match its kind (intrinsic):
  // `articulation` engaged iff kind == kArticulation, `voice` engaged iff
  // kind != kPedalSpan.  Empty vectors and duplicate items are also
  // rejected.
  [[nodiscard]] static std::optional<MarkingSet> create(
      std::vector<MarkingItem> items);

  [[nodiscard]] const std::vector<MarkingItem>& items() const noexcept {
    return items_;
  }

  [[nodiscard]] bool operator==(const MarkingSet&) const = default;

 private:
  explicit MarkingSet(std::vector<MarkingItem> items)
      : items_(std::move(items)) {}

  std::vector<MarkingItem> items_;
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
// notehead/chord selection and clipboard identity remapping.  Rest::id and
// every marking record's own id (DynamicMarking, Hairpin, Slur, PedalSpan)
// are load-bearing for exactly the same reason now that RestSet and
// MarkingSet address them; and because MarkingItem names articulations,
// ties and tuplets by (anchor event, kind), M03 must also preserve
// articulation *order-independent membership*, tied_to_next, and stable
// tuplet group identities and boundaries.
//
// New arms are appended rather than grouped with their neighbours so that
// the existing alternatives keep their variant indices.
using Selection =
    std::variant<NoteheadSet, ChordSet, FullMeasureSet, ArbitraryRangeSet,
                 NodeSet, ConnectorSet, InsertionCaretSet, RestSet, MarkingSet>;

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
  // The anchor entity exists and is the right kind, but the marking the
  // item names is not on it: a tie whose tied_to_next is clear, an
  // articulation absent from the event's list, or a tuplet on an event
  // that carries no tuplet ratio (or that is not its run's first event).
  // Distinct from kWrongEntityKind because it is the code a stale
  // selection produces after an edit deletes the marking, which callers
  // recover from by dropping the item rather than by re-resolving it.
  kMarkingNotPresent,
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
//   rest          — node/track/stave/lane exists; track is active;
//                   entity resolves to a top-level Rest (not Note,
//                   Chord, ChordNote, or GraceNote)
//   marking       — node/track/stave/lane exists; track is active; the
//                   anchor exists in the addressed scope (kEntityNotFound
//                   otherwise) and is the kind the MarkingKind requires
//                   (kWrongEntityKind otherwise); for kArticulation,
//                   kTie and kTuplet the named marking is actually
//                   present on that anchor (kMarkingNotPresent
//                   otherwise).  The kind/discriminator shape rules are
//                   intrinsic and enforced by MarkingSet::create, so no
//                   diagnostic exists for them.
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
