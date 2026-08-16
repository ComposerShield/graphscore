// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include <graphscore/domain/node_timeline.hpp>
#include <graphscore/domain/paste_fragment_command.hpp>
#include <graphscore/domain/selection.hpp>
#include <graphscore/notation/notation_palette.hpp>
#include <graphscore/notation/notation_types.hpp>

namespace graphscore {

class Project;

// Resolves `point` against `layout` (produced by a prior layout_notation()/
// NotationLayoutCache::update() call for the same project/node) to the
// single Selection the composer's click names, for
// docs/plan/05-notation-editor.md's "Select individual noteheads, whole
// chord events, rests, markings, ranges, and insertion carets through
// explicit hit regions." This increment resolves noteheads, chords, rests,
// markings, and insertion carets; every multi-item/range selection is a
// later increment's scope.
//
// `palette`'s armed voice is the sole source of truth for which voice an
// insertion-caret result names, mirroring preview_note_entry's own use of
// it. It is also used as a preference when two stemless chords in different
// voices at the same onset emit overlapping equal-area notehead-column
// regions that hit_test cannot geometrically distinguish (see the
// notehead-column paragraph below). Otherwise, a notehead/chord/rest hit
// names whichever staff and voice actually own the hit's entity id, which
// may differ from the armed voice: clicking an existing note in Voice 2
// while Voice 1 is armed still selects that Voice 2 note.
//
// Resolution:
//   kNotehead hit  -- NoteheadSet (one item): the Note, ChordNote, or
//                     GraceNote the hit names.
//   kEvent hit     -- ChordSet, RestSet, or NoteheadSet (one item),
//                     depending on what the hit's semantic entity resolves
//                     to: a top-level Chord, a top-level Rest, a top-level
//                     Note, or an embedded ChordNote (a chord notehead's
//                     own accidental/dot/stem hit region) -- the last two
//                     both select that one notehead, matching a direct
//                     kNotehead hit on it. A stemless top-level event -- a
//                     whole note or whole-note chord, for which the
//                     engraver draws no stem -- has no stem hit region to
//                     carry the whole event, so the engraver emits a
//                     notehead-column region in its place: one kEvent
//                     region carrying the event's own id, spanning exactly
//                     the bounding box of the event's noteheads (the union
//                     of their own kNotehead regions, which for a chord
//                     additionally covers the vertical gaps between them).
//                     Clicking a whole-note chord inside that box, where no
//                     region ranked above the column also covers the click,
//                     therefore yields the same whole-chord ChordSet that
//                     clicking a stemmed chord's stem yields.
//
//                     The column is the sole occupant of a priority rank of
//                     its own, above every container region and below every
//                     region naming an engraved object (see
//                     HitRegion::priority). Two consequences follow, and
//                     both hold regardless of the font's glyph metrics and
//                     of where the engraver placed a glyph. First, a click
//                     inside the column never falls through to
//                     insertion-caret resolution: the column outranks every
//                     container region, and no region that outranks the
//                     column resolves to a caret either. Second, a click
//                     that lands on a region ranked above the column
//                     resolves through that region and not through the
//                     column -- so for the three per-notehead cases that
//                     overlap a column in practice (a notehead itself, and
//                     a chord note's own accidental and augmentation dots)
//                     the result is a NoteheadSet naming that ChordNote,
//                     never the enclosing chord. Regions of other kinds
//                     that outrank the column resolve as they always do,
//                     which is not necessarily to a ChordNote: an
//                     articulation, for instance, still yields its own
//                     MarkingSet.
//
//                     A tie's span segment ranks above the column
//                     (kHitPrioritySpanSegment > kHitPriorityNoteheadColumn),
//                     but its hit region is now tight to the actual drawn
//                     tie curve (see add_span_segment's kHitRoleTie branch
//                     in src/notation/notation_engraving.cpp) rather than a
//                     universal four-staff-space band.  A click on the drawn
//                     curve itself still selects the tie; a click away from the
//                     curve, inside the notehead-column region of a
//                     close-voiced tied stemless chord, now reaches that
//                     chord's ChordSet instead.  Articulation glyphs on a
//                     chord carrying both a tie and articulations are
//                     likewise no longer shadowed by the tie band away from
//                     the actual curve.
//
//                     Two stemless chords in different voices at the same
//                     onset emit overlapping equal-area column regions.
//                     hit_test cannot geometrically distinguish them, so its
//                     semantic_id tie-break would decide by UUID ordering.
//                     This function instead uses `palette`'s armed voice as a
//                     preference only among candidates truly tied at the
//                     UUID-order stage: same priority, equal area (within
//                     floating-point tolerance), same staff/system, and same
//                     musical onset.  When priority or area distinguishes the
//                     candidates, hit_test's own order is deterministically
//                     correct regardless of UUID values, and the geometric
//                     winner is preserved as-is.  Direct notehead/glyph hits
//                     (kNotehead, kMarking) are unaffected and always resolve
//                     to their actual owning voice, which may differ from the
//                     armed voice.
//
//                     For a stemless single Note the column coincides
//                     exactly with that note's sole notehead region and is
//                     fully shadowed by it, so it changes nothing there.
//   kMarking hit   -- MarkingSet (one item) naming the single dynamic,
//                     hairpin, slur, pedal span, articulation, tie, or
//                     tuplet the hit's own id and semantic id resolve to.
//                     Dynamic/hairpin/slur/pedal span are told apart by
//                     looking the hit's semantic id up against the voice's
//                     own dynamics()/hairpins()/slurs() or the stave's own
//                     pedal_spans() -- the semantic id *is* that record's
//                     own id, and those four id spaces are disjoint, so a
//                     successful lookup is unambiguous regardless of the
//                     hit id's own shape. Articulation, tie, and tuplet
//                     carry no record of their own (an Articulation is a
//                     value in Note/Chord::articulations, a tie is a bool
//                     on Note/ChordNote, a tuplet is a TupletRatio inside
//                     Duration), so their semantic id is instead the
//                     anchoring event's own id -- the same space
//                     kNotehead/kEvent hits already search -- and this
//                     function tells the three apart by the hit id's own
//                     path shape instead (.../articulation/N/hit,
//                     .../tie/segment/system-N/hit,
//                     .../tuplet/digit/N/hit). `voice` is engaged for every
//                     kind except kPedalSpan, which is stave-scoped rather
//                     than voice-scoped; `articulation` is engaged if and
//                     only if the kind is kArticulation, naming the exact
//                     Articulation the clicked glyph drew. A kTuplet
//                     selection's anchor is always the tuplet run's true
//                     first event, never merely the event the clicked
//                     digit happened to be drawn against: the engraver's
//                     own per-system layout only prepends one measure of
//                     lookback context when scanning for a run that began
//                     earlier, so this function separately walks the
//                     addressed voice's own full (unfragmented) event list
//                     backward from the hit's anchor while the preceding
//                     event carries the same TupletGroupId before building
//                     the selection. Adjacent equal-ratio groups therefore
//                     remain distinct selections.
//   kSystem/kMeasure/kStaff/kVoice/kStaffMeasure hit, or no hit at all --
//                     InsertionCaretSet
//                     (one item) at the nearest onset in `palette`'s armed
//                     voice that preview_note_entry would also snap its own
//                     preview to, filtered by one further check this
//                     function alone makes: the onset must additionally
//                     satisfy the domain's own caret-legality rule
//                     (validate_insertion_caret_set,
//                     graphscore/domain/selection.cpp) -- position 0,
//                     TrackLane::total_length(), or an existing event
//                     boundary in the armed voice. That filter matters
//                     specifically when the armed voice is empty:
//                     preview_note_entry's snapped onset can then come from
//                     a hypothetical measure-aligned rest fill (see its own
//                     comment) that is a legal preview position but not yet
//                     a legal caret before that fill is materialized: this
//                     function returns std::nullopt for such a point rather
//                     than a Selection validate_selection would reject.
//
// Returns std::nullopt when `point` resolves to nothing usable: outside
// every system/staff, a notehead/event hit whose semantic entity cannot be
// found in any staff's voices in this layout (a stale layout), a kMarking
// hit whose named dynamic/hairpin/slur/pedal span/articulation/tie/tuplet
// can no longer be found or no longer carries the shape its kind requires
// (a stale layout -- e.g. an articulation index beyond the event's current
// articulation count, or a tie hit on a note that is no longer tied), an
// insertion-caret attempt preview_note_entry's own resolution would also
// reject (no measure at that x, no onset in the armed voice's resolved
// measure, etc.), or an insertion-caret attempt whose otherwise-resolved
// onset fails the domain's own caret-legality check described above. A
// pure query: never mutates `project`, `layout`, or `palette`.
[[nodiscard]] std::optional<Selection> resolve_selection_at(
    const Project& project, const NotationLayout& layout,
    const NotePaletteState& palette, NotationPoint point);

// Resolves `point` against `layout` (produced by a prior
// layout_notation()/NotationLayoutCache::update() call for the same
// project/node) to the single one-item FullMeasureSet Selection naming the
// whole measure on the whole staff `point` names, for
// docs/plan/05-notation-editor.md's "Emit a per-staff measure hit region
// and resolve a pointer position on it to a one-measure full-measure
// selection on that staff/track."
//
// `point` is resolved through layout.hit_test(point). Only a hit whose role
// is HitRole::kStaffMeasure -- the per-(staff, measure) region
// layout_notation now emits alongside the pre-existing kSystem/kMeasure/
// kStaff/kVoice containers, ranked strictly below every engraved-object
// region and strictly above those four coarser containers (see
// HitRegion::priority) -- resolves to a selection here; every other role
// (kNotehead, kEvent, kMarking) and every other container returns
// std::nullopt, so a click on a notehead, an accidental, a stem, a rest, or
// a span segment never produces a measure selection, and neither does a hit
// that resolves to one of the four coarser container regions instead of a
// staff-measure region -- e.g. the margin outside every drawn measure
// (kSystem), or the ledger/marking lane above or below a staff, which falls
// outside every staff-measure region's own tight bounds. Conversely, an
// ordinary click on blank staff space *inside* a drawn measure is the
// primary success case: it resolves to that measure's own kStaffMeasure
// region and yields the one-item FullMeasureSet below. The winning region's
// own semantic_id is then matched back against the layout tree (never
// parsed) to recover the typed NodeId/TrackId/StaveId/measure ordinal the
// FullMeasureItem needs.
//
// This is a separate entry point from resolve_selection_at, rather than a
// new arm of it, specifically so that resolve_selection_at's own already-
// delivered "blank staff area falls through to an insertion caret" behavior
// is untouched: HitRole::kStaffMeasure is one more role
// resolve_selection_at's own container fall-through catches, exactly like
// kSystem/kMeasure/kStaff/kVoice today, so every point resolve_selection_at
// used to resolve still resolves identically. A future measure-selection
// affordance calls this function instead, at the point in its own pointer
// handling where it chooses to select a measure rather than enter a note or
// extend an insertion caret.
//
// Returns std::nullopt when `point` is non-finite, when it does not resolve
// to a HitRole::kStaffMeasure region, when that region's own semantic id
// cannot be found in `layout`'s own system/staff/measure tree -- a
// defensive check against a future emitter drifting from this resolution
// logic (mirroring resolve_selection_at's own hit-id/entity-kind
// cross-checks in notation_hit_resolution.cpp), not a live path for a layout
// merely stale relative to `project`: an id drawn from `layout.hit_regions`
// is always found in that same layout's own `systems` tree, since both are
// built together by the one layout_notation() pass -- or when the
// resulting Selection would not satisfy
// validate_selection(project, selection).empty(), which is where genuine
// staleness relative to `project` is actually caught (e.g. a measure
// ordinal the layout still carries but the node's own NodeTimeline no
// longer does, rejected as kMeasureIndexOutOfRange in
// validate_full_measure_set, src/domain/selection.cpp). That same function
// also carries a TimelineRegion::kPickdown check, but it is unreachable
// from here: layout_notation only ever builds a HitRole::kStaffMeasure
// region from system.measures, which is exactly the node's own NodeTimeline
// main region (see measure_map.hpp), so every measure ordinal this
// function can produce is main-region material by construction and the
// pickdown check never has an ordinal to fire on.
//
// A pure query: never mutates `project` or `layout`.
[[nodiscard]] std::optional<Selection> resolve_measure_selection_at(
    const Project& project, const NotationLayout& layout, NotationPoint point);

// Resolves two staff-measure hit points on the same staff to the inclusive,
// contiguous measure range between them. This is measure-aligned selection
// only: neither endpoint is interpreted as an arbitrary musical position.
[[nodiscard]] std::optional<Selection> resolve_measure_range_selection(
    const Project& project, const NotationLayout& layout, NotationPoint anchor,
    NotationPoint focus);

// One (track, stave) scope a caller chooses to extend a measure selection
// onto.  Both fields name GraphScore-owned types; no platform or
// third-party type crosses this boundary.
struct MeasureScope {
  TrackId track_id;
  StaveId stave_id;

  [[nodiscard]] bool operator==(const MeasureScope&) const = default;
};

// The project-wide score order every range-selection resolver below
// applies to a pair of staff endpoints: Project::active_tracks() order,
// then each track's own StaffLayout::staves() order -- the identical order
// layout_notation itself assigns to every system's own StaffSystemLayout
// list, so every system carries this exact same ordered staff set
// project-wide. resolve_range_selection, resolve_range_selection_spec,
// extend_range_selection, and extend_range_selection_staff_scope (below)
// all resolve their own staff endpoints through one private helper in
// src/notation/notation_range_selection.cpp; this function delegates to that
// same helper rather than copying the rule. (extend_measure_selection, also in
// src/notation/notation_measure_selection.cpp, restates this order in its own
// in-place filter loop rather than calling the shared helper; it pre-dates this
// function and is the one remaining sibling not yet routed through it.)
//
// Exposed publicly for docs/plan/05-notation-editor.md's M5-phase-19b:
// the app layer's keyboard staff-scope extension needs to answer "which
// staff is one position below/above the current last staff?" and must
// not re-derive the traversal to answer it -- a second, independently
// written copy of the ordering rule could silently drift from the one the
// resolvers above actually use, producing a keyboard extension that
// disagrees with what a pointer drag over the same staves would select.
// This function is that shared order, as a value a caller can index,
// search, or feed straight back into extend_range_selection_staff_scope/
// extend_measure_selection as MeasureScope endpoints without converting.
//
// Returns one MeasureScope per (track, stave) pair, in that order. A
// project with no active tracks (and, transitively, a project with active
// tracks whose own StaffLayout carries no staves) returns an empty
// vector.
//
// A pure query: never mutates `project`.
[[nodiscard]] std::vector<MeasureScope> score_ordered_staves(
    const Project& project);

// Extends an existing aligned FullMeasureSet across additional chosen
// (track, stave) scopes, for docs/plan/05-notation-editor.md's "Extend an
// aligned measure selection across additional chosen tracks/staves."
//
// `existing` must be a FullMeasureSet whose every item shares one anchor
// NodeId and one global measure_index -- an aligned set, which is what
// resolve_measure_selection_at's own one-item result produces, and what a
// prior extend_measure_selection call itself returns.  A misaligned set
// (different nodes or measure indices) is rejected with std::nullopt rather
// than silently normalized.
//
// Each scope in `additional` must name an active track (never archived or
// unknown), a stave that exists in that track's fixed StaffLayout, and a
// usable lane/stave in the anchor node (that node must carry a TrackLane
// for the track, and that lane must carry a StaveVoices for the stave).
// The entire call fails with std::nullopt for any invalid/stale/archive/
// missing scope -- it never silently skips caller choices.
//
// Items already present in `existing` and `additional` items that
// duplicate an existing or already-added scope are idempotent.  The
// returned FullMeasureSet's items are always in deterministic score order:
// Project::active_tracks() order then each track's own
// StaffLayout::staves() order, regardless of caller input order or
// existing-item order.  Every item carries the one shared anchor NodeId
// and measure_index.
//
// When `additional` is empty the result is the existing set itself,
// re-validated against `project`.
//
// The resulting Selection is validated with validate_selection before
// being returned; a non-empty diagnostic list rejects it with std::nullopt.
//
// A pure query: never mutates `project`.
[[nodiscard]] std::optional<Selection> extend_measure_selection(
    const Project& project, const FullMeasureSet& existing,
    const std::vector<MeasureScope>& additional);

// Resolves a pointer drag from `anchor` to `focus` against `layout`
// (produced by a prior layout_notation()/NotationLayoutCache::update() call
// for the same project/node) to the single ArbitraryRangeSet Selection a
// dedicated range-selection tool's drag names, for
// docs/plan/05-notation-editor.md's "Add a dedicated selection tool whose
// pointer drag creates a contiguous musical-time selection across the
// intersected staves/voices rather than selecting engraving glyph bounds
// individually."
//
// Deliberately takes no NotePaletteState: unlike resolve_selection_at and
// preview_note_entry, this is a range-selection tool, distinct from note
// entry, so the composer's armed voice must never filter which voices the
// drag selects -- every voice with overlapping content is included below
// regardless of what is currently armed.
//
// Staff range: `anchor` and `focus` are each resolved to a staff via the
// same resolve_staff_at logic resolve_selection_at itself consumes. Every
// (track, stave) between the two resolved staves, inclusive, in score
// order -- Project::active_tracks() order, then each track's own
// StaffLayout::staves() order, the identical order layout_notation itself
// assigns to every system's own StaffSystemLayout list -- is included.
// This is anchor-to-focus by that global order, not a rectangle
// intersection test: a drag spanning a system break covers different
// staves in each system it touches, while every system carries the
// identical ordered staff set project-wide, so score order is the only
// unambiguous way to name "the staves this drag crosses".
//
// Voices: VoiceLayout carries no geometry of its own (see its own comment
// above), so which voices a drag crosses cannot be answered geometrically.
// Instead, for each staff in the resolved range, a voice is included iff
// at least one of its own events' own [onset, onset + duration) extent
// overlaps the resolved span below. A voice with no overlapping content is
// excluded outright, so a single-voice score never produces the other
// three voices' own empty items.
//
// Span: each endpoint's x is mapped to a musical time via the same
// unsnapped time_at_x inverse mapping resolve_insertion_site consumes,
// evaluated against the measure resolved at that endpoint's own point.
// start is the smaller of the two resulting times and end the larger, so a
// backwards or upward drag produces the identical span a forward drag over
// the same two points would, and a drag spanning a system break still
// produces one contiguous span, since musical time is global across the
// node. Every emitted item shares this one span exactly, satisfying
// extract_arbitrary_range's own same-span requirement
// (src/domain/notation_fragment.cpp).
//
// Quantization: time_at_x returns a continuous double; MusicalSpan needs an
// exact Rational. Each endpoint's time is rounded to the nearest multiple
// of 1/kRangeSelectionGridDenominator (192 = lcm(64, 3)) of a whole note --
// a grid fine enough to land exactly on every plain binary subdivision
// through a sixty-fourth note and every plain triplet subdivision. A drag
// endpoint that should land exactly on a dotted-sixty-fourth boundary (e.g.
// a single dot's 3/128, or a double dot's 7/256), on a quintuplet/
// septuplet/etc. division, or on any other boundary this grid does not
// exactly represent still only resolves to the nearest 1/192, up to half a
// 192nd note (1/384 of a whole note) off that boundary;
// kRangeSelectionGridDenominator (notation_range_selection.cpp) is trivially
// widened to also cover a specific such family if that error ever matters.
// This is acceptable specifically because this quantization only ever
// affects the drag endpoint's own position, never rhythmic content: this
// never snaps to an event onset, unlike resolve_selection_at's
// insertion-caret arm, so the domain's own clipping rules
// (docs/plan/02-domain-model.md R1-R12) -- which exist specifically to
// handle a span whose boundary straddles an event -- still apply exactly
// regardless of where within an event the quantized boundary lands.
//
// Returns std::nullopt when either point is non-finite, when either point
// fails to resolve to a staff (resolve_staff_at) or to a measure within
// that staff's own system (resolve_measure_at), when either resolved
// measure's ordinal is not within the node's own NodeTimeline (or the node
// has no NodeTimeline at all), when either resolved staff cannot be found
// in the project's own score-ordered staff list, when the quantized span is
// zero-length (start == end -- a click, not a drag, is resolve_selection_at's
// own job), or when no voice anywhere in the resolved staff range has any
// overlapping content at all (ArbitraryRangeSet::create itself rejects an
// empty item vector). A staff within the resolved range whose own
// TrackLane/StaveVoices cannot be found is skipped rather than treated as a
// nullopt-triggering failure -- see the implementation's own comment where
// it does so.
//
// A pure query: never mutates `project`, `layout`, or any cache.
//
// The staff-range rule, the voice-inclusion rule, the lane/voices-missing
// skip behavior, and the final ArbitraryRangeSet construction described
// above -- everything below "each endpoint's x is mapped to a musical time"
// -- are factored into a private helper (notation_range_selection.cpp) shared
// with resolve_range_selection_spec, extend_range_selection, and
// extend_range_selection_staff_scope below: those three reach the identical
// rules from a resolved MusicalSpan and a pair of score-order staff
// endpoints supplied directly, rather than derived from a pointer drag.
// This function's own observable behavior is unchanged by that sharing.
[[nodiscard]] std::optional<Selection> resolve_range_selection(
    const Project& project, const NotationLayout& layout, NotationPoint anchor,
    NotationPoint focus);

// Names an explicit musical-coordinate range selection for
// resolve_range_selection_spec below: which node, the exact musical span,
// and the two (order-insensitive) staff endpoints it covers.
struct RangeSelectionSpec {
  NodeId       node_id;
  MusicalSpan  span;
  MeasureScope first_staff;
  MeasureScope last_staff;
};

// The explicit musical-coordinate equivalent of a dedicated-selection-tool
// pointer drag, for docs/plan/05-notation-editor.md's "accessible start/
// end/staff-scope controls that produce the same selection as pointer
// dragging." `span` and the two staff endpoints are supplied directly by
// the caller -- no geometry, no NotationLayout, no NotationPoint -- so a
// keyboard or assistive-technology control can name "start", "end", and
// "staff scope" as independent, explicit values.
//
// first_staff/last_staff reuse MeasureScope (above) rather than a
// parallel type; like resolve_range_selection's own anchor/focus staff
// pair, they are order-insensitive -- resolved via std::minmax over the
// project's score order exactly as resolve_range_selection's own anchor/
// focus staves are, so first_staff naming the later staff and last_staff
// the earlier one produces the identical result as the reverse.
//
// Quantization: deliberately none. resolve_range_selection quantizes
// because a pixel-derived double must be snapped to some exact Rational
// before it can become a MusicalSpan; kRangeSelectionGridDenominator picks
// that grid. `spec.span` here is already an exact caller-supplied
// Rational -- there is no continuous value to snap, and rounding an
// already-exact dotted-sixty-fourth-note or quintuplet-division boundary
// onto the pointer path's 1/192 grid would make the keyboard/accessible
// path strictly less precise than the composer's own notated rhythm.
// Consequently, this function and resolve_range_selection agree exactly
// whenever the pointer path's own quantized output is fed back into
// `spec.span` -- the equivalence the plan bullet actually asks for -- and
// this function can additionally express spans resolve_range_selection's
// 192nds-of-a-whole-note grid cannot land on exactly.
//
// Empty/zero-length span: rejected with std::nullopt, exactly like
// resolve_range_selection's own `start == end` rejection -- a single
// instant, not a range, is a caret concern, not this function's.
//
// Bounds: like every pointer drag resolve_range_selection can ever
// produce (each endpoint is resolved from a rendered, on-screen measure,
// which is always node.timeline()'s main-region material -- see
// resolve_measure_selection_at's own comment above on why a pickdown
// ordinal is unreachable from it), `spec.span` here must fall entirely
// within the main region: 0 <= span.start and span.end <=
// node.timeline()->measures().total_length(). A span reaching past that
// bound is rejected with std::nullopt rather than silently accepted into
// the pickdown region a pointer drag could never select. This is a
// deliberate difference from validate_selection: validate_selection
// performs no span-range check at all for an ArbitraryRangeSet (it invokes
// NodeTimeline::classify only for reachability and discards the result --
// see validate_arbitrary_range_set, src/domain/selection.cpp), so this
// main-region-only bound is this function's own, applied specifically to
// keep this function's own result set equivalent to what a drag could
// produce.
//
// Validation: the resulting Selection is run through
// validate_selection(project, selection) before being returned; a
// non-empty diagnostic list rejects it with std::nullopt, exactly like
// extend_measure_selection.
//
// Returns std::nullopt when: node_id does not name a node in `project`;
// that node has no NodeTimeline; span.start is not strictly less than
// span.end; span.start is negative or span.end exceeds the timeline's own
// main-region total_length(); first_staff or last_staff does not name an
// active track and one of its own staves in the project's score order
// (this single check is what rejects an unknown track, an archived/
// inactive track, and a stave id that exists but not on the named track --
// score_ordered_staves(project) only ever enumerates active tracks' own
// staves, so none of those three ever appears in it); or no voice anywhere
// in the resolved staff range has any content overlapping `span`
// (ArbitraryRangeSet::create itself rejects an empty item vector). A
// staff within the resolved range whose own TrackLane/StaveVoices cannot
// be found is skipped rather than treated as a nullopt-triggering failure,
// exactly as in resolve_range_selection. `spec`'s Rational fields carry no
// separate "non-finite" failure mode: Rational is always an exact,
// finitely-representable value once constructed (Rational::create rejects
// only a zero denominator), unlike NotationPoint's double coordinates.
//
// A pure query: never mutates `project`.
[[nodiscard]] std::optional<Selection> resolve_range_selection_spec(
    const Project& project, const RangeSelectionSpec& spec);

// Which end of an ArbitraryRangeSet's shared musical span
// extend_range_selection moves.
enum class RangeEdge : std::uint8_t {
  kStart,
  kEnd,
};

// Moves one edge of `existing`'s shared musical span to `time`, holding
// the other edge fixed, for docs/plan/05-notation-editor.md's "Shift/
// keyboard range extension" -- the primitive a Shift+arrow-style action
// calls with a freshly computed target time. This function deliberately
// takes an explicit target Rational rather than a step size: how far one
// keyboard press moves (a diatonic step, a beat, a measure, ...) is a
// product decision for the action table docs/plan/05-notation-editor.md's
// M5-phase-26/M5-phase-27 own, not something this notation-layer primitive
// bakes in.
//
// `existing` must be an aligned ArbitraryRangeSet: every item shares one
// NodeId and one MusicalSpan -- what resolve_range_selection,
// resolve_range_selection_spec, and this function itself all produce. A
// misaligned set (different nodes or spans across items) is rejected with
// std::nullopt rather than silently normalized, exactly like
// extend_measure_selection's own alignment precondition on FullMeasureSet.
//
// The staff range held fixed is derived from `existing`'s own items: the
// lowest and highest score-order position among all of `existing`'s own
// (track, stave) pairs. This reconstruction is unique to this function --
// extend_range_selection_staff_scope deliberately takes both endpoints
// explicitly rather than reconstructing them, for exactly the reason given
// on its own declaration below. It carries a documented limitation: a
// staff at the extreme of the originally resolved range whose own voices
// happened to carry no overlapping content contributes no item, so it
// cannot be recovered as part of the "fixed" staff range here either. A
// caller that must preserve such a staff through an edge-only extension
// should track the staff endpoints alongside the Selection rather than
// reconstructing them from items.
//
// edge == kStart moves the span's start to `time`; edge == kEnd moves the
// end. When the moved edge would cross the fixed one (a kStart move past
// the current end, or a kEnd move before the current start), the two are
// swapped rather than rejected, so "extend the start past the end" reads
// as "the selection now runs the other way" -- consistent with
// resolve_range_selection's own anchor/focus order-insensitivity. Moving
// an edge exactly onto the fixed edge produces a zero-length span, which
// is rejected below like every other zero-length span.
//
// Bounds and validation follow resolve_range_selection_spec exactly: the
// new span must satisfy 0 <= start < end <= node.timeline()->measures().
// total_length(), and the resulting Selection must pass
// validate_selection(project, selection) before being returned.
//
// Returns std::nullopt when: `existing` is misaligned; the node named by
// `existing`'s own items cannot be found in `project`, or has no
// NodeTimeline; the new span is zero-length after the move/swap above, or
// falls outside [0, total_length()]; any (track, stave) pair in
// `existing`'s own items cannot be found in the project's score order (a
// stale/fabricated `existing`); or no voice anywhere in the held-fixed
// staff range has any content overlapping the new span.
//
// A pure query: never mutates `project`.
[[nodiscard]] std::optional<Selection> extend_range_selection(
    const Project& project, const ArbitraryRangeSet& existing, RangeEdge edge,
    Rational time);

// Replaces the staff scope of an existing ArbitraryRangeSet with the score-
// order range spanning `first_staff` and `last_staff`, holding the shared
// musical span fixed, for docs/plan/05-notation-editor.md's "Shift/
// keyboard range extension" applied to the staff axis rather than the time
// axis -- the sibling of extend_range_selection above.
//
// Unlike extend_range_selection's single-edge-plus-target shape, this
// function takes both new staff endpoints explicitly (reusing MeasureScope,
// order-insensitive via std::minmax, exactly like
// resolve_range_selection_spec's own first_staff/last_staff fields) rather
// than holding one of the two current endpoints implicitly fixed. Two
// reasons: first, unlike a musical-time edge, the staff range's own current
// endpoints are not reliably recoverable from `existing`'s items alone (see
// extend_range_selection's own comment on that limitation) -- there is no
// dependable "fixed" endpoint to hold. Second, this shape covers both a
// single-step keyboard extension (the caller recomputes the moving
// endpoint from the score order and passes the other one back unchanged)
// and an explicit accessible staff-scope control (the caller names both
// endpoints directly) with one function rather than two, and both widening
// and contracting the staff range are ordinary calls rather than a special
// case: `first_staff`/`last_staff` simply name the new range, regardless
// of whether it is wider or narrower than the current one.
//
// The shared musical span is taken from `existing`'s own items, which must
// be aligned (one NodeId, one MusicalSpan) exactly as
// extend_range_selection requires.
//
// Bounds and validation: the node named by `existing` must exist and carry
// a NodeTimeline (the span itself is unchanged, so no total_length() bound
// re-check applies -- an already-valid span stays valid). That reasoning
// holds for any `existing` this file's own API can produce; it does not
// hold for a caller-fabricated `existing` carrying a span already outside
// the main region, since neither this function nor
// validate_selection/validate_arbitrary_range_set re-derive that bound --
// the latter invokes NodeTimeline::classify(item.span) only to confirm the
// span is reachable, not to reject an out-of-bounds one. This is
// asymmetric with extend_range_selection, which does bound-check its own
// (caller-supplied target time, not caller-supplied span) new span. The
// resulting Selection is run through validate_selection(project, selection)
// before being returned.
//
// Returns std::nullopt when: `existing` is misaligned or empty; the node
// named by `existing` cannot be found in `project`, or has no
// NodeTimeline; first_staff or last_staff does not name an active track
// and one of its own staves in the project's score order (subsuming an
// unknown track, an archived/inactive track, and a stave id absent from
// the named track -- see resolve_range_selection_spec's own comment on
// this same check); or no voice anywhere in the resulting staff range has
// any content overlapping the unchanged span.
//
// A pure query: never mutates `project`.
[[nodiscard]] std::optional<Selection> extend_range_selection_staff_scope(
    const Project& project, const ArbitraryRangeSet& existing,
    MeasureScope first_staff, MeasureScope last_staff);

// Produces the visual highlight geometry for a range selection against
// `layout`.  Returns layout-space rectangles, one per (system, staff,
// measure) that the selection's musical span overlaps.  Each rectangle spans
// the staff height and the portion of the measure whose musical time falls
// within the span.  The rectangles are in the same coordinate space as
// NotationLayout::bounds, so they can be overdrawn directly without further
// coordinate transformation.  The caller (the writer shell) renders them
// with a semi-transparent highlight colour.
//
// `selection` must be an ArbitraryRangeSet; any other arm returns an empty
// vector.  `project` provides the MeasureMap the x↔time mapping needs; it
// must be the same project `layout` was produced from, or the result is
// unspecified.
//
// A pure query: never mutates `project` or `layout`.
[[nodiscard]] std::vector<NotationRect> build_range_highlight_rects(
    const Selection& selection, const Project& project,
    const NotationLayout& layout);

// Projects a validated domain paste placement into layout-space rectangles.
// Every affected destination stave receives the placement's exact half-open
// musical-time span. Styling and opacity remain shell/application policy.
// A pure query: never mutates the project, layout, or placement.
[[nodiscard]] std::vector<NotationRect> build_paste_preview_rects(
    const PastePlacement& placement, const Project& project,
    const NotationLayout& layout);

// The writer's active tool discriminator.  Range selection occurs only when
// the active tool is kSelection; any other pointer-drag path must not
// accidentally produce an ArbitraryRangeSet.  Individual-notehead/chord/rest
// single-click resolution (resolve_selection_at) is not gated by this --
// only the dedicated selection-tool drag is.
//
// The startup tool is application policy, not determined by enum order.
// The current temporary M05 app (graphscore_writer_app) starts kSelection
// until the note-palette and pointer-entry phases wire kNoteEntry; kNoteEntry
// is the eventual long-term default.  The two tools are mutually exclusive:
// switching from one to the other cancels any in-progress state (drag,
// preview) the departing tool owned.
enum class ActiveTool : std::uint8_t {
  kNoteEntry,  // Note-entry pointer tool (eventual long-term default).
  kSelection,  // Dedicated range-selection pointer tool (M05 temporary
               // default).
};

// The lifecycle of one dedicated-selection-tool pointer drag.  Owned by the
// application assembly layer (graphscore_writer_app); holds only
// GraphScore-owned value types and never stores references to Project or
// NotationLayout.  Every method is a pure query over the supplied arguments
// -- no allocation, no mutation of project or layout, no platform type.
//
// Lifecycle:
//   begin()     -- pointer press with ActiveTool::kSelection armed.
//   update()    -- pointer move while dragging (is_dragging() == true).
//   commit()    -- pointer release; clears drag state and returns the final
//                  committed Selection.
//   cancel()    -- discard the in-progress drag without committing.
//
// set_committed_selection() is not part of that pointer-drag lifecycle: it
// is the keyboard/accessible entry point (M5-phase-19b) that writes
// committed_selection() directly from a Selection the drag lifecycle above
// never produced, for a Shift/keyboard range extension or an accessible
// start/end/staff-scope control. See its own declaration below for why it
// still cancels any in-progress drag first.
//
// Every begin() call cancels any in-progress drag before validating its
// arguments.  A non-kSelection tool, a non-finite anchor, or any other
// reason to return false leaves no stale drag state — only
// committed_selection_ (if any) persists.
class SelectionDragState {
 public:
  SelectionDragState() = default;

  // Pointer press with `tool`.  Always cancels any in-progress drag first
  // (preserving committed_selection_), then validates.  Returns true when
  // tool == kSelection and anchor is a non-NaN point; the caller must
  // resolve validity with update() -- begin() itself does not look at
  // project or layout.
  [[nodiscard]] bool begin(ActiveTool tool, NotationPoint anchor) noexcept;

  // Advance the live extent for the current pointer position.  Calls
  // resolve_range_selection(anchor_, focus) with the commit-c252fbe
  // semantics preserved exactly.  Returns nullopt when the resolution fails
  // (off-stave, zero-length span, etc.) or when no drag is in progress.
  // Stores the resolved Selection as live_extent() for the caller to render
  // as a live visual highlight.
  [[nodiscard]] std::optional<Selection> update(const Project&        project,
                                                const NotationLayout& layout,
                                                NotationPoint         focus);

  // Commit the final selection and clear drag state.  Returns the Selection
  // most recently resolved by update() (which is the caller's live extent),
  // or nullopt when no valid drag was in progress or update() never
  // resolved a valid extent.  Also moves the result to
  // committed_selection() so the caller can reread it without repeating the
  // resolution.  After commit(), is_dragging() returns false.
  [[nodiscard]] std::optional<Selection> commit() noexcept;

  // Discard the current drag without committing.  Clears is_dragging(),
  // anchor, live extent -- committed_selection is untouched (its previous
  // value persists until the next commit() or set_committed_selection()).
  void cancel() noexcept;

  // The keyboard/accessible counterpart to commit(): stores `selection` as
  // the new committed_selection() directly, for
  // docs/plan/05-notation-editor.md's M5-phase-19b, where Shift/keyboard
  // range extension and the accessible start/end/staff-scope controls
  // produce a fresh Selection (via extend_range_selection,
  // extend_range_selection_staff_scope, or resolve_range_selection_spec)
  // with no pointer drag involved, so commit() -- which requires
  // is_dragging() -- has nowhere to store it.
  //
  // Always cancels any in-progress drag first, exactly like begin() does,
  // before storing `selection`. This is not merely for consistency: if a
  // drag stayed live across this call, a later commit() would overwrite
  // this keyboard-set selection with the drag's own stale live extent,
  // so the two selection sources -- pointer drag and keyboard/accessible
  // range extension -- could disagree about what is actually selected.
  // Cancelling here closes that gap by leaving no drag for a later
  // commit() to resolve.
  //
  // That guarantee covers only the drag in progress at the time of this
  // call: a begin()/commit() cycle that starts afterward owns
  // committed_selection() again like any other drag, including the case
  // where its own update() never resolves a valid extent -- that commit()
  // still clears committed_selection() to std::nullopt exactly as it would
  // for a selection commit() itself produced, silently discarding the
  // Selection this call just stored.
  //
  // `selection` becomes the new committed_selection() exactly as passed;
  // std::nullopt is the supported way to deselect via this path. After
  // this call, is_dragging() is false and live_extent() is std::nullopt.
  void set_committed_selection(std::optional<Selection> selection) noexcept;

  [[nodiscard]] bool is_dragging() const noexcept { return dragging_; }

  // The Selection most recently resolved by update(), or nullopt.  Valid
  // only while is_dragging() and after at least one successful update().
  [[nodiscard]] const std::optional<Selection>& live_extent() const noexcept {
    return live_extent_;
  }

  // The Selection committed by the most recent commit() or
  // set_committed_selection() call, or nullopt.  Survives cancel() and
  // begin().
  [[nodiscard]] const std::optional<Selection>& committed_selection()
      const noexcept {
    return committed_selection_;
  }

  // The drag anchor set by the most recent successful begin().
  [[nodiscard]] NotationPoint anchor() const noexcept { return anchor_; }

  // The active tool in effect for the current or most recent drag.
  [[nodiscard]] ActiveTool active_tool() const noexcept { return active_tool_; }

 private:
  ActiveTool               active_tool_ = ActiveTool::kSelection;
  bool                     dragging_    = false;
  NotationPoint            anchor_;
  std::optional<Selection> live_extent_;
  std::optional<Selection> committed_selection_;
};

}  // namespace graphscore
