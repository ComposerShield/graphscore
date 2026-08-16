// SPDX-License-Identifier: Apache-2.0

#include "selftests.hpp"

#include "../app_project.hpp"
#include "../command_palette.hpp"
#include "../selection_tool_handler.hpp"
#include "selftest_fixtures.hpp"
#include "selftest_support.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>
#include <graphscore/notation/graphscore_notation.hpp>
#include <graphscore/writer_shell/graphscore_writer_shell.hpp>

namespace graphscore::writer_app {
namespace {

constexpr graphscore::Rational q(std::int64_t numerator,
                                 std::int64_t denominator) {
  return *graphscore::Rational::create(numerator, denominator);
}

struct MarkingFixture {
  graphscore::Project                       project;
  graphscore::NodeId                        node;
  graphscore::TrackId                       track;
  graphscore::StaveId                       stave;
  graphscore::Voice                         voice;
  std::vector<graphscore::NotationEntityId> events;
  graphscore::NotationLayout                layout;
};

class FailingStyleInverseCommand final : public graphscore::Command {
 public:
  FailingStyleInverseCommand(std::unique_ptr<graphscore::Command> inner,
                             int fail_undo_at, int fail_redo_at)
      : inner_(std::move(inner)),
        fail_undo_at_(fail_undo_at),
        fail_redo_at_(fail_redo_at) {}

  graphscore::Result execute(graphscore::Project& project) noexcept override {
    return inner_->execute(project);
  }

  graphscore::Result undo(graphscore::Project& project) noexcept override {
    ++undo_calls_;
    return undo_calls_ == fail_undo_at_
               ? graphscore::Result(graphscore::ResultCode::kInternalError)
               : inner_->undo(project);
  }

  graphscore::Result redo(graphscore::Project& project) noexcept override {
    ++redo_calls_;
    return redo_calls_ == fail_redo_at_
               ? graphscore::Result(graphscore::ResultCode::kInternalError)
               : inner_->redo(project);
  }

  graphscore::Result compensate_undo(
      graphscore::Project& project) noexcept override {
    return inner_->compensate_undo(project);
  }

  graphscore::Result compensate_redo(
      graphscore::Project& project) noexcept override {
    return inner_->compensate_redo(project);
  }

 private:
  std::unique_ptr<graphscore::Command> inner_;
  int                                  fail_undo_at_;
  int                                  fail_redo_at_;
  int                                  undo_calls_ = 0;
  int                                  redo_calls_ = 0;
};

// Two 4/4 measures carrying eight quarter notes in voice 1. Every voice of the
// stave is normalized because stave-scoped pedal commands require complete
// rhythm in every voice of the addressed stave.
[[nodiscard]] std::optional<MarkingFixture> build_fixture(
    const graphscore::GlyphMetrics& metrics,
    graphscore::NoteValue           value = graphscore::NoteValue::kQuarter,
    int                             event_count = 8) {
  graphscore::Project project{graphscore::ProjectId::generate(), "Markings"};
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
  const auto signature = graphscore::TimeSignature::create(4, 4);
  if (!signature.has_value())
    return std::nullopt;
  auto timeline = graphscore::NodeTimeline::create(
      {{*signature, graphscore::KeySignature{}},
       {*signature, graphscore::KeySignature{}}},
      project.active_tracks().front().layout().staves());
  if (!timeline.has_value())
    return std::nullopt;
  project.find_node(node)->set_timeline(std::move(*timeline));
  const auto voice    = graphscore::Voice::create(1);
  const auto duration = graphscore::Duration::create(value, 0);
  const auto pitch =
      graphscore::SpelledPitch::create(graphscore::Letter::kC, 4);
  if (!voice.has_value() || !duration.has_value() || !pitch.has_value())
    return std::nullopt;
  const graphscore::Rational node_end =
      project.find_node(node)->timeline()->node_end();
  std::vector<graphscore::NotationEntityId> events;
  auto& content = lane->stave(stave)->voice(*voice);
  for (int index = 0; index < event_count; ++index) {
    graphscore::Note note = graphscore::make_note(*pitch, *duration);
    events.push_back(note.id);
    if (!content.append(std::move(note)).ok())
      return std::nullopt;
  }
  for (std::uint8_t index = graphscore::Voice::kMin;
       index <= graphscore::Voice::kMax; ++index) {
    const auto slot = graphscore::Voice::create(index);
    if (!slot.has_value() ||
        !lane->stave(stave)->voice(*slot).normalize(node_end).ok())
      return std::nullopt;
  }
  auto layout = graphscore::layout_notation(project, node, metrics);
  if (!layout.layout.has_value())
    return std::nullopt;
  return MarkingFixture{
      std::move(project),       node, *track, stave, *voice, std::move(events),
      std::move(*layout.layout)};
}

[[nodiscard]] graphscore::Selection note_selection(
    const MarkingFixture& fixture, std::size_t index) {
  return graphscore::Selection{*graphscore::NoteheadSet::create(
      {{fixture.node, fixture.track, fixture.stave, fixture.voice,
        fixture.events[index]}})};
}

[[nodiscard]] graphscore::Selection range_selection(
    const MarkingFixture& fixture, graphscore::Rational start,
    graphscore::Rational end) {
  return graphscore::Selection{
      *graphscore::ArbitraryRangeSet::create({graphscore::ArbitraryRangeItem{
          fixture.node, fixture.track, fixture.stave, fixture.voice,
          graphscore::MusicalSpan{start, end}}})};
}

[[nodiscard]] std::optional<graphscore::Selection> marking_selection(
    const MarkingFixture& fixture, graphscore::MarkingKind kind,
    graphscore::NotationEntityId anchor) {
  const std::optional<graphscore::Voice> item_voice =
      kind == graphscore::MarkingKind::kPedalSpan
          ? std::nullopt
          : std::optional<graphscore::Voice>{fixture.voice};
  auto set = graphscore::MarkingSet::create(
      {graphscore::MarkingItem{fixture.node, fixture.track, fixture.stave,
                               item_voice, kind, anchor, std::nullopt}});
  if (!set.has_value())
    return std::nullopt;
  return graphscore::Selection{*set};
}

[[nodiscard]] const graphscore::VoiceContent* content(
    const SelectionToolHandler& handler, const MarkingFixture& fixture) {
  const auto* node  = handler.project().find_node(fixture.node);
  const auto* lane  = node == nullptr ? nullptr : node->lane(fixture.track);
  const auto* stave = lane == nullptr ? nullptr : lane->stave(fixture.stave);
  return stave == nullptr ? nullptr : &stave->voice(fixture.voice);
}

[[nodiscard]] const std::vector<graphscore::PedalSpan>* pedal_spans(
    const SelectionToolHandler& handler, const MarkingFixture& fixture) {
  const auto* node = handler.project().find_node(fixture.node);
  const auto* lane = node == nullptr ? nullptr : node->lane(fixture.track);
  return lane == nullptr ? nullptr : lane->pedal_spans(fixture.stave);
}

// refresh_layout() relayouts incrementally, reusing every system
// marking_edit_invalidation() did not name, so an under-scoped invalidation
// shows up as a layout that disagrees with a from-scratch pass over the same
// post-edit project. Comparing the retained layout across a FAILED edit proves
// nothing about that scope; this is the check that does.
[[nodiscard]] bool layout_matches_fresh(
    const SelectionToolHandler& handler, const MarkingFixture& fixture,
    const graphscore::GlyphMetrics& metrics) {
  const auto fresh =
      graphscore::layout_notation(handler.project(), fixture.node, metrics);
  return fresh.layout.has_value() && handler.layout() == *fresh.layout;
}

void prepare(SelectionToolHandler&           handler,
             const graphscore::GlyphMetrics& metrics) {
  handler.set_metrics(&metrics);
  handler.warm_layout_cache();
  handler.set_surface_publisher([](const graphscore::NotationLayout&) {
    return graphscore::ShellResult{};
  });
}

// Which family's apply route the publication-compensation checks drive. Each
// one lands on a different domain command, so each proves that command's own
// snapshot-based compensate_undo/compensate_redo.
enum class ApplyRoute {
  kDynamic,
  kChangeDynamic,
  kHairpin,
  kPedalSpan,
  kTie,
  kSlur,
  kBeam
};

[[nodiscard]] bool run_apply_route(SelectionToolHandler& handler,
                                   const MarkingFixture& fixture,
                                   ApplyRoute            route) {
  switch (route) {
    case ApplyRoute::kDynamic:
      handler.set_committed_selection(note_selection(fixture, 0));
      return handler.run_palette_command(PaletteCommandId::kApplyDynamicMf);
    case ApplyRoute::kChangeDynamic: {
      handler.set_committed_selection(note_selection(fixture, 0));
      if (!handler.run_palette_command(PaletteCommandId::kApplyDynamicMf))
        return false;
      const auto* voice = content(handler, fixture);
      if (voice == nullptr || voice->dynamics().empty())
        return false;
      const auto selected =
          marking_selection(fixture, graphscore::MarkingKind::kDynamic,
                            voice->dynamics().front().id);
      if (!selected.has_value())
        return false;
      handler.set_committed_selection(*selected);
      return handler.run_palette_command(PaletteCommandId::kChangeDynamicToFff);
    }
    case ApplyRoute::kHairpin:
      handler.set_committed_selection(
          range_selection(fixture, q(3, 4), q(5, 4)));
      return handler.run_palette_command(PaletteCommandId::kApplyCrescendo);
    case ApplyRoute::kPedalSpan:
      handler.set_committed_selection(
          range_selection(fixture, q(3, 4), q(5, 4)));
      return handler.run_palette_command(PaletteCommandId::kApplyPedalSpan);
    case ApplyRoute::kTie:
      handler.set_committed_selection(note_selection(fixture, 0));
      return handler.run_palette_command(PaletteCommandId::kApplyTie);
    case ApplyRoute::kSlur:
      handler.set_committed_selection(
          range_selection(fixture, q(0, 1), q(1, 2)));
      return handler.run_palette_command(PaletteCommandId::kApplySlur);
    case ApplyRoute::kBeam:
      handler.set_committed_selection(
          range_selection(fixture, q(0, 1), q(1, 4)));
      return handler.run_palette_command(PaletteCommandId::kApplyBeamBreak);
  }
  return false;
}

// The observable marking state an undo/redo publication failure must leave
// exactly as it found it.
[[nodiscard]] std::string marking_state(const SelectionToolHandler& handler,
                                        const MarkingFixture&       fixture) {
  const auto* voice = content(handler, fixture);
  if (voice == nullptr)
    return "missing";
  std::string state;
  for (const auto& dynamic : voice->dynamics())
    state += "d" + std::to_string(static_cast<int>(dynamic.value));
  for (const auto& hairpin : voice->hairpins())
    state += "h" + std::to_string(static_cast<int>(hairpin.direction));
  for (const auto& slur : voice->slurs())
    state += "s" + slur.id.to_string();
  for (const auto& beam : voice->beam_overrides())
    state += "b" + std::to_string(static_cast<int>(beam.kind));
  if (const auto* note =
          std::get_if<graphscore::Note>(&voice->events().front());
      note != nullptr && note->tied_to_next) {
    state += "t";
  }
  const auto* spans = pedal_spans(handler, fixture);
  if (spans != nullptr)
    state += "p" + std::to_string(spans->size());
  return state;
}

[[nodiscard]] int check_publication_compensation(
    const graphscore::GlyphMetrics& metrics, graphscore::WriterShell& shell,
    ApplyRoute route) {
  {
    auto undo_fixture =
        route == ApplyRoute::kBeam
            ? build_fixture(metrics, graphscore::NoteValue::kEighth, 16)
            : build_fixture(metrics);
    if (!undo_fixture.has_value())
      return 1;
    SelectionToolHandler undo(std::move(undo_fixture->project),
                              std::move(undo_fixture->layout), &shell);
    prepare(undo, metrics);
    undo.set_surface_publisher(
        [&shell](const graphscore::NotationLayout& value) {
          return publish_headless_test_surface(value, &shell);
        });
    undo.set_marking_edit_command_wrapper(
        [](std::unique_ptr<graphscore::Command> command) {
          return std::make_unique<FailingStyleInverseCommand>(
              std::move(command), 0, 1);
        });
    if (!run_apply_route(undo, *undo_fixture, route))
      return 1;
    const std::string applied         = marking_state(undo, *undo_fixture);
    const auto        applied_layout  = undo.layout();
    const auto        applied_surface = shell.test_snapshot_notation_surface();
    const std::size_t applied_undo_depth = undo.test_undo_stack_size();

    graphscore::NotationLayoutOptions invalid_options;
    invalid_options.system_width = 0.0;
    undo.set_layout_options(invalid_options);
    if (undo.run_palette_command(PaletteCommandId::kUndo) ||
        undo.test_undo_stack_size() != applied_undo_depth ||
        undo.test_redo_stack_size() != 0u ||
        marking_state(undo, *undo_fixture) != applied ||
        undo.layout() != applied_layout ||
        shell.test_snapshot_notation_surface() != applied_surface ||
        undo.take_diagnostics() !=
            std::vector<std::string>{
                "undo: layout refresh failed; undo restored"}) {
      std::fprintf(stderr,
                   "marking-style-edit-test: undo compensation failed\n");
      return 1;
    }
  }
  {
    auto redo_fixture =
        route == ApplyRoute::kBeam
            ? build_fixture(metrics, graphscore::NoteValue::kEighth, 16)
            : build_fixture(metrics);
    if (!redo_fixture.has_value())
      return 1;
    SelectionToolHandler redo(std::move(redo_fixture->project),
                              std::move(redo_fixture->layout), &shell);
    prepare(redo, metrics);
    redo.set_surface_publisher(
        [&shell](const graphscore::NotationLayout& value) {
          return publish_headless_test_surface(value, &shell);
        });
    redo.set_marking_edit_command_wrapper(
        [](std::unique_ptr<graphscore::Command> command) {
          return std::make_unique<FailingStyleInverseCommand>(
              std::move(command), 2, 0);
        });
    if (!run_apply_route(redo, *redo_fixture, route) ||
        !redo.run_palette_command(PaletteCommandId::kUndo)) {
      return 1;
    }
    const std::string undone         = marking_state(redo, *redo_fixture);
    const auto        undone_layout  = redo.layout();
    const auto        undone_surface = shell.test_snapshot_notation_surface();
    const std::size_t undone_undo_depth = redo.test_undo_stack_size();
    redo.set_surface_publisher([](const graphscore::NotationLayout&) {
      return graphscore::ShellResult{
          graphscore::ShellError::kRenderingSetupFailed,
          "injected marking redo publish failure"};
    });
    if (redo.run_palette_command(PaletteCommandId::kRedo) ||
        redo.test_undo_stack_size() != undone_undo_depth ||
        redo.test_redo_stack_size() != 1u ||
        marking_state(redo, *redo_fixture) != undone ||
        redo.layout() != undone_layout ||
        shell.test_snapshot_notation_surface() != undone_surface ||
        redo.take_diagnostics() !=
            std::vector<std::string>{
                "redo: layout refresh failed; redo restored"}) {
      std::fprintf(stderr,
                   "marking-style-edit-test: redo compensation failed\n");
      return 1;
    }
  }
  return 0;
}

}  // namespace

int marking_style_edit_test() {
  const SelfTestMetrics   metrics;
  graphscore::WriterShell shell;

  constexpr std::array apply_dynamics = {
      PaletteCommandId::kApplyDynamicPpp, PaletteCommandId::kApplyDynamicPp,
      PaletteCommandId::kApplyDynamicP,   PaletteCommandId::kApplyDynamicMp,
      PaletteCommandId::kApplyDynamicMf,  PaletteCommandId::kApplyDynamicF,
      PaletteCommandId::kApplyDynamicFf,  PaletteCommandId::kApplyDynamicFff};
  constexpr std::array change_dynamics = {
      PaletteCommandId::kChangeDynamicToPpp,
      PaletteCommandId::kChangeDynamicToPp,
      PaletteCommandId::kChangeDynamicToP,
      PaletteCommandId::kChangeDynamicToMp,
      PaletteCommandId::kChangeDynamicToMf,
      PaletteCommandId::kChangeDynamicToF,
      PaletteCommandId::kChangeDynamicToFf,
      PaletteCommandId::kChangeDynamicToFff};

  // Every dynamic enumerator has both an apply route and a change route, and
  // the change route preserves the marking's own identity.
  for (std::size_t index = 0; index < apply_dynamics.size(); ++index) {
    auto fixture = build_fixture(metrics);
    if (!fixture.has_value())
      return 1;
    SelectionToolHandler handler(std::move(fixture->project),
                                 std::move(fixture->layout), &shell);
    prepare(handler, metrics);
    handler.set_committed_selection(note_selection(*fixture, 0));
    if (!handler.palette_command_available(apply_dynamics[index]) ||
        !handler.run_palette_command(apply_dynamics[index])) {
      std::fprintf(stderr, "marking-style-edit-test: apply dynamic failed\n");
      return 1;
    }
    const auto* voice = content(handler, *fixture);
    if (voice == nullptr || voice->dynamics().size() != 1u ||
        voice->dynamics().front().value != graphscore::kAllDynamics[index] ||
        voice->dynamics().front().at_event != fixture->events.front()) {
      std::fprintf(stderr, "marking-style-edit-test: applied dynamic wrong\n");
      return 1;
    }
    const auto marking = voice->dynamics().front().id;
    const auto selected =
        marking_selection(*fixture, graphscore::MarkingKind::kDynamic, marking);
    if (!selected.has_value())
      return 1;
    handler.set_committed_selection(*selected);
    const std::size_t other = index == 0u ? 1u : 0u;
    if (!handler.palette_command_available(change_dynamics[other]) ||
        !handler.run_palette_command(change_dynamics[other]) ||
        content(handler, *fixture)->dynamics().size() != 1u ||
        content(handler, *fixture)->dynamics().front().id != marking ||
        content(handler, *fixture)->dynamics().front().value !=
            graphscore::kAllDynamics[other]) {
      std::fprintf(stderr, "marking-style-edit-test: change dynamic failed\n");
      return 1;
    }
    if (!layout_matches_fresh(handler, *fixture, metrics)) {
      std::fprintf(stderr,
                   "marking-style-edit-test: dynamic invalidation scope\n");
      return 1;
    }
    // The already-set value is rejected with the composer-facing reason.
    if (handler.palette_command_available(change_dynamics[other]) ||
        handler.palette_command_unavailable_reason(change_dynamics[other]) !=
            "dynamic is already set") {
      std::fprintf(stderr, "marking-style-edit-test: no-op change was live\n");
      return 1;
    }
    if (!handler.test_undo() || !handler.test_redo() ||
        !handler.palette_command_available(PaletteCommandId::kRemoveDynamic) ||
        !handler.run_palette_command(PaletteCommandId::kRemoveDynamic) ||
        !content(handler, *fixture)->dynamics().empty() ||
        handler.drag_state().committed_selection().has_value()) {
      std::fprintf(stderr, "marking-style-edit-test: remove dynamic failed\n");
      return 1;
    }
  }

  // Hairpins: a two-event cross-measure range applies, the marking route
  // changes and removes it, and undo/redo round-trips.
  {
    auto fixture = build_fixture(metrics);
    if (!fixture.has_value())
      return 1;
    SelectionToolHandler handler(std::move(fixture->project),
                                 std::move(fixture->layout), &shell);
    prepare(handler, metrics);
    handler.set_committed_selection(
        range_selection(*fixture, q(3, 4), q(5, 4)));
    if (!handler.palette_command_available(PaletteCommandId::kApplyCrescendo) ||
        !handler.run_palette_command(PaletteCommandId::kApplyCrescendo) ||
        content(handler, *fixture)->hairpins().size() != 1u ||
        content(handler, *fixture)->hairpins().front().start_event !=
            fixture->events[3] ||
        content(handler, *fixture)->hairpins().front().end_event !=
            fixture->events[4]) {
      std::fprintf(stderr, "marking-style-edit-test: apply hairpin failed\n");
      return 1;
    }
    // The [3/4, 5/4) range straddles the measure boundary, so this is the
    // cross-measure invalidation marking_edit_invalidation() must widen to.
    if (!layout_matches_fresh(handler, *fixture, metrics)) {
      std::fprintf(stderr,
                   "marking-style-edit-test: hairpin invalidation scope\n");
      return 1;
    }
    const auto marking = content(handler, *fixture)->hairpins().front().id;
    const auto selected =
        marking_selection(*fixture, graphscore::MarkingKind::kHairpin, marking);
    if (!selected.has_value())
      return 1;
    handler.set_committed_selection(*selected);
    if (!handler.run_palette_command(
            PaletteCommandId::kChangeHairpinToDiminuendo) ||
        content(handler, *fixture)->hairpins().front().id != marking ||
        content(handler, *fixture)->hairpins().front().direction !=
            graphscore::HairpinDirection::kDiminuendo ||
        handler.palette_command_available(
            PaletteCommandId::kChangeHairpinToDiminuendo) ||
        handler.palette_command_unavailable_reason(
            PaletteCommandId::kChangeHairpinToDiminuendo) !=
            "hairpin direction is already set") {
      std::fprintf(stderr, "marking-style-edit-test: change hairpin failed\n");
      return 1;
    }
    if (!handler.test_undo() || !handler.test_redo() ||
        !handler.run_palette_command(PaletteCommandId::kRemoveHairpin) ||
        !content(handler, *fixture)->hairpins().empty() ||
        handler.drag_state().committed_selection().has_value()) {
      std::fprintf(stderr, "marking-style-edit-test: remove hairpin failed\n");
      return 1;
    }
    // A single-event range cannot carry a hairpin.
    handler.set_committed_selection(
        range_selection(*fixture, q(0, 1), q(1, 4)));
    if (handler.palette_command_available(PaletteCommandId::kApplyCrescendo) ||
        handler.palette_command_unavailable_reason(
            PaletteCommandId::kApplyCrescendo) !=
            "requires a range of at least two events") {
      std::fprintf(stderr, "marking-style-edit-test: hairpin range feedback\n");
      return 1;
    }
  }

  // Pedal spans: stave-scoped apply over a range, then removal by marking.
  {
    auto fixture = build_fixture(metrics);
    if (!fixture.has_value())
      return 1;
    SelectionToolHandler handler(std::move(fixture->project),
                                 std::move(fixture->layout), &shell);
    prepare(handler, metrics);
    handler.set_committed_selection(
        range_selection(*fixture, q(3, 4), q(5, 4)));
    if (!handler.palette_command_available(PaletteCommandId::kApplyPedalSpan) ||
        !handler.run_palette_command(PaletteCommandId::kApplyPedalSpan) ||
        pedal_spans(handler, *fixture) == nullptr ||
        pedal_spans(handler, *fixture)->size() != 1u ||
        pedal_spans(handler, *fixture)->front().start != q(3, 4) ||
        pedal_spans(handler, *fixture)->front().end != q(5, 4)) {
      std::fprintf(stderr, "marking-style-edit-test: apply pedal failed\n");
      return 1;
    }
    if (!layout_matches_fresh(handler, *fixture, metrics)) {
      std::fprintf(stderr,
                   "marking-style-edit-test: pedal invalidation scope\n");
      return 1;
    }
    const auto marking  = pedal_spans(handler, *fixture)->front().id;
    const auto selected = marking_selection(
        *fixture, graphscore::MarkingKind::kPedalSpan, marking);
    if (!selected.has_value())
      return 1;
    handler.set_committed_selection(*selected);
    if (!handler.test_undo() || !handler.test_redo() ||
        !handler.palette_command_available(
            PaletteCommandId::kRemovePedalSpan) ||
        !handler.run_palette_command(PaletteCommandId::kRemovePedalSpan) ||
        !pedal_spans(handler, *fixture)->empty() ||
        handler.drag_state().committed_selection().has_value()) {
      std::fprintf(stderr, "marking-style-edit-test: remove pedal failed\n");
      return 1;
    }
  }

  // Ties preserve the selected notehead and round-trip as one transaction.
  {
    auto fixture = build_fixture(metrics);
    if (!fixture.has_value())
      return 1;
    SelectionToolHandler handler(std::move(fixture->project),
                                 std::move(fixture->layout), &shell);
    prepare(handler, metrics);
    const auto selected = note_selection(*fixture, 0);
    handler.set_committed_selection(selected);
    if (!handler.palette_command_available(PaletteCommandId::kApplyTie) ||
        !handler.run_palette_command(PaletteCommandId::kApplyTie) ||
        !std::get<graphscore::Note>(
             content(handler, *fixture)->events().front())
             .tied_to_next ||
        handler.drag_state().committed_selection() != selected ||
        !handler.test_undo() || !handler.test_redo() ||
        handler.palette_command_available(PaletteCommandId::kApplyTie) ||
        !handler.palette_command_available(PaletteCommandId::kRemoveTie) ||
        !handler.run_palette_command(PaletteCommandId::kRemoveTie) ||
        std::get<graphscore::Note>(content(handler, *fixture)->events().front())
            .tied_to_next ||
        handler.drag_state().committed_selection() != selected) {
      std::fprintf(stderr, "marking-style-edit-test: tie transaction failed\n");
      return 1;
    }
    handler.set_committed_selection(note_selection(*fixture, 3));
    const auto tie_invalidation = handler.test_marking_edit_invalidation(true);
    if (!tie_invalidation.has_value() ||
        tie_invalidation->kind !=
            graphscore::NotationInvalidationKind::kCrossMeasureSpan ||
        tie_invalidation->first_measure != 0u ||
        tie_invalidation->last_measure != 1u) {
      std::fprintf(stderr,
                   "marking-style-edit-test: tie invalidation failed\n");
      return 1;
    }
  }

  // Slur apply preserves the range; removal clears its now-stale marking.
  {
    auto fixture = build_fixture(metrics);
    if (!fixture.has_value())
      return 1;
    SelectionToolHandler handler(std::move(fixture->project),
                                 std::move(fixture->layout), &shell);
    prepare(handler, metrics);
    const auto range = range_selection(*fixture, q(0, 1), q(1, 2));
    handler.set_committed_selection(range);
    if (!handler.palette_command_available(PaletteCommandId::kApplySlur) ||
        !handler.run_palette_command(PaletteCommandId::kApplySlur) ||
        content(handler, *fixture)->slurs().size() != 1u ||
        handler.drag_state().committed_selection() != range ||
        !handler.test_undo() || !handler.test_redo()) {
      std::fprintf(stderr, "marking-style-edit-test: slur apply failed\n");
      return 1;
    }
    const auto marking = content(handler, *fixture)->slurs().front().id;
    const auto selected =
        marking_selection(*fixture, graphscore::MarkingKind::kSlur, marking);
    if (!selected.has_value())
      return 1;
    handler.set_committed_selection(*selected);
    if (!handler.palette_command_available(PaletteCommandId::kRemoveSlur) ||
        !handler.run_palette_command(PaletteCommandId::kRemoveSlur) ||
        !content(handler, *fixture)->slurs().empty() ||
        handler.drag_state().committed_selection().has_value()) {
      std::fprintf(stderr, "marking-style-edit-test: slur remove failed\n");
      return 1;
    }
  }

  // A half-open range ending exactly at the next measure's boundary remains a
  // local-content invalidation in the preceding measure.
  {
    auto fixture = build_fixture(metrics);
    if (!fixture.has_value())
      return 1;
    SelectionToolHandler handler(std::move(fixture->project),
                                 std::move(fixture->layout), &shell);
    prepare(handler, metrics);
    handler.set_committed_selection(
        range_selection(*fixture, q(0, 1), q(1, 1)));
    const auto invalidation = handler.test_marking_edit_invalidation();
    if (!invalidation.has_value() ||
        invalidation->kind !=
            graphscore::NotationInvalidationKind::kLocalContent ||
        invalidation->first_measure != 0u || invalidation->last_measure != 0u) {
      std::fprintf(stderr,
                   "marking-style-edit-test: half-open invalidation failed\n");
      return 1;
    }
  }

  // Beam overrides are range-only: apply break/join replaces in place with a
  // stable identity, removal matches the same exact range, and every operation
  // preserves that range through one-step undo/redo.
  {
    auto fixture = build_fixture(metrics, graphscore::NoteValue::kEighth, 16);
    if (!fixture.has_value())
      return 1;
    SelectionToolHandler handler(std::move(fixture->project),
                                 std::move(fixture->layout), &shell);
    prepare(handler, metrics);
    const auto selected = range_selection(*fixture, q(0, 1), q(1, 4));
    handler.set_committed_selection(selected);
    const std::size_t initial_depth = handler.test_undo_stack_size();
    if (!handler.palette_command_available(PaletteCommandId::kApplyBeamBreak) ||
        !handler.run_palette_command(PaletteCommandId::kApplyBeamBreak) ||
        content(handler, *fixture)->beam_overrides().size() != 1u ||
        content(handler, *fixture)->beam_overrides().front().events !=
            std::vector<graphscore::NotationEntityId>{fixture->events[0],
                                                      fixture->events[1]} ||
        handler.drag_state().committed_selection() != selected ||
        handler.test_undo_stack_size() != initial_depth + 1u ||
        !layout_matches_fresh(handler, *fixture, metrics)) {
      std::fprintf(stderr,
                   "marking-style-edit-test: apply beam break failed\n");
      return 1;
    }
    const auto stable_id =
        content(handler, *fixture)->beam_overrides().front().id;
    if (handler.palette_command_available(PaletteCommandId::kApplyBeamBreak) ||
        handler.palette_command_unavailable_reason(
            PaletteCommandId::kApplyBeamBreak) !=
            "beam break is already applied to this exact range" ||
        !handler.palette_command_available(PaletteCommandId::kApplyBeamJoin) ||
        !handler.run_palette_command(PaletteCommandId::kApplyBeamJoin) ||
        handler.test_undo_stack_size() != initial_depth + 2u ||
        content(handler, *fixture)->beam_overrides().size() != 1u ||
        content(handler, *fixture)->beam_overrides().front().id != stable_id ||
        content(handler, *fixture)->beam_overrides().front().kind !=
            graphscore::BeamOverride::Kind::kJoin ||
        handler.drag_state().committed_selection() != selected ||
        !handler.test_undo() ||
        content(handler, *fixture)->beam_overrides().front().kind !=
            graphscore::BeamOverride::Kind::kBreak ||
        handler.drag_state().committed_selection() != selected ||
        !handler.test_redo() ||
        content(handler, *fixture)->beam_overrides().front().kind !=
            graphscore::BeamOverride::Kind::kJoin ||
        handler.drag_state().committed_selection() != selected) {
      std::fprintf(stderr, "marking-style-edit-test: replace beam failed\n");
      return 1;
    }
    if (!handler.palette_command_available(
            PaletteCommandId::kRemoveBeamOverride) ||
        !handler.run_palette_command(PaletteCommandId::kRemoveBeamOverride) ||
        !content(handler, *fixture)->beam_overrides().empty() ||
        handler.drag_state().committed_selection() != selected ||
        !handler.test_undo() ||
        content(handler, *fixture)->beam_overrides().front().id != stable_id ||
        handler.drag_state().committed_selection() != selected ||
        !handler.test_redo() ||
        !content(handler, *fixture)->beam_overrides().empty() ||
        handler.drag_state().committed_selection() != selected ||
        handler.palette_command_available(
            PaletteCommandId::kRemoveBeamOverride) ||
        handler.palette_command_unavailable_reason(
            PaletteCommandId::kRemoveBeamOverride) !=
            "no beam override exists on this exact range") {
      std::fprintf(stderr, "marking-style-edit-test: remove beam failed\n");
      return 1;
    }
  }

  // Palette inventory/search exposes exactly the three chord-less beam rows,
  // and their selection precondition is ungated by the active tool.
  {
    auto fixture = build_fixture(metrics, graphscore::NoteValue::kEighth, 16);
    if (!fixture.has_value())
      return 1;
    SelectionToolHandler handler(std::move(fixture->project),
                                 std::move(fixture->layout), &shell);
    prepare(handler, metrics);
    const auto selected = range_selection(*fixture, q(0, 1), q(1, 4));
    handler.set_active_tool(graphscore::ActiveTool::kNoteEntry);
    handler.set_committed_selection(selected);
    handler.command_palette_set_filter("BEAM");
    const auto rows = handler.command_palette_filtered();
    if (rows.size() != 3u ||
        std::ranges::any_of(rows,
                            [](const PaletteCommand& row) {
                              return !row.chord_hint.empty();
                            }) ||
        !handler.palette_command_available(PaletteCommandId::kApplyBeamBreak) ||
        !handler.run_palette_command(PaletteCommandId::kApplyBeamBreak)) {
      std::fprintf(stderr, "marking-style-edit-test: beam palette inventory\n");
      return 1;
    }
  }

  // A failed beam-edit publication rolls the provisional command back and
  // preserves project, retained geometry, range selection, and history.
  {
    auto fixture = build_fixture(metrics, graphscore::NoteValue::kEighth, 16);
    if (!fixture.has_value())
      return 1;
    const auto exact = graphscore::make_beam_override(
        graphscore::BeamOverride::Kind::kJoin,
        {fixture->events[0], fixture->events[1]});
    const auto overlapping = graphscore::make_beam_override(
        graphscore::BeamOverride::Kind::kJoin,
        {fixture->events[0], fixture->events[1], fixture->events[2]});
    auto* initial_stave = fixture->project.find_node(fixture->node)
                              ->lane(fixture->track)
                              ->stave(fixture->stave);
    if (!initial_stave->voice(fixture->voice).add_beam_override(exact).ok() ||
        !initial_stave->voice(fixture->voice)
             .add_beam_override(overlapping)
             .ok()) {
      std::fprintf(stderr, "marking-style-edit-test: beam rollback setup\n");
      return 1;
    }
    auto initial_layout =
        graphscore::layout_notation(fixture->project, fixture->node, metrics);
    if (!initial_layout.layout.has_value())
      return 1;
    fixture->layout = std::move(*initial_layout.layout);
    SelectionToolHandler handler(std::move(fixture->project),
                                 std::move(fixture->layout), &shell);
    handler.set_metrics(&metrics);
    handler.warm_layout_cache();
    handler.set_surface_publisher([](const graphscore::NotationLayout&) {
      return graphscore::ShellResult{
          graphscore::ShellError::kRenderingSetupFailed,
          "injected beam publication failure"};
    });
    const auto selected = range_selection(*fixture, q(0, 1), q(1, 4));
    handler.set_committed_selection(selected);
    const auto original_layout  = handler.layout();
    const auto original_surface = shell.test_snapshot_notation_surface();
    if (handler.run_palette_command(PaletteCommandId::kApplyBeamBreak) ||
        content(handler, *fixture)->beam_overrides() !=
            std::vector<graphscore::BeamOverride>{exact, overlapping} ||
        handler.layout() != original_layout ||
        shell.test_snapshot_notation_surface() != original_surface ||
        handler.drag_state().committed_selection() != selected ||
        handler.test_undo_stack_size() != 0u ||
        handler.test_redo_stack_size() != 0u ||
        handler.take_diagnostics() !=
            std::vector<std::string>{"beam override: layout refresh failed"}) {
      std::fprintf(stderr, "marking-style-edit-test: beam rollback failed\n");
      return 1;
    }
  }

  // Non-beamable and absent targets report the same stable reason through
  // availability and execution, with no history or selection mutation.
  {
    auto fixture = build_fixture(metrics);
    if (!fixture.has_value())
      return 1;
    SelectionToolHandler handler(std::move(fixture->project),
                                 std::move(fixture->layout), &shell);
    prepare(handler, metrics);
    const auto selected = range_selection(*fixture, q(0, 1), q(1, 2));
    handler.set_committed_selection(selected);
    if (handler.palette_command_available(PaletteCommandId::kApplyBeamJoin) ||
        handler.palette_command_unavailable_reason(
            PaletteCommandId::kApplyBeamJoin) !=
            "every selected event must be beamable" ||
        handler.run_palette_command(PaletteCommandId::kApplyBeamJoin) ||
        handler.take_diagnostics() !=
            std::vector<std::string>{
                "beam override: every selected event must be beamable"} ||
        handler.test_undo_stack_size() != 0u ||
        handler.drag_state().committed_selection() != selected) {
      std::fprintf(stderr, "marking-style-edit-test: beam invalid feedback\n");
      return 1;
    }
    handler.set_committed_selection(std::nullopt);
    if (handler.run_palette_command(PaletteCommandId::kApplyBeamBreak) ||
        handler.take_diagnostics() !=
            std::vector<std::string>{
                "beam override: requires an exact range of complete events on "
                "one live staff and voice"}) {
      std::fprintf(stderr, "marking-style-edit-test: absent beam target\n");
      return 1;
    }
  }

  // Undo and redo publication compensation bypasses a failing normal inverse
  // on every marking command this phase routes through the palette.
  for (const ApplyRoute route :
       {ApplyRoute::kDynamic, ApplyRoute::kChangeDynamic, ApplyRoute::kHairpin,
        ApplyRoute::kPedalSpan, ApplyRoute::kTie, ApplyRoute::kSlur,
        ApplyRoute::kBeam}) {
    if (check_publication_compensation(metrics, shell, route) != 0)
      return 1;
  }

  // A failing publication leaves the edit itself atomic: nothing committed,
  // no history entry, and the action stays available for a second attempt.
  {
    auto fixture = build_fixture(metrics);
    if (!fixture.has_value())
      return 1;
    SelectionToolHandler handler(std::move(fixture->project),
                                 std::move(fixture->layout), &shell);
    handler.set_metrics(&metrics);
    handler.warm_layout_cache();
    bool fail_publish = true;
    handler.set_surface_publisher(
        [&fail_publish](const graphscore::NotationLayout&) {
          if (fail_publish) {
            fail_publish = false;
            return graphscore::ShellResult{
                graphscore::ShellError::kRenderingSetupFailed,
                "injected marking publish failure"};
          }
          return graphscore::ShellResult{};
        });
    handler.set_committed_selection(note_selection(*fixture, 0));
    if (handler.run_palette_command(PaletteCommandId::kApplyDynamicMf) ||
        handler.test_undo_stack_size() != 0u ||
        !content(handler, *fixture)->dynamics().empty() ||
        !handler.palette_command_available(PaletteCommandId::kApplyDynamicMf) ||
        !handler.run_palette_command(PaletteCommandId::kApplyDynamicMf) ||
        content(handler, *fixture)->dynamics().size() != 1u) {
      std::fprintf(stderr, "marking-style-edit-test: atomic rollback failed\n");
      return 1;
    }
  }

  // Invalid targets produce the exact composer-facing diagnostics.
  {
    auto fixture = build_fixture(metrics);
    if (!fixture.has_value())
      return 1;
    SelectionToolHandler handler(std::move(fixture->project),
                                 std::move(fixture->layout), &shell);
    prepare(handler, metrics);
    handler.set_committed_selection(note_selection(*fixture, 0));
    std::ignore = handler.take_diagnostics();
    if (handler.run_palette_command(PaletteCommandId::kChangeDynamicToF) ||
        handler.take_diagnostics() !=
            std::vector<std::string>{
                "dynamic: requires one live dynamic marking"}) {
      std::fprintf(stderr, "marking-style-edit-test: dynamic diagnostic\n");
      return 1;
    }
    if (handler.run_palette_command(PaletteCommandId::kApplyCrescendo) ||
        handler.take_diagnostics() !=
            std::vector<std::string>{
                "hairpin: requires a range of complete events on one staff "
                "and voice"}) {
      std::fprintf(stderr, "marking-style-edit-test: hairpin diagnostic\n");
      return 1;
    }
    if (handler.run_palette_command(PaletteCommandId::kRemovePedalSpan) ||
        handler.take_diagnostics() !=
            std::vector<std::string>{
                "pedal span: requires one live pedal span marking"}) {
      std::fprintf(stderr, "marking-style-edit-test: pedal diagnostic\n");
      return 1;
    }
    if (handler.run_palette_command(PaletteCommandId::kRemoveTie) ||
        handler.take_diagnostics() !=
            std::vector<std::string>{"tie: notehead has no tie to remove"} ||
        handler.run_palette_command(PaletteCommandId::kApplySlur) ||
        handler.take_diagnostics() !=
            std::vector<std::string>{
                "slur: requires a range of complete events on one staff and "
                "voice"}) {
      std::fprintf(stderr, "marking-style-edit-test: tie/slur diagnostic\n");
      return 1;
    }
    handler.set_committed_selection(std::nullopt);
    if (handler.run_palette_command(PaletteCommandId::kApplyDynamicMf) ||
        handler.take_diagnostics() !=
            std::vector<std::string>{
                "dynamic: requires a note, chord, or dynamic marking"} ||
        handler.run_palette_command(PaletteCommandId::kApplyPedalSpan) ||
        handler.take_diagnostics() !=
            std::vector<std::string>{
                "pedal span: requires a range or pedal span marking"}) {
      std::fprintf(stderr, "marking-style-edit-test: empty selection route\n");
      return 1;
    }
  }

  for (const auto& row : palette_inventory()) {
    if (row.id >= PaletteCommandId::kApplyDynamicPpp &&
        !row.chord_hint.empty()) {
      std::fprintf(stderr, "marking-style-edit-test: nonempty chord hint\n");
      return 1;
    }
  }
  return 0;
}

}  // namespace graphscore::writer_app
