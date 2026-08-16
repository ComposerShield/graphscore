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

struct StyleFixture {
  graphscore::Project          project;
  graphscore::NodeId           node;
  graphscore::TrackId          track;
  graphscore::StaveId          stave;
  graphscore::Voice            voice;
  graphscore::NotationEntityId event;
  graphscore::NotationLayout   layout;
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

[[nodiscard]] std::optional<StyleFixture> build_fixture(
    const graphscore::GlyphMetrics& metrics, bool chord) {
  graphscore::Project project{graphscore::ProjectId::generate(), "Style"};
  const auto          channel = graphscore::MidiChannel::create(0);
  if (!channel.has_value())
    return std::nullopt;
  const auto track = project.add_track(
      "Track", graphscore::StaffLayout::single_staff(graphscore::Clef::kTreble),
      *channel);
  if (!track.has_value())
    return std::nullopt;
  const auto node = project.add_node("Node");
  const auto stave =
      project.active_tracks().front().layout().staves().front().id;
  auto* lane = project.find_node(node)->lane(*track);
  lane->ensure_stave(stave);
  const auto signature = graphscore::TimeSignature::create(4, 4);
  if (!signature.has_value())
    return std::nullopt;
  auto timeline = graphscore::NodeTimeline::create(
      {{*signature, graphscore::KeySignature{}}},
      project.active_tracks().front().layout().staves());
  if (!timeline.has_value())
    return std::nullopt;
  project.find_node(node)->set_timeline(std::move(*timeline));
  const auto voice = graphscore::Voice::create(1);
  const auto duration =
      graphscore::Duration::create(graphscore::NoteValue::kQuarter, 0);
  const auto c = graphscore::SpelledPitch::create(graphscore::Letter::kC, 4);
  const auto e = graphscore::SpelledPitch::create(graphscore::Letter::kE, 4);
  if (!voice.has_value() || !duration.has_value() || !c.has_value() ||
      !e.has_value())
    return std::nullopt;
  graphscore::VoiceEvent event =
      chord ? graphscore::VoiceEvent{graphscore::make_chord(
                  *duration,
                  {{graphscore::NotationEntityId::generate(), *c, false},
                   {graphscore::NotationEntityId::generate(), *e, false}})}
            : graphscore::VoiceEvent{graphscore::make_note(*c, *duration)};
  const auto event_id = graphscore::event_id(event);
  auto&      content  = lane->stave(stave)->voice(*voice);
  if (!content.append(std::move(event)).ok() ||
      !content.normalize(project.find_node(node)->timeline()->node_end()).ok())
    return std::nullopt;
  auto layout = graphscore::layout_notation(project, node, metrics);
  if (!layout.layout.has_value())
    return std::nullopt;
  return StyleFixture{
      std::move(project),       node, *track, stave, *voice, event_id,
      std::move(*layout.layout)};
}

[[nodiscard]] graphscore::Selection event_selection(const StyleFixture& fixture,
                                                    bool                chord) {
  if (chord) {
    return graphscore::Selection{*graphscore::ChordSet::create(
        {{fixture.node, fixture.track, fixture.stave, fixture.voice,
          fixture.event}})};
  }
  return graphscore::Selection{*graphscore::NoteheadSet::create(
      {{fixture.node, fixture.track, fixture.stave, fixture.voice,
        fixture.event}})};
}

[[nodiscard]] const graphscore::VoiceEvent* style_event(
    const SelectionToolHandler& handler, const StyleFixture& fixture) {
  const auto* node  = handler.project().find_node(fixture.node);
  const auto* lane  = node == nullptr ? nullptr : node->lane(fixture.track);
  const auto* stave = lane == nullptr ? nullptr : lane->stave(fixture.stave);
  if (stave == nullptr)
    return nullptr;
  const auto& events = stave->voice(fixture.voice).events();
  const auto  found  = std::ranges::find_if(events, [&](const auto& event) {
    return graphscore::event_id(event) == fixture.event;
  });
  return found == events.end() ? nullptr : &*found;
}

void prepare(SelectionToolHandler&           handler,
             const graphscore::GlyphMetrics& metrics) {
  handler.set_metrics(&metrics);
  handler.warm_layout_cache();
  handler.set_surface_publisher([](const graphscore::NotationLayout&) {
    return graphscore::ShellResult{};
  });
}

}  // namespace

int event_style_edit_test() {
  const SelfTestMetrics   metrics;
  graphscore::WriterShell shell;

  constexpr std::array apply_ids = {
      PaletteCommandId::kApplyAccent, PaletteCommandId::kApplyMarcato,
      PaletteCommandId::kApplyStaccato, PaletteCommandId::kApplyStaccatissimo,
      PaletteCommandId::kApplyTenuto};
  constexpr std::array change_ids = {
      PaletteCommandId::kChangeArticulationToAccent,
      PaletteCommandId::kChangeArticulationToMarcato,
      PaletteCommandId::kChangeArticulationToStaccato,
      PaletteCommandId::kChangeArticulationToStaccatissimo,
      PaletteCommandId::kChangeArticulationToTenuto};

  // Every articulation enumerator has both an apply route and a change route.
  for (std::size_t index = 0; index < apply_ids.size(); ++index) {
    auto apply_fixture = build_fixture(metrics, false);
    if (!apply_fixture.has_value())
      return 1;
    SelectionToolHandler apply(std::move(apply_fixture->project),
                               std::move(apply_fixture->layout), &shell);
    prepare(apply, metrics);
    apply.set_committed_selection(event_selection(*apply_fixture, false));
    if (!apply.palette_command_available(apply_ids[index]) ||
        !apply.run_palette_command(apply_ids[index])) {
      std::fprintf(stderr, "event-style-edit-test: apply inventory failed\n");
      return 1;
    }

    auto change_fixture = build_fixture(metrics, false);
    if (!change_fixture.has_value())
      return 1;
    SelectionToolHandler change(std::move(change_fixture->project),
                                std::move(change_fixture->layout), &shell);
    prepare(change, metrics);
    change.set_committed_selection(event_selection(*change_fixture, false));
    const std::size_t source_index = index == 0u ? 1u : 0u;
    if (!change.run_palette_command(apply_ids[source_index]))
      return 1;
    const auto marking =
        graphscore::MarkingSet::create({graphscore::MarkingItem{
            change_fixture->node, change_fixture->track, change_fixture->stave,
            change_fixture->voice, graphscore::MarkingKind::kArticulation,
            change_fixture->event,
            graphscore::kAllArticulations[source_index]}});
    if (!marking.has_value())
      return 1;
    change.set_committed_selection(graphscore::Selection{*marking});
    if (!change.palette_command_available(change_ids[index]) ||
        !change.run_palette_command(change_ids[index])) {
      std::fprintf(stderr, "event-style-edit-test: change inventory failed\n");
      return 1;
    }
  }

  for (const bool chord : {false, true}) {
    auto fixture = build_fixture(metrics, chord);
    if (!fixture.has_value())
      return 1;
    const graphscore::Selection selected = event_selection(*fixture, chord);
    SelectionToolHandler        handler(std::move(fixture->project),
                                        std::move(fixture->layout), &shell);
    prepare(handler, metrics);
    handler.set_committed_selection(selected);
    if (!handler.palette_command_available(PaletteCommandId::kApplyAccent) ||
        !handler.run_palette_command(PaletteCommandId::kApplyAccent) ||
        handler.drag_state().committed_selection() != selected ||
        !handler.test_undo() || !handler.test_redo() ||
        !handler.palette_command_available(PaletteCommandId::kStemUp) ||
        !handler.run_palette_command(PaletteCommandId::kStemUp) ||
        !handler.run_palette_command(PaletteCommandId::kStemAuto) ||
        !handler.run_palette_command(PaletteCommandId::kStemDown)) {
      std::fprintf(stderr, "event-style-edit-test: event routing failed\n");
      return 1;
    }
  }

  // Undo and redo publication compensation bypasses a failing normal inverse.
  for (const bool articulation : {true, false}) {
    const PaletteCommandId edit = articulation ? PaletteCommandId::kApplyAccent
                                               : PaletteCommandId::kStemUp;
    auto                   undo_fixture = build_fixture(metrics, false);
    if (!undo_fixture.has_value())
      return 1;
    const graphscore::Selection selected =
        event_selection(*undo_fixture, false);
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
    undo.set_committed_selection(selected);
    if (!undo.run_palette_command(edit))
      return 1;
    const auto* applied_event = style_event(undo, *undo_fixture);
    if (applied_event == nullptr)
      return 1;
    const graphscore::VoiceEvent applied        = *applied_event;
    const auto                   applied_layout = undo.layout();
    const auto applied_surface   = shell.test_snapshot_notation_surface();
    const auto applied_highlight = shell.test_snapshot_highlight_rects();

    graphscore::NotationLayoutOptions invalid_options;
    invalid_options.system_width = 0.0;
    undo.set_layout_options(invalid_options);
    if (undo.run_palette_command(PaletteCommandId::kUndo) ||
        undo.test_undo_stack_size() != 1u ||
        undo.test_redo_stack_size() != 0u ||
        style_event(undo, *undo_fixture) == nullptr ||
        *style_event(undo, *undo_fixture) != applied ||
        undo.layout() != applied_layout ||
        undo.drag_state().committed_selection() != selected ||
        shell.test_snapshot_notation_surface() != applied_surface ||
        shell.test_snapshot_highlight_rects() != applied_highlight ||
        undo.take_diagnostics() !=
            std::vector<std::string>{
                "undo: layout refresh failed; undo restored"}) {
      std::fprintf(stderr,
                   "event-style-edit-test: undo layout compensation failed\n");
      return 1;
    }

    auto redo_fixture = build_fixture(metrics, false);
    if (!redo_fixture.has_value())
      return 1;
    const graphscore::Selection redo_selected =
        event_selection(*redo_fixture, false);
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
    redo.set_committed_selection(redo_selected);
    if (!redo.run_palette_command(edit) ||
        !redo.run_palette_command(PaletteCommandId::kUndo)) {
      return 1;
    }
    const auto* undone_event = style_event(redo, *redo_fixture);
    if (undone_event == nullptr)
      return 1;
    const graphscore::VoiceEvent undone        = *undone_event;
    const auto                   undone_layout = redo.layout();
    const auto undone_surface   = shell.test_snapshot_notation_surface();
    const auto undone_highlight = shell.test_snapshot_highlight_rects();
    redo.set_surface_publisher([](const graphscore::NotationLayout&) {
      return graphscore::ShellResult{
          graphscore::ShellError::kRenderingSetupFailed,
          "injected undo-redo publish failure"};
    });
    if (redo.run_palette_command(PaletteCommandId::kRedo) ||
        redo.test_undo_stack_size() != 0u ||
        redo.test_redo_stack_size() != 1u ||
        style_event(redo, *redo_fixture) == nullptr ||
        *style_event(redo, *redo_fixture) != undone ||
        redo.layout() != undone_layout ||
        redo.drag_state().committed_selection() != redo_selected ||
        shell.test_snapshot_notation_surface() != undone_surface ||
        shell.test_snapshot_highlight_rects() != undone_highlight ||
        redo.take_diagnostics() !=
            std::vector<std::string>{
                "redo: layout refresh failed; redo restored"}) {
      std::fprintf(stderr,
                   "event-style-edit-test: redo publish compensation failed\n");
      return 1;
    }
  }

  auto rollback_fixture = build_fixture(metrics, false);
  if (!rollback_fixture.has_value())
    return 1;
  SelectionToolHandler rollback(std::move(rollback_fixture->project),
                                std::move(rollback_fixture->layout), &shell);
  rollback.set_metrics(&metrics);
  rollback.warm_layout_cache();
  bool fail_publish = true;
  rollback.set_surface_publisher(
      [&fail_publish](const graphscore::NotationLayout&) {
        if (fail_publish) {
          fail_publish = false;
          return graphscore::ShellResult{
              graphscore::ShellError::kRenderingSetupFailed,
              "injected style publish failure"};
        }
        return graphscore::ShellResult{};
      });
  rollback.set_committed_selection(event_selection(*rollback_fixture, false));
  if (rollback.run_palette_command(PaletteCommandId::kApplyAccent) ||
      rollback.test_undo_stack_size() != 0u ||
      !rollback.palette_command_available(PaletteCommandId::kApplyAccent) ||
      !rollback.run_palette_command(PaletteCommandId::kApplyAccent)) {
    std::fprintf(stderr, "event-style-edit-test: atomic rollback failed\n");
    return 1;
  }

  auto fixture = build_fixture(metrics, false);
  if (!fixture.has_value())
    return 1;
  SelectionToolHandler handler(std::move(fixture->project),
                               std::move(fixture->layout), &shell);
  prepare(handler, metrics);
  handler.set_committed_selection(event_selection(*fixture, false));
  if (!handler.run_palette_command(PaletteCommandId::kApplyStaccato) ||
      handler.palette_command_available(PaletteCommandId::kApplyStaccato) ||
      handler.palette_command_available(PaletteCommandId::kApplyTenuto) ||
      handler.palette_command_unavailable_reason(
          PaletteCommandId::kApplyTenuto) !=
          "conflicts with the existing duration articulation") {
    std::fprintf(stderr, "event-style-edit-test: conflict feedback failed\n");
    return 1;
  }
  const auto marking = graphscore::MarkingSet::create({graphscore::MarkingItem{
      fixture->node, fixture->track, fixture->stave, fixture->voice,
      graphscore::MarkingKind::kArticulation, fixture->event,
      graphscore::Articulation::kStaccato}});
  if (!marking.has_value())
    return 1;
  handler.set_committed_selection(graphscore::Selection{*marking});
  if (!handler.run_palette_command(
          PaletteCommandId::kChangeArticulationToTenuto) ||
      !handler.run_palette_command(PaletteCommandId::kRemoveArticulation)) {
    std::fprintf(stderr, "event-style-edit-test: change/remove failed\n");
    return 1;
  }
  const auto stale = graphscore::NoteheadSet::create({graphscore::NoteheadItem{
      fixture->node, fixture->track, fixture->stave, fixture->voice,
      graphscore::NotationEntityId::generate()}});
  if (!stale.has_value())
    return 1;
  handler.set_committed_selection(graphscore::Selection{*stale});
  if (handler.palette_command_available(PaletteCommandId::kStemUp) ||
      handler.palette_command_unavailable_reason(PaletteCommandId::kStemUp) !=
          "requires one live note or chord event") {
    std::fprintf(stderr, "event-style-edit-test: stale feedback failed\n");
    return 1;
  }

  auto grace_fixture = build_notehead_click_fixture(metrics);
  if (!grace_fixture.has_value())
    return 1;
  SelectionToolHandler grace(std::move(grace_fixture->project),
                             std::move(grace_fixture->layout), &shell);
  prepare(grace, metrics);
  if (!select_noteheads(
          grace,
          {{grace_fixture->node_id, grace_fixture->track_id,
            grace_fixture->stave_id, voice_one(), grace_fixture->grace_id}})) {
    return 1;
  }
  constexpr std::array grace_actions = {
      PaletteCommandId::kApplyAccent,
      PaletteCommandId::kApplyMarcato,
      PaletteCommandId::kApplyStaccato,
      PaletteCommandId::kApplyStaccatissimo,
      PaletteCommandId::kApplyTenuto,
      PaletteCommandId::kChangeArticulationToAccent,
      PaletteCommandId::kChangeArticulationToMarcato,
      PaletteCommandId::kChangeArticulationToStaccato,
      PaletteCommandId::kChangeArticulationToStaccatissimo,
      PaletteCommandId::kChangeArticulationToTenuto,
      PaletteCommandId::kRemoveArticulation,
      PaletteCommandId::kStemAuto,
      PaletteCommandId::kStemUp,
      PaletteCommandId::kStemDown};
  for (const PaletteCommandId action : grace_actions) {
    const std::string violation = no_op_violation(
        grace, shell, grace_fixture->node_id, grace_fixture->track_id,
        grace_fixture->stave_id, "grace event style",
        [&]() { std::ignore = grace.run_palette_command(action); });
    if (grace.palette_command_available(action) ||
        grace.palette_command_unavailable_reason(action) !=
            "grace notes do not support event style editing" ||
        !violation.empty()) {
      std::fprintf(stderr, "event-style-edit-test: grace action was live\n");
      return 1;
    }
  }
  handler.set_committed_selection(std::nullopt);
  if (handler.run_palette_command(PaletteCommandId::kStemUp) ||
      handler.diagnostics().empty()) {
    std::fprintf(stderr, "event-style-edit-test: unavailable route failed\n");
    return 1;
  }
  for (const auto& row : palette_inventory()) {
    if (row.id >= PaletteCommandId::kApplyAccent && !row.chord_hint.empty()) {
      std::fprintf(stderr, "event-style-edit-test: nonempty chord hint\n");
      return 1;
    }
  }
  return 0;
}

}  // namespace graphscore::writer_app
