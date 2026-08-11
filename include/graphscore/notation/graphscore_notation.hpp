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
  //   4  notehead column (stemless events only)
  //   5  every individually engraved glyph that carries a semantic id --
  //      accidentals, augmentation dots, rests, flags, articulations,
  //      dynamics, tuplet digits -- plus the stem's own widened region
  //   6  span segments: hairpin, slur, tie, pedal
  //   7  noteheads, ordinary and grace
  //
  // 0-3 are the containers a click falls through to when it names no
  // engraved object. 5-7 are the engraved objects themselves, and their
  // relative order is what makes a click on an object select that object
  // rather than whatever it was drawn on top of.
  //
  // 4 is deliberately a rank of its own between the two groups, holding
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
  // every glyph region is unconditionally correct, which is why 4 exists
  // and why nothing else may be moved into it.
  int priority = 0;

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
// it. A notehead/chord/rest hit instead names whichever staff and voice
// actually own the hit's entity id, which may differ from the armed voice:
// clicking an existing note in Voice 2 while Voice 1 is armed still selects
// that Voice 2 note.
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
//                     A tie is the case where that second consequence bites
//                     hardest, because a tie's span segment both outranks
//                     the column and is drawn far larger than the notes it
//                     joins: the engraver gives it a band four staff-spaces
//                     tall, a fixed height that does not grow with the
//                     chord. Wherever that band covers the click, the tie's
//                     own MarkingSet wins and no ChordSet is produced --
//                     and for an ordinary close-voiced chord the band
//                     covers the whole column, so this affordance does not
//                     reach a tied chord at all. That is the tie region's
//                     own geometry rather than anything about the column;
//                     it is a known limitation, pinned by a test for the
//                     close-voiced case. A chord voiced widely enough to
//                     put part of its column outside the band is neither
//                     claimed nor tested here.
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
//   kSystem/kMeasure/kStaff/kVoice hit, or no hit at all -- InsertionCaretSet
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
[[nodiscard]] std::optional<Selection> resolve_range_selection(
    const Project& project, const NotationLayout& layout, NotationPoint anchor,
    NotationPoint focus);

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
