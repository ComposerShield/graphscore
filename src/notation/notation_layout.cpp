// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/project.hpp>
#include <graphscore/notation/notation_layout.hpp>

#include "engraving.hpp"
#include "layout_builder.hpp"
#include "layout_index.hpp"
#include "measure_math.hpp"
#include "notation_geometry.hpp"
#include "notation_ids.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace graphscore {

namespace {

[[nodiscard]] NotationLayoutResult fail(NotationLayoutError error) {
  return NotationLayoutResult{error, std::nullopt};
}

}  // namespace

// Shared per-command finiteness rule behind both NotationLayout::
// geometry_is_finite() and NotationPreview::geometry_is_finite(), so a
// preview's standalone command list can be validated the same way without
// folding it into a real NotationLayout.
[[nodiscard]] bool finite_command(const NotationCommand& command) {
  return std::visit(
      [](const auto& concrete) {
        using Command = std::decay_t<decltype(concrete)>;
        if constexpr (std::is_same_v<Command, GlyphCommand>) {
          return finite_point(concrete.origin) &&
                 std::isfinite(concrete.staff_space) &&
                 concrete.staff_space > 0.0;
        } else if constexpr (std::is_same_v<Command, LineCommand>) {
          return finite_point(concrete.from) && finite_point(concrete.to) &&
                 std::isfinite(concrete.width) && concrete.width >= 0.0;
        } else if constexpr (std::is_same_v<Command, PathCommand>) {
          return std::isfinite(concrete.stroke_width) &&
                 concrete.stroke_width >= 0.0 &&
                 std::all_of(concrete.elements.begin(), concrete.elements.end(),
                             [](const PathElement& element) {
                               return finite_point(element.control1) &&
                                      finite_point(element.control2) &&
                                      finite_point(element.end);
                             });
        } else {
          return finite_rect(concrete.bounds);
        }
      },
      command);
}

namespace {

[[nodiscard]] bool geometry_is_bounded(const NotationLayout& layout) {
  if (!bounded_rect(layout.bounds) ||
      !std::ranges::all_of(
          layout.systems,
          [](const SystemLayout& system) {
            return bounded_rect(system.bounds) &&
                   std::ranges::all_of(system.measures,
                                       [](const MeasureLayout& measure) {
                                         return bounded_rect(measure.bounds);
                                       }) &&
                   std::ranges::all_of(
                       system.staves, [](const StaffSystemLayout& staff) {
                         return bounded_rect(staff.bounds) &&
                                std::ranges::all_of(staff.measure_bounds,
                                                    bounded_rect);
                       });
          }) ||
      !std::ranges::all_of(layout.hit_regions, [](const HitRegion& hit) {
        return bounded_rect(hit.bounds);
      })) {
    return false;
  }
  return std::ranges::all_of(
      layout.commands, [](const NotationCommand& command) {
        return std::visit(
            [](const auto& concrete) {
              using Command = std::decay_t<decltype(concrete)>;
              if constexpr (std::is_same_v<Command, GlyphCommand>) {
                return bounded_point(concrete.origin) &&
                       concrete.staff_space <=
                           NotationLayoutOptions::kMaximumCoordinate;
              } else if constexpr (std::is_same_v<Command, LineCommand>) {
                return bounded_point(concrete.from) &&
                       bounded_point(concrete.to) &&
                       concrete.width <=
                           NotationLayoutOptions::kMaximumCoordinate;
              } else if constexpr (std::is_same_v<Command, PathCommand>) {
                return concrete.stroke_width <=
                           NotationLayoutOptions::kMaximumCoordinate &&
                       std::ranges::all_of(
                           concrete.elements, [](const PathElement& element) {
                             return bounded_point(element.control1) &&
                                    bounded_point(element.control2) &&
                                    bounded_point(element.end);
                           });
              } else {
                return bounded_rect(concrete.bounds);
              }
            },
            command);
      });
}

}  // namespace

namespace {

// Horizontal overhang the pickdown region's emitted geometry needs at each end
// of the content span, in layout pixels, measured from the preflight engraver's
// actual commands and hit regions rather than a parallel mirror of placement
// constants.
struct PickdownContentPadding {
  double left  = 0.0;
  double right = 0.0;
};

// Measures how far the preflight engraver's emitted geometry hangs past each
// end of the trial content span [boundary_x + inset,
// boundary_x + pickdown_width - inset]. Every glyph re-derives its box from
// the injected metrics at its own emitted staff_space; lines add their stroke
// half-width; paths add their stroke half-width over every endpoint and control
// point (a cubic's x-extent lies within its control points' convex hull); hit
// regions contribute their own bounds. ClipCommands carry the deliberately
// oversized preflight system bounds rather than content, so they are skipped.
[[nodiscard]] PickdownContentPadding measure_pickdown_padding(
    const NotationLayout& output, const GlyphMetrics& metrics,
    double boundary_x, double pickdown_width, double inset) {
  PickdownContentPadding padding;
  const double           content_left  = boundary_x + inset;
  const double           content_right = boundary_x + pickdown_width - inset;
  const auto             update        = [&](double left, double right) {
    padding.left  = std::max(padding.left, content_left - left);
    padding.right = std::max(padding.right, right - content_right);
  };
  for (const NotationCommand& command : output.commands) {
    std::visit(
        [&](const auto& concrete) {
          using Command = std::decay_t<decltype(concrete)>;
          if constexpr (std::is_same_v<Command, GlyphCommand>) {
            const GlyphMetricsValue value = metrics.glyph_metrics(
                concrete.code_point, concrete.staff_space);
            update(concrete.origin.x + value.bounds.x,
                   concrete.origin.x + value.bounds.x + value.bounds.width);
          } else if constexpr (std::is_same_v<Command, LineCommand>) {
            const double half = concrete.width * 0.5;
            update(std::min(concrete.from.x, concrete.to.x) - half,
                   std::max(concrete.from.x, concrete.to.x) + half);
          } else if constexpr (std::is_same_v<Command, PathCommand>) {
            const double half = concrete.stroke_width * 0.5;
            for (const PathElement& element : concrete.elements) {
              if (element.verb != PathVerb::kMove) {
                update(element.control1.x - half, element.control1.x + half);
                update(element.control2.x - half, element.control2.x + half);
              }
              update(element.end.x - half, element.end.x + half);
            }
          }
          // ClipCommand: its bounds are the preflight system, not content.
        },
        command);
  }
  for (const HitRegion& hit : output.hit_regions) {
    update(hit.bounds.x, hit.bounds.x + hit.bounds.width);
  }
  return padding;
}

}  // namespace

NotationLayoutResult layout_internal(
    const Project& project, NodeId node_id, const GlyphMetrics& metrics,
    const NotationLayoutOptions& options, const LayoutIndex& layout_index,
    const std::vector<double>&         widths,
    const std::vector<SystemFragment>* previous,
    const std::vector<std::size_t>&    invalid_systems,
    std::vector<SystemFragment>* retained, NotationLayoutWork* work) {
  if (!options.valid()) {
    return fail(NotationLayoutError::kInvalidOptions);
  }
  const Node* node = project.find_node(node_id);
  if (node == nullptr) {
    return fail(NotationLayoutError::kNodeNotFound);
  }
  const NodeTimeline* timeline = node->timeline();
  if (timeline == nullptr) {
    return fail(NotationLayoutError::kTimelineMissing);
  }
  LayoutBuilder builder{*timeline, metrics, options, NotationLayout{},
                        NotationLayoutError::kNone};
  builder.output.node_id     = node_id;
  const MeasureMap& measures = timeline->measures();
  const double      content_width =
      options.system_width - options.left_margin - options.right_margin;
  const auto        ranges         = system_ranges(widths, content_width);
  const std::size_t measure_count  = measures.measure_count();
  double            pickdown_width = pickdown_region_width(
      *timeline, widths.empty() ? 0.0 : widths.back(), options.staff_space);
  // Content-aware pickdown sizing (M5-phase-31): the fixed one-space inset
  // only contains a bare notehead column. Boundary accidentals/grace/stems and
  // node-end dots/dynamics need a wider inset, so engrave the pickdown content
  // once into a throwaway builder through the same emission path the final
  // pass uses, measure every emitted glyph/line/path/hit extent, and grow both
  // the inset and the region width so all of it stays inside the
  // transition-to-node-end area. Measuring the real emitted geometry keeps the
  // inset single-sourced in the engraver, so an omitted family can no longer
  // drift out of a parallel list of placement constants.
  double pickdown_content_inset_value =
      pickdown_content_inset(options.staff_space);
  if (pickdown_width > 0.0) {
    double total_used_width = 0.0;
    for (const double width : widths) {
      total_used_width += width;
    }
    const double boundary_x  = options.left_margin + total_used_width;
    const double trial_inset = pickdown_content_inset_value;
    const double trial_width = pickdown_width;

    LayoutBuilder preflight{*timeline, metrics, options, NotationLayout{},
                            NotationLayoutError::kNone};
    SystemLayout  preflight_system;
    preflight_system.id            = make_id(node_id, "pickdown-preflight");
    preflight_system.first_measure = measure_count - 1;
    // Oversized so the preflight never clips span/tie hit regions, keeping the
    // measurement a conservative superset of the final pass's geometry.
    preflight_system.bounds = NotationRect{-1.0e9, 0.0, 2.0e9, 2.0e9};

    static const StaveVoices kEmptyVoices;
    for (const Track& track : project.active_tracks()) {
      const TrackLane* lane = node->lane(track.id());
      if (lane == nullptr) {
        continue;
      }
      for (const StaveDefinition& stave_definition : track.layout().staves()) {
        const StaveVoices* voices = lane->stave(stave_definition.id);
        if (voices == nullptr) {
          voices = &kEmptyVoices;
        }
        const IndexedStaff* staff_index =
            indexed_staff(layout_index, stave_definition.id);
        if (staff_index == nullptr) {
          continue;
        }
        StaffSystemLayout preflight_staff;
        preflight_staff.track_id = track.id();
        preflight_staff.stave_id = stave_definition.id;
        preflight_staff.id       = make_id(stave_definition.id, "preflight");
        preflight_staff.bounds =
            NotationRect{options.left_margin, 0.0,
                         boundary_x + trial_width - options.left_margin,
                         options.staff_space * 4.0};
        if (!add_pickdown_content(preflight, preflight_system, preflight_staff,
                                  *voices, *staff_index, boundary_x,
                                  trial_width, trial_inset)) {
          return fail(preflight.error);
        }
      }
    }

    const PickdownContentPadding padding = measure_pickdown_padding(
        preflight.output, metrics, boundary_x, trial_width, trial_inset);
    pickdown_content_inset_value =
        options.staff_space *
        std::max({1.0, padding.left / options.staff_space + 0.35,
                  padding.right / options.staff_space});
    // Preserve the exact trial content-span width (trial_width - 2*S) when the
    // final inset grows to I > S: grow the region width by 2*(I - S) before any
    // other conservative minimum. Without this, the content span shrinks from
    // W0-2S to W0-2I and every later/interior onset shifts left by (I - S),
    // letting geometry the preflight had already measured escape the transition
    // boundary under valid metrics.
    pickdown_width = std::max(
        pickdown_width + 2.0 * (pickdown_content_inset_value - trial_inset),
        2.0 * pickdown_content_inset_value + options.staff_space);
  }

  std::size_t stave_count = 0;
  for (const Track& track : project.active_tracks()) {
    stave_count += track.layout().stave_count();
  }
  const double staff_height = options.staff_space * 4.0;
  // Each stave owns vertical engraving lanes above/below its five lines.
  // Reserving the lanes in the system bounds makes span clips truthful and
  // keeps dynamics/pedal geometry from colliding with the following stave.
  // Reserve a stable per-system marking budget, divided among its staves.
  // Overlap-based lane reuse means later disjoint collections do not alter
  // this extent or push unrelated retained systems. A one-staff system has
  // room for well over one hundred simultaneous lanes; dense multi-staff
  // systems retain a sixteen-space minimum per stave.
  const double staff_slot_height =
      options.staff_space *
      std::max(16.0, 256.0 / static_cast<double>(
                                 std::max(stave_count, std::size_t{1})));
  const double system_top_padding = options.staff_space * 6.0;
  const double system_height =
      stave_count == 0
          ? 0.0
          : system_top_padding +
                static_cast<double>(stave_count) * staff_slot_height +
                static_cast<double>(stave_count - 1) * options.stave_gap;

  double system_y      = options.top_margin;
  double maximum_width = options.system_width;
  for (const auto [first, end] : ranges) {
    const bool   is_final_system = end == measure_count;
    const double pickdown_extra  = is_final_system ? pickdown_width : 0.0;
    double       used_width      = 0.0;
    for (std::size_t index = first; index < end; ++index) {
      used_width += widths[index];
    }
    const double actual_width =
        std::max(options.system_width,
                 options.left_margin + used_width + options.right_margin) +
        pickdown_extra;
    maximum_width = std::max(maximum_width, actual_width);
    const auto old =
        previous == nullptr
            ? std::vector<SystemFragment>::const_iterator{}
            : std::ranges::find_if(*previous, [first](const auto& fragment) {
                return fragment.system.first_measure == first;
              });
    const bool invalid =
        std::ranges::find(invalid_systems, first) != invalid_systems.end();
    const NotationRect expected_bounds{0.0, system_y, actual_width,
                                       system_height};
    if (previous != nullptr && !invalid && old != previous->end() &&
        old->system.bounds == expected_bounds &&
        old->system.measures.size() == end - first) {
      append_fragment(builder.output, *old);
      if (retained != nullptr) {
        retained->push_back(*old);
      }
      if (work != nullptr) {
        work->reused_systems.push_back(first);
        for (std::size_t ordinal = first; ordinal < end; ++ordinal) {
          work->reused_measures.push_back(ordinal);
        }
      }
      system_y += system_height + options.system_gap;
      continue;
    }

    const std::size_t command_begin    = builder.output.commands.size();
    const std::size_t hit_begin        = builder.output.hit_regions.size();
    const std::size_t diagnostic_begin = builder.output.diagnostics.size();
    SystemLayout      system;
    system.first_measure = first;
    system.id            = make_id(node_id, "system/" + std::to_string(first));
    system.bounds = NotationRect{0.0, system_y, actual_width, system_height};
    builder.output.hit_regions.push_back(HitRegion{
        make_id(system.id.value, "hit"), system.id, HitRole::kSystem,
        system.bounds, kHitPrioritySystem, std::nullopt, std::nullopt});
    builder.output.commands.emplace_back(ClipCommand{
        make_id(system.id.value, "clip/begin"), system.bounds, true});

    double measure_x = options.left_margin;
    for (std::size_t index = first; index < end; ++index) {
      const NotationId measure_id =
          make_id(node_id, "measure/" + std::to_string(index));
      const NotationRect bounds{measure_x, system_y, widths[index],
                                system_height};
      system.measures.push_back(MeasureLayout{index, measure_id, bounds});
      builder.output.hit_regions.push_back(HitRegion{
          make_id(measure_id.value, "system/" + std::to_string(first) + "/hit"),
          measure_id, HitRole::kMeasure, bounds, kHitPriorityMeasure,
          std::nullopt, std::nullopt});
      measure_x += widths[index];
    }

    std::size_t stave_ordinal = 0;
    for (const Track& track : project.active_tracks()) {
      const TrackLane* lane = node->lane(track.id());
      if (lane == nullptr) {
        return fail(NotationLayoutError::kLaneMissing);
      }
      for (const StaveDefinition& stave_definition : track.layout().staves()) {
        const StaveVoices* voices = lane->stave(stave_definition.id);
        if (voices == nullptr) {
          static const StaveVoices kEmptyVoices;
          voices = &kEmptyVoices;
        }
        StaffSystemLayout staff;
        staff.track_id = track.id();
        staff.stave_id = stave_definition.id;
        staff.id =
            make_id(stave_definition.id, "system/" + std::to_string(first));
        const double staff_y = system_y + system_top_padding +
                               static_cast<double>(stave_ordinal) *
                                   (staff_slot_height + options.stave_gap);
        const double staff_right_x = measure_x + pickdown_extra;
        staff.bounds =
            NotationRect{options.left_margin, staff_y,
                         staff_right_x - options.left_margin, staff_height};
        for (const MeasureLayout& measure : system.measures) {
          const NotationRect measure_staff_bounds{
              measure.bounds.x, staff_y, measure.bounds.width, staff_height};
          staff.measure_bounds.push_back(measure_staff_bounds);
          const NotationId staff_measure_id =
              staff_measure_semantic_id(staff.id, measure.ordinal);
          builder.output.hit_regions.push_back(HitRegion{
              make_id(staff_measure_id.value, "hit"), staff_measure_id,
              HitRole::kStaffMeasure, measure_staff_bounds,
              kHitPriorityStaffMeasure, std::nullopt, std::nullopt});
        }
        builder.output.hit_regions.push_back(HitRegion{
            make_id(staff.id.value, "hit"),
            NotationId{stave_definition.id.to_string()}, HitRole::kStaff,
            staff.bounds, kHitPriorityStaff, std::nullopt, std::nullopt});
        for (std::uint8_t voice_index = Voice::kMin; voice_index <= Voice::kMax;
             ++voice_index) {
          const Voice      voice    = *Voice::create(voice_index);
          const NotationId voice_id = make_id(
              stave_definition.id, "voice/" + std::to_string(voice_index));
          const IndexedVoice& indexed_voice =
              indexed_staff(layout_index, stave_definition.id)
                  ->voices[voice_index - Voice::kMin];
          std::size_t event_count = 0;
          for (std::size_t ordinal = first; ordinal < end; ++ordinal) {
            event_count += indexed_voice.measures[ordinal].size();
          }
          if (is_final_system) {
            event_count += indexed_voice.pickdown.size();
          }
          staff.voices.push_back(VoiceLayout{voice, voice_id, event_count});
          builder.output.hit_regions.push_back(
              HitRegion{make_id(voice_id.value,
                                "system/" + std::to_string(first) + "/hit"),
                        voice_id, HitRole::kVoice, staff.bounds,
                        kHitPriorityVoice, std::nullopt, std::nullopt});
        }
        for (int line = 0; line < 5; ++line) {
          const double y =
              staff_y + static_cast<double>(line) * options.staff_space;
          builder.add_line(
              make_id(staff.id.value, "line/" + std::to_string(line)),
              NotationPoint{options.left_margin, y},
              NotationPoint{staff_right_x, y}, options.staff_space * 0.1);
        }
        for (const MeasureLayout& measure : system.measures) {
          builder.add_line(
              make_id(
                  staff.id.value,
                  "measure/" + std::to_string(measure.ordinal) + "/barline"),
              NotationPoint{measure.bounds.x, staff_y},
              NotationPoint{measure.bounds.x, staff_y + staff_height},
              options.staff_space * 0.15);
          const ClefLane* clef_lane = timeline->clef_lane(stave_definition.id);
          const Rational  measure_start =
              measures.measure_start(measure.ordinal);
          const Clef clef         = clef_lane == nullptr
                                        ? stave_definition.default_clef
                                        : clef_lane->clef_at(measure_start);
          const bool system_start = measure.ordinal == first;
          const bool key_change =
              measure.ordinal == 0 ||
              measures.measure(measure.ordinal - 1).key_signature !=
                  measures.measure(measure.ordinal).key_signature;
          const bool time_change =
              measure.ordinal == 0 ||
              measures.measure(measure.ordinal - 1).time_signature !=
                  measures.measure(measure.ordinal).time_signature;
          const bool clef_change =
              clef_lane != nullptr &&
              std::ranges::any_of(clef_lane->changes(),
                                  [&](const ClefChange& change) {
                                    return change.position == measure_start;
                                  });
          if (!add_signature_glyphs(
                  builder, measures.measure(measure.ordinal), clef, staff.id,
                  NotationPoint{measure.bounds.x, staff_y}, measure.ordinal,
                  system_start || clef_change, system_start || key_change,
                  system_start || time_change,
                  key_change && measure.ordinal > 0
                      ? std::optional<KeySignature>{measures
                                                        .measure(
                                                            measure.ordinal - 1)
                                                        .key_signature}
                      : std::nullopt)) {
            return fail(builder.error);
          }
          if (clef_lane != nullptr) {
            const Rational measure_end =
                measure_start + measures.measure_length(measure.ordinal);
            for (const ClefChange& change : clef_lane->changes()) {
              if (change.position <= measure_start ||
                  change.position >= measure_end) {
                continue;
              }
              const auto        components = change.position.to_components();
              const std::string role =
                  "clef-change/" + std::to_string(components.numerator) + "-" +
                  std::to_string(components.denominator);
              const NotationId change_id = make_id(staff.id.value, role);
              const double     measure_position =
                  position_x(measures, widths, measure.ordinal, change.position,
                             measure.bounds.x, options.staff_space);
              const double x = measure_position + options.staff_space;
              if (!builder
                       .add_glyph(change_id, clef_glyph(change.clef),
                                  {x, staff_y},
                                  NotationId{stave_definition.id.to_string()})
                       .has_value()) {
                return fail(builder.error);
              }
            }
          }
        }
        const MeasureLayout& last       = system.measures.back();
        const double         right_edge = last.bounds.x + last.bounds.width;
        if (is_final_system && pickdown_width > 0.0) {
          // A distinct double-barline transition boundary at the main-region
          // end, explicitly test-identifiable rather than reusing the ordinary
          // final barline that already sits there.
          builder.add_line(make_id(staff.id.value, "pickdown-boundary/first"),
                           NotationPoint{right_edge, staff_y},
                           NotationPoint{right_edge, staff_y + staff_height},
                           options.staff_space * 0.1);
          builder.add_line(
              make_id(staff.id.value, "pickdown-boundary/second"),
              NotationPoint{right_edge + options.staff_space * 0.35, staff_y},
              NotationPoint{right_edge + options.staff_space * 0.35,
                            staff_y + staff_height},
              options.staff_space * 0.1);
          const double node_end_x = right_edge + pickdown_width;
          builder.add_line(make_id(staff.id.value, "pickdown-end-barline"),
                           NotationPoint{node_end_x, staff_y},
                           NotationPoint{node_end_x, staff_y + staff_height},
                           options.staff_space * 0.15);
        } else {
          builder.add_line(make_id(staff.id.value,
                                   "measure/" + std::to_string(last.ordinal) +
                                       "/end-barline"),
                           NotationPoint{right_edge, staff_y},
                           NotationPoint{right_edge, staff_y + staff_height},
                           options.staff_space * 0.15);
        }
        if (!add_rhythm(builder, system, staff, *voices,
                        *indexed_staff(layout_index, stave_definition.id),
                        widths, system.measures)) {
          return fail(builder.error);
        }
        const IndexedStaff* staff_index =
            indexed_staff(layout_index, stave_definition.id);
        std::vector<PedalSpan> system_pedals;
        for (const NotationEntityId& id :
             system_reference_ids(staff_index->pedals, system.measures)) {
          system_pedals.push_back(staff_index->pedals.entries.at(id).record);
        }
        {
          const MeasureMap& pedal_map = measures;
          const Rational    pedal_start =
              pedal_map.measure_start(system.measures.front().ordinal);
          const MeasureLayout& pedal_last = system.measures.back();
          const Rational       pedal_end =
              pedal_map.measure_start(pedal_last.ordinal) +
              pedal_map.measure_length(pedal_last.ordinal);
          const double pedal_left_x =
              system.measures.front().bounds.x + options.staff_space * 0.5;
          const double pedal_right_x = pedal_last.bounds.x +
                                       pedal_last.bounds.width -
                                       options.staff_space * 0.5;
          const auto pedal_x_at = [&](Rational position) {
            const std::size_t measure = *pedal_map.measure_index_at(position);
            const MeasureLayout& layout =
                system.measures[measure - system.first_measure];
            const double fraction =
                ((position - pedal_map.measure_start(measure)).to_double() /
                 pedal_map.measure_length(measure).to_double());
            return layout.bounds.x + layout.bounds.width * fraction;
          };
          if (!add_pedal_spans(
                  builder, system, staff, system_pedals, pedal_start, pedal_end,
                  pedal_left_x, pedal_right_x, pedal_x_at,
                  system.first_measure == 0,
                  "system-" + std::to_string(system.first_measure))) {
            return fail(builder.error);
          }
        }
        if (is_final_system && pickdown_width > 0.0) {
          if (!add_pickdown_content(builder, system, staff, *voices,
                                    *staff_index, measure_x, pickdown_width,
                                    pickdown_content_inset_value)) {
            return fail(builder.error);
          }
        }
        system.staves.push_back(std::move(staff));
        ++stave_ordinal;
      }
    }
    builder.output.commands.emplace_back(ClipCommand{
        make_id(system.id.value, "clip/end"), system.bounds, false});
    builder.output.systems.push_back(std::move(system));
    if (work != nullptr) {
      work->rebuilt_systems.push_back(first);
      for (std::size_t ordinal = first; ordinal < end; ++ordinal) {
        work->visited_measures.push_back(ordinal);
        work->rebuilt_measures.push_back(ordinal);
      }
    }
    if (retained != nullptr) {
      retained->push_back(
          SystemFragment{builder.output.systems.back(),
                         {builder.output.commands.begin() +
                              static_cast<std::ptrdiff_t>(command_begin),
                          builder.output.commands.end()},
                         {builder.output.hit_regions.begin() +
                              static_cast<std::ptrdiff_t>(hit_begin),
                          builder.output.hit_regions.end()},
                         {builder.output.diagnostics.begin() +
                              static_cast<std::ptrdiff_t>(diagnostic_begin),
                          builder.output.diagnostics.end()}});
    }
    system_y += system_height + options.system_gap;
  }

  const double total_height =
      ranges.empty() ? options.top_margin + options.bottom_margin
                     : system_y - options.system_gap + options.bottom_margin;
  builder.output.bounds = NotationRect{0.0, 0.0, maximum_width, total_height};
  if (!builder.output.geometry_is_finite() ||
      !geometry_is_bounded(builder.output)) {
    return fail(NotationLayoutError::kInvalidGeometry);
  }
  return NotationLayoutResult{NotationLayoutError::kNone,
                              std::move(builder.output)};
}

NotationLayoutResult layout_notation(const Project& project, NodeId node_id,
                                     const GlyphMetrics&          metrics,
                                     const NotationLayoutOptions& options) {
  const Node* node = project.find_node(node_id);
  if (node == nullptr) {
    return fail(NotationLayoutError::kNodeNotFound);
  }
  const NodeTimeline* timeline = node->timeline();
  if (timeline == nullptr) {
    return fail(NotationLayoutError::kTimelineMissing);
  }
  const LayoutIndex index = build_index(project, *node, timeline->measures(),
                                        metrics, options, nullptr);
  const std::vector<double> widths =
      measure_widths(timeline->measures(), index, metrics, options);
  return layout_internal(project, node_id, metrics, options, index, widths,
                         nullptr, {}, nullptr, nullptr);
}

}  // namespace graphscore
