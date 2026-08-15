// SPDX-License-Identifier: Apache-2.0

#include "selftests.hpp"

#include "../app_project.hpp"
#include "../command_palette.hpp"
#include "../selection_tool_handler.hpp"
#include "selftest_fixtures.hpp"
#include "selftest_support.hpp"

#include <graphscore/domain/graphscore_domain.hpp>
#include <graphscore/notation/graphscore_notation.hpp>
#include <graphscore/writer_shell/graphscore_writer_shell.hpp>

#include <cstdio>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

namespace graphscore::writer_app {
namespace {

constexpr graphscore::Rational q(std::int64_t numerator,
                                 std::int64_t denominator) {
  return *graphscore::Rational::create(numerator, denominator);
}

struct TupletFixture {
  graphscore::Project        project;
  graphscore::NodeId         node;
  graphscore::TrackId        track;
  graphscore::StaveId        stave;
  graphscore::Voice          voice;
  graphscore::NotationLayout layout;
};

[[nodiscard]] std::optional<TupletFixture> build_fixture(
    const graphscore::GlyphMetrics& metrics, std::size_t event_count,
    std::size_t measure_count) {
  graphscore::Project project{graphscore::ProjectId::generate(), "Tuplets"};
  const auto          channel = graphscore::MidiChannel::create(0);
  if (!channel.has_value())
    return std::nullopt;
  const auto track = project.add_track(
      "Track", graphscore::StaffLayout::single_staff(graphscore::Clef::kTreble),
      *channel);
  if (!track.has_value())
    return std::nullopt;
  const graphscore::NodeId  node = project.add_node("Node");
  const graphscore::StaveId stave =
      project.active_tracks().front().layout().staves().front().id;
  auto* lane = project.find_node(node)->lane(*track);
  lane->ensure_stave(stave);
  std::vector<graphscore::Measure> measures;
  const auto signature = graphscore::TimeSignature::create(4, 4);
  if (!signature.has_value())
    return std::nullopt;
  for (std::size_t index = 0; index < measure_count; ++index)
    measures.push_back({*signature, graphscore::KeySignature{}});
  auto timeline = graphscore::NodeTimeline::create(
      std::move(measures), project.active_tracks().front().layout().staves());
  if (!timeline.has_value())
    return std::nullopt;
  project.find_node(node)->set_timeline(std::move(*timeline));
  const auto voice = graphscore::Voice::create(1);
  const auto pitch =
      graphscore::SpelledPitch::create(graphscore::Letter::kC, 4);
  const auto eighth =
      graphscore::Duration::create(graphscore::NoteValue::kEighth, 0);
  if (!voice.has_value() || !pitch.has_value() || !eighth.has_value())
    return std::nullopt;
  auto& content = lane->stave(stave)->voice(*voice);
  for (std::size_t index = 0; index < event_count; ++index) {
    if (!content.append(graphscore::make_note(*pitch, *eighth)).ok())
      return std::nullopt;
  }
  if (!content.normalize(project.find_node(node)->timeline()->node_end()).ok())
    return std::nullopt;
  auto layout = graphscore::layout_notation(project, node, metrics);
  if (!layout.layout.has_value())
    return std::nullopt;
  return TupletFixture{std::move(project),       node, *track, stave, *voice,
                       std::move(*layout.layout)};
}

[[nodiscard]] bool select_range(SelectionToolHandler& handler,
                                const TupletFixture&  fixture,
                                graphscore::Rational  end) {
  const auto set =
      graphscore::ArbitraryRangeSet::create({graphscore::ArbitraryRangeItem{
          fixture.node, fixture.track, fixture.stave, fixture.voice,
          graphscore::MusicalSpan{graphscore::Rational(0), end}}});
  if (!set.has_value())
    return false;
  handler.set_committed_selection(graphscore::Selection{*set});
  return true;
}

void prepare_handler(SelectionToolHandler&           handler,
                     const graphscore::GlyphMetrics& metrics) {
  handler.set_metrics(&metrics);
  handler.warm_layout_cache();
  handler.set_surface_publisher([](const graphscore::NotationLayout&) {
    return graphscore::ShellResult{};
  });
}

}  // namespace

int tuplet_edit_test() {
  const SelfTestMetrics   metrics;
  graphscore::WriterShell shell;

  // Common one-action triplet, followed by undo/redo and removal.
  auto simple = build_fixture(metrics, 3, 1);
  if (!simple.has_value())
    return 1;
  SelectionToolHandler handler(std::move(simple->project),
                               std::move(simple->layout), &shell);
  prepare_handler(handler, metrics);
  if (!select_range(handler, *simple, q(3, 8)) ||
      !handler.palette_command_available(PaletteCommandId::kCreateTriplet) ||
      !handler.run_palette_command(PaletteCommandId::kCreateTriplet) ||
      !handler.test_undo() || !handler.test_redo() ||
      !handler.remove_selected_tuplet() || !handler.test_undo()) {
    std::fprintf(stderr, "tuplet-edit-test: triplet/undo/remove failed\n");
    return 1;
  }

  // Power-user request handoff and arbitrary 10:9 execution.
  auto arbitrary = build_fixture(metrics, 10, 2);
  if (!arbitrary.has_value())
    return 1;
  SelectionToolHandler power(std::move(arbitrary->project),
                             std::move(arbitrary->layout), &shell);
  prepare_handler(power, metrics);
  power.run_palette_command(PaletteCommandId::kTupletRatioEntry);
  if (!power.tuplet_ratio_entry_requested() ||
      !select_range(power, *arbitrary, q(5, 4)) ||
      !power.apply_tuplet_ratio(10, 9) ||
      power.tuplet_ratio_entry_requested() || !power.apply_tuplet_ratio(5, 4)) {
    std::fprintf(stderr, "tuplet-edit-test: arbitrary ratio route failed\n");
    return 1;
  }

  power.set_committed_selection(std::nullopt);
  if (power.create_triplet() || power.diagnostics().empty()) {
    std::fprintf(stderr, "tuplet-edit-test: invalid feedback failed\n");
    return 1;
  }
  return 0;
}

}  // namespace graphscore::writer_app
