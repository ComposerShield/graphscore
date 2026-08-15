// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <graphscore/core/strong_id.hpp>
#include <graphscore/core/voice.hpp>
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
  // notation_ids.hpp for the separate, load-bearing hit_test priority rank).
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
  kTupletColon,
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

}  // namespace graphscore
