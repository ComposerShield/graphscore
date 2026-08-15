// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <graphscore/domain/graphscore_domain.hpp>
#include <graphscore/notation/graphscore_notation.hpp>
#include <graphscore/writer_shell/graphscore_writer_shell.hpp>

#include "command_palette.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace graphscore::writer_app {

// The app-owned step-entry cursor (docs/plan/05-notation-editor-action-table.md
// §8.1): a distinct value from the committed Selection, holding the
// (node, track, stave, voice, position) the next step-entry commit targets.
// It exists only while the note-entry tool is active; the committed Selection
// keeps its own kSelection meaning.
struct StepEntryCursor {
  graphscore::NodeId   node;
  graphscore::TrackId  track;
  graphscore::StaveId  stave;
  graphscore::Voice    voice;
  graphscore::Rational position;

  [[nodiscard]] bool operator==(const StepEntryCursor&) const = default;
};

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
  void on_text_input(graphscore::TextInputEvent event) override;

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

  // ---- step entry (M5-phase-27) -------------------------------------------

  // Initializes the app-owned step-entry cursor from the committed Selection
  // per §8.2; discards any prior cursor and pitch reference.
  void enter_step_entry();

  // Discards the cursor and pitch reference (tool exit, §8.4).
  void exit_step_entry();

  // Commits a natural `letter` at the cursor (§8.3/§8.5). Returns false and
  // leaves the cursor and pitch reference unchanged on rejection.
  bool step_entry_commit_pitch(graphscore::Letter letter);

  // Commits a rest of the armed duration at the cursor (KP_0).
  bool step_entry_commit_rest();

  // Pointer note entry (M5-phase-27 §8.4): a kNoteEntry click commits a note
  // or rest at the onset the notation layer's preview resolves, repositions
  // the step-entry cursor to that onset, updates the pitch reference, and
  // resets the octave offset. A no-op (with a diagnostic) when the click
  // resolves to nothing or the tool is not kNoteEntry.
  bool pointer_note_entry(graphscore::NotationPoint point);

  // Arms a duration (KP_1..KP_7, §7.6). Selection-independent.
  bool step_entry_arm_duration(graphscore::NoteValue value);

  // Cycles the armed dot count 0→1→2→0 (KP_DECIMAL).
  bool step_entry_cycle_dots();

  // Arms a voice (Alt+1..4, §7.5). Resets the pitch reference on a change.
  bool step_entry_arm_voice(graphscore::Voice voice);

  // Steps the octave reference (Alt+Up/Down): direction > 0 is up, < 0 down.
  bool step_entry_step_octave(int direction);

  // Toggles the active tool between kNoteEntry and kSelection (`N`).
  void toggle_tool();

  // ---- clipboard (M5-phase-27) --------------------------------------------

  bool copy_selection();
  bool cut_selection();
  bool paste_clipboard();

  // ---- undo / redo (M5-phase-27) ------------------------------------------

  bool undo_action();
  bool redo_action();

  // ---- command palette (M5-phase-27) --------------------------------------

  void toggle_command_palette();
  void close_command_palette();

  [[nodiscard]] bool command_palette_open() const noexcept;

  [[nodiscard]] const std::string& command_palette_filter() const noexcept;

  void command_palette_set_filter(std::string filter);

  // The inventory filtered by name/description, case-insensitively (§11
  // search). Filtering never changes a row's availability or chord hint.
  [[nodiscard]] std::vector<PaletteCommand> command_palette_filtered() const;

  // Moves the palette selection within the filtered list (Up/Down).
  void command_palette_move_selection(int delta);

  [[nodiscard]] std::size_t command_palette_selected_index() const noexcept;

  // Runs the selected row. Returns false when the filtered list is empty or
  // the action's own fallback is a no-op.
  bool command_palette_run_selected();

  [[nodiscard]] bool palette_command_available(PaletteCommandId id) const;

  [[nodiscard]] std::string palette_command_unavailable_reason(
      PaletteCommandId id) const;

  bool run_palette_command(PaletteCommandId id);

  // ---- diagnostics (M5-phase-27) ------------------------------------------

  // Appends one composer diagnostic to the app-owned sink. This is the
  // observable mechanism the action table's §7/§8/§10 fallbacks surface;
  // it never couples to a platform logging path.
  void post_diagnostic(std::string message);

  [[nodiscard]] const std::vector<std::string>& diagnostics() const noexcept;

  // Returns and clears the accumulated diagnostics.
  [[nodiscard]] std::vector<std::string> take_diagnostics();

  // ---- test access ---------------------------------------------------------

  [[nodiscard]] std::optional<StepEntryCursor> step_entry_cursor()
      const noexcept;

  [[nodiscard]] std::optional<graphscore::SpelledPitch> previous_pitch()
      const noexcept;

  [[nodiscard]] int octave_offset() const noexcept;

  [[nodiscard]] bool clipboard_has_fragment() const noexcept;

  [[nodiscard]] const std::optional<graphscore::NotationFragment>& clipboard()
      const noexcept;

  [[nodiscard]] graphscore::NoteValue armed_note_value() const noexcept;

  [[nodiscard]] std::uint8_t armed_dots() const noexcept;

  [[nodiscard]] graphscore::Voice armed_voice() const noexcept;

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

  // ---- step-entry helpers --------------------------------------------------

  void initialize_step_entry_cursor();

  // Moves the step-entry cursor to the same voice and position on the prior/
  // next staff of the node (wrapping), resetting the pitch reference (§8.4).
  bool step_cursor_staff(graphscore::StaffStepDirection direction);

  [[nodiscard]] std::optional<graphscore::Rational> snap_caret(
      graphscore::NodeId node, graphscore::TrackId track,
      graphscore::StaveId stave, graphscore::Voice voice,
      graphscore::Rational position) const;

  [[nodiscard]] std::optional<graphscore::Rational> event_onset(
      graphscore::NodeId node, graphscore::TrackId track,
      graphscore::StaveId stave, graphscore::Voice voice,
      graphscore::NotationEntityId entity) const;

  [[nodiscard]] std::optional<graphscore::Clef> active_clef_at_cursor() const;

  [[nodiscard]] std::optional<graphscore::SpelledPitch>
  resolve_pitch_for_letter(graphscore::Letter letter) const;

  void reset_pitch_reference() noexcept;

  bool commit_step_entry(graphscore::NotePaletteEntryKind        kind,
                         std::optional<graphscore::SpelledPitch> pitch);

  [[nodiscard]] std::unique_ptr<graphscore::Command> make_note_entry_command_at(
      graphscore::NodeId node, graphscore::TrackId track,
      graphscore::StaveId stave, graphscore::Voice voice,
      graphscore::Rational position, graphscore::NotePaletteEntryKind kind,
      std::optional<graphscore::SpelledPitch> pitch) const;

  // Shared core of a note-entry commit at an explicit (node, track, stave,
  // voice, position): builds make_note_entry_command, runs the reversible
  // transaction, selects the inserted notehead/rest, updates the pitch
  // reference on a note, and either advances the cursor by the armed
  // duration (keyboard §8.5, advance_cursor == true) or repositions it to
  // the commit position (pointer §8.4, advance_cursor == false). `voice` is
  // the sole target voice: the cursor's voice for every cursor-init arm and
  // the preview's armed voice for a pointer commit -- never the palette's
  // armed voice, which the cursor may disagree with.
  bool commit_note_entry_at(graphscore::NodeId node, graphscore::TrackId track,
                            graphscore::StaveId stave, graphscore::Voice voice,
                            graphscore::Rational                    position,
                            graphscore::NotePaletteEntryKind        kind,
                            std::optional<graphscore::SpelledPitch> pitch,
                            bool advance_cursor);

  [[nodiscard]] std::optional<graphscore::NotationInvalidation>
  step_entry_invalidation(graphscore::NodeId node, bool voice_was_empty,
                          graphscore::Rational position) const;

  [[nodiscard]] std::optional<graphscore::Selection>
  selection_after_step_entry_commit(
      graphscore::NodeId node, graphscore::TrackId track,
      graphscore::StaveId stave, graphscore::Voice voice,
      graphscore::Rational                    position,
      std::optional<graphscore::SpelledPitch> pitch) const;

  void record_step_entry_previous_pitch(const graphscore::SpelledPitch& pitch);

  // ---- clipboard helpers ---------------------------------------------------

  [[nodiscard]] std::optional<graphscore::PasteAnchor> derive_paste_anchor()
      const;

  [[nodiscard]] std::optional<graphscore::NotationInvalidation>
  full_invalidation() const;

  // The start of the node's measure `measure_index`, or nullopt when the
  // node/track/stave is absent, the track is archived, or the index is out
  // of range -- a bounded, validated lookup used by every selection-derived
  // measure_start call so a stale full-measure selection is rejected
  // atomically rather than indexing past MeasureMap::measure_count().
  [[nodiscard]] std::optional<graphscore::Rational> measure_start_at(
      graphscore::NodeId node, graphscore::TrackId track,
      graphscore::StaveId stave, std::size_t measure_index) const;

  // Whether (node, track, stave) still names a live, active stave the node
  // engraves (the track is not archived and the lane/stave exist).
  [[nodiscard]] bool live_stave(graphscore::NodeId  node,
                                graphscore::TrackId track,
                                graphscore::StaveId stave) const;

  bool run_interval_action(graphscore::IntervalDirection direction,
                           std::uint8_t                  interval);

  // ---- palette eligibility probes (M5-phase-27 §11) -----------------------

  // These are the shared, side-effect-free probes the command-palette
  // availability switch uses. Each mirrors the exact precondition/fallback
  // its chord's execution path enforces, so availability and execution can
  // never drift (the palette never offers an action its chord would no-op).

  // The single NoteheadItem when the committed selection is a single-item
  // NoteheadSet; nullopt otherwise. Arm/cardinality only.
  [[nodiscard]] std::optional<graphscore::NoteheadItem> single_notehead_item()
      const;

  // The single ChordItem when the committed selection is a single-item
  // ChordSet; nullopt otherwise.
  [[nodiscard]] std::optional<graphscore::ChordItem> single_chord_item() const;

  // Whether the committed selection names a single item of a given arm
  // (RestSet / InsertionCaretSet), cardinality-checked.
  [[nodiscard]] bool single_rest_item() const;
  [[nodiscard]] bool single_caret_item() const;

  // Whether the committed selection is an eligible note/chord target for
  // "R" (convert-to-rest): a single-item NoteheadSet naming a Note/ChordNote
  // (never a GraceNote) or a single-item ChordSet, exactly the no-op set
  // make_convert_event_to_rest_command enforces.
  [[nodiscard]] bool convert_to_rest_available() const;

  // Whether the committed selection is an eligible staff-step source: a
  // single-item NoteheadSet/ChordSet/RestSet/InsertionCaretSet that still
  // validates against the project (stale/archived/missing rejected).
  [[nodiscard]] bool staff_step_available() const;

  // Whether the committed selection is an eligible copy/cut target whose
  // extraction would succeed: a FullMeasureSet or ArbitraryRangeSet sharing
  // one node and one measure index / identical span, passing
  // validate_selection -- the same preconditions extract_fragment enforces.
  [[nodiscard]] bool copy_cut_available() const;

  // Exact PasteFragmentCommand eligibility against a project value copy.
  [[nodiscard]] bool paste_available() const;

  // Whether a live step-entry cursor exists (the pitch/rest commit rows'
  // precondition, distinct from the palette-arming rows that need none).
  [[nodiscard]] bool step_entry_cursor_live() const;

  [[nodiscard]] bool step_entry_pitch_available(
      graphscore::Letter letter) const;
  [[nodiscard]] bool step_entry_rest_available() const;

  [[nodiscard]] bool octave_step_available(int direction) const noexcept;

  [[nodiscard]] bool staff_step_available(
      graphscore::StaffStepDirection direction) const;

  // ---- command-palette helpers ---------------------------------------------

  [[nodiscard]] std::size_t palette_filtered_size() const;

  [[nodiscard]] std::optional<PaletteCommand> palette_selected_command() const;

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

  // kNoteEntry pointer press tracking (§8.4): a press followed by a release
  // with no intervening move is the note-entry click. The drag state machine
  // is selection-only, so note entry tracks its own press here.
  bool                      note_entry_press_pending_ = false;
  graphscore::NotationPoint note_entry_press_point_;

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

  // ---- M5-phase-27 state ---------------------------------------------------

  // The app-owned step-entry cursor (§8.1), live only while kNoteEntry is
  // active.
  std::optional<StepEntryCursor> step_entry_cursor_;
  // The pitch reference (§8.3): the last committed natural spelling, and the
  // pending single-shot octave offset in [-8, +8].
  std::optional<graphscore::SpelledPitch> previous_pitch_;
  int                                     octave_offset_ = 0;
  // The undo-stack depth right after the commit that produced the current
  // previous_pitch_ (including an interval-inserted producer). Undo only
  // clears the pitch reference when the undo stack shrinks below this depth,
  // i.e. when the producer itself was undone -- never for an unrelated or
  // later command (docs/plan/05-notation-editor-action-table.md §8.3).
  std::size_t previous_pitch_undo_depth_ = 0;

  // The app-owned in-memory clipboard (§10): a single slot holding at most
  // one NotationFragment. Not part of CommandHistory; undo/redo never
  // touches it.
  std::optional<graphscore::NotationFragment> clipboard_;

  // The nonvisual command-palette model (§11).
  bool        palette_open_ = false;
  std::string palette_filter_;
  std::size_t palette_selected_index_ = 0;

  // The app-owned composer diagnostic sink (§1, §8.5, §10): appended by
  // every rejected action's fallback, observable and resettable by tests.
  std::vector<std::string> diagnostics_;
};

}  // namespace graphscore::writer_app
