// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <graphscore/notation/notation_types.hpp>

namespace graphscore {

class Project;

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
