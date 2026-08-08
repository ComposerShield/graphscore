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

}  // namespace graphscore
