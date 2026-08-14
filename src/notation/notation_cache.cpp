// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/project.hpp>
#include <graphscore/notation/notation_layout.hpp>

#include "layout_index.hpp"
#include "measure_math.hpp"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace graphscore {

struct NotationLayoutCache::Impl {
  std::vector<SystemFragment> fragments;
  std::optional<NodeTimeline> timeline;
  std::vector<std::string>    track_structure;
  NotationLayoutOptions       options;
  NodeId                      node_id;
  LayoutIndex                 index;
  std::vector<double>         cached_widths;
  bool                        initialized = false;
};

namespace {

[[nodiscard]] std::vector<std::string> track_structure(const Project& project,
                                                       const Node&    node) {
  std::vector<std::string> result;
  for (const Track& track : project.active_tracks()) {
    const TrackLane* const lane = node.lane(track.id());
    result.push_back(track.id().to_string());
    for (const StaveDefinition& stave : track.layout().staves()) {
      result.push_back(
          stave.id.to_string() + "/" +
          std::to_string(static_cast<int>(stave.default_clef)) + "/" +
          (lane != nullptr && lane->has_stave(stave.id) ? "1" : "0"));
    }
  }
  return result;
}

[[nodiscard]] bool invalidation_is_well_formed(
    const NotationInvalidation& invalidation, std::size_t measure_count) {
  switch (invalidation.kind) {
    case NotationInvalidationKind::kLocalContent:
    case NotationInvalidationKind::kCrossMeasureSpan:
      return invalidation.first_measure <= invalidation.last_measure &&
             invalidation.last_measure < measure_count;
    case NotationInvalidationKind::kContext:
    case NotationInvalidationKind::kMeasureStructure:
      return invalidation.first_measure < measure_count &&
             invalidation.last_measure == invalidation.first_measure;
    case NotationInvalidationKind::kTrackStaffArchive:
    case NotationInvalidationKind::kLayoutOptionsOrMetrics:
    case NotationInvalidationKind::kFullReset:
      return invalidation.first_measure == 0 && invalidation.last_measure == 0;
  }
}

void add_invalid_system(std::vector<std::size_t>& systems,
                        std::size_t               first_measure) {
  if (std::ranges::find(systems, first_measure) == systems.end()) {
    systems.push_back(first_measure);
  }
}

}  // namespace

NotationLayoutCache::NotationLayoutCache() : impl_(std::make_unique<Impl>()) {}

NotationLayoutCache::NotationLayoutCache(NotationLayoutCache&&) noexcept =
    default;

NotationLayoutCache& NotationLayoutCache::operator=(
    NotationLayoutCache&&) noexcept = default;

NotationLayoutCache::~NotationLayoutCache() = default;

IncrementalNotationLayoutResult NotationLayoutCache::update(
    const Project& project, NodeId node_id, const GlyphMetrics& metrics,
    const NotationLayoutOptions&             options,
    const std::vector<NotationInvalidation>& invalidations) {
  if (!options.valid()) {
    return {NotationLayoutError::kInvalidOptions, std::nullopt, {}};
  }
  const Node* const node = project.find_node(node_id);
  if (node == nullptr) {
    return {NotationLayoutError::kNodeNotFound, std::nullopt, {}};
  }
  const NodeTimeline* const timeline = node->timeline();
  if (timeline == nullptr) {
    return {NotationLayoutError::kTimelineMissing, std::nullopt, {}};
  }
  const std::size_t measure_count = timeline->measures().measure_count();
  if (!std::ranges::all_of(invalidations, [&](const auto& invalidation) {
        return invalidation_is_well_formed(invalidation, measure_count);
      })) {
    return {NotationLayoutError::kInvalidInvalidation, std::nullopt, {}};
  }

  NotationLayoutWork             work;
  const std::vector<std::string> structure = track_structure(project, *node);
  const bool                     timeline_change_declared = std::ranges::any_of(
      invalidations, [](const NotationInvalidation& invalidation) {
        return invalidation.kind == NotationInvalidationKind::kContext ||
               invalidation.kind ==
                   NotationInvalidationKind::kMeasureStructure ||
               invalidation.kind == NotationInvalidationKind::kFullReset;
      });
  bool full_reset = !impl_->initialized || impl_->node_id != node_id ||
                    impl_->options != options ||
                    impl_->track_structure != structure;
  if (impl_->initialized && impl_->timeline != *timeline &&
      !timeline_change_declared) {
    full_reset = true;
  }
  const bool structure_change = std::ranges::any_of(
      invalidations, [](const NotationInvalidation& invalidation) {
        return invalidation.kind == NotationInvalidationKind::kMeasureStructure;
      });
  if (std::ranges::any_of(
          invalidations, [](const NotationInvalidation& invalidation) {
            return invalidation.kind ==
                       NotationInvalidationKind::kTrackStaffArchive ||
                   invalidation.kind ==
                       NotationInvalidationKind::kLayoutOptionsOrMetrics ||
                   invalidation.kind == NotationInvalidationKind::kFullReset;
          })) {
    full_reset = true;
  }
  if (full_reset || structure_change) {
    impl_->index = build_index(project, *node, timeline->measures(), metrics,
                               options, &work);
    impl_->cached_widths =
        measure_widths(timeline->measures(), impl_->index, metrics, options);
  } else {
    const std::size_t count = timeline->measures().measure_count();
    for (const NotationInvalidation& invalidation : invalidations) {
      if (invalidation.kind == NotationInvalidationKind::kLocalContent ||
          invalidation.kind == NotationInvalidationKind::kCrossMeasureSpan) {
        refresh_index_range(
            project, *node, timeline->measures(), invalidation.first_measure,
            invalidation.last_measure, metrics, options, impl_->index, work);
      } else if (invalidation.kind == NotationInvalidationKind::kContext) {
        // Context changes shift measure boundaries; rebuild event assignments
        // and reference bucket membership for the suffix.
        if (count > 0 && invalidation.first_measure < count) {
          refresh_index_range(project, *node, timeline->measures(),
                              invalidation.first_measure, count - 1, metrics,
                              options, impl_->index, work);
        }
      }
    }
    // Recompute widths for affected measures.
    for (const NotationInvalidation& invalidation : invalidations) {
      if (invalidation.kind == NotationInvalidationKind::kLocalContent ||
          invalidation.kind == NotationInvalidationKind::kCrossMeasureSpan) {
        const std::size_t wfirst = invalidation.first_measure;
        const std::size_t wlast =
            std::min(invalidation.last_measure + 1, count - 1);
        for (std::size_t measure = wfirst; measure <= wlast; ++measure) {
          impl_->cached_widths[measure] = compute_measure_width(
              measure, timeline->measures(), impl_->index, metrics, options);
        }
      } else if (invalidation.kind == NotationInvalidationKind::kContext) {
        // Recompute widths for the suffix.
        for (std::size_t measure = invalidation.first_measure; measure < count;
             ++measure) {
          impl_->cached_widths[measure] = compute_measure_width(
              measure, timeline->measures(), impl_->index, metrics, options);
        }
      }
    }
  }

  // Context changes and other timeline mutations can affect measure widths
  // without requiring a full index rebuild.  When the cached timeline
  // differs from the current one, recompute all widths.
  if (!(full_reset || structure_change) && impl_->initialized &&
      impl_->timeline != *timeline) {
    impl_->cached_widths =
        measure_widths(timeline->measures(), impl_->index, metrics, options);
  }

  const auto ranges = system_ranges(
      impl_->cached_widths,
      options.system_width - options.left_margin - options.right_margin);
  std::vector<std::size_t> invalid_systems;
  const auto               invalidate_all = [&] {
    for (const auto& [first, end] : ranges) {
      (void)end;
      add_invalid_system(invalid_systems, first);
    }
  };
  const auto invalidate_intersection = [&](std::size_t first_measure,
                                           std::size_t last_measure) {
    for (const auto& [first, end] : ranges) {
      if (first <= last_measure && end > first_measure) {
        add_invalid_system(invalid_systems, first);
      }
    }
  };
  const auto invalidate_suffix = [&](std::size_t first_measure) {
    for (const auto& [first, end] : ranges) {
      if (end > first_measure) {
        add_invalid_system(invalid_systems, first);
      }
    }
  };

  for (const NotationInvalidation& invalidation : invalidations) {
    switch (invalidation.kind) {
      case NotationInvalidationKind::kLocalContent:
      case NotationInvalidationKind::kCrossMeasureSpan:
        invalidate_intersection(invalidation.first_measure,
                                invalidation.last_measure);
        break;
      case NotationInvalidationKind::kContext:
      case NotationInvalidationKind::kMeasureStructure:
        invalidate_suffix(invalidation.first_measure);
        break;
      case NotationInvalidationKind::kTrackStaffArchive:
      case NotationInvalidationKind::kLayoutOptionsOrMetrics:
      case NotationInvalidationKind::kFullReset:
        break;
    }
  }
  if (full_reset) {
    invalid_systems.clear();
    invalidate_all();
  }

  std::vector<SystemFragment> next_fragments;
  const auto                  result = layout_internal(
      project, node_id, metrics, options, impl_->index, impl_->cached_widths,
      impl_->initialized && !full_reset ? &impl_->fragments : nullptr,
      invalid_systems, &next_fragments, &work);
  if (!result) {
    return {result.error, std::nullopt, std::move(work)};
  }
  impl_->fragments       = std::move(next_fragments);
  impl_->timeline        = *timeline;
  impl_->track_structure = structure;
  impl_->options         = options;
  impl_->node_id         = node_id;
  impl_->initialized     = true;
  return {NotationLayoutError::kNone, result.layout, std::move(work)};
}

void NotationLayoutCache::reset() noexcept {
  impl_->fragments.clear();
  impl_->timeline.reset();
  impl_->track_structure.clear();
  impl_->cached_widths.clear();
  impl_->initialized = false;
}

}  // namespace graphscore
