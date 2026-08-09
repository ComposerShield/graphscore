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
  int          priority = 0;

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
// Returns std::nullopt when `point` falls outside every system, when it
// falls outside every staff's own ledger/marking lane (the staff's five
// lines plus a bounded allowance above and below, matching the layout's own
// marking budget -- never an unbounded nearest-staff guess across the far
// larger system extent), when the armed voice has no onset at all within
// the resolved measure (an empty voice, or a measure with no event boundary
// in it for that voice), or when (kNote only) the resolved staff step falls
// outside SpelledPitch's valid octave range -- never a clamped value in any
// of these cases. A pure query: never mutates `project`, `layout`, or any
// cache.
[[nodiscard]] std::optional<NotationPreview> preview_note_entry(
    const Project& project, const NotationLayout& layout,
    const NotePaletteState& palette, NotationPoint point);

}  // namespace graphscore
