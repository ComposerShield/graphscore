// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include <graphscore/core/graphscore_core.hpp>
#include <graphscore/domain/notation_event.hpp>
#include <graphscore/domain/notation_markings.hpp>

namespace graphscore {

class NodeTimeline;

// Monotonic revision token for incremental consumers.
// A VoiceRevision advances on every successful mutation to a VoiceContent
// (or TrackLane). Consumers may snapshot the current value with
// capture_revision() and later call delta_since() to learn exactly what
// changed. The ring buffer holds kMaxTrackedRevisions (16) entries; a
// token more than 16 revisions behind is stale and delta_since() returns
// std::nullopt, requiring a full rebuild.
//
// Lineage (epoch) component: every VoiceRevision carries a unique lineage
// token generated at construction. When a VoiceContent (or TrackLane) is
// copy-assigned or move-assigned, the destination's revision tracking is
// reset to a fresh lineage, invalidating all prior tokens. Moving also resets
// the source to a fresh lineage, so tokens captured before its content was
// moved away require a full refresh. Comparison checks both fields so a
// pre-assignment token at {0} cannot accidentally compare equal to a newly
// assigned object also at {0}. Semantic equality
// (VoiceContent/TrackLane::operator==) intentionally excludes mutation-tracking
// state.
class VoiceRevision {
 public:
  VoiceRevision() : value_(0), lineage_(generate_lineage()) {}

  // Two VoiceRevisions compare equal only when both value and lineage
  // match. This ensures tokens from different lineages (e.g. before/after
  // copy assignment) are never confused.
  [[nodiscard]] bool operator==(const VoiceRevision& other) const {
    return value_ == other.value_ && lineage_ == other.lineage_;
  }

  [[nodiscard]] bool operator!=(const VoiceRevision& other) const {
    return !(*this == other);
  }

 private:
  friend class VoiceContent;
  friend class TrackLane;

  explicit VoiceRevision(std::uint32_t value, std::uint32_t lineage) noexcept
      : value_(value), lineage_(lineage) {}

  // Generates a globally unique lineage token (monotonic process-wide).
  [[nodiscard]] static std::uint32_t generate_lineage() noexcept {
    static std::atomic<std::uint32_t> next{1};
    return next.fetch_add(1, std::memory_order_relaxed);
  }

  std::uint32_t value_{0};
  std::uint32_t lineage_{0};
};

// One operation on a reference family.  Carries the full record data so
// consumers never need to scan source collections to discover what
// changed.  kRemove carries only the id (record is empty); kAdd and
// kUpdate carry the full record.
enum class RefOpKind : std::uint8_t {
  kAdd = 0,
  kRemove,
  kUpdate,
};

template <typename Record>
struct RefOp {
  RefOpKind        kind = RefOpKind::kAdd;
  NotationEntityId id;
  Record           record;  // valid for kAdd/kUpdate; default for kRemove
};

// Aggregated mutation record since a given revision.  If `full_reset` is
// true, the consumer must perform a complete rebuild (stale cursor, lineage
// change, copy/assignment, move-from, or a replace_event mutation that
// changed the top-level event ID, which may remap any of the five reference
// families).  Otherwise each per-family operation vector carries exactly the
// add/remove/update records in mutation order.  `event_reorder` is true when
// the event vector's size or order changed (requiring index-per-measure
// rebuilds), false when only in-place event content or references changed.
struct VoiceDelta {
  std::vector<NotationEntityId>      changed_event_ids;
  std::vector<RefOp<DynamicMarking>> dynamic_ops;
  std::vector<RefOp<Hairpin>>        hairpin_ops;
  std::vector<RefOp<Slur>>           slur_ops;
  std::vector<RefOp<BeamOverride>>   beam_override_ops;
  std::vector<RefOp<GraceGroup>>     grace_group_ops;
  bool                               event_reorder = false;
  bool                               full_reset    = false;
};

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

  // Copy/move assignment reset mutation tracking on the destination so that
  // any pre-existing tokens are invalidated (lineage change). Move construction
  // and move assignment also reset tracking on the moved-from source, making
  // tokens captured before its content was moved stale for that source.
  VoiceContent(const VoiceContent& other);
  VoiceContent& operator=(const VoiceContent& other);
  VoiceContent(VoiceContent&& other) noexcept;
  VoiceContent& operator=(VoiceContent&& other) noexcept;

  [[nodiscard]] const std::vector<VoiceEvent>& events() const noexcept {
    return events_;
  }

  // Appends `event` to the end of the voice. A legacy ratio-bearing event
  // without an explicit group identity inherits the immediately preceding
  // equal-ratio group's identity, or starts a fresh group. Fails, leaving the
  // voice unchanged, if `event` holds a Chord with fewer than two notes, or if
  // its NotationEntityId duplicates an id already present in the voice.
  [[nodiscard]] Result append(VoiceEvent event);

  // Returns the index of the event whose start position exactly equals
  // `position`, or std::nullopt if no event starts there. An empty voice
  // returns std::nullopt for every position including Rational(0).
  [[nodiscard]] std::optional<std::size_t> find_event_index_at(
      Rational position) const;

  // Returns the exact start position of the event that owns `id`, or
  // std::nullopt if no voice content entity carries that id.
  //
  // Resolution rules:
  //  - A top-level Note, Chord, or Rest id → that event's start position.
  //  - A ChordNote id → the owning Chord's start position.
  //  - A GraceNote id → resolved through GraceGroup indirection:
  //    the principal_event is followed until a top-level event or
  //    ChordNote is found.  There is no fixed depth cap: any finite
  //    acyclic chain of GraceNote → GraceNote principal references is
  //    resolved.  A repeated principal_event id (cycle) or a dangling
  //    principal returns std::nullopt.  Resolution is a bounded
  //    non-recursive scan that cannot overflow the stack on malformed
  //    GraceGroup references.
  //  - A marking id (Dynamic, Hairpin, Slur, etc.) or a GraceGroup id →
  //    std::nullopt (not a position-bearing entity).
  [[nodiscard]] std::optional<Rational> position_of_event(
      NotationEntityId id) const;

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
  // IDs owned exclusively by the replaced event, including embedded IDs,
  // may be reused in either top-level or embedded roles. Fails, leaving the
  // voice unchanged, if no event starts at `position`, if `event` holds a
  // Chord with fewer than two notes, if IDs are duplicated within `event`,
  // if an ID is owned by another event or a reference, marking, or grace
  // record, if the new total length would exceed `target_length`, or if
  // normalization fails.
  [[nodiscard]] Result replace_event(Rational position, VoiceEvent event,
                                     Rational target_length);

  // Replaces the tuplet ratio/group metadata on one contiguous event slice.
  // Rhythmic growth consumes immediately-following plain Rest coverage;
  // contraction is completed by normalizing the voice at its end. The event
  // identities and all reference collections are retained. Transactional.
  [[nodiscard]] Result set_tuplet_group(std::size_t first, std::size_t count,
                                        std::optional<TupletGroupId> group,
                                        std::optional<TupletRatio>   ratio,
                                        Rational target_length);

  // Sets only the pitch of the notehead named by `id` to `pitch`. This is
  // the one pitch-only, index-preserving mutation primitive: `id` must name
  // a top-level Note, a ChordNote inside a Chord, or a GraceNote inside a
  // GraceGroup, and the only change is that notehead's pitch. Its identity,
  // tie state, articulations, and stem override (or, for a grace note, its
  // duration, type, and slash flag) are retained verbatim, as are every
  // other notehead, chord, grace group, event, and reference in the voice,
  // so event ordering and rhythm are always preserved and no rest is ever
  // created or removed.
  //
  // Fails, leaving the voice unchanged, if `id` does not name a Note,
  // ChordNote, or GraceNote -- including a Rest id, a Chord's own top-level
  // id (which names the chord column, not a notehead), a marking id, or an
  // unknown/stale id. There is no parameter that can change a variant's
  // kind, a duration, an id, a notehead set, or a marking.
  [[nodiscard]] Result set_notehead_pitch(NotationEntityId id,
                                          SpelledPitch     pitch);

  // Clears events and every reference collection, then advances the
  // revision so that any prior token becomes stale (not current).
  void clear();

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

  // Replaces the beam override named by `id` with `replacement` in place,
  // preserving the override's position in the beam-override vector and its
  // identity. Fails, leaving the voice unchanged, if `id` does not name an
  // existing beam override or `replacement.id` differs from `id`.
  [[nodiscard]] Result replace_beam_override(NotationEntityId id,
                                             BeamOverride     replacement);

  [[nodiscard]] const std::vector<GraceGroup>& grace_groups() const noexcept {
    return grace_groups_;
  }

  [[nodiscard]] Result add_grace_group(GraceGroup group);

  [[nodiscard]] Result remove_grace_group(NotationEntityId id);

  // Removes the grace note named by `id` from its containing GraceGroup in
  // place, preserving the group vector's order and every other group's
  // identity. When the removed note was the group's only note, the now-empty
  // group is removed in place instead. Fails, leaving the voice unchanged,
  // if `id` does not name a GraceNote.
  [[nodiscard]] Result remove_grace_group_note(NotationEntityId id);

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

  // Mutation revision tracking. On every successful mutation the internal
  // monotonic counter advances. Consumers snapshot with capture_revision()
  // and later call delta_since() to learn exactly what changed.
  // delta_since() merges the ring buffer of deltas since the captured
  // token; returns std::nullopt when the token is stale (fell off the ring
  // buffer, the object was copy/move-assigned, or the object was moved from —
  // an explicit full-refresh signal).
  [[nodiscard]] VoiceRevision             capture_revision() const noexcept;
  [[nodiscard]] std::optional<VoiceDelta> delta_since(
      VoiceRevision since) const;

  // Semantic equality excludes mutation-tracking state, preserving
  // existing operator== semantics.
  [[nodiscard]] bool operator==(const VoiceContent& other) const;

 private:
  // True if `id` appears as an event id, an embedded ChordNote or
  // GraceNote id, or in any marking collection.
  [[nodiscard]] bool marking_id_exists(NotationEntityId id) const;

  // True if `id` belongs to a reference, marking, or grace record, or is
  // owned by an event outside `event_index`. Used by replace_event to allow
  // IDs owned exclusively by the replaced event to be reused across roles.
  [[nodiscard]] bool id_collision_if_not_target(NotationEntityId id,
                                                std::size_t event_index) const;

  // Commits a fully-prepared delta to the fixed-capacity ring and advances
  // revision_. The slot count is fixed; each delta's vector payload is still
  // dynamically allocated while the mutation is prepared.
  void advance_revision(VoiceDelta delta) noexcept;

  // Called from copy/move assignment to invalidate prior tokens.
  void reset_revision_tracking() noexcept;

  // Rewrites every event-reference field in all five reference families
  // that currently holds `old_id` to hold `new_id` instead. Callers must
  // signal reference remapping in the revision-tracking delta (currently
  // by setting VoiceDelta::full_reset). Performs no rewrite when
  // old_id == new_id.
  //
  // Maintained as a member so it has direct access to private reference
  // vectors during an in-flight replace_event transaction.
  void remap_event_id_across_references(NotationEntityId old_id,
                                        NotationEntityId new_id);

  static constexpr std::size_t kMaxTrackedRevisions = 16;

  std::vector<VoiceEvent>     events_;
  std::vector<DynamicMarking> dynamics_;
  std::vector<Hairpin>        hairpins_;
  std::vector<Slur>           slurs_;
  std::vector<BeamOverride>   beam_overrides_;
  std::vector<GraceGroup>     grace_groups_;

  // Mutation tracking — excluded from semantic equality.
  VoiceRevision                                revision_;
  std::array<VoiceDelta, kMaxTrackedRevisions> deltas_{};
  std::uint32_t                                ring_head_{0};
  std::uint32_t                                ring_count_{0};
};

// Decomposes a strictly positive whole-note `length` into the fewest plain
// (non-tuplet) Rests, using bounded exact dynamic programming to minimize
// the rest count, with largest-duration ordering as the deterministic
// tie-breaker so automatic rests read as idiomatic notation rather than
// one arbitrary fraction. Fails if `length` is not strictly positive, if
// the length cannot be expressed as an exact sum of base-and-dot Duration
// values (e.g. a gap left by a tuplet whose remainder is not dyadic), or
// if the minimal decomposition would exceed the internal 64-term cap.
[[nodiscard]] std::optional<std::vector<Rest>> decompose_rest(Rational length);

// The duration-only, non-allocating-id core of decompose_measure_aligned_rests
// below: the same measure-by-measure tiling, but as plain Duration values
// rather than minted Rest objects. Each main-region measure of
// timeline.measures() is decomposed independently (never
// decompose_rest(timeline.node_end()) as one span -- see
// decompose_measure_aligned_rests for why that would cross barlines); a
// trailing pickdown region, when timeline has one, is decomposed as its own
// final group after every main-region measure's durations.
//
// Returns std::nullopt, allocating nothing observable and mutating nothing,
// under the same conditions decompose_measure_aligned_rests documents.
//
// A caller that only needs the shape (e.g. preview_note_entry,
// graphscore_notation, which resolves onsets from the durations alone and
// discards everything else) should call this directly rather than
// decompose_measure_aligned_rests: minting and immediately discarding a
// fresh Rest -- and the Uuid::generate() that costs -- per term on every
// pointer move is pure waste. decompose_measure_aligned_rests itself is a
// thin id-minting wrapper over this function, so the two can never drift
// apart on the durations they agree on.
[[nodiscard]] std::optional<std::vector<Duration>>
decompose_measure_aligned_rest_durations(const NodeTimeline& timeline);

// The measure-aligned counterpart to decompose_rest(): the ordered Rest
// sequence that exactly tiles [0, timeline.node_end()) such that no single
// Rest crosses a measure boundary, with a freshly minted id per Rest. This
// is deliberately different from calling decompose_rest(timeline.node_end())
// once: a naive whole-span decomposition can and does produce rests
// spanning a barline (e.g. three 3/4 measures decompose as a dotted whole
// plus a dotted half, neither of which respects a single 3/4 barline),
// which is not valid notation.
//
// Returns std::nullopt, allocating nothing observable and mutating
// nothing, if any individual measure's or the pickdown's span cannot be
// expressed exactly by decompose_rest -- never a partial or approximate
// fill. Every measure length MeasureMap can produce is strictly positive,
// and NodeTimeline::set_pickdown enforces a strictly positive pickdown
// duration, so the only realistic failure mode is a span whose duration is
// not an exact sum of base-and-dot Duration values, or one whose minimal
// decomposition would exceed decompose_rest's internal 64-term cap (the
// same conditions decompose_rest itself documents).
//
// This is the single source of truth for "what would filling this node's
// currently-empty voice with normalized rests look like": both
// preview_note_entry (graphscore_notation, via
// decompose_measure_aligned_rest_durations above) and CreateVoiceStreamCommand
// (via this function) derive from the same tiling, so the pointer-entry
// preview and the command it previews can never drift apart.
[[nodiscard]] std::optional<std::vector<Rest>> decompose_measure_aligned_rests(
    const NodeTimeline& timeline);

}  // namespace graphscore
