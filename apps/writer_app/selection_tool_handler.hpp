// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <graphscore/domain/graphscore_domain.hpp>
#include <graphscore/notation/graphscore_notation.hpp>
#include <graphscore/writer_shell/graphscore_writer_shell.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace graphscore::writer_app {

// Owns the drag state machine and active tool at the application assembly
// layer. Registered with WriterShell before open_window(), so the shell's
// event loop dispatches real pointer events to it.
//
// Lifecycle:
//   - Constructed before open_window(), destroyed after it returns.
//   - A raw pointer to the shell is stored; the handler never owns it.
//   - Cancel is called on destruction and on tool switch, so the shell
//     never sees a stale in-progress drag after the window closes.
class SelectionToolHandler final : public graphscore::InputHandler {
 public:
  SelectionToolHandler(graphscore::Project        project,
                       graphscore::NotationLayout layout,
                       graphscore::WriterShell*   shell);

  SelectionToolHandler(const SelectionToolHandler&)            = delete;
  SelectionToolHandler& operator=(const SelectionToolHandler&) = delete;

  ~SelectionToolHandler() override;

  // ---- InputHandler --------------------------------------------------------

  void on_pointer_press(graphscore::PointerEvent event) override;
  void on_pointer_move(graphscore::PointerEvent event) override;
  void on_pointer_release(graphscore::PointerEvent event) override;
  void on_cancel() override;
  void on_key_press(graphscore::KeyEvent event) override;

  // "Backspace"/"Delete" (M5-phase-22): deletes the single selected notehead.
  bool delete_selected_notehead();

  // "R" (M5-phase-23): converts the entire selected note/chord event to an
  // equal-duration rest and selects the resulting rest.
  bool convert_selection_to_rest();

  // Primary+Up/Down (M5-phase-24): moves the committed selection to the
  // prior/next staff of the node, wrapping within it. A pure SELECTION
  // change that builds no Command and opens no history transaction.
  bool step_selected_staff(graphscore::StaffStepDirection direction);

  // ---- accessible range controls (M5-phase-19b-iii) ------------------------

  bool extend_range_edge(graphscore::RangeEdge edge, graphscore::Rational time);

  bool extend_range_staff_scope(graphscore::MeasureScope first_staff,
                                graphscore::MeasureScope last_staff);

  bool select_to_node_start();

  bool select_to_node_end();

  // ---- tool switching ------------------------------------------------------

  void set_active_tool(graphscore::ActiveTool tool);

  [[nodiscard]] graphscore::ActiveTool active_tool() const noexcept;

  // Supplies the glyph metrics used to refresh the retained layout after a
  // notehead move (M5-phase-20).
  void set_metrics(const graphscore::GlyphMetrics* metrics) noexcept;

  // The step that publishes the rasterised notation surface for a refreshed
  // layout to the shell.
  using SurfacePublisher =
      std::function<graphscore::ShellResult(const graphscore::NotationLayout&)>;

  void set_surface_publisher(SurfacePublisher publisher);

  // Supplies the layout options the incremental layout cache uses.
  void set_layout_options(graphscore::NotationLayoutOptions options);

  // The step that builds the reversible command for a notehead move.
  using MoveCommandFactory = std::function<std::unique_ptr<graphscore::Command>(
      const graphscore::Project&, const graphscore::NoteheadItem&,
      graphscore::NoteheadStepDirection)>;

  void set_move_command_factory(MoveCommandFactory factory);

  // The step that builds the reversible command for an accidental step.
  using AccidentalCommandFactory =
      std::function<std::unique_ptr<graphscore::Command>(
          const graphscore::Project&, const graphscore::NoteheadItem&,
          graphscore::AccidentalStepDirection)>;

  void set_accidental_command_factory(AccidentalCommandFactory factory);

  // Builds the retained incremental layout cache from the current project and
  // layout, so a later refresh_layout() reuses unaffected systems.
  void warm_layout_cache();

  // Stores a selection directly, mirroring SelectionDragState's own
  // keyboard/accessible entry point (M5-phase-19b).
  void set_committed_selection(std::optional<graphscore::Selection> selection);

  // Moves the single selected notehead one diatonic staff step (M5-phase-20).
  bool move_selected_notehead(graphscore::NoteheadStepDirection direction);

  // Steps the single selected notehead's accidental one rung along the
  // double-flat .. double-sharp ladder (M5-phase-21).
  bool step_selected_accidental(graphscore::AccidentalStepDirection direction);

  // Adds one key-spelled diatonic interval notehead above/below the single
  // selected notehead (M5-phase-25).
  bool add_selected_interval(graphscore::IntervalDirection direction,
                             std::uint8_t                  interval);

  // ---- test access ---------------------------------------------------------

  [[nodiscard]] const graphscore::SelectionDragState& drag_state()
      const noexcept;

  [[nodiscard]] const graphscore::Project& project() const noexcept;

  [[nodiscard]] const graphscore::NotationLayout& layout() const noexcept;

  [[nodiscard]] const std::optional<graphscore::NoteAuditionRequest>&
  last_audition() const noexcept;

  [[nodiscard]] const graphscore::NotationLayoutWork& test_last_layout_work()
      const noexcept;

  [[nodiscard]] std::size_t test_undo_stack_size() const noexcept;

  [[nodiscard]] std::size_t test_redo_stack_size() const noexcept;

  [[nodiscard]] bool history_unavailable() const noexcept;

  // Retries the rollback of a failed move's provisional command once the
  // history has been poisoned by that failure.
  void recover_from_failed_rollback();

  [[nodiscard]] bool test_undo();

  [[nodiscard]] bool test_redo();

  [[nodiscard]] std::optional<graphscore::MeasureScope> first_staff()
      const noexcept;

  [[nodiscard]] std::optional<graphscore::MeasureScope> last_staff()
      const noexcept;

 private:
  [[nodiscard]] const graphscore::ArbitraryRangeSet* current_range_set()
      const noexcept;

  [[nodiscard]] const graphscore::NoteheadSet* current_notehead_set()
      const noexcept;

  [[nodiscard]] const graphscore::ChordSet* current_chord_set() const noexcept;

  void sync_staff_endpoints_from_committed();

  bool step_staff_scope(int direction);

  void update_highlight();

  void resolve_single_click_selection(graphscore::NotationPoint point);

  [[nodiscard]] std::optional<graphscore::NotationInvalidation>
  notehead_invalidation(const graphscore::NoteheadItem& item) const;

  [[nodiscard]] std::optional<graphscore::NotationInvalidation>
  interval_invalidation(const graphscore::NoteheadItem& item) const;

  [[nodiscard]] const graphscore::VoiceContent* resolve_voice(
      graphscore::NodeId node, graphscore::TrackId track,
      graphscore::StaveId stave, graphscore::Voice voice) const;

  [[nodiscard]] std::optional<graphscore::NotationInvalidation>
  chord_notes_invalidation(const graphscore::Chord& chord,
                           graphscore::NodeId node, graphscore::TrackId track,
                           graphscore::StaveId stave,
                           graphscore::Voice   voice) const;

  [[nodiscard]] std::optional<graphscore::NotationInvalidation>
  convert_to_rest_invalidation(
      const std::optional<graphscore::NoteheadItem>& notehead_item,
      const std::optional<graphscore::ChordItem>&    chord_item) const;

  bool refresh_layout(
      std::optional<graphscore::NotationInvalidation> forced = std::nullopt);

  graphscore::Project            project_;
  graphscore::NotationLayout     layout_;
  graphscore::WriterShell*       shell_;
  graphscore::SelectionDragState drag_;
  // Reversible-command history for notehead moves (M5-phase-20) and
  // accidental steps (M5-phase-21). Undo/redo
  // key bindings belong to M5-phase-26; this phase routes each mutation
  // through begin_transaction()/commit()/abort() so it is undoable once
  // those bindings exist and a failed surface publication restores the exact
  // prior history (including any pre-existing redo).
  graphscore::CommandHistory history_;
  // The notehead-move command factory; see set_move_command_factory(). The
  // default builds the real MoveNoteheadCommand; the rollback-failure tests
  // replace it with a deterministic failing wrapper.
  MoveCommandFactory move_command_factory_ =
      &graphscore::make_move_notehead_command;
  // The accidental-step command factory; see set_accidental_command_factory().
  // The default builds the real StepAccidentalCommand; the rollback-failure
  // tests replace it with a deterministic failing wrapper.
  AccidentalCommandFactory accidental_command_factory_ =
      &graphscore::make_step_accidental_command;
  // Glyph metrics used to refresh the retained layout after a move; see
  // set_metrics() and refresh_layout().
  const graphscore::GlyphMetrics* metrics_ = nullptr;
  // The armed palette used only to resolve a click to a selection
  // (resolve_selection_at's armed voice for stemless-chord disambiguation and
  // insertion-caret naming). Defaults to a quarter-note, voice-1 note-entry
  // state; the note-entry tool's palette wiring is a later milestone.
  graphscore::NotePaletteState palette_;
  // Retained incremental layout state (M5-phase-8). Seeded at startup by
  // warm_layout_cache() (run()) and re-seeded after any failed-move rollback,
  // so refresh_layout() is always incremental on the first move.
  graphscore::NotationLayoutCache   layout_cache_;
  graphscore::NotationLayoutOptions layout_options_;
  // The surface publish step; see set_surface_publisher().
  SurfacePublisher publish_surface_;
  // The audition request the most recent successful move issued; see
  // last_audition().
  std::optional<graphscore::NoteAuditionRequest> last_audition_;
  // The incremental-layout work the most recent refresh_layout() recorded;
  // see test_last_layout_work().
  graphscore::NotationLayoutWork last_layout_work_;
  // True once the pointer has moved during the current drag, so a
  // press-and-release-without-move is distinguished from a genuine range drag.
  bool                      moved_during_drag_ = false;
  graphscore::ActiveTool    active_tool_ = graphscore::ActiveTool::kSelection;
  graphscore::PointerButton initiating_button_ =
      graphscore::PointerButton::kUnknown;

  // Which edge of the committed selection's span the next Shift+Left/Right
  // step moves (M5-phase-19b-iii). extend_range_edge() recomputes this
  // after every successful call, since a crossing extension swaps which
  // edge now carries the moved value.
  graphscore::RangeEdge focus_edge_ = graphscore::RangeEdge::kEnd;

  // The staff scope of the committed selection. A pointer-drag commit
  // derives these from the committed selection's own items
  // (sync_staff_endpoints_from_committed()); extend_range_staff_scope()
  // instead sets them to its own caller-supplied endpoints directly,
  // which is what lets a later edge-only extension preserve a staff the
  // item-derivation would lose (see extend_range_edge()'s own comment).
  // Both are std::nullopt exactly when there is no committed
  // ArbitraryRangeSet selection.
  std::optional<graphscore::MeasureScope> first_staff_;
  std::optional<graphscore::MeasureScope> last_staff_;
};

}  // namespace graphscore::writer_app
