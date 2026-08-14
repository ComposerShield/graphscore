// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>

namespace graphscore {

struct NotationPoint {
  double x = 0.0;
  double y = 0.0;

  [[nodiscard]] bool operator==(const NotationPoint&) const = default;
};

struct NotationRect {
  double x      = 0.0;
  double y      = 0.0;
  double width  = 0.0;
  double height = 0.0;

  [[nodiscard]] bool contains(NotationPoint point) const noexcept;
  [[nodiscard]] bool operator==(const NotationRect&) const = default;
};

// Retained identities are readable canonical strings, not hashes. The prefix
// is an existing domain UUID and the remaining fields are owned semantic
// roles/ordinals. Consequently identities never depend on addresses, container
// hash order, glyph metrics, or unrelated notation content.
struct NotationId {
  std::string value;

  [[nodiscard]] bool operator==(const NotationId&) const  = default;
  [[nodiscard]] auto operator<=>(const NotationId&) const = default;
};

struct GlyphMetricsValue {
  NotationRect bounds;
  double       advance = 0.0;
};

// Implemented by graphscore_rendering and injected at application assembly.
// Code points are Unicode SMuFL values; staff_space defines the conversion to
// layout units. No font-library or renderer type crosses this interface.
class GlyphMetrics {
 public:
  virtual ~GlyphMetrics() = default;

  [[nodiscard]] virtual GlyphMetricsValue glyph_metrics(
      char32_t code_point, double staff_space) const             = 0;
  [[nodiscard]] virtual double kerning(char32_t left, char32_t right,
                                       double staff_space) const = 0;
};

struct GlyphCommand {
  NotationId    id;
  char32_t      code_point = U'\0';
  NotationPoint origin;
  double        staff_space = 0.0;

  [[nodiscard]] bool operator==(const GlyphCommand&) const = default;
};

struct LineCommand {
  NotationId    id;
  NotationPoint from;
  NotationPoint to;
  double        width = 0.0;

  [[nodiscard]] bool operator==(const LineCommand&) const = default;
};

enum class PathVerb : std::uint8_t { kMove, kLine, kQuadratic, kCubic, kClose };

struct PathElement {
  PathVerb      verb = PathVerb::kMove;
  NotationPoint control1;
  NotationPoint control2;
  NotationPoint end;

  [[nodiscard]] bool operator==(const PathElement&) const = default;
};

struct PathCommand {
  NotationId               id;
  std::vector<PathElement> elements;
  double                   stroke_width = 0.0;
  bool                     filled       = false;

  [[nodiscard]] bool operator==(const PathCommand&) const = default;
};

struct ClipCommand {
  NotationId   id;
  NotationRect bounds;
  bool         begin = true;

  [[nodiscard]] bool operator==(const ClipCommand&) const = default;
};

using NotationCommand =
    std::variant<GlyphCommand, LineCommand, PathCommand, ClipCommand>;

enum class HitRole : std::uint8_t {
  kSystem,
  kMeasure,
  kStaff,
  kVoice,
  // One measure on one staff -- the region resolve_measure_selection_at
  // resolves to a one-item FullMeasureSet. Distinct from kMeasure, which
  // spans every staff in the system, and from kStaff, which spans every
  // measure in the system; this is the two-way intersection. Placed here,
  // after the other three container roles and before the engraved-object
  // roles below, because nothing in this codebase depends on HitRole's
  // underlying numeric values (see kHitPriorityStaffMeasure in
  // notation.cpp for the separate, load-bearing hit_test priority rank).
  kStaffMeasure,
  kEvent,
  kNotehead,
  kMarking,
};

// GraphScore's complete notation-to-SMuFL vocabulary. Values are semantic,
// rather than Unicode values, so callers never need to duplicate Bravura's
// mapping. smufl_codepoint() is total: an unknown enum representation returns
// the visible SMuFL replacement glyph U+FFFD instead of silently selecting a
// different musical symbol. Font loading and missing-font-glyph handling stay
// in graphscore_rendering.
enum class SmuflGlyph : std::uint8_t {
  kGClef,
  kCClef,
  kFClef,
  kNoteheadWhole,
  kNoteheadHalf,
  kNoteheadBlack,
  kRestWhole,
  kRestHalf,
  kRestQuarter,
  kRestEighth,
  kRest16th,
  kRest32nd,
  kRest64th,
  kAugmentationDot,
  kAccidentalDoubleFlat,
  kAccidentalFlat,
  kAccidentalNatural,
  kAccidentalSharp,
  kAccidentalDoubleSharp,
  kFlag8thUp,
  kFlag16thUp,
  kFlag32ndUp,
  kFlag64thUp,
  kFlag8thDown,
  kFlag16thDown,
  kFlag32ndDown,
  kFlag64thDown,
  kDynamicP,
  kDynamicM,
  kDynamicF,
  kArticAccentAbove,
  kArticMarcatoAbove,
  kArticStaccatoAbove,
  kArticStaccatissimoAbove,
  kArticTenutoAbove,
  kPedalDown,
  kPedalUp,
  kTimeDigit0,
  kTupletDigit0,
};

[[nodiscard]] char32_t smufl_codepoint(SmuflGlyph glyph) noexcept;

struct HitRegion {
  NotationId   id;
  NotationId   semantic_id;
  HitRole      role = HitRole::kSystem;
  NotationRect bounds;

  // The ladder hit_test() ranks overlapping regions by, highest wins:
  //
  //   0  system
  //   1  measure
  //   2  staff
  //   3  voice
  //   4  staff-measure (one measure on one staff)
  //   5  notehead column (stemless events only)
  //   6  every individually engraved glyph that carries a semantic id --
  //      accidentals, augmentation dots, rests, flags, articulations,
  //      dynamics, tuplet digits -- plus the stem's own widened region
  //   7  span segments: hairpin, slur, tie, pedal
  //   8  noteheads, ordinary and grace
  //
  // 0-4 are the containers a click falls through to when it names no
  // engraved object. 6-8 are the engraved objects themselves, and their
  // relative order is what makes a click on an object select that object
  // rather than whatever it was drawn on top of.
  //
  // 4 (staff-measure) must outrank 0-3, the coarser containers it overlaps
  // exactly (every staff-measure region is covered by one system, one
  // measure, one staff, and four voice regions, all sharing its bounds or
  // larger), so resolve_measure_selection_at can tell "this click named one
  // measure on one staff" apart from "this click named nothing more
  // specific than a system/measure/staff/voice column". A rank tied with
  // kVoice (3) happens to resolve correctly today too: on a system carrying
  // exactly one measure the staff-measure region and its containing kVoice
  // region (which shares the same staff.bounds) have equal area, so
  // hit_test falls to its semantic_id tie-break, and the staff-measure
  // region's id sorts first purely because "staff-measure" precedes
  // "voice" lexically ('s' < 'v'). That outcome depends on the incidental
  // shape of the two id strings, not on anything hit_test's contract
  // promises; a future rename of either id shape could silently flip it.
  // Giving the staff-measure region a rank of its own removes that
  // dependence, so the outcome no longer turns on how the ids happen to be
  // spelled. It must in turn lose to every engraved object (5-8): a click
  // on a note, accidental, or stem must keep selecting that object rather
  // than falling back to a whole-measure selection just because the object
  // happens to sit inside one measure of one staff.
  //
  // 5 is deliberately a rank of its own between the two groups, holding
  // exactly one region: the notehead column a stemless event emits in place
  // of the stem it does not draw (see resolve_selection_at). That column
  // spans the bounding box of its own noteheads, so it necessarily overlaps
  // every per-notehead glyph region inside it, and those glyphs must keep
  // selecting their own notehead. Neither cheaper separation achieves that.
  // Ranking the column with the glyphs and letting hit_test's smaller-area
  // tie-break decide would make the outcome depend on the font's glyph
  // metrics. Relying on the engraver's placement offsets does not work
  // either: those are measured from the clicked note's own notehead x,
  // while the column spans every notehead's, and the seconds and
  // voice-collision rules both displace a notehead far enough to carry the
  // column out past an accidental or a dot. Only a rank strictly below
  // every glyph region is unconditionally correct, which is why 5 exists
  // and why nothing else may be moved into it.
  int priority = 0;

  // The owning SystemLayout and StaffSystemLayout's own id at emission
  // time, recorded so that resolve_selection_at's armed-voice column
  // override can compare actual emitting systems rather than inferring
  // ownership from a resolve_hit_entity scan of the complete VoiceContent
  // (which always returns the first system that happens to carry the
  // entity, not the one that emitted this region).  Empty for regions that
  // are not notehead-columns (containers, glyphs, span segments, etc.) and
  // for hand-constructed regions in tests.
  std::optional<NotationId> owner_system_id = std::nullopt;
  std::optional<NotationId> owner_staff_id  = std::nullopt;

  [[nodiscard]] bool operator==(const HitRegion&) const = default;
};

struct HitResult {
  NotationId id;
  NotationId semantic_id;
  HitRole    role = HitRole::kSystem;

  [[nodiscard]] bool operator==(const HitResult&) const = default;
};

struct VoiceLayout {
  Voice       voice;
  NotationId  id;
  std::size_t event_count = 0;

  [[nodiscard]] bool operator==(const VoiceLayout&) const = default;
};

struct StaffSystemLayout {
  TrackId                   track_id;
  StaveId                   stave_id;
  NotationId                id;
  NotationRect              bounds;
  std::vector<NotationRect> measure_bounds;
  std::vector<VoiceLayout>  voices;

  [[nodiscard]] bool operator==(const StaffSystemLayout&) const = default;
};

struct MeasureLayout {
  std::size_t  ordinal = 0;
  NotationId   id;
  NotationRect bounds;

  [[nodiscard]] bool operator==(const MeasureLayout&) const = default;
};

struct SystemLayout {
  std::size_t                    first_measure = 0;
  NotationId                     id;
  NotationRect                   bounds;
  std::vector<MeasureLayout>     measures;
  std::vector<StaffSystemLayout> staves;

  [[nodiscard]] bool operator==(const SystemLayout&) const = default;
};

struct NotationLayout {
  NodeId                       node_id;
  NotationRect                 bounds;
  std::vector<SystemLayout>    systems;
  std::vector<NotationCommand> commands;
  std::vector<HitRegion>       hit_regions;

  struct Diagnostic {
    NotationEntityId entity_id;
    std::string      policy;

    [[nodiscard]] bool operator==(const Diagnostic&) const = default;
  };

  // Invalid references are omitted, while unaffected notation is retained.
  // Each omission is recorded here in deterministic traversal order.
  std::vector<Diagnostic> diagnostics;

  // Highest priority wins. Ties use the smallest area, then semantic ID,
  // then region ID, all ascending. This order is independent of insertion
  // order and is therefore reproducible for overlapping voices.
  [[nodiscard]] std::optional<HitResult> hit_test(NotationPoint point) const;

  // Layout production accepts only finite metrics and options and verifies the
  // complete retained result before returning it. This query is also useful to
  // consumers that assemble or transform commands of their own.
  [[nodiscard]] bool geometry_is_finite() const;

  [[nodiscard]] bool operator==(const NotationLayout&) const = default;
};

struct NotationLayoutOptions {
  // Rejecting larger values keeps coordinate arithmetic bounded and catches
  // accidental pixel/staff-unit mismatches before producing geometry.
  static constexpr double kMaximumCoordinate = 1'000'000.0;

  double system_width          = 960.0;
  double left_margin           = 24.0;
  double right_margin          = 24.0;
  double top_margin            = 24.0;
  double bottom_margin         = 24.0;
  double staff_space           = 10.0;
  double stave_gap             = 50.0;
  double system_gap            = 70.0;
  double minimum_measure_width = 120.0;
  double whole_note_spacing    = 180.0;

  // Measures are greedily assigned in timeline order. A measure's width is
  // the maximum of the configured rhythmic width and a deterministic
  // content width computed from signature extents, glyph bounds/advances,
  // accidentals, dots, and rhythmic density.
  // Equality with the remaining content width stays on the current system;
  // an individually wider measure occupies one system without truncation.
  // No stretch/justification is performed, making every break and x position
  // independent of later systems and deterministic across platforms.
  // Every option must be finite and no greater than kMaximumCoordinate.
  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] bool operator==(const NotationLayoutOptions&) const = default;
};

enum class NotationLayoutError : std::uint8_t {
  kNone,
  kNodeNotFound,
  kTimelineMissing,
  kLaneMissing,
  kInvalidOptions,
  kInvalidMetrics,
  kInvalidGeometry,
  kInvalidInvalidation,
};

struct NotationLayoutResult {
  NotationLayoutError           error = NotationLayoutError::kNone;
  std::optional<NotationLayout> layout;

  [[nodiscard]] explicit operator bool() const noexcept {
    return layout.has_value();
  }
};

[[nodiscard]] NotationLayoutResult layout_notation(
    const Project& project, NodeId node_id, const GlyphMetrics& metrics,
    const NotationLayoutOptions& options = {});

enum class NotationInvalidationKind : std::uint8_t {
  // Content or markings wholly contained by [first_measure, last_measure].
  // Voice duration before and after the range, measure metadata, and every
  // cross-range reference must be unchanged.
  kLocalContent,
  // A tie, beam override, slur, hairpin, tuplet, grace group, or pedal span
  // changed. The range must include every measure touched by the old and new
  // form of the span.
  kCrossMeasureSpan,
  // Clef, key, or time context changed at first_measure. Layout is invalidated
  // from there to the end because prevailing context is inherited.
  kContext,
  // Measures were inserted, removed, reordered, or changed length. The first
  // changed ordinal is supplied; all later systems are invalidated.
  kMeasureStructure,
  // Active/archive state, track order, staff definitions, or lane availability
  // changed. The complete node is invalidated.
  kTrackStaffArchive,
  // Any NotationLayoutOptions value or GlyphMetrics implementation/state
  // changed. The complete node is invalidated.
  kLayoutOptionsOrMetrics,
  // Discard all retained state, including after replacing the project/node.
  kFullReset,
};

struct NotationInvalidation {
  NotationInvalidationKind kind = NotationInvalidationKind::kLocalContent;
  std::size_t              first_measure = 0;
  std::size_t              last_measure  = 0;
};

struct NotationLayoutWork {
  std::vector<std::size_t> visited_measures;
  std::vector<std::size_t> rebuilt_measures;
  std::vector<std::size_t> reused_measures;
  // System IDs are represented by their stable first-measure ordinals.
  std::vector<std::size_t> rebuilt_systems;
  std::vector<std::size_t> reused_systems;
  // Records inspected by the engraving index/fragment refresh. These do not
  // count domain mutation, validation bookkeeping, invalidation discovery,
  // revision-delta merging, retained pedal scans, or assembly/copying of the
  // returned complete layout, and are not total CPU-work counters.
  std::size_t event_visits     = 0;
  std::size_t reference_visits = 0;

  [[nodiscard]] bool operator==(const NotationLayoutWork&) const = default;
};

struct IncrementalNotationLayoutResult {
  NotationLayoutError           error = NotationLayoutError::kNone;
  std::optional<NotationLayout> layout;
  NotationLayoutWork            work;

  [[nodiscard]] explicit operator bool() const noexcept {
    return layout.has_value();
  }
};

// Retains complete per-system layout, commands, hit regions, and diagnostics.
// update() consumes the exhaustive invalidation list since the previous
// successful call. Empty means that the domain, options, and metrics are
// unchanged. Malformed ranges fail without changing the cache. Structural
// state and options are also compared with the retained snapshot; an omitted
// broad invalidation can therefore never reuse stale geometry for those
// detectable changes. Content mutations are intentionally consumed explicitly
// because Project exposes no mutation generation; callers must obey the range
// contracts above. A failed update leaves the last successful snapshot intact.
class NotationLayoutCache final {
 public:
  NotationLayoutCache();
  NotationLayoutCache(NotationLayoutCache&&) noexcept;
  NotationLayoutCache& operator=(NotationLayoutCache&&) noexcept;
  ~NotationLayoutCache();

  NotationLayoutCache(const NotationLayoutCache&)            = delete;
  NotationLayoutCache& operator=(const NotationLayoutCache&) = delete;

  [[nodiscard]] IncrementalNotationLayoutResult update(
      const Project& project, NodeId node_id, const GlyphMetrics& metrics,
      const NotationLayoutOptions&             options,
      const std::vector<NotationInvalidation>& invalidations);

  void reset() noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Whether the palette's next entry inserts a sounded Note/Chord or a Rest.
// Rests still reuse Duration (graphscore/domain/notation_event.hpp's
// make_rest()); there is no separate rest-value enum.
enum class NotePaletteEntryKind : std::uint8_t {
  kNote = 0,
  kRest,
};

// What the palette's currently armed selection would produce if the
// composer entered a note/rest right now: everything later pointer/keyboard
// entry phases (L35-L37) need to build a Note, Chord, or Rest and stage its
// markings, without re-deriving palette state themselves. There is no
// separate note-value field: `duration.base()` is that datum, and storing
// it twice would let a defaulted operator== treat semantically identical
// specs as unequal if the two ever desynced.
struct NotePaletteEntrySpec {
  Duration                          duration;
  NotePaletteEntryKind              entry_kind = NotePaletteEntryKind::kNote;
  Voice                             voice;
  std::vector<Articulation>         articulations;
  std::optional<Dynamic>            dynamic;
  std::optional<HairpinDirection>   hairpin;
  bool                              tie_to_next = false;
  bool                              slur        = false;
  bool                              pedal       = false;
  std::optional<BeamOverride::Kind> beam_override;

  [[nodiscard]] bool operator==(const NotePaletteEntrySpec&) const = default;
};

// The composer's current note-palette selection: a toolkit-neutral,
// value-semantic model of every in-scope palette control (durations through
// sixty-fourth, dots, rests, voices, tuplets, ties/slurs, articulations,
// dynamics, hairpins, pedal, and beam overrides). This type only models
// what is armed; it never mutates Project, issues a Command, or performs
// hit-testing/preview geometry -- those are later phases (see
// docs/plan/05-notation-editor.md's "Note palette and pointer entry" and
// "Structural editing" deliverables).
//
// Factories/mutators that can produce an invalid state reject it and return
// std::nullopt, matching Duration::create/Voice::create/TupletRatio::create;
// mutators whose argument is already a validated type (Voice, TupletRatio,
// Dynamic, HairpinDirection, BeamOverride::Kind) cannot fail and return
// NotePaletteState directly.
class NotePaletteState {
 public:
  constexpr NotePaletteState() = default;

  // `dots` is the only argument this factory itself validates; `tuplet`, if
  // present, was already validated by TupletRatio::create, matching how
  // Duration::create documents the same division of responsibility.
  [[nodiscard]] static constexpr std::optional<NotePaletteState> create(
      NoteValue note_value, std::uint8_t dots, NotePaletteEntryKind entry_kind,
      Voice voice, std::optional<TupletRatio> tuplet = std::nullopt) noexcept {
    if (dots > Duration::kMaxDots)
      return std::nullopt;
    return NotePaletteState(note_value, dots, entry_kind, voice, tuplet);
  }

  [[nodiscard]] constexpr NoteValue note_value() const noexcept {
    return note_value_;
  }

  [[nodiscard]] constexpr std::uint8_t dots() const noexcept { return dots_; }

  [[nodiscard]] constexpr NotePaletteEntryKind entry_kind() const noexcept {
    return entry_kind_;
  }

  [[nodiscard]] constexpr Voice voice() const noexcept { return voice_; }

  [[nodiscard]] constexpr const std::optional<TupletRatio>& tuplet()
      const noexcept {
    return tuplet_;
  }

  [[nodiscard]] constexpr NotePaletteState with_note_value(
      NoteValue note_value) const noexcept {
    NotePaletteState next = *this;
    next.note_value_      = note_value;
    return next;
  }

  [[nodiscard]] constexpr std::optional<NotePaletteState> with_dots(
      std::uint8_t dots) const noexcept {
    if (dots > Duration::kMaxDots)
      return std::nullopt;
    NotePaletteState next = *this;
    next.dots_            = dots;
    return next;
  }

  [[nodiscard]] constexpr NotePaletteState with_entry_kind(
      NotePaletteEntryKind entry_kind) const noexcept {
    NotePaletteState next = *this;
    next.entry_kind_      = entry_kind;
    return next;
  }

  [[nodiscard]] constexpr NotePaletteState with_voice(
      Voice voice) const noexcept {
    NotePaletteState next = *this;
    next.voice_           = voice;
    return next;
  }

  [[nodiscard]] constexpr NotePaletteState with_tuplet(
      std::optional<TupletRatio> tuplet) const noexcept {
    NotePaletteState next = *this;
    next.tuplet_          = tuplet;
    return next;
  }

  // True if `articulation` is currently armed.
  [[nodiscard]] constexpr bool has_articulation(
      Articulation articulation) const noexcept {
    return (articulation_mask_ & articulation_bit(articulation)) != 0;
  }

  // Accent and marcato freely combine with anything; staccato,
  // staccatissimo, and tenuto (is_duration_articulation(),
  // graphscore/core/articulation.hpp) are mutually exclusive on one event,
  // matching the referential validator's rule
  // (graphscore/domain/notation_validation.cpp). Arming an already-armed
  // articulation is a no-op success, not a conflict. An out-of-range
  // `articulation` (e.g. from an unchecked toolkit-binding index) is
  // rejected rather than silently accepted: this class's documented
  // contract is that a factory/mutator either changes the state as asked or
  // returns std::nullopt, and articulation_bit()'s 0-bit fallback would
  // otherwise make this call report success while arming nothing.
  [[nodiscard]] constexpr std::optional<NotePaletteState>
  with_articulation_armed(Articulation articulation) const noexcept {
    if (static_cast<std::uint8_t>(articulation) >= kArticulationCount)
      return std::nullopt;
    if (is_duration_articulation(articulation) &&
        has_conflicting_duration_articulation(articulation))
      return std::nullopt;
    NotePaletteState next   = *this;
    next.articulation_mask_ = static_cast<std::uint8_t>(
        next.articulation_mask_ | articulation_bit(articulation));
    return next;
  }

  [[nodiscard]] constexpr NotePaletteState with_articulation_disarmed(
      Articulation articulation) const noexcept {
    NotePaletteState next   = *this;
    next.articulation_mask_ = static_cast<std::uint8_t>(
        next.articulation_mask_ & ~articulation_bit(articulation));
    return next;
  }

  [[nodiscard]] std::vector<Articulation> armed_articulations() const;

  [[nodiscard]] constexpr const std::optional<Dynamic>& dynamic()
      const noexcept {
    return dynamic_;
  }

  [[nodiscard]] constexpr NotePaletteState with_dynamic(
      std::optional<Dynamic> dynamic) const noexcept {
    NotePaletteState next = *this;
    next.dynamic_         = dynamic;
    return next;
  }

  [[nodiscard]] constexpr const std::optional<HairpinDirection>&
  hairpin_direction() const noexcept {
    return hairpin_;
  }

  [[nodiscard]] constexpr NotePaletteState with_hairpin_direction(
      std::optional<HairpinDirection> hairpin) const noexcept {
    NotePaletteState next = *this;
    next.hairpin_         = hairpin;
    return next;
  }

  [[nodiscard]] constexpr bool tie_to_next_armed() const noexcept {
    return tie_to_next_;
  }

  [[nodiscard]] constexpr NotePaletteState with_tie_to_next_armed(
      bool armed) const noexcept {
    NotePaletteState next = *this;
    next.tie_to_next_     = armed;
    return next;
  }

  [[nodiscard]] constexpr bool slur_armed() const noexcept { return slur_; }

  [[nodiscard]] constexpr NotePaletteState with_slur_armed(
      bool armed) const noexcept {
    NotePaletteState next = *this;
    next.slur_            = armed;
    return next;
  }

  [[nodiscard]] constexpr bool pedal_armed() const noexcept { return pedal_; }

  [[nodiscard]] constexpr NotePaletteState with_pedal_armed(
      bool armed) const noexcept {
    NotePaletteState next = *this;
    next.pedal_           = armed;
    return next;
  }

  [[nodiscard]] constexpr const std::optional<BeamOverride::Kind>&
  beam_override_kind() const noexcept {
    return beam_override_;
  }

  [[nodiscard]] constexpr NotePaletteState with_beam_override_kind(
      std::optional<BeamOverride::Kind> kind) const noexcept {
    NotePaletteState next = *this;
    next.beam_override_   = kind;
    return next;
  }

  // Composes note_value()/dots()/tuplet() through Duration::create(). This
  // is provably total, not merely "shouldn't fail in practice": create()
  // and with_dots() are this type's only ways to set dots_, and both
  // already reject anything outside [0, Duration::kMaxDots] -- the one
  // condition under which Duration::create() itself can return
  // std::nullopt (graphscore/core/duration.hpp) -- and tuplet_ is always
  // either std::nullopt or an already-validated TupletRatio. So
  // Duration::create(note_value_, dots_, tuplet_) can never fail here, and
  // this returns Duration by value rather than pushing a dead optional
  // branch onto every caller. clang-tidy's dataflow analysis cannot see
  // that class invariant across this call, hence the suppression below.
  [[nodiscard]] constexpr Duration resolved_duration() const noexcept {
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    return *Duration::create(note_value_, dots_, tuplet_);
  }

  // What entering a note/rest right now would produce, for later pointer/
  // keyboard entry phases (L35-L37) to consume directly. Always succeeds,
  // for the same reason resolved_duration() does.
  [[nodiscard]] NotePaletteEntrySpec next_entry_spec() const;

  [[nodiscard]] bool operator==(const NotePaletteState&) const = default;

 private:
  constexpr NotePaletteState(NoteValue note_value, std::uint8_t dots,
                             NotePaletteEntryKind entry_kind, Voice voice,
                             std::optional<TupletRatio> tuplet) noexcept
      : note_value_(note_value),
        dots_(dots),
        entry_kind_(entry_kind),
        voice_(voice),
        tuplet_(tuplet) {}

  // Bit position of `articulation` within articulation_mask_. Returns 0 for
  // any Articulation value at or past kArticulationCount instead of
  // shifting by an out-of-range amount (undefined behavior) or aliasing an
  // in-range slot: no real enumerator produces such a value, but this is an
  // inline function in a public header, reachable from a toolkit binding
  // that feeds an unchecked integer through
  // static_cast<Articulation>(control_index). A 0 bit makes
  // has_articulation() report false and with_articulation_disarmed() a
  // no-op for an out-of-range value; with_articulation_armed() rejects such
  // a value outright (see its own comment) rather than relying on this
  // fallback, but the bounds check here is still what prevents the
  // out-of-range shift, so it stays regardless of that caller's own policy.
  [[nodiscard]] static constexpr std::uint8_t articulation_bit(
      Articulation articulation) noexcept {
    const std::uint8_t index = static_cast<std::uint8_t>(articulation);
    if (index >= kArticulationCount)
      return 0;
    return static_cast<std::uint8_t>(1U << index);
  }

  [[nodiscard]] constexpr bool has_conflicting_duration_articulation(
      Articulation articulation) const noexcept {
    for (const Articulation other : kAllArticulations) {
      if (other != articulation && is_duration_articulation(other) &&
          has_articulation(other))
        return true;
    }
    return false;
  }

  NoteValue                         note_value_ = NoteValue::kQuarter;
  std::uint8_t                      dots_       = 0;
  NotePaletteEntryKind              entry_kind_ = NotePaletteEntryKind::kNote;
  Voice                             voice_;
  std::optional<TupletRatio>        tuplet_;
  std::uint8_t                      articulation_mask_ = 0;
  std::optional<Dynamic>            dynamic_;
  std::optional<HairpinDirection>   hairpin_;
  bool                              tie_to_next_ = false;
  bool                              slur_        = false;
  bool                              pedal_       = false;
  std::optional<BeamOverride::Kind> beam_override_;
};

// Pins the default state's documented contract (quarter note, no dots, note
// entry kind, voice 1, nothing armed) and that NotePaletteState is usable in
// a constexpr context end to end, both at zero runtime cost.
static_assert(NotePaletteState().note_value() == NoteValue::kQuarter);
static_assert(NotePaletteState().dots() == 0);
static_assert(NotePaletteState().entry_kind() == NotePaletteEntryKind::kNote);
static_assert(NotePaletteState().voice() == *Voice::create(1));
static_assert(!NotePaletteState().tuplet().has_value());
static_assert(!NotePaletteState().has_articulation(Articulation::kAccent));
static_assert(!NotePaletteState().has_articulation(Articulation::kMarcato));
static_assert(!NotePaletteState().has_articulation(Articulation::kStaccato));
static_assert(
    !NotePaletteState().has_articulation(Articulation::kStaccatissimo));
static_assert(!NotePaletteState().has_articulation(Articulation::kTenuto));
static_assert(!NotePaletteState().dynamic().has_value());
static_assert(!NotePaletteState().hairpin_direction().has_value());
static_assert(!NotePaletteState().tie_to_next_armed());
static_assert(!NotePaletteState().slur_armed());
static_assert(!NotePaletteState().pedal_armed());
static_assert(!NotePaletteState().beam_override_kind().has_value());
static_assert(NotePaletteState().resolved_duration() ==
              *Duration::create(NoteValue::kQuarter, 0));
static_assert(NotePaletteState()
                  .with_articulation_armed(Articulation::kAccent)
                  .has_value());

// The pointer-entry preview from "Note palette and pointer entry"
// (docs/plan/05-notation-editor.md): a note/rest glyph plus dots shown at
// the candidate staff pitch and nearest valid onset before the composer
// clicks. `commands` is a standalone list, deliberately never appended to
// NotationLayout::commands: NotationCommand carries no color or alpha
// field, and ADR 0003 forbids graphscore_notation from depending on
// graphscore_rendering, where RgbaColor lives. So the preview's documented
// yellow, semitransparent appearance is not a property of these commands --
// it belongs to a later, separate rasterize_notation pass over `commands`
// with its own yellow, semitransparent RasterOptions, kept apart from the
// opaque pass that rasterizes NotationLayout::commands. Producing a preview
// never mutates the Project, the NotationLayout it reads bounds from, or
// any cache.
struct NotationPreview {
  std::vector<NotationCommand> commands;
  TrackId                      track_id;
  StaveId                      stave_id;
  Voice                        voice;
  NotePaletteEntryKind         entry_kind = NotePaletteEntryKind::kNote;

  // The staff step the pointer resolves to, always spelled natural: a click
  // selects a diatonic staff position, never an accidental, so this never
  // carries invented accidental inference. Meaningful only when entry_kind
  // is kNote -- a Rest has no pitch, so a rest preview always leaves this
  // std::nullopt rather than reporting a placeholder staff position as
  // though it meant something.
  std::optional<SpelledPitch> candidate_pitch;

  // The nearest existing rhythmic-event onset (including a normalized rest)
  // in the armed voice/measure the pointer resolves to. There is no metric
  // grid derived from the armed duration: a measure covered by one
  // whole-measure rest offers exactly one onset (its own start), so any
  // click within it previews at the measure start.
  Rational candidate_onset;

  [[nodiscard]] bool operator==(const NotationPreview&) const = default;

  // Applies the same finiteness/bound rules
  // NotationLayout::geometry_is_finite() applies to its own commands, so
  // preview geometry can be validated the same way without folding preview
  // commands into a real layout.
  [[nodiscard]] bool geometry_is_finite() const;
};

// Resolves `point` against `layout` (produced by a prior layout_notation()/
// NotationLayoutCache::update() call for the same project/node) to a
// candidate staff pitch/rest position and nearest valid onset, and builds
// the standalone preview geometry for what `palette` would insert there.
// `palette`'s duration, dots, note-vs-rest kind, and voice are the sole
// source of truth for what would be entered; this never re-derives them.
//
// Explicit voice-stream workflow: when the armed voice is entirely empty
// (VoiceContent::events().empty()), onset resolution behaves exactly as if
// that voice had already been filled by
// decompose_measure_aligned_rests(voice_content.hpp) -- the same
// measure-aligned rest tiling make_note_entry_command below actually
// applies on the composer's click. This function reads that hypothetical
// fill's onset shape (never the Rest ids themselves) on every pointer
// move, so it calls the duration-only core,
// decompose_measure_aligned_rest_durations, directly rather than minting
// and immediately discarding a fresh Rest id per term; both derive from
// the same underlying tiling, so they can never disagree on shape. The
// nearest onset among that hypothetical fill's durations within the
// resolved measure is used, so the very first click into a never-touched
// voice (e.g. arming "Voice 2" for the first time) previews at a real
// onset instead of failing outright. This narrow carve-out applies only
// to a voice with zero events; a voice that already holds some content
// but has no event boundary within the resolved measure specifically (a
// short/incomplete voice) is unaffected and still falls through to the
// std::nullopt case below.
//
// Returns std::nullopt when `point` falls outside every system, when it
// falls outside every staff's own ledger/marking lane (the staff's five
// lines plus a bounded allowance above and below, matching the layout's own
// marking budget -- never an unbounded nearest-staff guess across the far
// larger system extent), when the armed voice is non-empty but has no
// onset at all within the resolved measure (a measure with no event
// boundary in it for that voice), when the armed voice is empty and its
// hypothetical measure-aligned fill cannot be produced exactly, or when
// (kNote only) the resolved staff step falls outside SpelledPitch's valid
// octave range -- never a clamped value in any of these cases. A pure
// query: never mutates `project`, `layout`, or any cache, even when it
// internally computes the hypothetical fill's shape for an empty voice.
[[nodiscard]] std::optional<NotationPreview> preview_note_entry(
    const Project& project, const NotationLayout& layout,
    const NotePaletteState& palette, NotationPoint point);

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
//                     in src/notation/notation.cpp) rather than a universal
//                     four-staff-space band.  A click on the drawn curve
//                     itself still selects the tie; a click away from the
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
//                     backward from the hit's anchor, while the preceding
//                     event carries an equal TupletRatio, before building
//                     the selection.
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
// cross-checks at notation.cpp:4380-4389), not a live path for a layout
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
// src/notation/notation.cpp; this function delegates to that same helper
// rather than copying the rule. (extend_measure_selection, also in
// src/notation/notation.cpp, restates this order in its own in-place
// filter loop rather than calling the shared helper; it pre-dates this
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
// kRangeSelectionGridDenominator (src/notation/notation.cpp) is trivially
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
// -- are factored into a private helper (notation.cpp) shared with
// resolve_range_selection_spec, extend_range_selection, and
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

// Constructs a reversible domain command for the note-entry pointer action
// described in docs/plan/05-notation-editor.md ("Clicking an existing
// rhythmic event in the selected voice changes its selected duration;
// clicking another pitch at the onset builds a chord").
//
// `position` is the onset of the existing event to replace (typically
// NotationPreview::candidate_onset from the same click). `armed` is the
// palette's current next_entry_spec(); its `.voice` field is the sole
// source of truth for which voice this command targets.
// `candidate_pitch` is NotationPreview::candidate_pitch from the same
// click; it must be present when armed.entry_kind is kNote, and is
// ignored when kRest.
//
// Duration-only replacement preserves the existing event's top-level
// identity (Note::id, Chord::id, Rest::id) and every existing embedded
// identity (ChordNote::id).  A new notehead on an existing event adds a
// ChordNote with a fresh NotationEntityId; the existing ChordNote ids are
// carried forward unchanged.  A Note promoted to a Chord keeps the Note's
// id as one of the Chord's ChordNotes.  A Rest at `position` is replaced
// with a Note when kNote is armed; a Note or Chord is replaced with a Rest
// when kRest is armed.  Duplicate-pitch clicks are treated as
// duration-only: neither duplicate noteheads nor duplicate ChordNotes are
// created.
//
// Explicit voice-stream workflow: this is the composer's only path to
// start a rhythmic line in a voice that has never held anything (a stave
// always carries four structural VoiceContent slots, but an untouched one
// is empty until something fills it, and every case above operates on an
// *existing* event boundary that an empty voice does not have). When the
// armed voice is entirely empty, `position` is matched against the onsets
// of that voice's hypothetical measure-aligned rest fill
// (decompose_measure_aligned_rests, voice_content.hpp -- the same
// computation preview_note_entry above previews) rather than against any
// existing event, and on a match the returned Command is a
// CommandTransaction (command_transaction.hpp) composing, in order, a
// CreateVoiceStreamCommand that materializes the fill followed by the same
// SetEventCommand the non-empty path above would build at that onset.
// Because SetEventCommand replaces by exact Rational position rather than
// by id, it needs no knowledge of the ids CreateVoiceStreamCommand mints.
// The transaction is one CommandHistory entry: a single undo removes the
// note (or rest) and the entire stream together, returning the voice to
// completely empty, exactly as if the composer had never clicked; redo is
// id-for-id identical to the original execute. Arming kRest into an empty
// voice still materializes the stream -- the result is simply a voice of
// normalized rests -- rather than being special-cased into a no-op, so
// arming a rest duration and clicking an empty voice has the same
// voice-creating effect as arming a note.
//
// Returns nullptr when the armed voice is non-empty and no event starts at
// `position` in it, when the armed voice is empty and `position` is not an
// onset of its hypothetical measure-aligned fill (or that fill cannot be
// produced exactly), when the armed voice is empty and the node has no
// NodeTimeline (the non-empty path above never needs one, so this is a
// failure mode unique to the empty-voice path), when armed.entry_kind is
// kNote but candidate_pitch is absent, when the constructed replacement
// would violate domain invariants (fewer than two notes in a Chord,
// duplicate IDs, etc.), or when `project` does not own the specified
// node/track/stave.  The non-empty path's behavior (including every
// rejection above it) is unchanged by this addition: it still returns a
// bare SetEventCommand, never a transaction.  The returned command has not
// been executed; the caller applies it through a CommandHistory.
//
// This function is toolkit-neutral, consumes only the armed entry kind/
// duration/voice and candidate pitch, and does not apply markings
// (articulations, dynamics, etc.) — those are Structural editing scope.
[[nodiscard]] std::unique_ptr<Command> make_note_entry_command(
    const Project& project, NodeId node_id, TrackId track_id, StaveId stave_id,
    Rational position, const NotePaletteEntrySpec& armed,
    std::optional<SpelledPitch> candidate_pitch);

// Constructs a reversible domain command for the keyboard pitch action
// described in docs/plan/05-notation-editor.md M5-phase-20: "Up/Down moves a
// selected notehead one diatonic staff step and preserves its accidental."
//
// `notehead` is the single selected NoteheadItem
// (graphscore/domain/selection.hpp); `direction` is up or down. The returned
// MoveNoteheadCommand re-resolves `notehead.entity` against the project at
// execute time, so a stale identity fails atomically rather than mutating a
// different notehead. Pitch arithmetic and identity preservation live entirely
// in MoveNoteheadCommand; this helper owns only the selection-to-command
// translation, mirroring make_note_entry_command.
//
// Returns nullptr when `notehead` does not name a single valid notehead in the
// project -- an unknown node/track/stave/lane, an archived track, or an entity
// that is not a Note/ChordNote/GraceNote in the addressed voice -- which is
// exactly validate_selection's own notehead-arm check. Building the command
// never mutates the project; the caller applies it through a CommandHistory.
[[nodiscard]] std::unique_ptr<Command> make_move_notehead_command(
    const Project& project, const NoteheadItem& notehead,
    NoteheadStepDirection direction);

// Constructs the reversible command for deleting one selected notehead. A
// complete Note or Chord becomes a same-duration Rest; removing one pitch from
// a Chord preserves the remaining pitches and contracts a two-note Chord to a
// normal Note. Grace-note deletion removes its note from its GraceGroup.
// Returns nullptr unless `notehead` is a valid single notehead selection.
[[nodiscard]] std::unique_ptr<Command> make_delete_notehead_command(
    const Project& project, const NoteheadItem& notehead);

// Resolves the selection to hold after deleting `notehead`, using the state
// immediately before the delete. The preceding event onset in the same voice
// is selected when one exists; otherwise an insertion caret is placed at the
// deleted event's onset. The returned selection is intended to be installed
// after the delete command succeeds.
[[nodiscard]] std::optional<Selection> selection_after_notehead_delete(
    const Project& project, const NoteheadItem& notehead);

// Constructs the reversible command for the keyboard action described in
// docs/plan/05-notation-editor.md M5-phase-23: "`R` converts the entire
// selected note/chord event to an equal-duration rest."
//
// Takes the complete Selection (graphscore/domain/selection.hpp) rather
// than a single item type, because deciding whether `selection` is even
// eligible -- and, when it is a NoteheadSet, whether its entity names a
// top-level Note, a ChordNote (whose WHOLE containing Chord then converts,
// unlike make_delete_notehead_command's per-pitch semantics), or a
// GraceNote (rejected) -- is itself the arm-dispatch this function owns; a
// caller holding a full Selection would otherwise have to replicate that
// dispatch before it could pick which single-item overload to call.
// Accepts a single-item NoteheadSet or a single-item ChordSet;
// VoiceContent::position_of_event resolves either arm's entity to the
// exact Rational position ConvertEventToRestCommand addresses.
//
// Every other selection is a no-op returning nullptr: an empty or
// multi-item set on either accepted arm, any other Selection arm (RestSet
// -- already a rest -- MarkingSet, FullMeasureSet, ArbitraryRangeSet,
// InsertionCaretSet, NodeSet, ConnectorSet), no selection at all, a stale
// selection that no longer resolves, and a NoteheadSet entity that names a
// GraceNote. Building the command never mutates the project; the caller
// applies it through a CommandHistory.
[[nodiscard]] std::unique_ptr<Command> make_convert_event_to_rest_command(
    const Project& project, const Selection& selection);

// Resolves the selection to hold after converting `selection`'s single
// note/chord event to a rest, using the state immediately before the
// conversion: a single-item RestSet addressing the converted event's own
// preserved NotationEntityId. Returns std::nullopt under exactly the same
// conditions make_convert_event_to_rest_command returns nullptr for, so a
// caller checks one before invoking the other without needing to check
// both. The returned selection is intended to be installed after the
// convert-to-rest command succeeds.
[[nodiscard]] std::optional<Selection> selection_after_convert_to_rest(
    const Project& project, const Selection& selection);

// The short audition request for the same keyboard pitch action
// make_move_notehead_command above builds a command for, mirroring
// audition_for_note_entry: this milestone produces the request as a value and
// nothing plays it (graphscore_writer_audio consumes it in Milestone 08).
//
// The request sounds the single post-edit sounding MIDI pitch of the moved
// notehead (a tied notehead moves its whole chain, every member of which
// shares one pitch, so the one post-edit pitch is still the whole audible
// change), through the notehead's own track at the project's default dynamic.
// A chord notehead move auditions that notehead's post-edit pitch alone -- not
// the whole chord -- because the composer is re-pitching one notehead, unlike
// note entry, where building a harmony auditions the whole resulting chord.
//
// A pure query over the PRE-execution project: it never mutates `project` and
// builds no command. It returns std::nullopt -- auditions nothing -- whenever
// the move itself would fail or change nothing audible: an invalid/stale
// notehead (the same notehead-arm check make_move_notehead_command uses), or
// a step that would leave the SpelledPitch/MIDI range (the boundary the
// command also rejects atomically). A caller invokes it before executing the
// command and discards the request when execution fails.
[[nodiscard]] std::optional<NoteAuditionRequest> audition_for_notehead_move(
    const Project& project, const NoteheadItem& notehead,
    NoteheadStepDirection direction);

// Constructs a reversible domain command for the keyboard accidental action
// described in docs/plan/05-notation-editor.md M5-phase-21: "`-` and `=` step
// through double-flat, flat, natural, sharp, and double-sharp."
//
// `notehead` is the single selected NoteheadItem
// (graphscore/domain/selection.hpp); `direction` is one rung down (`-`) or up
// (`=`) that ladder. The returned StepAccidentalCommand re-resolves
// `notehead.entity` against the project at execute time, so a stale identity
// fails atomically rather than mutating a different notehead. Spelling
// arithmetic and identity preservation live entirely in
// StepAccidentalCommand; this helper owns only the selection-to-command
// translation, mirroring make_move_notehead_command.
//
// Returns nullptr when `notehead` does not name a single valid notehead in the
// project -- an unknown node/track/stave/lane, an archived track, or an entity
// that is not a Note/ChordNote/GraceNote in the addressed voice -- which is
// exactly validate_selection's own notehead-arm check. Building the command
// never mutates the project; the caller applies it through a CommandHistory.
[[nodiscard]] std::unique_ptr<Command> make_step_accidental_command(
    const Project& project, const NoteheadItem& notehead,
    AccidentalStepDirection direction);

// The short audition request for the same keyboard accidental action
// make_step_accidental_command above builds a command for. An accidental step
// changes the sounding pitch, so it auditions exactly like a diatonic move
// (M5-phase-15's "pitch-edited notes issue a short preview request"); this
// milestone produces the request as a value and nothing plays it
// (graphscore_writer_audio consumes it in Milestone 08).
//
// The request sounds the single post-edit sounding MIDI pitch of the stepped
// notehead (a tied notehead steps its whole chain, every member of which
// shares one pitch, so the one post-edit pitch is still the whole audible
// change), through the notehead's own track at the project's default dynamic.
// A chord notehead auditions that notehead's post-edit pitch alone -- not the
// whole chord -- because the composer is re-spelling one notehead.
//
// A pure query over the PRE-execution project: it never mutates `project` and
// builds no command. It returns std::nullopt -- auditions nothing -- whenever
// the step itself would fail: an invalid/stale notehead (the same
// notehead-arm check make_step_accidental_command uses), or a step off either
// end of the ladder or out of the sounding MIDI range (the boundaries the
// command also rejects atomically). A caller invokes it before executing the
// command and discards the request when execution fails.
[[nodiscard]] std::optional<NoteAuditionRequest> audition_for_accidental_step(
    const Project& project, const NoteheadItem& notehead,
    AccidentalStepDirection direction);

// The short audition request for the same note-entry pointer action
// make_note_entry_command above builds a command for
// (docs/plan/05-notation-editor.md: "Newly inserted or pitch-edited notes
// issue a short preview request; actual plugin audition is connected in
// Milestone 08"). The parameter list mirrors make_note_entry_command's
// exactly so a caller invokes both with the same arguments; this milestone
// produces the request as a value and nothing plays it, so there is no
// consumer in this repository yet -- graphscore_writer_audio picks it up in
// Milestone 08 (ADR 0003 assigns "note-preview insertion audition" there,
// which is also why NoteAuditionRequest itself is declared in
// graphscore_core rather than here; see core/note_audition.hpp).
//
// A pure query: it never mutates `project` and builds no command. It is
// evaluated against the PRE-execution project state, i.e. a caller calls it
// before executing make_note_entry_command's command, since it reads the
// event currently at `position` to decide what the click newly sounds.
//
// Both functions resolve the click through one shared internal branch
// resolution, so they can never disagree about which case a click falls
// into. What each branch auditions:
//
//   * a new note into an entirely empty armed voice: the one new pitch;
//   * a rest replaced by a note: the one new pitch;
//   * a note promoted to a two-note chord: BOTH pitches;
//   * a notehead added to an existing chord: EVERY pitch of the resulting
//     chord, pre-existing ones included;
//   * a same-pitch click on a note, or a duplicate-pitch click on a chord
//     (both pure duration changes): std::nullopt;
//   * armed.entry_kind == kRest: std::nullopt;
//   * any input make_note_entry_command rejects (returns nullptr for):
//     std::nullopt.
//
// The two product decisions behind that table, in the composer's terms:
// adding a pitch to a chord auditions the WHOLE resulting chord, because
// the composer is building a harmony and wants to hear it rather than the
// single pitch they clicked; and a pure duration change is SILENT, because
// no pitch was inserted and none changed, so clicking through rhythms on
// one note never re-sounds it.
//
// Velocity is velocity_for_dynamic(project.default_dynamic())
// (core/playback_mapping.hpp) and nothing more. Accent/marcato emphasis
// (apply_emphasis), hairpin interpolation, and resolving whichever
// DynamicMarking actually governs `position` in the timeline are all
// deliberately NOT applied: armed articulations and markings are not yet
// applied on note entry at all (see make_note_entry_command's last
// paragraph -- that is Structural editing scope), and position-based
// dynamic-context resolution is explicitly outside playback_mapping.hpp's
// own scope ("Scope: math only, not context resolution"). When a later
// phase resolves governing context across a timeline, this call site is
// where the richer resolution belongs.
//
// Pitch conversion: SpelledPitch::to_midi_pitch() (core/spelled_pitch.hpp)
// fails at extreme octaves. When the NEWLY INSERTED pitch does not convert,
// this returns std::nullopt outright -- there is nothing meaningful to
// audition. Otherwise every pre-existing chord pitch that converts is
// included and any that does not is silently skipped, so one unsoundable
// notehead already in a chord never suppresses the audition of the pitch
// the composer just added.
[[nodiscard]] std::optional<NoteAuditionRequest> audition_for_note_entry(
    const Project& project, NodeId node_id, TrackId track_id, StaveId stave_id,
    Rational position, const NotePaletteEntrySpec& armed,
    std::optional<SpelledPitch> candidate_pitch);

}  // namespace graphscore
