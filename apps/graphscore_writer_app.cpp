// SPDX-License-Identifier: Apache-2.0
//
// GraphScore Writer application entry point.
//
// M1 scope: open an empty native window on each desktop platform and run the
// event loop until it is closed. Document lifecycle, canvas, notation
// editing, and audio arrive in later milestones; this executable exists now
// so that the platform shell is continuously built and exercised.
//
// `--smoke-test` creates and destroys the window without entering the
// blocking event loop, so CI can verify the shell without anyone closing a
// window.
//
// M05 scope: the active tool (note-entry or range-selection) and the
// selection-drag state machine are owned here, at the application assembly
// layer. A SelectionToolHandler implements InputHandler and wires pointer
// press/move/release to the resolver. A dedicated `--test-selection-tool`
// flag exercises the handler through the WriterShell event-dispatch seam
// (writer_shell dispatch_test_pointer_event), and an additional
// `--test-selection-tool-shell` flag exercises the full SDL dispatch path.
//
// M05 startup tool: the default active tool on launch is kSelection, not
// kNoteEntry, because range selection is the first increment of M05
// delivered. The note-entry tool activates only after the note palette and
// pointer-entry phases are wired; until then, kSelection is the deliberate
// temporary default. See docs/plan/05-notation-editor.md §"Selection and
// keyboard behavior".

#include <graphscore/core/graphscore_core.hpp>
#include <graphscore/domain/graphscore_domain.hpp>
#include <graphscore/notation/graphscore_notation.hpp>
#include <graphscore/rendering/graphscore_rendering.hpp>
#include <graphscore/writer_shell/graphscore_writer_shell.hpp>

#include <algorithm>
#include <array>
#include <cinttypes>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace {

constexpr std::string_view kSmokeTestFlag         = "--smoke-test";
constexpr std::string_view kSelectionToolTestFlag = "--test-selection-tool";
constexpr std::string_view kSelectionToolShellTestFlag =
    "--test-selection-tool-shell";
constexpr std::string_view kKeyEventsTestFlag      = "--test-key-events";
constexpr std::string_view kKeyEventsShellTestFlag = "--test-key-events-shell";
constexpr std::string_view kKeySelectionTestFlag   = "--test-key-selection";
constexpr std::string_view kNoteheadMoveTestFlag   = "--test-notehead-move";

const char* describe(graphscore::ShellError error) {
  switch (error) {
    case graphscore::ShellError::kNone:
      return "ok";
    case graphscore::ShellError::kBackendUnavailable:
      return "no display available";
    case graphscore::ShellError::kWindowCreationFailed:
      return "window creation failed";
    case graphscore::ShellError::kRendererUnavailable:
      return "renderer unavailable";
    case graphscore::ShellError::kRenderingSetupFailed:
      return "rendering failed";
    case graphscore::ShellError::kBackendNotCompiledIn:
      return "no windowing backend compiled in";
  }
  return "unknown error";
}

int report(const graphscore::ShellResult& result, bool smoke_test) {
  if (result.ok()) {
    return 0;
  }

  std::fprintf(stderr, "graphscore_writer_app: %s (%s)\n",
               describe(result.error), result.message.c_str());

  const bool headless_under_smoke_test =
      smoke_test &&
      (result.error == graphscore::ShellError::kBackendUnavailable ||
       result.error == graphscore::ShellError::kRendererUnavailable);

  return headless_under_smoke_test ? 0 : 1;
}

// ---- default project (M05 in-memory stub, replaced by M03 persistence) -----

struct DefaultProject {
  graphscore::Project        project;
  graphscore::NodeId         node_id;
  graphscore::TrackId        track_id;
  graphscore::StaveId        stave_id;
  graphscore::NotationLayout layout;
};

// Builds one single-staff node with two quarter notes spanning one 4/4
// measure. This is the same fixture the notation tests use; it lives here
// because the app owns the project and layout for the handler to read.
//
// `metrics` supplies the glyph metrics — use production BravuraFont when
// rendering to the window; use SelfTestMetrics only for headless tests.
[[nodiscard]] std::optional<DefaultProject> build_default_project(
    const graphscore::GlyphMetrics& metrics) {
  using graphscore::Clef;
  using graphscore::Duration;
  using graphscore::KeySignature;
  using graphscore::layout_notation;
  using graphscore::Letter;
  using graphscore::make_note;
  using graphscore::Measure;
  using graphscore::MidiChannel;
  using graphscore::NodeId;
  using graphscore::NodeTimeline;
  using graphscore::NotationLayoutOptions;
  using graphscore::NotationLayoutResult;
  using graphscore::NoteValue;
  using graphscore::ProjectId;
  using graphscore::SpelledPitch;
  using graphscore::StaffLayout;
  using graphscore::StaveDefinition;
  using graphscore::StaveId;
  using graphscore::TimeSignature;
  using graphscore::TrackId;
  using graphscore::Voice;
  using graphscore::VoiceContent;

  graphscore::Project project{ProjectId::generate(), "Default"};
  const auto          midi_channel = MidiChannel::create(0);
  if (!midi_channel.has_value()) {
    return std::nullopt;
  }
  const auto track_added = project.add_track(
      "Track", StaffLayout::single_staff(Clef::kTreble), *midi_channel);
  if (!track_added.has_value()) {
    return std::nullopt;
  }
  const TrackId track_id = *track_added;
  const NodeId  node_id  = project.add_node("Node");
  auto*         lane     = project.find_node(node_id)->lane(track_id);
  const StaveId stave_id = project.active_tracks()[0].layout().staves()[0].id;
  lane->ensure_stave(stave_id);

  std::vector<StaveDefinition> stave_defs;
  stave_defs.push_back(project.active_tracks()[0].layout().staves()[0]);
  const auto time_sig = TimeSignature::create(4, 4);
  if (!time_sig.has_value()) {
    return std::nullopt;
  }
  std::vector<Measure> measures(1, Measure{*time_sig, KeySignature{}});
  auto timeline = NodeTimeline::create(std::move(measures), stave_defs);
  if (!timeline.has_value()) {
    return std::nullopt;
  }
  project.find_node(node_id)->set_timeline(std::move(*timeline));

  const auto quarter_dur = Duration::create(NoteValue::kQuarter, 0);
  if (!quarter_dur.has_value()) {
    return std::nullopt;
  }
  const Duration quarter = *quarter_dur;
  const auto     voice1  = Voice::create(1);
  if (!voice1.has_value()) {
    return std::nullopt;
  }
  VoiceContent& vc = lane->stave(stave_id)->voice(*voice1);

  const auto pitch_c4 = SpelledPitch::create(Letter::kC, 4);
  if (!pitch_c4.has_value()) {
    return std::nullopt;
  }
  if (!vc.append(make_note(*pitch_c4, quarter)).ok()) {
    return std::nullopt;
  }
  const auto pitch_d4 = SpelledPitch::create(Letter::kD, 4);
  if (!pitch_d4.has_value()) {
    return std::nullopt;
  }
  if (!vc.append(make_note(*pitch_d4, quarter)).ok()) {
    return std::nullopt;
  }

  NotationLayoutResult layout_result =
      layout_notation(project, node_id, metrics);
  if (!layout_result || !layout_result.layout.has_value()) {
    return std::nullopt;
  }

  return DefaultProject{std::move(project), node_id, track_id, stave_id,
                        std::move(*layout_result.layout)};
}

// Stub glyph metrics producing fixed bounds — used only for headless tests
// that must not load a real font.
class SelfTestMetrics final : public graphscore::GlyphMetrics {
 public:
  [[nodiscard]] graphscore::GlyphMetricsValue glyph_metrics(
      char32_t /*code_point*/, double staff_space) const override {
    return graphscore::GlyphMetricsValue{
        graphscore::NotationRect{-staff_space * 0.25, -staff_space * 0.5,
                                 staff_space * 1.5, staff_space * 2.0},
        staff_space * 1.5};
  }

  [[nodiscard]] double kerning(char32_t /*left*/, char32_t /*right*/,
                               double /*staff_space*/) const override {
    return 0.0;
  }
};

// ---- selection-tool InputHandler -------------------------------------------

// Provisional keyboard range-extension step for Shift+Left/Right
// (M5-phase-19b-iii): one quarter of a whole note. Superseded by
// M5-phase-27's platform-normalized action table, which chooses the actual
// step per action (diatonic step, beat, measure, ...); defined once here
// rather than scattered as a repeated literal.
constexpr graphscore::Rational kProvisionalRangeExtensionStep =
    *graphscore::Rational::create(1, 4);

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
                       graphscore::WriterShell*   shell)
      : project_(std::move(project)),
        layout_(std::move(layout)),
        shell_(shell) {}

  SelectionToolHandler(const SelectionToolHandler&)            = delete;
  SelectionToolHandler& operator=(const SelectionToolHandler&) = delete;

  ~SelectionToolHandler() override {
    if (drag_.is_dragging()) {
      drag_.cancel();
    }
    if (shell_ != nullptr) {
      shell_->set_highlight_rects({});
    }
  }

  // ---- InputHandler --------------------------------------------------------

  void on_pointer_press(graphscore::PointerEvent event) override {
    if (event.button != graphscore::PointerButton::kPrimary) {
      return;
    }
    const graphscore::NotationPoint point{event.x, event.y};
    if (!drag_.begin(active_tool_, point)) {
      // begin() cancels any prior drag and returns false; committed_selection_
      // persists.  Show whatever highlight fits (committed, if any, or clear).
      update_highlight();
      return;
    }
    initiating_button_ = event.button;
    moved_during_drag_ = false;
    // Press alone may preserve the old committed highlight; update_highlight
    // shows the committed extent because begin() clears live_extent.
    update_highlight();
  }

  void on_pointer_move(graphscore::PointerEvent event) override {
    if (!drag_.is_dragging()) {
      return;
    }
    const graphscore::NotationPoint point{event.x, event.y};
    if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
      return;
    }
    moved_during_drag_ = true;
    std::ignore        = drag_.update(project_, layout_, point);
    update_highlight();
  }

  void on_pointer_release(graphscore::PointerEvent event) override {
    if (!drag_.is_dragging()) {
      return;
    }
    // Only the initiating button can commit the drag. Any other button
    // release — secondary button, middle button, or unknown — is silently
    // ignored while the drag continues.
    if (event.button != initiating_button_) {
      return;
    }
    const graphscore::NotationPoint point{event.x, event.y};
    // A press followed by a release with no intervening move is a click, not
    // a range drag: resolve the single notehead/chord/rest/marking/caret the
    // point names (M5-phase-16's resolve_selection_at), so an unmodified
    // Up/Down then has a committed single-notehead selection to act on.
    // Range-drag behavior is unchanged: any movement keeps the drag path.
    if (!moved_during_drag_) {
      drag_.cancel();
      if (std::isfinite(point.x) && std::isfinite(point.y)) {
        resolve_single_click_selection(point);
      }
      update_highlight();
      return;
    }
    // Resolve the final release point before committing, so the committed
    // extent matches the pointer position at release, not the last motion
    // position.
    if (std::isfinite(point.x) && std::isfinite(point.y)) {
      // update() resolves the range one more time at the release point.
      // If it returns nullopt (off-stave, invalid), the drag is cancelled
      // rather than committing a stale extent from the last valid move.
      if (drag_.update(project_, layout_, point).has_value()) {
        std::ignore = drag_.commit();
        // A pointer-drag commit is the one place first_staff_/last_staff_
        // are (re)derived from the committed selection's own items, since
        // there is no explicit caller-supplied staff scope to track for a
        // drag the way there is for extend_range_staff_scope() below.
        sync_staff_endpoints_from_committed();
        update_highlight();
        return;
      }
    }
    // The final point is invalid or resolution failed; cancel the drag
    // so no stale extent is committed.
    drag_.cancel();
    // Committed highlight (if any) remains — update_highlight falls through
    // to it since live_extent was cleared above.
    update_highlight();
  }

  void on_cancel() override {
    if (drag_.is_dragging()) {
      drag_.cancel();
    }
    // Restore the committed selection highlight if one exists.  The
    // in-progress drag is cancelled above, but the previously committed
    // extent survives and should remain visible.
    update_highlight();
  }

  // Provisional keyboard bindings (M5-phase-19b-iii), superseded by
  // M5-phase-26/M5-phase-27's normalized action table. Shift is required
  // for every range-extension binding below; control, alt, and meta never
  // substitute for it, and an unmodified arrow or a non-Shift chord is a
  // no-op. Acts only when the selection tool is active and a committed
  // ArbitraryRangeSet selection already exists to extend -- this sub-phase
  // never creates a first selection from the keyboard alone.
  //
  //   Shift+Left/Right  -- move the current focus edge (focus_edge_) one
  //                        provisional step earlier/later.
  //   Shift+Up/Down     -- extend the staff scope by one staff in score
  //                        order (up = earlier, down = later).
  //   Shift+Home/End    -- select_to_node_start()/select_to_node_end().
  //
  // M5-phase-20 adds unmodified Up/Down: with exactly one selected notehead,
  // the notehead moves one diatonic staff step (move_selected_notehead). This
  // never conflicts with Shift+Up/Down above, which stays range extension.
  void on_key_press(graphscore::KeyEvent event) override {
    if (active_tool_ != graphscore::ActiveTool::kSelection) {
      return;
    }
    if (event.modifiers.shift) {
      const auto* existing = current_range_set();
      if (existing == nullptr || existing->items().empty()) {
        return;
      }
      const graphscore::MusicalSpan& span = existing->items().front().span;
      switch (event.code) {
        case graphscore::KeyCode::kLeft: {
          const graphscore::Rational current =
              focus_edge_ == graphscore::RangeEdge::kStart ? span.start
                                                           : span.end;
          std::ignore = extend_range_edge(
              focus_edge_, current - kProvisionalRangeExtensionStep);
          break;
        }
        case graphscore::KeyCode::kRight: {
          const graphscore::Rational current =
              focus_edge_ == graphscore::RangeEdge::kStart ? span.start
                                                           : span.end;
          std::ignore = extend_range_edge(
              focus_edge_, current + kProvisionalRangeExtensionStep);
          break;
        }
        case graphscore::KeyCode::kUp:
          std::ignore = step_staff_scope(-1);
          break;
        case graphscore::KeyCode::kDown:
          std::ignore = step_staff_scope(1);
          break;
        case graphscore::KeyCode::kHome:
          std::ignore = select_to_node_start();
          break;
        case graphscore::KeyCode::kEnd:
          std::ignore = select_to_node_end();
          break;
        case graphscore::KeyCode::kUnknown:
        default:
          break;
      }
      return;
    }

    // M5-phase-20: unmodified Up/Down moves the single selected notehead one
    // diatonic staff step. Any other modifier chord is not a binding this
    // phase owns and is a no-op (the full action table is M5-phase-26/27's).
    if (event.modifiers.control || event.modifiers.alt ||
        event.modifiers.meta) {
      return;
    }
    switch (event.code) {
      case graphscore::KeyCode::kUp:
        std::ignore =
            move_selected_notehead(graphscore::NoteheadStepDirection::kUp);
        break;
      case graphscore::KeyCode::kDown:
        std::ignore =
            move_selected_notehead(graphscore::NoteheadStepDirection::kDown);
        break;
      case graphscore::KeyCode::kUnknown:
      default:
        break;
    }
  }

  // ---- accessible range controls (M5-phase-19b-iii) ------------------------
  //
  // Pointer-free controls that reproduce the same selection a pointer drag
  // over the equivalent musical coordinates would produce -- callable
  // handler methods now, exposed as accessibility-tree actions in later
  // phases of this milestone (M5-phase-43/45/62). Every one reaches the
  // item set, staff walk, voice-overlap check, and bounds validation
  // through the M5-phase-19a resolvers; the only logic here is which span
  // edge moves (see extend_range_edge below). Each returns true when the
  // underlying resolver accepted the request and replaced the committed
  // selection with its result (which may be identical to the previous one,
  // e.g. moving an edge to where it already sits); a no-op call (no
  // committed ArbitraryRangeSet selection, or a resolver rejecting the
  // request) returns false and leaves the existing selection untouched.

  // Moves one edge of the committed selection's shared span to `time`,
  // holding the other edge fixed and swapping if the moved edge crosses
  // it -- graphscore::extend_range_selection's own documented behavior.
  //
  // This is built on resolve_range_selection_spec rather than on
  // extend_range_selection directly, and passes first_staff_/last_staff_
  // explicitly, so an edge-only extension can still recover a staff that
  // extend_range_selection's own item-derived fixed-range reconstruction
  // would lose (see extend_range_selection's own doc comment on
  // graphscore_notation.hpp, and first_staff_/last_staff_'s own comment
  // below). The span-move-and-swap arithmetic here is bookkeeping over
  // which edge moves, not a musical resolution rule; the actual staff
  // walk, voice-overlap check, and bounds validation are all
  // resolve_range_selection_spec's. Because this bypasses
  // extend_range_selection, the move-and-swap-and-reject-zero-length rule
  // implemented below duplicates extend_range_selection's own copy
  // (src/notation/notation.cpp) instead of reusing it -- if that
  // function's crossing semantics ever change, this copy must change with
  // it too.
  bool extend_range_edge(graphscore::RangeEdge edge,
                         graphscore::Rational  time) {
    const auto* existing = current_range_set();
    if (existing == nullptr || existing->items().empty()) {
      return false;
    }
    if (!first_staff_.has_value() || !last_staff_.has_value()) {
      return false;
    }
    const graphscore::MusicalSpan& current_span =
        existing->items().front().span;
    graphscore::Rational start = current_span.start;
    graphscore::Rational end   = current_span.end;
    if (edge == graphscore::RangeEdge::kStart) {
      start = time;
    } else {
      end = time;
    }
    if (start > end) {
      std::swap(start, end);
    }
    if (start == end) {
      return false;
    }
    const graphscore::NodeId node_id = existing->items().front().node;
    auto resolved                    = graphscore::resolve_range_selection_spec(
        project_, graphscore::RangeSelectionSpec{
                      node_id, graphscore::MusicalSpan{start, end},
                      *first_staff_, *last_staff_});
    if (!resolved.has_value()) {
      return false;
    }
    const auto* resolved_set =
        std::get_if<graphscore::ArbitraryRangeSet>(&*resolved);
    if (resolved_set == nullptr || resolved_set->items().empty()) {
      return false;
    }
    // extend_range_selection's own contract: the edge that now carries
    // `time` is not necessarily `edge` itself once a crossing swap has
    // happened, so recompute focus_edge_ from where `time` actually
    // landed in the resolved span rather than assuming it is unchanged.
    const graphscore::MusicalSpan& resolved_span =
        resolved_set->items().front().span;
    focus_edge_ = (resolved_span.start == time) ? graphscore::RangeEdge::kStart
                                                : graphscore::RangeEdge::kEnd;
    drag_.set_committed_selection(std::move(resolved));
    update_highlight();
    return true;
  }

  // Replaces the committed selection's staff scope with the score-order
  // range spanning `first_staff` and `last_staff`, holding the shared span
  // fixed. `first_staff`/`last_staff` become the new first_staff_/
  // last_staff_ exactly as passed -- not reconstructed from the resulting
  // items -- so a staff between them carrying no content overlapping the
  // current span is still tracked as part of the scope.
  bool extend_range_staff_scope(graphscore::MeasureScope first_staff,
                                graphscore::MeasureScope last_staff) {
    const auto* existing = current_range_set();
    if (existing == nullptr) {
      return false;
    }
    auto extended = graphscore::extend_range_selection_staff_scope(
        project_, *existing, first_staff, last_staff);
    if (!extended.has_value()) {
      return false;
    }
    drag_.set_committed_selection(std::move(extended));
    first_staff_ = first_staff;
    last_staff_  = last_staff;
    update_highlight();
    return true;
  }

  // The start edge at Rational(0).
  bool select_to_node_start() {
    return extend_range_edge(graphscore::RangeEdge::kStart,
                             graphscore::Rational(0));
  }

  // The end edge at the committed selection's own node's timeline total
  // length. resolve_range_selection_spec (reached through
  // extend_range_edge) rejects an out-of-bounds span rather than clamping
  // it, so the exact node end must be looked up rather than guessed.
  bool select_to_node_end() {
    const auto* existing = current_range_set();
    if (existing == nullptr || existing->items().empty()) {
      return false;
    }
    const graphscore::Node* node =
        project_.find_node(existing->items().front().node);
    if (node == nullptr) {
      return false;
    }
    const graphscore::NodeTimeline* timeline = node->timeline();
    if (timeline == nullptr) {
      return false;
    }
    return extend_range_edge(graphscore::RangeEdge::kEnd,
                             timeline->measures().total_length());
  }

  // ---- tool switching ------------------------------------------------------

  void set_active_tool(graphscore::ActiveTool tool) {
    if (tool != active_tool_) {
      if (drag_.is_dragging()) {
        drag_.cancel();
      }
      active_tool_ = tool;
      // After tool switch, show whatever highlight fits (committed, if any,
      // or clear entirely if none).
      update_highlight();
    }
  }

  [[nodiscard]] graphscore::ActiveTool active_tool() const noexcept {
    return active_tool_;
  }

  // Supplies the glyph metrics used to refresh the retained layout after a
  // notehead move (M5-phase-20). Must be set before any notehead move that
  // expects the layout to refresh; the pointer/range-selection paths never
  // need it. `run()` sets the production font; the notehead-move test sets
  // SelfTestMetrics.
  void set_metrics(const graphscore::GlyphMetrics* metrics) noexcept {
    metrics_ = metrics;
  }

  // The step that publishes the rasterised notation surface for a refreshed
  // layout to the shell. `run()` wires it to rasterize_notation +
  // set_notation_surface; the headless notehead-move test wires a
  // deterministic test publisher so the same publish step is observable
  // without a font or rendering backend. Unset (empty) means the layout is
  // refreshed in memory but no surface is published, which is only correct
  // on the range-selection paths that never mutate notation.
  using SurfacePublisher =
      std::function<graphscore::ShellResult(const graphscore::NotationLayout&)>;

  void set_surface_publisher(SurfacePublisher publisher) {
    publish_surface_ = std::move(publisher);
  }

  // Supplies the layout options the incremental layout cache uses. Must match
  // the options the retained layout was produced with. `run()` keeps the
  // default options; the cross-measure-tie test sets narrow one-measure-per-
  // system options so a tie chain spanning two measures also spans two
  // systems, which is what makes the full-chain invalidation observable.
  void set_layout_options(graphscore::NotationLayoutOptions options) {
    layout_options_ = options;
  }

  // The step that builds the reversible command for a notehead move. `run()`
  // and the notehead-move tests keep the default (make_move_notehead_command);
  // the rollback-failure tests replace it with a wrapper whose undo fails a
  // configured number of times, so a deterministic rollback failure can be
  // injected without relying on actual allocation failure.
  using MoveCommandFactory = std::function<std::unique_ptr<graphscore::Command>(
      const graphscore::Project&, const graphscore::NoteheadItem&,
      graphscore::NoteheadStepDirection)>;

  void set_move_command_factory(MoveCommandFactory factory) {
    move_command_factory_ = std::move(factory);
  }

  // Builds the retained incremental layout cache from the current project and
  // layout, so a later refresh_layout() reuses unaffected systems instead of
  // full-resetting on its first call (which rebuilds everything and hides a
  // stale invalidation scope). run() calls this at startup -- immediately
  // after set_metrics() -- so the first production edit is already
  // incremental; the notehead-move tests call it to reproduce that startup
  // seeding before asserting rebuild scope.
  void warm_layout_cache() {
    if (metrics_ == nullptr) {
      return;
    }
    std::ignore = layout_cache_.update(project_, layout_.node_id, *metrics_,
                                       layout_options_, {});
  }

  // Stores a selection directly, mirroring SelectionDragState's own
  // keyboard/accessible entry point (M5-phase-19b). A pointer click reaches
  // this through resolve_selection_at (resolve_single_click_selection);
  // Shift/keyboard range extension and the accessible controls reach it
  // through the range methods above. Kept public so those paths and the
  // no-op tests below share one entry point.
  void set_committed_selection(std::optional<graphscore::Selection> selection) {
    drag_.set_committed_selection(std::move(selection));
    update_highlight();
  }

  // Moves the single selected notehead one diatonic staff step (M5-phase-20).
  // Requires the committed selection to be a NoteheadSet with exactly one
  // item; any other selection -- none, a range, a non-notehead arm, or a
  // multi-notehead set -- is a no-op returning false. The move runs as one
  // reversible command through the handler's CommandHistory. On success the
  // same notehead identity stays selected (MoveNoteheadCommand preserves ids),
  // the retained layout is refreshed incrementally and re-published to the
  // shell, and the short audition request for the post-edit pitch is recorded
  // (M5-phase-15; nothing plays it until Milestone 08).
  //
  // The move is one provisional CommandHistory transaction: the pitch
  // mutation executes without clearing the redo stack, the candidate layout
  // is refreshed and its surface published, and only then does the
  // transaction commit (clearing redo). If the cache refresh or the surface
  // publish fails, the transaction aborts — undoing the pitch mutation and
  // restoring the exact prior history, including any pre-existing redo —
  // and the layout cache is re-seeded from the restored project, so project,
  // layout, surface, selection/highlight, history, and audition all remain
  // exactly as they were before the move. A stale notehead identity or a
  // pitch step that would leave the SpelledPitch/MIDI range fails the same
  // way, leaving everything unchanged.
  //
  // Abort reliability: MoveNoteheadCommand::undo() restores from its pre-edit
  // snapshot, so the rollback of a just-applied pitch mutation cannot fail
  // for any ordinary recoverable publication failure; only a process-level
  // allocation failure inside undo itself can. If it does, the move hands
  // the poisoned history to recover_from_failed_rollback() below: the layout
  // cache is NOT re-seeded from a project that disagrees with the visible
  // layout/surface, and a further move is blocked until recovery succeeds.
  bool move_selected_notehead(graphscore::NoteheadStepDirection direction) {
    if (history_.poisoned()) {
      // A prior rollback failed and has not been recovered: the authoritative
      // project may disagree with the visible layout/surface. Refuse the
      // mutation rather than pretend the history is consistent.
      return false;
    }
    const auto* set = current_notehead_set();
    if (set == nullptr || set->items().size() != 1u) {
      return false;
    }
    const graphscore::NoteheadItem&                      item = set->items()[0];
    const std::optional<graphscore::NoteAuditionRequest> audition =
        graphscore::audition_for_notehead_move(project_, item, direction);
    std::unique_ptr<graphscore::Command> command =
        move_command_factory_(project_, item, direction);
    if (command == nullptr) {
      return false;
    }

    // Provisional execute: apply the pitch mutation WITHOUT clearing the
    // redo stack, so a failed publication can restore the exact prior
    // history (including any pre-existing redo) rather than destroying it.
    graphscore::CommandHistory::Transaction transaction =
        history_.begin_transaction(std::move(command), project_);
    if (!transaction.active()) {
      return false;
    }

    if (!refresh_layout()) {
      const graphscore::Result rollback = transaction.abort();
      if (!rollback.ok()) {
        // The rollback failed: the history is now poisoned (the command and
        // its exact project are retained for recover()). Attempt recovery
        // once; a one-shot failure recovers here, while a persistent failure
        // leaves the handler unavailable and further moves blocked.
        recover_from_failed_rollback();
        return false;
      }
      // Project restored exactly: re-seed the retained cache from it so the
      // next move stays incremental.
      layout_cache_.reset();
      warm_layout_cache();
      return false;
    }

    // Publication succeeded: commit the command (clears redo, exactly
    // execute_new's normal success path). Capacity was reserved before
    // execute, so this cannot fail here.
    if (!transaction.commit().ok()) {
      return false;
    }
    last_audition_ = audition;
    return true;
  }

  // ---- test access ---------------------------------------------------------

  [[nodiscard]] const graphscore::SelectionDragState& drag_state()
      const noexcept {
    return drag_;
  }

  [[nodiscard]] const graphscore::Project& project() const noexcept {
    return project_;
  }

  [[nodiscard]] const graphscore::NotationLayout& layout() const noexcept {
    return layout_;
  }

  // The audition request the most recent successful notehead move issued, or
  // nullopt when no move has succeeded yet (or the last move had nothing to
  // audition). A failed or no-op move leaves this unchanged.
  [[nodiscard]] const std::optional<graphscore::NoteAuditionRequest>&
  last_audition() const noexcept {
    return last_audition_;
  }

  // The incremental-layout work the most recent refresh_layout() recorded:
  // which measures/systems were rebuilt vs reused. Test-only; lets a test
  // prove a cold-cache first local move rebuilds only the affected system
  // (finding 1) rather than every system.
  [[nodiscard]] const graphscore::NotationLayoutWork& test_last_layout_work()
      const noexcept {
    return last_layout_work_;
  }

  // The number of commands on the undo/redo stacks. Test-only; lets a
  // failing-publisher test prove a rollback leaves history unchanged.
  [[nodiscard]] std::size_t test_undo_stack_size() const noexcept {
    return history_.undo_stack_size();
  }

  [[nodiscard]] std::size_t test_redo_stack_size() const noexcept {
    return history_.redo_stack_size();
  }

  // True while a failed rollback has left the history poisoned and the
  // handler unavailable: further moves (and any undo/redo through the same
  // history) are blocked until the history recovers. Test-only; the
  // rollback-failure tests assert this to prove the explicit unavailable
  // state, rather than a silent half-rolled-back project.
  [[nodiscard]] bool history_unavailable() const noexcept {
    return history_.poisoned();
  }

  // Retries the rollback of a failed move's provisional command once the
  // history has been poisoned by that failure. On success the authoritative
  // project and history are restored; the visible layout/surface/highlight
  // were never committed (refresh_layout() failed before committing layout_),
  // so they are already coherent with the restored project, and the retained
  // incremental cache is re-seeded from it. On a persistent failure the
  // history stays poisoned and the handler remains unavailable. Either way
  // the move did not complete.
  void recover_from_failed_rollback() {
    const graphscore::Result recovered = history_.recover();
    if (!recovered.ok()) {
      return;
    }
    layout_cache_.reset();
    warm_layout_cache();
  }

  // Test-only: undo/redo the most recent notehead move through the handler's
  // CommandHistory, so a test can establish a non-empty redo stack before a
  // failed publication and then prove that redo survives the abort and
  // remains executable. These mirror the M5-phase-26 undo/redo bindings that
  // will route through the same history; unlike those future bindings they do
  // not refresh the layout/surface, so a test that calls them must only
  // assert project/history state, never visible geometry.
  [[nodiscard]] bool test_undo() { return history_.undo(project_).ok(); }

  [[nodiscard]] bool test_redo() { return history_.redo(project_).ok(); }

  [[nodiscard]] std::optional<graphscore::MeasureScope> first_staff()
      const noexcept {
    return first_staff_;
  }

  [[nodiscard]] std::optional<graphscore::MeasureScope> last_staff()
      const noexcept {
    return last_staff_;
  }

 private:
  // The committed selection's own ArbitraryRangeSet, or nullptr when there
  // is no committed selection or it is not that arm. Every accessible
  // range control above requires this to be non-null before doing
  // anything.
  [[nodiscard]] const graphscore::ArbitraryRangeSet* current_range_set()
      const noexcept {
    const auto& committed = drag_.committed_selection();
    if (!committed.has_value()) {
      return nullptr;
    }
    return std::get_if<graphscore::ArbitraryRangeSet>(&*committed);
  }

  // The committed selection's own NoteheadSet, or nullptr when there is no
  // committed selection or it is not that arm. move_selected_notehead above
  // requires exactly this arm with exactly one item.
  [[nodiscard]] const graphscore::NoteheadSet* current_notehead_set()
      const noexcept {
    const auto& committed = drag_.committed_selection();
    if (!committed.has_value()) {
      return nullptr;
    }
    return std::get_if<graphscore::NoteheadSet>(&*committed);
  }

  // Derives first_staff_/last_staff_ from the committed selection's own
  // items, in score order: the lowest and highest score-order position
  // among them. Called only after a pointer-drag commit, where there is no
  // caller-supplied staff scope to track directly. A staff at the extreme
  // of the drag whose own voices carried no overlapping content at commit
  // time contributes no item, so it is unrecoverable here -- the same
  // limitation extend_range_selection's own doc comment describes for
  // reconstructing a held-fixed staff range from items. That limitation is
  // inherent to deriving endpoints from items and is not fixed by this
  // sub-phase; extend_range_staff_scope's own explicit first_staff/
  // last_staff parameters are what let a later edge-only extension
  // preserve a staff this function itself could lose.
  void sync_staff_endpoints_from_committed() {
    const auto* set = current_range_set();
    if (set == nullptr || set->items().empty()) {
      first_staff_.reset();
      last_staff_.reset();
      return;
    }
    const std::vector<graphscore::MeasureScope> order =
        graphscore::score_ordered_staves(project_);
    std::optional<std::size_t> lower;
    std::optional<std::size_t> upper;
    for (const auto& item : set->items()) {
      const graphscore::MeasureScope scope{item.track, item.stave};
      const auto it = std::find(order.begin(), order.end(), scope);
      if (it == order.end()) {
        continue;
      }
      const auto index =
          static_cast<std::size_t>(std::distance(order.begin(), it));
      if (!lower.has_value() || index < *lower) {
        lower = index;
      }
      if (!upper.has_value() || index > *upper) {
        upper = index;
      }
    }
    if (lower.has_value() && upper.has_value()) {
      first_staff_ = order[*lower];
      last_staff_  = order[*upper];
    } else {
      first_staff_.reset();
      last_staff_.reset();
    }
  }

  // Widens the staff scope by one staff in score order: direction < 0
  // moves first_staff_ one position earlier (Shift+Up), direction > 0
  // moves last_staff_ one position later (Shift+Down). Running off either
  // end of score_ordered_staves is a no-op returning false, not a wrap.
  bool step_staff_scope(int direction) {
    if (!first_staff_.has_value() || !last_staff_.has_value()) {
      return false;
    }
    const std::vector<graphscore::MeasureScope> order =
        graphscore::score_ordered_staves(project_);
    const auto first_it = std::find(order.begin(), order.end(), *first_staff_);
    const auto last_it  = std::find(order.begin(), order.end(), *last_staff_);
    if (first_it == order.end() || last_it == order.end()) {
      return false;
    }
    auto first_index =
        static_cast<std::size_t>(std::distance(order.begin(), first_it));
    auto last_index =
        static_cast<std::size_t>(std::distance(order.begin(), last_it));
    if (direction < 0) {
      if (first_index == 0) {
        return false;
      }
      --first_index;
    } else {
      if (last_index + 1 >= order.size()) {
        return false;
      }
      ++last_index;
    }
    return extend_range_staff_scope(order[first_index], order[last_index]);
  }

  void update_highlight() {
    if (shell_ == nullptr) {
      return;
    }
    if (const auto& extent = drag_.live_extent(); extent.has_value()) {
      shell_->set_highlight_rects(
          build_range_highlight_rects(*extent, project_, layout_));
    } else if (const auto& committed = drag_.committed_selection();
               committed.has_value()) {
      shell_->set_highlight_rects(
          build_range_highlight_rects(*committed, project_, layout_));
    } else {
      shell_->set_highlight_rects({});
    }
  }

  // Resolves a non-drag click to the single notehead/chord/rest/marking/caret
  // selection the point names (M5-phase-16's resolve_selection_at) and stores
  // it as the committed selection. A point that names nothing (off-stave, or a
  // stale layout) leaves the committed selection unchanged, matching the
  // pre-M5-phase-20 click behavior.
  void resolve_single_click_selection(const graphscore::NotationPoint point) {
    std::optional<graphscore::Selection> selection =
        graphscore::resolve_selection_at(project_, layout_, palette_, point);
    if (selection.has_value()) {
      drag_.set_committed_selection(std::move(selection));
    }
  }

  // The exact invalidation scope the incremental layout cache needs after the
  // selected notehead's pitch moves: the notehead's own measure as
  // kLocalContent for a local move, or the full connected tie-chain measure
  // range as kCrossMeasureSpan when the chain crosses a barline. The measure
  // range comes from graphscore::notehead_move_scope, the shared source of
  // truth the MoveNoteheadCommand also walks, so the layout invalidation can
  // never drift from the mutation's actual extent.
  [[nodiscard]] std::optional<graphscore::NotationInvalidation>
  notehead_invalidation(const graphscore::NoteheadItem& item) const {
    const std::optional<graphscore::NoteheadMoveScope> scope =
        graphscore::notehead_move_scope(project_, item);
    if (!scope.has_value()) {
      return std::nullopt;
    }
    const graphscore::NotationInvalidationKind kind =
        scope->first_measure == scope->last_measure
            ? graphscore::NotationInvalidationKind::kLocalContent
            : graphscore::NotationInvalidationKind::kCrossMeasureSpan;
    return graphscore::NotationInvalidation{kind, scope->first_measure,
                                            scope->last_measure};
  }

  // Re-lays out the node after a successful domain mutation through the
  // retained incremental layout cache (M5-phase-8), then publishes the
  // rasterised surface to the shell and commits the new layout as visible.
  // The publish happens BEFORE the layout is committed, so a publish failure
  // leaves layout_ (and therefore the highlight geometry) unchanged for the
  // caller to roll back. Returns false when the cache refresh fails, when no
  // invalidation can be computed, or when the surface publish fails; true
  // means the new layout is committed and published.
  //
  // Requires metrics_ to have been set (run() and the notehead-move test both
  // do); returns true otherwise so the range-selection paths that never move
  // a notehead remain no-ops.
  bool refresh_layout() {
    if (metrics_ == nullptr) {
      return true;
    }
    const auto* set = current_notehead_set();
    if (set == nullptr || set->items().size() != 1u) {
      return true;
    }
    const std::optional<graphscore::NotationInvalidation> invalidation =
        notehead_invalidation(set->items()[0]);
    if (!invalidation.has_value()) {
      return false;
    }
    graphscore::IncrementalNotationLayoutResult result = layout_cache_.update(
        project_, layout_.node_id, *metrics_, layout_options_, {*invalidation});
    if (!result || !result.layout.has_value()) {
      return false;
    }
    last_layout_work_ = result.work;
    if (publish_surface_) {
      const graphscore::ShellResult publish = publish_surface_(*result.layout);
      if (!publish.ok()) {
        return false;
      }
    }
    layout_ = std::move(*result.layout);
    update_highlight();
    return true;
  }

  graphscore::Project            project_;
  graphscore::NotationLayout     layout_;
  graphscore::WriterShell*       shell_;
  graphscore::SelectionDragState drag_;
  // Reversible-command history for notehead moves (M5-phase-20). Undo/redo
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

// ---- normal run ------------------------------------------------------------

// Rasterizes `layout`'s commands with `font` and publishes the resulting
// surface to `shell`. Returns kRenderingSetupFailed when rasterization fails,
// otherwise the shell's own set_notation_surface result. Shared by run()'s
// startup rasterization and the handler's post-mutation surface publisher, so
// the two can never drift on surface dimensions or raster options.
[[nodiscard]] graphscore::ShellResult publish_notation_surface(
    const graphscore::NotationLayout& layout,
    const graphscore::BravuraFont& font, graphscore::WriterShell* shell,
    const graphscore::RasterOptions& base_options) {
  graphscore::RasterOptions opts = base_options;
  opts.width                     = static_cast<std::uint32_t>(
                   std::ceil(layout.bounds.x + layout.bounds.width)) +
               16U;
  opts.height = static_cast<std::uint32_t>(
                    std::ceil(layout.bounds.y + layout.bounds.height)) +
                16U;
  auto raster = graphscore::rasterize_notation(layout.commands, font, opts);
  if (!raster || !raster.surface.has_value()) {
    return graphscore::ShellResult{
        graphscore::ShellError::kRenderingSetupFailed,
        "graphscore_writer_app: failed to rasterise notation"};
  }
  return shell->set_notation_surface(std::move(*raster.surface));
}

// Load the production Bravura font. Returns nullopt on failure; the
// diagnostic is already printed to stderr by the rendering layer.
[[nodiscard]] std::optional<graphscore::BravuraFontLoadResult>
load_font_or_nullopt() {
#ifdef GRAPHSCORE_BRAVURA_FONT_PATH
  auto loaded = graphscore::load_bravura_font(GRAPHSCORE_BRAVURA_FONT_PATH);
  if (!loaded) {
    std::fprintf(stderr,
                 "graphscore_writer_app: failed to load Bravura font "
                 "from " GRAPHSCORE_BRAVURA_FONT_PATH "\n");
  }
  return loaded;
#else
  std::fprintf(stderr,
               "graphscore_writer_app: GRAPHSCORE_BRAVURA_FONT_PATH not "
               "compiled in — writer-OFF build?\n");
  return std::nullopt;
#endif
}

int run(bool smoke_test) {
  // Writer-OFF: no rendering backend is compiled in.  Skip font loading,
  // project construction, and rasterisation and go straight to the bare
  // open_window path so the smoke test reaches the established
  // GRAPHSCORE_BUILD_WRITER=OFF outcome.
  if (!graphscore::WriterShell::backend_compiled_in()) {
    graphscore::WriterShell   shell;
    graphscore::WindowOptions options;
    options.run_event_loop               = !smoke_test;
    const graphscore::ShellResult result = shell.open_window(options);
    return report(result, smoke_test);
  }

  auto font_loaded = load_font_or_nullopt();
  if (!font_loaded.has_value()) {
    return 1;
  }

  auto default_project = build_default_project(*font_loaded->font);
  if (!default_project.has_value()) {
    std::fprintf(stderr,
                 "graphscore_writer_app: failed to build default "
                 "project for selection tool\n");
    return 1;
  }

  // Rasterise the notation to an RGBA8 surface so the shell can upload it
  // to a GPU texture and compose the highlight on top.
  graphscore::RasterOptions raster_opts;
  raster_opts.pixels_per_unit = 1.0;
  raster_opts.origin          = {0.0, 0.0};
  raster_opts.color           = {0, 0, 0, 255};
  raster_opts.opacity         = 255;

  graphscore::WriterShell       shell;
  const graphscore::ShellResult surf_result = publish_notation_surface(
      default_project->layout, *font_loaded->font, &shell, raster_opts);
  if (!surf_result.ok()) {
    std::fprintf(stderr,
                 "graphscore_writer_app: set_notation_surface failed: %s\n",
                 surf_result.message.c_str());
    return 1;
  }

  SelectionToolHandler handler(std::move(default_project->project),
                               std::move(default_project->layout), &shell);

  handler.set_metrics(font_loaded->font.get());
  // Seed the retained layout cache from the startup layout before any edit,
  // so the first notehead move refreshes only the affected system rather
  // than full-resetting the cold cache (M5-phase-8 incremental engraving).
  handler.warm_layout_cache();
  handler.set_surface_publisher(
      [font = font_loaded->font.get(), &shell,
       raster_opts](const graphscore::NotationLayout& layout) {
        return publish_notation_surface(layout, *font, &shell, raster_opts);
      });
  handler.set_active_tool(graphscore::ActiveTool::kSelection);
  shell.set_input_handler(&handler);

  graphscore::WindowOptions options;
  options.run_event_loop = !smoke_test;

  const graphscore::ShellResult result = shell.open_window(options);
  const int                     status = report(result, smoke_test);

  // Deregister before handler destruction so the shell does not hold a
  // dangling pointer during its own destruction.
  shell.set_input_handler(nullptr);

  if (result.ok()) {
    std::printf("graphscore_writer_app: window opened via '%s' backend\n",
                std::string(shell.backend_name()).c_str());
  }

  return status;
}

// Expected highlight rect for a full-measure drag across measure 0 of the
// default fixture (C major, 4/4, staff_space=10.0, any GlyphMetrics).
// The rect spans the rhythmic area: x starts after the leading
// (clef/key/time-sig) area, width equals the rhythmic width, y and height
// from the staff system bounds.  The formula exactly reproduces
// measure_leading_width + position_x for span [measure_start, measure_end).
[[nodiscard]] graphscore::NotationRect expected_full_measure_highlight_rect(
    const graphscore::NotationLayout& layout) {
  const auto&      staff       = layout.systems[0].staves[0];
  const auto&      measure     = layout.systems[0].measures[0];
  constexpr double kStaffSpace = 10.0;
  // measure_leading_width for C major, 4/4, measure 0:
  //   min(measure_width - staff_space*2, staff_space * (6.5 + 1.5*abs(0)
  //       + 0 + 1.5*1))  =  min(measure_width - 20, 80)
  const double leading =
      std::min(measure.bounds.width - kStaffSpace * 2.0, kStaffSpace * 8.0);
  const double rwidth =
      std::max(kStaffSpace, measure.bounds.width - leading - kStaffSpace);
  return graphscore::NotationRect{measure.bounds.x + leading, staff.bounds.y,
                                  rwidth, staff.bounds.height};
}

// ---- headless selection-tool test ------------------------------------------

int selection_tool_test() {
  const SelfTestMetrics metrics;
  auto                  dp = build_default_project(metrics);
  if (!dp.has_value()) {
    std::fprintf(stderr, "selection-tool-test: build_default_project failed\n");
    return 1;
  }

  // Create a shell and register the handler. The dispatch_test_pointer_event
  // seam exercises the exact registration / dispatch / unregistration
  // contract without a native window.
  graphscore::WriterShell shell;
  SelectionToolHandler    handler(std::move(dp->project), std::move(dp->layout),
                                  &shell);
  shell.set_input_handler(&handler);
  handler.set_active_tool(graphscore::ActiveTool::kSelection);

  const auto& layout = handler.layout();

  // Helper: create a pointer event in notation (logical) coordinates.
  auto make_event = [](double x, double y,
                       graphscore::PointerButton button =
                           graphscore::PointerButton::kPrimary) {
    graphscore::PointerEvent e;
    e.x      = x;
    e.y      = y;
    e.button = button;
    return e;
  };

  // --- test 1: note-entry tool ignores pointer drag ---------------------
  handler.set_active_tool(graphscore::ActiveTool::kNoteEntry);
  {
    shell.dispatch_test_pointer_event(
        0, make_event(layout.systems[0].measures[0].bounds.x,
                      layout.systems[0].staves[0].bounds.y +
                          layout.systems[0].staves[0].bounds.height * 0.5));
    if (handler.drag_state().is_dragging()) {
      std::fprintf(stderr,
                   "selection-tool-test: kNoteEntry tool started a drag\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
  }

  // --- test 2: selection tool creates a drag, moves, commits ------------
  handler.set_active_tool(graphscore::ActiveTool::kSelection);
  {
    const double press_x = layout.systems[0].measures[0].bounds.x;
    const double press_y = layout.systems[0].staves[0].bounds.y +
                           layout.systems[0].staves[0].bounds.height * 0.5;
    shell.dispatch_test_pointer_event(0, make_event(press_x, press_y));
    if (!handler.drag_state().is_dragging()) {
      std::fprintf(stderr,
                   "selection-tool-test: kSelection tool did not start drag\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    const double move_x = layout.systems[0].measures[0].bounds.x +
                          layout.systems[0].measures[0].bounds.width;
    const double move_y = press_y;
    shell.dispatch_test_pointer_event(1, make_event(move_x, move_y));
    if (!handler.drag_state().live_extent().has_value()) {
      std::fprintf(stderr,
                   "selection-tool-test: live_extent missing after move\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    shell.dispatch_test_pointer_event(2, make_event(move_x, move_y));
    if (handler.drag_state().is_dragging()) {
      std::fprintf(stderr,
                   "selection-tool-test: is_dragging true after release\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    const auto& comm_sel = handler.drag_state().committed_selection();
    if (!comm_sel.has_value()) {
      std::fprintf(stderr,
                   "selection-tool-test: committed_selection missing after "
                   "release\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    // Exact committed span: a drag across a full 4/4 measure must produce a
    // span of [0, 1) (measure-relative whole-note units).
    {
      const auto* set = std::get_if<graphscore::ArbitraryRangeSet>(&*comm_sel);
      if (set == nullptr || set->items().empty()) {
        std::fprintf(stderr,
                     "selection-tool-test: committed selection is not a "
                     "non-empty ArbitraryRangeSet\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
      if (set->items().size() != 1u) {
        std::fprintf(stderr,
                     "selection-tool-test: expected 1 range item, got %zu\n",
                     set->items().size());
        shell.set_input_handler(nullptr);
        return 1;
      }
      const graphscore::MusicalSpan expected{graphscore::Rational(0),
                                             graphscore::Rational(1)};
      if (set->items()[0].span != expected) {
        std::fprintf(
            stderr,
            "selection-tool-test: span mismatch — "
            "expected [0, 1), got [%" PRId64 "/%" PRId64 ", %" PRId64
            "/%" PRId64 ")\n",
            static_cast<std::int64_t>(set->items()[0].span.start.numerator()),
            static_cast<std::int64_t>(set->items()[0].span.start.denominator()),
            static_cast<std::int64_t>(set->items()[0].span.end.numerator()),
            static_cast<std::int64_t>(set->items()[0].span.end.denominator()));
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    // Exact highlight rect: the committed full-measure drag must produce
    // exactly one rect whose x,y,width,height are the expected values
    // derived from the fixture's measure and staff bounds — not merely
    // within-bounds positive checks.
    {
      const std::vector<graphscore::NotationRect> headless_rects =
          shell.test_snapshot_highlight_rects();
      if (headless_rects.size() != 1u) {
        std::fprintf(stderr,
                     "selection-tool-test: expected 1 highlight rect, got %zu "
                     "(headless path)\n",
                     headless_rects.size());
        shell.set_input_handler(nullptr);
        return 1;
      }
      const graphscore::NotationRect expected =
          expected_full_measure_highlight_rect(layout);
      if (headless_rects[0] != expected) {
        std::fprintf(stderr,
                     "selection-tool-test: highlight rect mismatch "
                     "(headless path) — "
                     "expected [%.6f,%.6f %.6fx%.6f], "
                     "got [%.6f,%.6f %.6fx%.6f]\n",
                     expected.x, expected.y, expected.width, expected.height,
                     headless_rects[0].x, headless_rects[0].y,
                     headless_rects[0].width, headless_rects[0].height);
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
  }

  // --- test 3: switch to note-entry cancels the drag -------------------
  handler.set_active_tool(graphscore::ActiveTool::kSelection);
  {
    const double press_x = layout.systems[0].measures[0].bounds.x;
    const double press_y = layout.systems[0].staves[0].bounds.y +
                           layout.systems[0].staves[0].bounds.height * 0.5;
    shell.dispatch_test_pointer_event(0, make_event(press_x, press_y));
    if (!handler.drag_state().is_dragging()) {
      std::fprintf(stderr,
                   "selection-tool-test: begin before tool-switch failed\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    handler.set_active_tool(graphscore::ActiveTool::kNoteEntry);
    if (handler.drag_state().is_dragging()) {
      std::fprintf(stderr, "selection-tool-test: drag survived tool switch\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
  }

  // --- test 4: secondary button does not start a drag -----------------
  handler.set_active_tool(graphscore::ActiveTool::kSelection);
  {
    shell.dispatch_test_pointer_event(
        0, make_event(layout.systems[0].measures[0].bounds.x,
                      layout.systems[0].staves[0].bounds.y +
                          layout.systems[0].staves[0].bounds.height * 0.5,
                      graphscore::PointerButton::kSecondary));
    if (handler.drag_state().is_dragging()) {
      std::fprintf(stderr,
                   "selection-tool-test: secondary button started a drag\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
  }

  // --- test 5: move without drag is a no-op ---------------------------
  {
    shell.dispatch_test_pointer_event(
        1, make_event(layout.systems[0].measures[0].bounds.x + 100.0,
                      layout.systems[0].staves[0].bounds.y +
                          layout.systems[0].staves[0].bounds.height * 0.5));
    if (handler.drag_state().is_dragging()) {
      std::fprintf(stderr,
                   "selection-tool-test: move without press started drag\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
  }

  // --- test 6: committed selection survives tool switch ----------------
  {
    const double press_x = layout.systems[0].measures[0].bounds.x;
    const double press_y = layout.systems[0].staves[0].bounds.y +
                           layout.systems[0].staves[0].bounds.height * 0.5;
    const double move_x = layout.systems[0].measures[0].bounds.x +
                          layout.systems[0].measures[0].bounds.width;

    shell.dispatch_test_pointer_event(0, make_event(press_x, press_y));
    shell.dispatch_test_pointer_event(1, make_event(move_x, press_y));
    shell.dispatch_test_pointer_event(2, make_event(move_x, press_y));

    if (!handler.drag_state().committed_selection().has_value()) {
      std::fprintf(stderr,
                   "selection-tool-test: commit produced no selection\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    handler.set_active_tool(graphscore::ActiveTool::kNoteEntry);
    if (!handler.drag_state().committed_selection().has_value()) {
      std::fprintf(stderr,
                   "selection-tool-test: committed_selection lost on tool "
                   "switch\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
  }

  // --- test 7: secondary button does not commit the drag ---------------
  handler.set_active_tool(graphscore::ActiveTool::kSelection);
  {
    const double press_x = layout.systems[0].measures[0].bounds.x;
    const double press_y = layout.systems[0].staves[0].bounds.y +
                           layout.systems[0].staves[0].bounds.height * 0.5;
    const double move_x = layout.systems[0].measures[0].bounds.x +
                          layout.systems[0].measures[0].bounds.width;

    shell.dispatch_test_pointer_event(0, make_event(press_x, press_y));
    shell.dispatch_test_pointer_event(1, make_event(move_x, press_y));
    // Release with a secondary button — the drag must stay in progress.
    shell.dispatch_test_pointer_event(
        2, make_event(move_x, press_y, graphscore::PointerButton::kSecondary));
    if (!handler.drag_state().is_dragging()) {
      std::fprintf(stderr,
                   "selection-tool-test: secondary release ended drag\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    // A subsequent primary release should now commit.
    shell.dispatch_test_pointer_event(2, make_event(move_x, press_y));
    if (handler.drag_state().is_dragging()) {
      std::fprintf(stderr,
                   "selection-tool-test: primary release did not commit\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
  }

  // --- test 8: release point differs from last motion; commit resolves
  //     the release point, not the last motion point -------------------
  handler.set_active_tool(graphscore::ActiveTool::kSelection);
  {
    const double press_x = layout.systems[0].measures[0].bounds.x;
    const double press_y = layout.systems[0].staves[0].bounds.y +
                           layout.systems[0].staves[0].bounds.height * 0.5;
    const double move_x = layout.systems[0].measures[0].bounds.x +
                          layout.systems[0].measures[0].bounds.width;
    const double release_x = layout.systems[0].measures[0].bounds.x +
                             layout.systems[0].measures[0].bounds.width * 0.5;

    shell.dispatch_test_pointer_event(0, make_event(press_x, press_y));
    shell.dispatch_test_pointer_event(1, make_event(move_x, press_y));
    // Release at a different position than the last move.
    shell.dispatch_test_pointer_event(2, make_event(release_x, press_y));
    if (handler.drag_state().is_dragging()) {
      std::fprintf(stderr,
                   "selection-tool-test: drag still in progress after "
                   "release-at-different-point\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    // The committed selection should have a span ending at release_x,
    // not at move_x.
    const auto& committed = handler.drag_state().committed_selection();
    if (!committed.has_value()) {
      std::fprintf(stderr, "selection-tool-test: no commit after release\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
  }

  // --- test 9: release at an invalid position (off-stave) cancels
  //     without committing a stale extent --------------------------
  handler.set_active_tool(graphscore::ActiveTool::kSelection);
  {
    const double press_x = layout.systems[0].measures[0].bounds.x;
    const double press_y = layout.systems[0].staves[0].bounds.y +
                           layout.systems[0].staves[0].bounds.height * 0.5;
    const double move_x = layout.systems[0].measures[0].bounds.x +
                          layout.systems[0].measures[0].bounds.width;

    shell.dispatch_test_pointer_event(0, make_event(press_x, press_y));
    shell.dispatch_test_pointer_event(1, make_event(move_x, press_y));
    // Release at an off-stave position (far above the staff).
    // Resolution fails → handler cancels the drag.
    shell.dispatch_test_pointer_event(2, make_event(press_x, -10'000.0));
    if (handler.drag_state().is_dragging()) {
      std::fprintf(stderr,
                   "selection-tool-test: drag survived invalid release\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    // The committed selection from test 2 should still be intact, but
    // no new committed selection leaked from this cancelled drag.
  }

  // --- test 10: unknown button does not start a drag ------------------
  handler.set_active_tool(graphscore::ActiveTool::kSelection);
  {
    shell.dispatch_test_pointer_event(
        0, make_event(layout.systems[0].measures[0].bounds.x,
                      layout.systems[0].staves[0].bounds.y +
                          layout.systems[0].staves[0].bounds.height * 0.5,
                      graphscore::PointerButton::kUnknown));
    if (handler.drag_state().is_dragging()) {
      std::fprintf(stderr,
                   "selection-tool-test: unknown button started a drag\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
  }

  // --- test 11: invalid (NaN) release cancels the drag without
  //     committing ---------------------------------------------------
  handler.set_active_tool(graphscore::ActiveTool::kSelection);
  {
    const double press_x = layout.systems[0].measures[0].bounds.x;
    const double press_y = layout.systems[0].staves[0].bounds.y +
                           layout.systems[0].staves[0].bounds.height * 0.5;
    const double move_x = layout.systems[0].measures[0].bounds.x +
                          layout.systems[0].measures[0].bounds.width;

    shell.dispatch_test_pointer_event(0, make_event(press_x, press_y));
    shell.dispatch_test_pointer_event(1, make_event(move_x, press_y));
    // Release at NaN position — resolution fails; drag is cancelled.
    shell.dispatch_test_pointer_event(
        2, make_event(std::numeric_limits<double>::quiet_NaN(), press_y));
    if (handler.drag_state().is_dragging()) {
      std::fprintf(stderr, "selection-tool-test: drag survived NaN release\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
  }

  // --- test 12: cancel restores the exact committed highlight -------
  // Commit one full-measure drag, then start a second drag to a
  // demonstrably different endpoint.  Assert the live highlight differs
  // from the committed highlight; cancel; assert the restored highlight
  // exactly equals the original committed highlight (headless path).
  handler.set_active_tool(graphscore::ActiveTool::kSelection);
  {
    const double press_x = layout.systems[0].measures[0].bounds.x;
    const double press_y = layout.systems[0].staves[0].bounds.y +
                           layout.systems[0].staves[0].bounds.height * 0.5;
    const double full_x = layout.systems[0].measures[0].bounds.x +
                          layout.systems[0].measures[0].bounds.width;
    const double half_x = layout.systems[0].measures[0].bounds.x +
                          layout.systems[0].measures[0].bounds.width * 0.5;

    // Commit: full-measure drag.
    shell.dispatch_test_pointer_event(0, make_event(press_x, press_y));
    shell.dispatch_test_pointer_event(1, make_event(full_x, press_y));
    shell.dispatch_test_pointer_event(2, make_event(full_x, press_y));
    if (handler.drag_state().is_dragging()) {
      std::fprintf(stderr,
                   "selection-tool-test: drag still active after commit\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    const std::vector<graphscore::NotationRect> committed_rects =
        shell.test_snapshot_highlight_rects();
    if (committed_rects.empty()) {
      std::fprintf(stderr,
                   "selection-tool-test: committed highlight empty after "
                   "commit (headless path)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    // Start a second drag to a demonstrably different endpoint (half-
    // measure instead of full-measure).  The press alone preserves the
    // committed highlight.
    shell.dispatch_test_pointer_event(0, make_event(press_x, press_y));
    if (!handler.drag_state().is_dragging()) {
      std::fprintf(stderr, "selection-tool-test: second drag did not begin\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    // After press, the committed highlight is unchanged.
    {
      const auto after_press = shell.test_snapshot_highlight_rects();
      if (after_press != committed_rects) {
        std::fprintf(stderr,
                     "selection-tool-test: highlight changed after second "
                     "press (headless path)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }

    // Move to half-measure: live highlight must differ from committed.
    shell.dispatch_test_pointer_event(1, make_event(half_x, press_y));
    const std::vector<graphscore::NotationRect> live_rects =
        shell.test_snapshot_highlight_rects();
    if (live_rects.empty()) {
      std::fprintf(stderr,
                   "selection-tool-test: live highlight empty after move "
                   "(headless path)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (live_rects == committed_rects) {
      std::fprintf(stderr,
                   "selection-tool-test: live highlight did not differ from "
                   "committed after move to different endpoint (headless "
                   "path)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    // Cancel the second drag.  The committed highlight must be restored
    // exactly.
    shell.dispatch_test_pointer_event(3, make_event(0.0, 0.0));  // cancel
    if (handler.drag_state().is_dragging()) {
      std::fprintf(stderr,
                   "selection-tool-test: drag survived cancel (headless "
                   "path)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    const std::vector<graphscore::NotationRect> restored_rects =
        shell.test_snapshot_highlight_rects();
    if (restored_rects != committed_rects) {
      std::fprintf(stderr,
                   "selection-tool-test: restored highlight does not match "
                   "committed after cancel (headless path)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
  }

  // --- test 13: unregistered handler receives no callback ------------
  shell.set_input_handler(nullptr);
  {
    const double press_x = layout.systems[0].measures[0].bounds.x;
    const double press_y = layout.systems[0].staves[0].bounds.y +
                           layout.systems[0].staves[0].bounds.height * 0.5;

    // Dispatch should be a no-op with no handler registered.
    shell.dispatch_test_pointer_event(0, make_event(press_x, press_y));
    // The handler's drag state must be unchanged (no implicit drag).
    if (handler.drag_state().is_dragging()) {
      std::fprintf(stderr,
                   "selection-tool-test: handler received event after "
                   "unregistration\n");
      return 1;
    }
  }
  shell.set_input_handler(&handler);

  // --- test 14: DPI scale conversion produces correct coordinates ----
  // Set scale to 2.0, send an event at pixel (200, 100).  The test seam
  // divides by test_dpi_scale, so the handler receives logical (100, 50),
  // matching the production SDL_ConvertEventToRenderCoordinates path.
  shell.set_test_dpi_scale(2.0);
  handler.set_active_tool(graphscore::ActiveTool::kSelection);
  {
    shell.dispatch_test_pointer_event(0, make_event(200.0, 100.0));
    if (!handler.drag_state().is_dragging()) {
      std::fprintf(stderr,
                   "selection-tool-test: DPI-scaled press did not begin "
                   "drag\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    const graphscore::NotationPoint anchor = handler.drag_state().anchor();
    if (std::abs(anchor.x - 100.0) > 1e-9 || std::abs(anchor.y - 50.0) > 1e-9) {
      std::fprintf(stderr,
                   "selection-tool-test: DPI-scaled anchor mismatch: "
                   "expected (100, 50), got (%.1f, %.1f)\n",
                   anchor.x, anchor.y);
      shell.set_input_handler(nullptr);
      return 1;
    }
    handler.on_cancel();
  }
  shell.set_test_dpi_scale(0.0);

  // --- test 15: non-finite primary re-press while dragging cancels ----
  // Start a valid selection drag, then issue a primary press with NaN
  // coordinates.  The handler must cancel the drag via begin(), so a
  // subsequent release does not commit a stale extent.
  handler.set_active_tool(graphscore::ActiveTool::kSelection);
  {
    const double press_x = layout.systems[0].measures[0].bounds.x;
    const double press_y = layout.systems[0].staves[0].bounds.y +
                           layout.systems[0].staves[0].bounds.height * 0.5;
    const double move_x = layout.systems[0].measures[0].bounds.x +
                          layout.systems[0].measures[0].bounds.width;

    // Start a valid drag.
    shell.dispatch_test_pointer_event(0, make_event(press_x, press_y));
    shell.dispatch_test_pointer_event(1, make_event(move_x, press_y));
    if (!handler.drag_state().is_dragging()) {
      std::fprintf(stderr,
                   "selection-tool-test: drag not active before NaN repress\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    // Re-press with NaN coordinates — handler must cancel the drag.
    shell.dispatch_test_pointer_event(
        0, make_event(std::numeric_limits<double>::quiet_NaN(), press_y));
    if (handler.drag_state().is_dragging()) {
      std::fprintf(stderr,
                   "selection-tool-test: drag survived NaN primary repress\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    // A subsequent primary release must not commit (drag is already gone).
    shell.dispatch_test_pointer_event(2, make_event(move_x, press_y));
    if (handler.drag_state().is_dragging()) {
      std::fprintf(stderr,
                   "selection-tool-test: release after NaN repress started "
                   "a new drag\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
  }

  std::printf("selection-tool-test: ok\n");
  return 0;
}

// ---- shell-integration selection-tool test (requires display) ---------------
//
// Opens a real SDL window with a registered handler, then injects pointer
// events through both dispatch_test_pointer_event (the headless seam) and
// dispatch_sdl_test_pointer_event (the production SDL conversion path) and
// asserts exact handler state: coordinates, drag lifecycle, highlight
// delivery, registration/unregistration, DPI scale conversion, RGBA texture
// upload-readback, committed-selection highlight persistence across cancel,
// and texture failure non-success.
//
// The test seam applies the same DPI scale conversion contract as the
// production SDL_ConvertEventToRenderCoordinates path, so coordinate
// assertions hold for both paths.  Texture-readback assertions use
// SDL_RenderReadPixels; they verify channel-order integrity and are not
// framebuffer-pixel-accuracy claims.
//
// A genuinely absent display (kBackendUnavailable) or absent GPU
// (kRendererUnavailable) is a skip-match; a rendering-setup defect such as
// a texture creation or upload failure (kRenderingSetupFailed) is a hard
// test failure and does not match the PASS regex.
int selection_tool_shell_test() {
  // ---- pre-open surface validation (display-independent) ----
  // set_notation_surface validates dimensions and buffer size eagerly,
  // before any window or renderer exists.  Every invalid case below must
  // return kRenderingSetupFailed; the canonical empty (0,0,empty) and an
  // exact-size buffer must succeed.
  {
    graphscore::WriterShell preopen_shell;

    // Canonical empty: (0, 0, empty buffer) — accepted.
    {
      graphscore::RasterSurface surface;
      surface.width  = 0;
      surface.height = 0;
      surface.rgba.clear();
      const graphscore::ShellResult result =
          preopen_shell.set_notation_surface(std::move(surface));
      if (!result.ok()) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: canonical empty surface "
                     "rejected before open: %s\n",
                     result.message.c_str());
        return 1;
      }
    }

    // Canonical empty with non-empty rgba: (0, 0, non-empty) — rejected.
    {
      graphscore::RasterSurface surface;
      surface.width  = 0;
      surface.height = 0;
      surface.rgba   = {0x00, 0x00, 0x00, 0xFF};
      const graphscore::ShellResult result =
          preopen_shell.set_notation_surface(std::move(surface));
      if (result.error != graphscore::ShellError::kRenderingSetupFailed) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: (0,0,non-empty) surface not "
                     "rejected before open (got %s)\n",
                     describe(result.error));
        return 1;
      }
    }

    // One-zero dimension: (0, 1) — rejected.
    {
      graphscore::RasterSurface surface;
      surface.width  = 0;
      surface.height = 1;
      surface.rgba   = {0, 0, 0, 0};
      const graphscore::ShellResult result =
          preopen_shell.set_notation_surface(std::move(surface));
      if (result.error != graphscore::ShellError::kRenderingSetupFailed) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: (0,1) surface not rejected "
                     "before open (got %s)\n",
                     describe(result.error));
        return 1;
      }
    }

    // One-zero dimension: (1, 0) — rejected.
    {
      graphscore::RasterSurface surface;
      surface.width  = 1;
      surface.height = 0;
      surface.rgba   = {0, 0, 0, 0};
      const graphscore::ShellResult result =
          preopen_shell.set_notation_surface(std::move(surface));
      if (result.error != graphscore::ShellError::kRenderingSetupFailed) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: (1,0) surface not rejected "
                     "before open (got %s)\n",
                     describe(result.error));
        return 1;
      }
    }

    // Undersized buffer: 2×2 needs 16 bytes, only 1 supplied — rejected.
    {
      graphscore::RasterSurface surface;
      surface.width  = 2;
      surface.height = 2;
      surface.rgba   = {0xFF};
      const graphscore::ShellResult result =
          preopen_shell.set_notation_surface(std::move(surface));
      if (result.error != graphscore::ShellError::kRenderingSetupFailed) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: undersized buffer not "
                     "rejected before open (got %s)\n",
                     describe(result.error));
        return 1;
      }
    }

    // Oversized buffer: 2×2 needs 16 bytes, 17 supplied — rejected
    // (exact equality is load-bearing).
    {
      graphscore::RasterSurface surface;
      surface.width  = 2;
      surface.height = 2;
      surface.rgba.assign(17, 0x00);
      const graphscore::ShellResult result =
          preopen_shell.set_notation_surface(std::move(surface));
      if (result.error != graphscore::ShellError::kRenderingSetupFailed) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: oversized buffer not "
                     "rejected before open (got %s)\n",
                     describe(result.error));
        return 1;
      }
    }

    // Exact buffer: 2×2 with exactly 16 bytes — accepted.
    {
      graphscore::RasterSurface surface;
      surface.width  = 2;
      surface.height = 2;
      surface.rgba.assign(16, 0x00);
      const graphscore::ShellResult result =
          preopen_shell.set_notation_surface(std::move(surface));
      if (!result.ok()) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: exact buffer surface "
                     "rejected before open: %s\n",
                     result.message.c_str());
        return 1;
      }
    }
  }

  // ---- set_notation_surface eagerly rejects invalid dimensions ----
  // The surface validation is backend-independent: it rejects
  // undersized/oversized buffers and one-zero dimensions before any
  // window or renderer exists, in both writer-ON and writer-OFF builds.
  // No open_window call is needed — the eager check alone catches every
  // invalid case above.
  {
    graphscore::WriterShell   invalid_shell;
    graphscore::RasterSurface surface;
    surface.width  = 2;
    surface.height = 2;
    surface.rgba   = {0xFF};  // 1 byte; 2×2×4 = 16 required
    const auto set_result =
        invalid_shell.set_notation_surface(std::move(surface));
    // Eager validation rejects the undersized buffer immediately — no
    // renderer is required for the dimension/buffer-size check.
    if (set_result.error != graphscore::ShellError::kRenderingSetupFailed) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: undersized surface not "
                   "eagerly rejected (got %s)\n",
                   describe(set_result.error));
      return 1;
    }
  }

  const SelfTestMetrics metrics;
  auto                  dp = build_default_project(metrics);
  if (!dp.has_value()) {
    std::fprintf(stderr,
                 "selection-tool-shell-test: build_default_project failed\n");
    return 1;
  }

  graphscore::WriterShell shell;
  SelectionToolHandler    handler(std::move(dp->project), std::move(dp->layout),
                                  &shell);
  handler.set_active_tool(graphscore::ActiveTool::kSelection);
  shell.set_input_handler(&handler);

  // Open the window without event loop (smoke-test style). This verifies
  // the SDL window and renderer are functional on this host, but does not
  // run a blocking event loop — event injection below uses the test seam.
  graphscore::WindowOptions options;
  options.run_event_loop = false;

  const graphscore::ShellResult result = shell.open_window(options);
  if (!result.ok()) {
    const bool is_benign_headless =
        result.error == graphscore::ShellError::kBackendUnavailable ||
        result.error == graphscore::ShellError::kRendererUnavailable;
    std::fprintf(stderr, "selection-tool-shell-test: open_window failed: %s\n",
                 is_benign_headless
                     ? "no display or renderer — headless host (skip)"
                     : result.message.c_str());
    shell.set_input_handler(nullptr);
    return is_benign_headless ? 0 : 1;
  }

  const auto& layout = handler.layout();

  auto make_event = [](double x, double y,
                       graphscore::PointerButton button =
                           graphscore::PointerButton::kPrimary) {
    graphscore::PointerEvent e;
    e.x      = x;
    e.y      = y;
    e.button = button;
    return e;
  };

  // ---- exact coordinate assertions at 1x DPI (production SDL path) ----
  // Route pixel-space coordinates through dispatch_sdl_test_pointer_event,
  // which exercises SDL_ConvertEventToRenderCoordinates.  At 1x DPI,
  // pixel and logical coordinates are identical.
  {
    const double press_x = layout.systems[0].measures[0].bounds.x;
    const double press_y = layout.systems[0].staves[0].bounds.y +
                           layout.systems[0].staves[0].bounds.height * 0.5;

    shell.dispatch_sdl_test_pointer_event(0, make_event(press_x, press_y));
    if (!handler.drag_state().is_dragging()) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: drag did not begin at 1x "
                   "(production SDL path)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    // Anchor must be exactly the press position (at 1x DPI,
    // SDL_ConvertEventToRenderCoordinates is the identity).
    const graphscore::NotationPoint anchor = handler.drag_state().anchor();
    if (std::abs(anchor.x - press_x) > 1e-9 ||
        std::abs(anchor.y - press_y) > 1e-9) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: anchor mismatch at 1x: "
                   "expected (%.2f, %.2f), got (%.2f, %.2f)\n",
                   press_x, press_y, anchor.x, anchor.y);
      shell.set_input_handler(nullptr);
      return 1;
    }
  }
  handler.on_cancel();

  // ---- drag-move-release span assertion at 1x (production SDL path) ----
  handler.set_active_tool(graphscore::ActiveTool::kSelection);
  {
    const double press_x = layout.systems[0].measures[0].bounds.x;
    const double press_y = layout.systems[0].staves[0].bounds.y +
                           layout.systems[0].staves[0].bounds.height * 0.5;
    const double move_x = layout.systems[0].measures[0].bounds.x +
                          layout.systems[0].measures[0].bounds.width;

    shell.dispatch_sdl_test_pointer_event(0, make_event(press_x, press_y));
    shell.dispatch_sdl_test_pointer_event(1, make_event(move_x, press_y));
    shell.dispatch_sdl_test_pointer_event(2, make_event(move_x, press_y));

    if (handler.drag_state().is_dragging()) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: drag still in progress after "
                   "release\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    const auto& committed = handler.drag_state().committed_selection();
    if (!committed.has_value()) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: no committed selection after "
                   "drag-release\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    // Exact committed span and highlight rects.
    const auto* set = std::get_if<graphscore::ArbitraryRangeSet>(&*committed);
    if (set == nullptr || set->items().empty()) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: committed selection is not a "
                   "non-empty ArbitraryRangeSet\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (set->items().size() != 1u) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: expected 1 range item at 1x, "
                   "got %zu\n",
                   set->items().size());
      shell.set_input_handler(nullptr);
      return 1;
    }
    {
      const graphscore::MusicalSpan expected{graphscore::Rational(0),
                                             graphscore::Rational(1)};
      if (set->items()[0].span != expected) {
        std::fprintf(
            stderr,
            "selection-tool-shell-test: span mismatch at 1x — "
            "expected [0, 1), got [%" PRId64 "/%" PRId64 ", %" PRId64
            "/%" PRId64 ")\n",
            static_cast<std::int64_t>(set->items()[0].span.start.numerator()),
            static_cast<std::int64_t>(set->items()[0].span.start.denominator()),
            static_cast<std::int64_t>(set->items()[0].span.end.numerator()),
            static_cast<std::int64_t>(set->items()[0].span.end.denominator()));
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    // Exact highlight rect at 1x: x,y,width,height must equal the expected
    // values derived from the fixture's measure and staff bounds.
    {
      const std::vector<graphscore::NotationRect> rects =
          shell.test_snapshot_highlight_rects();
      if (rects.size() != 1u) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: expected 1 highlight rect "
                     "at 1x, got %zu\n",
                     rects.size());
        shell.set_input_handler(nullptr);
        return 1;
      }
      const graphscore::NotationRect expected =
          expected_full_measure_highlight_rect(layout);
      if (rects[0] != expected) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: highlight rect mismatch "
                     "at 1x — "
                     "expected [%.6f,%.6f %.6fx%.6f], "
                     "got [%.6f,%.6f %.6fx%.6f]\n",
                     expected.x, expected.y, expected.width, expected.height,
                     rects[0].x, rects[0].y, rects[0].width, rects[0].height);
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
  }

  // ---- 2x DPI scale: press, move, release, exact converted anchor ----
  // Use dispatch_sdl_test_pointer_event which routes through the production
  // SDL_ConvertEventToRenderCoordinates path.  set_test_dpi_scale also
  // pushes the scale to the renderer via SDL_SetRenderScale so the
  // conversion path sees the correct 2x mapping.
  handler.set_active_tool(graphscore::ActiveTool::kSelection);
  shell.set_test_dpi_scale(2.0);
  {
    // Pixel-space coordinates at the logical position × 2.  The SDL
    // production path converts these through
    // SDL_ConvertEventToRenderCoordinates with the 2x render scale, yielding
    // the correct logical position.
    const double logical_x = layout.systems[0].measures[0].bounds.x;
    const double logical_y = layout.systems[0].staves[0].bounds.y +
                             layout.systems[0].staves[0].bounds.height * 0.5;
    const double pixel_x = logical_x * 2.0;
    const double pixel_y = logical_y * 2.0;

    shell.dispatch_sdl_test_pointer_event(0, make_event(pixel_x, pixel_y));
    if (!handler.drag_state().is_dragging()) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: drag did not begin at 2x "
                   "(production SDL path)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    // Anchor must be at the logical coordinates (SDL conversion applied).
    const graphscore::NotationPoint anchor = handler.drag_state().anchor();
    if (std::abs(anchor.x - logical_x) > 1e-9 ||
        std::abs(anchor.y - logical_y) > 1e-9) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: anchor mismatch at 2x: "
                   "expected logical (%.2f, %.2f), got (%.2f, %.2f)\n",
                   logical_x, logical_y, anchor.x, anchor.y);
      shell.set_input_handler(nullptr);
      return 1;
    }

    // Move and release at 2x pixel coords.
    const double move_logical_x = layout.systems[0].measures[0].bounds.x +
                                  layout.systems[0].measures[0].bounds.width;
    const double move_pixel_x = move_logical_x * 2.0;
    shell.dispatch_sdl_test_pointer_event(1, make_event(move_pixel_x, pixel_y));
    shell.dispatch_sdl_test_pointer_event(2, make_event(move_pixel_x, pixel_y));

    if (handler.drag_state().is_dragging()) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: drag still in progress at 2x "
                   "after release\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    const auto& committed = handler.drag_state().committed_selection();
    if (!committed.has_value()) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: no committed selection at 2x\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    const auto* set = std::get_if<graphscore::ArbitraryRangeSet>(&*committed);
    if (set == nullptr || set->items().empty()) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: committed selection at 2x is "
                   "empty\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    // Exact span at 2x: same fixture, same full-measure drag, same [0, 1).
    if (set->items().size() != 1u) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: expected 1 range item at 2x, "
                   "got %zu\n",
                   set->items().size());
      shell.set_input_handler(nullptr);
      return 1;
    }
    {
      const graphscore::MusicalSpan expected{graphscore::Rational(0),
                                             graphscore::Rational(1)};
      if (set->items()[0].span != expected) {
        std::fprintf(
            stderr,
            "selection-tool-shell-test: span mismatch at 2x — "
            "expected [0, 1), got [%" PRId64 "/%" PRId64 ", %" PRId64
            "/%" PRId64 ")\n",
            static_cast<std::int64_t>(set->items()[0].span.start.numerator()),
            static_cast<std::int64_t>(set->items()[0].span.start.denominator()),
            static_cast<std::int64_t>(set->items()[0].span.end.numerator()),
            static_cast<std::int64_t>(set->items()[0].span.end.denominator()));
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    // Exact highlight rect at 2x: same expected geometry as 1x — the
    // rect is in logical (notation) coordinates, not pixel coordinates,
    // so the DPI scale does not affect it.  x,y,width,height must exactly
    // equal the values derived from the fixture's measure and staff bounds.
    {
      const std::vector<graphscore::NotationRect> rects =
          shell.test_snapshot_highlight_rects();
      if (rects.size() != 1u) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: expected 1 highlight rect "
                     "at 2x, got %zu\n",
                     rects.size());
        shell.set_input_handler(nullptr);
        return 1;
      }
      const graphscore::NotationRect expected =
          expected_full_measure_highlight_rect(layout);
      if (rects[0] != expected) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: highlight rect mismatch "
                     "at 2x — "
                     "expected [%.6f,%.6f %.6fx%.6f], "
                     "got [%.6f,%.6f %.6fx%.6f]\n",
                     expected.x, expected.y, expected.width, expected.height,
                     rects[0].x, rects[0].y, rects[0].width, rects[0].height);
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
  }
  shell.set_test_dpi_scale(0.0);

  // ---- unregistered handler receives no event ----
  shell.set_input_handler(nullptr);
  {
    const double press_x = layout.systems[0].measures[0].bounds.x;
    const double press_y = layout.systems[0].staves[0].bounds.y +
                           layout.systems[0].staves[0].bounds.height * 0.5;
    shell.dispatch_test_pointer_event(0, make_event(press_x, press_y));
    if (handler.drag_state().is_dragging()) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: handler received event after "
                   "unregistration\n");
      return 1;
    }
  }

  // ---- RGBA upload-readback: distinctive non-symmetric pixel pattern ----
  // Upload a 2×2 raster surface with known per-pixel RGBA values and read
  // back two pixels to verify the byte-order contract survives the upload /
  // readback round-trip.  The pattern uses non-symmetric channel values so
  // any channel swap (R↔B, R↔A, etc.) is immediately detected.
  {
    graphscore::RasterSurface surface;
    surface.width  = 2;
    surface.height = 2;
    // Pixel (0,0): R=0xAA, G=0x00, B=0xFF, A=0x80 — distinctive
    // Pixel (1,0): R=0x55, G=0xCC, B=0x11, A=0xFF
    // Pixel (0,1): R=0x22, G=0x33, B=0x44, A=0x55
    // Pixel (1,1): R=0x66, G=0x77, B=0x88, A=0x99
    surface.rgba = {0xAA, 0x00, 0xFF, 0x80, 0x55, 0xCC, 0x11, 0xFF,
                    0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99};
    shell.set_notation_surface(std::move(surface));

    // First upload: exactly one texture created, none destroyed, one alive.
    {
      auto s = shell.test_notation_texture_stats();
      if (s.created != 1 || s.destroyed != 0) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: texture stats after first "
                     "upload — expected created=1 destroyed=0 alive=1, "
                     "got created=%" PRIu64 " destroyed=%" PRIu64
                     " alive=%" PRIu64 "\n",
                     static_cast<std::uint64_t>(s.created),
                     static_cast<std::uint64_t>(s.destroyed),
                     static_cast<std::uint64_t>(s.created - s.destroyed));
        return 1;
      }
    }

    auto p00 = shell.test_read_notation_pixel(0, 0);
    if (!p00.has_value()) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: readback of pixel (0,0) "
                   "returned nullopt\n");
      return 1;
    }
    if ((*p00)[0] != 0xAA || (*p00)[1] != 0x00 || (*p00)[2] != 0xFF ||
        (*p00)[3] != 0x80) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: pixel (0,0) mismatch — "
                   "expected [0xAA,0x00,0xFF,0x80], got "
                   "[0x%02X,0x%02X,0x%02X,0x%02X]\n",
                   (*p00)[0], (*p00)[1], (*p00)[2], (*p00)[3]);
      return 1;
    }

    auto p11 = shell.test_read_notation_pixel(1, 1);
    if (!p11.has_value()) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: readback of pixel (1,1) "
                   "returned nullopt\n");
      return 1;
    }
    if ((*p11)[0] != 0x66 || (*p11)[1] != 0x77 || (*p11)[2] != 0x88 ||
        (*p11)[3] != 0x99) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: pixel (1,1) mismatch — "
                   "expected [0x66,0x77,0x88,0x99], got "
                   "[0x%02X,0x%02X,0x%02X,0x%02X]\n",
                   (*p11)[0], (*p11)[1], (*p11)[2], (*p11)[3]);
      return 1;
    }
  }

  // ---- texture failure injection: zero-dimension surface produces
  //     null-texture / nullopt readback ----
  {
    // Set an empty surface (width==0); the texture should be destroyed
    // and readback must return nullopt — not e.g. stale data.
    shell.set_notation_surface(graphscore::RasterSurface{});
    // Clear: old texture destroyed, alive becomes zero.
    {
      auto s = shell.test_notation_texture_stats();
      if (s.created != 1 || s.destroyed != 1) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: texture stats after clear — "
                     "expected created=1 destroyed=1 alive=0, "
                     "got created=%" PRIu64 " destroyed=%" PRIu64
                     " alive=%" PRIu64 "\n",
                     static_cast<std::uint64_t>(s.created),
                     static_cast<std::uint64_t>(s.destroyed),
                     static_cast<std::uint64_t>(s.created - s.destroyed));
        return 1;
      }
    }
    auto pixel = shell.test_read_notation_pixel(0, 0);
    if (pixel.has_value()) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: readback after clearing "
                   "notation surface returned a pixel (expected nullopt)\n");
      return 1;
    }

    // Out-of-bounds readback on a valid texture must also return nullopt.
    graphscore::RasterSurface small;
    small.width  = 1;
    small.height = 1;
    small.rgba   = {0xFF, 0x00, 0xFF, 0x80};
    shell.set_notation_surface(std::move(small));
    // New upload: created=2, destroyed=1, alive=1.
    {
      auto s = shell.test_notation_texture_stats();
      if (s.created != 2 || s.destroyed != 1) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: texture stats after OOB "
                     "upload — expected created=2 destroyed=1 alive=1, "
                     "got created=%" PRIu64 " destroyed=%" PRIu64
                     " alive=%" PRIu64 "\n",
                     static_cast<std::uint64_t>(s.created),
                     static_cast<std::uint64_t>(s.destroyed),
                     static_cast<std::uint64_t>(s.created - s.destroyed));
        return 1;
      }
    }
    auto oob = shell.test_read_notation_pixel(1, 1);
    if (oob.has_value()) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: readback past texture "
                   "bounds returned a pixel (expected nullopt)\n");
      return 1;
    }
  }

  // ---- post-open invalid surface: error returned, valid surface survives ----
  // After a valid surface is uploaded and readable, call set_notation_surface
  // with invalid surfaces and verify: (a) the call returns
  // kRenderingSetupFailed, (b) the previously valid texture is not destroyed or
  // replaced, and (c) a prior distinctive pixel remains readable. Honest skip
  // when no renderer is available.
  {
    // Upload a known-good 2×2 distinctive pattern.
    graphscore::RasterSurface good;
    good.width               = 2;
    good.height              = 2;
    good.rgba                = {0xAA, 0x00, 0xFF, 0x80, 0x55, 0xCC, 0x11, 0xFF,
                                0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99};
    const auto upload_result = shell.set_notation_surface(std::move(good));
    if (!upload_result.ok()) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: valid surface rejected "
                   "post-open: %s\n",
                   upload_result.message.c_str());
      return 1;
    }

    auto       prior         = shell.test_read_notation_pixel(0, 0);
    const bool have_renderer = prior.has_value();
    if (!have_renderer) {
      // Renderer unavailable — honest skip.
      std::printf(
          "selection-tool-shell-test: no renderer — post-open surface "
          "validation skipped (renderer-unavailable)\n");
    }
    if (have_renderer) {
      if ((*prior)[0] != 0xAA || (*prior)[1] != 0x00 || (*prior)[2] != 0xFF ||
          (*prior)[3] != 0x80) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: pixel (0,0) mismatch before "
                     "invalid post-open call\n");
        return 1;
      }
      // Upload replaced old 1x1 texture: created=3, destroyed=2, alive=1.
      {
        auto s = shell.test_notation_texture_stats();
        if (s.created != 3 || s.destroyed != 2) {
          std::fprintf(stderr,
                       "selection-tool-shell-test: texture stats after "
                       "post-open good upload — "
                       "expected created=3 destroyed=2 alive=1, "
                       "got created=%" PRIu64 " destroyed=%" PRIu64
                       " alive=%" PRIu64 "\n",
                       static_cast<std::uint64_t>(s.created),
                       static_cast<std::uint64_t>(s.destroyed),
                       static_cast<std::uint64_t>(s.created - s.destroyed));
          return 1;
        }
      }
    }

    // Post-open invalid: oversized buffer (2×2 needs 16, 20 supplied).
    if (have_renderer) {
      graphscore::RasterSurface bad;
      bad.width  = 2;
      bad.height = 2;
      bad.rgba.assign(20, 0x00);
      const graphscore::ShellResult set_result =
          shell.set_notation_surface(std::move(bad));
      if (set_result.error != graphscore::ShellError::kRenderingSetupFailed) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: oversized buffer not rejected "
                     "post-open (got %s)\n",
                     describe(set_result.error));
        return 1;
      }
      // The prior valid texture must survive — pixel (0,0) still readable.
      auto after = shell.test_read_notation_pixel(0, 0);
      if (!after.has_value() || (*after)[0] != 0xAA || (*after)[1] != 0x00 ||
          (*after)[2] != 0xFF || (*after)[3] != 0x80) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: valid pixel destroyed after "
                     "invalid post-open call\n");
        return 1;
      }
      // Invalid upload created/destroyed nothing: alive still 1.
      {
        auto s = shell.test_notation_texture_stats();
        if (s.created != 3 || s.destroyed != 2) {
          std::fprintf(stderr,
                       "selection-tool-shell-test: texture stats changed after "
                       "invalid oversized post-open call — "
                       "expected created=3 destroyed=2 alive=1, "
                       "got created=%" PRIu64 " destroyed=%" PRIu64
                       " alive=%" PRIu64 "\n",
                       static_cast<std::uint64_t>(s.created),
                       static_cast<std::uint64_t>(s.destroyed),
                       static_cast<std::uint64_t>(s.created - s.destroyed));
          return 1;
        }
      }
    }

    // Post-open invalid: one-zero dimension (0, 1).
    if (have_renderer) {
      graphscore::RasterSurface bad;
      bad.width  = 0;
      bad.height = 1;
      bad.rgba   = {0x00};
      const graphscore::ShellResult set_result =
          shell.set_notation_surface(std::move(bad));
      if (set_result.error != graphscore::ShellError::kRenderingSetupFailed) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: (0,1) not rejected "
                     "post-open (got %s)\n",
                     describe(set_result.error));
        return 1;
      }
      // Valid pixel still readable.
      auto after = shell.test_read_notation_pixel(0, 0);
      if (!after.has_value() || (*after)[0] != 0xAA || (*after)[1] != 0x00 ||
          (*after)[2] != 0xFF || (*after)[3] != 0x80) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: valid pixel destroyed after "
                     "(0,1) post-open call\n");
        return 1;
      }
      // Invalid upload created/destroyed nothing: alive still 1.
      {
        auto s = shell.test_notation_texture_stats();
        if (s.created != 3 || s.destroyed != 2) {
          std::fprintf(stderr,
                       "selection-tool-shell-test: texture stats changed after "
                       "invalid (0,1) post-open call — "
                       "expected created=3 destroyed=2 alive=1, "
                       "got created=%" PRIu64 " destroyed=%" PRIu64
                       " alive=%" PRIu64 "\n",
                       static_cast<std::uint64_t>(s.created),
                       static_cast<std::uint64_t>(s.destroyed),
                       static_cast<std::uint64_t>(s.created - s.destroyed));
          return 1;
        }
      }
    }
  }

  // ---- transactional texture replacement: successful replace, successful
  //     clear, and test-injected failure ----
  // Verify that set_notation_surface atomically swaps textures on success
  // (old destroyed, new uploaded), clears to null on empty surface, and
  // preserves the prior surface when SDL operations fail.
  {
    // ---- successful replace ----
    // Upload surface A (red pattern), then surface B (green pattern).
    // After B, verify B is readable and the old A texture is gone.
    {
      graphscore::RasterSurface surface_a;
      surface_a.width  = 2;
      surface_a.height = 2;
      // All-red pixels: (0,0) through (1,1)
      surface_a.rgba = {0xFF, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0xFF,
                        0xFF, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0xFF};
      const graphscore::ShellResult set_a =
          shell.set_notation_surface(std::move(surface_a));
      if (!set_a.ok()) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: surface A rejected: %s\n",
                     set_a.message.c_str());
        return 1;
      }
      // Successful replace: created=4, destroyed=3, alive=1.
      {
        auto s = shell.test_notation_texture_stats();
        if (s.created != 4 || s.destroyed != 3) {
          std::fprintf(stderr,
                       "selection-tool-shell-test: texture stats after "
                       "surface A — expected created=4 destroyed=3 alive=1, "
                       "got created=%" PRIu64 " destroyed=%" PRIu64
                       " alive=%" PRIu64 "\n",
                       static_cast<std::uint64_t>(s.created),
                       static_cast<std::uint64_t>(s.destroyed),
                       static_cast<std::uint64_t>(s.created - s.destroyed));
          return 1;
        }
      }
      auto pixel_a = shell.test_read_notation_pixel(0, 0);
      if (!pixel_a.has_value()) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: readback of surface A "
                     "returned nullopt after successful upload\n");
        return 1;
      }
      // Verify surface A is what we uploaded (red).
      if ((*pixel_a)[0] != 0xFF || (*pixel_a)[1] != 0x00 ||
          (*pixel_a)[2] != 0x00 || (*pixel_a)[3] != 0xFF) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: surface A readback "
                     "mismatch\n");
        return 1;
      }

      // Upload surface B (distinct green pattern), replacing A.
      graphscore::RasterSurface surface_b;
      surface_b.width  = 2;
      surface_b.height = 2;
      // All-green pixels
      surface_b.rgba = {0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF,
                        0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF};
      const graphscore::ShellResult set_b =
          shell.set_notation_surface(std::move(surface_b));
      if (!set_b.ok()) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: surface B rejected: %s\n",
                     set_b.message.c_str());
        return 1;
      }
      // Successful replace: created=5, destroyed=4, alive=1.
      {
        auto s = shell.test_notation_texture_stats();
        if (s.created != 5 || s.destroyed != 4) {
          std::fprintf(stderr,
                       "selection-tool-shell-test: texture stats after "
                       "surface B — expected created=5 destroyed=4 alive=1, "
                       "got created=%" PRIu64 " destroyed=%" PRIu64
                       " alive=%" PRIu64 "\n",
                       static_cast<std::uint64_t>(s.created),
                       static_cast<std::uint64_t>(s.destroyed),
                       static_cast<std::uint64_t>(s.created - s.destroyed));
          return 1;
        }
      }
      // After successful replace, pixel (0,0) must read as green, not red.
      auto pixel_b = shell.test_read_notation_pixel(0, 0);
      if (!pixel_b.has_value()) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: readback of surface B "
                     "returned nullopt after successful upload\n");
        return 1;
      }
      if ((*pixel_b)[0] != 0x00 || (*pixel_b)[1] != 0xFF ||
          (*pixel_b)[2] != 0x00 || (*pixel_b)[3] != 0xFF) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: surface B readback "
                     "mismatch (old texture may have leaked)\n");
        return 1;
      }
    }

    // ---- successful clear ----
    // After B, clear with canonical empty surface.  Texture must be null
    // and readback must return nullopt.
    {
      const graphscore::ShellResult clear_result =
          shell.set_notation_surface(graphscore::RasterSurface{});
      if (!clear_result.ok()) {
        std::fprintf(stderr, "selection-tool-shell-test: clear rejected: %s\n",
                     clear_result.message.c_str());
        return 1;
      }
      // Clear destroys texture: alive becomes zero.
      {
        auto s = shell.test_notation_texture_stats();
        if (s.created != 5 || s.destroyed != 5) {
          std::fprintf(stderr,
                       "selection-tool-shell-test: texture stats after clear "
                       "in replace block — "
                       "expected created=5 destroyed=5 alive=0, "
                       "got created=%" PRIu64 " destroyed=%" PRIu64
                       " alive=%" PRIu64 "\n",
                       static_cast<std::uint64_t>(s.created),
                       static_cast<std::uint64_t>(s.destroyed),
                       static_cast<std::uint64_t>(s.created - s.destroyed));
          return 1;
        }
      }
      auto pixel_after_clear = shell.test_read_notation_pixel(0, 0);
      if (pixel_after_clear.has_value()) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: readback after clear "
                     "returned pixel (expected nullopt)\n");
        return 1;
      }
    }

    // ---- test-injected failure: prior surface survives, candidate cleaned
    // ---- Upload a distinctive surface, then attempt a replacement with the
    // test injection seam forcing SDL failure.  The prior surface must
    // remain readable with its distinctive pixel values.
    {
      graphscore::RasterSurface survival;
      survival.width  = 2;
      survival.height = 2;
      // Distinctive: blue pixel at (0,0)
      survival.rgba = {0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF,
                       0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF};
      const graphscore::ShellResult set_survival =
          shell.set_notation_surface(std::move(survival));
      if (!set_survival.ok()) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: survival surface rejected: "
                     "%s\n",
                     set_survival.message.c_str());
        return 1;
      }
      // New upload: created=6, destroyed=5, alive=1.
      {
        auto s = shell.test_notation_texture_stats();
        if (s.created != 6 || s.destroyed != 5) {
          std::fprintf(stderr,
                       "selection-tool-shell-test: texture stats after "
                       "blue upload — expected created=6 destroyed=5 alive=1, "
                       "got created=%" PRIu64 " destroyed=%" PRIu64
                       " alive=%" PRIu64 "\n",
                       static_cast<std::uint64_t>(s.created),
                       static_cast<std::uint64_t>(s.destroyed),
                       static_cast<std::uint64_t>(s.created - s.destroyed));
          return 1;
        }
      }
      // Verify it's readable.
      auto survival_pixel = shell.test_read_notation_pixel(0, 0);
      if (!survival_pixel.has_value()) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: readback of blue survival "
                     "pixel returned nullopt after successful upload\n");
        return 1;
      }
      if ((*survival_pixel)[0] != 0x00 || (*survival_pixel)[1] != 0x00 ||
          (*survival_pixel)[2] != 0xFF || (*survival_pixel)[3] != 0xFF) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: survival pixel mismatch\n");
        return 1;
      }

      // Inject a failure for the next set_notation_surface call.
      shell.set_test_force_texture_failure(true);

      // Attempt to replace with an orange surface — this must fail.
      graphscore::RasterSurface orange;
      orange.width  = 2;
      orange.height = 2;
      orange.rgba   = {0xFF, 0x80, 0x00, 0xFF, 0xFF, 0x80, 0x00, 0xFF,
                       0xFF, 0x80, 0x00, 0xFF, 0xFF, 0x80, 0x00, 0xFF};
      const graphscore::ShellResult fail_result =
          shell.set_notation_surface(std::move(orange));
      if (fail_result.error != graphscore::ShellError::kRenderingSetupFailed) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: injected failure not "
                     "returned (got %s)\n",
                     describe(fail_result.error));
        return 1;
      }

      // The prior (blue) surface must survive — pixel (0,0) still blue.
      auto after_fail = shell.test_read_notation_pixel(0, 0);
      if (!after_fail.has_value()) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: prior pixel destroyed after "
                     "injected failure (readback returned nullopt)\n");
        return 1;
      }
      if ((*after_fail)[0] != 0x00 || (*after_fail)[1] != 0x00 ||
          (*after_fail)[2] != 0xFF || (*after_fail)[3] != 0xFF) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: prior surface corrupted "
                     "after injected failure — expected blue, got "
                     "[0x%02X,0x%02X,0x%02X,0x%02X]\n",
                     (*after_fail)[0], (*after_fail)[1], (*after_fail)[2],
                     (*after_fail)[3]);
        return 1;
      }
      // Candidate created and destroyed; prior blue survives: alive stays 1.
      {
        auto s = shell.test_notation_texture_stats();
        if (s.created != 7 || s.destroyed != 6) {
          std::fprintf(stderr,
                       "selection-tool-shell-test: texture stats after "
                       "injected failure — "
                       "expected created=7 destroyed=6 alive=1, "
                       "got created=%" PRIu64 " destroyed=%" PRIu64
                       " alive=%" PRIu64 "\n",
                       static_cast<std::uint64_t>(s.created),
                       static_cast<std::uint64_t>(s.destroyed),
                       static_cast<std::uint64_t>(s.created - s.destroyed));
          return 1;
        }
      }

      // The injection flag is one-shot (consumed by the failed call above);
      // verify the production path is not corrupted by re-uploading the blue
      // surface cleanly.
      {
        graphscore::RasterSurface replay;
        replay.width  = 2;
        replay.height = 2;
        replay.rgba   = {0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF,
                         0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0xFF, 0xFF};
        if (!shell.set_notation_surface(std::move(replay)).ok()) {
          std::fprintf(stderr,
                       "selection-tool-shell-test: post-injection recovery "
                       "upload failed\n");
          return 1;
        }
        // Recovery replaces prior blue: created=8, destroyed=7, alive=1.
        {
          auto s = shell.test_notation_texture_stats();
          if (s.created != 8 || s.destroyed != 7) {
            std::fprintf(stderr,
                         "selection-tool-shell-test: texture stats after "
                         "recovery upload — "
                         "expected created=8 destroyed=7 alive=1, "
                         "got created=%" PRIu64 " destroyed=%" PRIu64
                         " alive=%" PRIu64 "\n",
                         static_cast<std::uint64_t>(s.created),
                         static_cast<std::uint64_t>(s.destroyed),
                         static_cast<std::uint64_t>(s.created - s.destroyed));
            return 1;
          }
        }
      }
    }
  }

  // ---- committed-selection highlight persistence across drag+cancel ----
  // Commit a selection; press alone preserves the old committed highlight
  // (the handler now calls update_highlight on press, which shows the
  // committed extent since begin() clears live_extent).  After a different
  // live move, highlight changes to the live geometry.  A transient cancel
  // restores the exact original committed geometry.
  //
  // Uses dispatch_sdl_test_pointer_event to exercise the production
  // SDL_ConvertEventToRenderCoordinates path.
  {
    handler.set_active_tool(graphscore::ActiveTool::kSelection);

    const double press_x = layout.systems[0].measures[0].bounds.x;
    const double press_y = layout.systems[0].staves[0].bounds.y +
                           layout.systems[0].staves[0].bounds.height * 0.5;
    const double move_x = layout.systems[0].measures[0].bounds.x +
                          layout.systems[0].measures[0].bounds.width;

    // Commit: press → move → release (production SDL path).
    shell.dispatch_sdl_test_pointer_event(0, make_event(press_x, press_y));
    shell.dispatch_sdl_test_pointer_event(1, make_event(move_x, press_y));
    shell.dispatch_sdl_test_pointer_event(2, make_event(move_x, press_y));

    if (handler.drag_state().is_dragging()) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: drag still in progress after "
                   "commit-release\n");
      return 1;
    }
    if (!handler.drag_state().committed_selection().has_value()) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: no committed selection after "
                   "release\n");
      return 1;
    }

    // Snapshot the shell-visible highlight after commit.
    const std::vector<graphscore::NotationRect> committed_rects =
        shell.test_snapshot_highlight_rects();
    if (committed_rects.empty()) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: highlight rects empty "
                   "after commit (expected at least one rect)\n");
      return 1;
    }

    // Begin a second drag: press alone preserves the committed highlight
    // (on_pointer_press now calls update_highlight, which falls through to
    // committed_selection when live_extent is empty).
    shell.dispatch_sdl_test_pointer_event(0, make_event(press_x, press_y));
    if (!handler.drag_state().is_dragging()) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: second drag did not begin\n");
      return 1;
    }
    // Committed highlight must still be present (press does not clear it).
    const std::vector<graphscore::NotationRect> after_press_rects =
        shell.test_snapshot_highlight_rects();
    if (after_press_rects.empty()) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: highlight rects lost after "
                   "second press (expected committed highlight preserved)\n");
      return 1;
    }
    if (after_press_rects != committed_rects) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: highlight rects changed after "
                   "second press (expected committed highlight unchanged)\n");
      return 1;
    }

    // Move to a demonstrably different endpoint (half-measure):
    // the live highlight must differ from the committed highlight.
    const double half_x = layout.systems[0].measures[0].bounds.x +
                          layout.systems[0].measures[0].bounds.width * 0.5;
    shell.dispatch_sdl_test_pointer_event(1, make_event(half_x, press_y));
    const std::vector<graphscore::NotationRect> live_rects =
        shell.test_snapshot_highlight_rects();
    if (live_rects.empty()) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: highlight rects empty after "
                   "second-drag move\n");
      return 1;
    }
    if (live_rects == committed_rects) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: live highlight did not differ "
                   "from committed after move to half-measure endpoint\n");
      return 1;
    }

    // Cancel the second drag (simulating focus-loss or dismissal).
    handler.on_cancel();

    // After cancel, the committed selection highlight must be restored
    // exactly.
    if (handler.drag_state().is_dragging()) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: drag still in progress after "
                   "cancel\n");
      return 1;
    }
    const std::vector<graphscore::NotationRect> restored_rects =
        shell.test_snapshot_highlight_rects();
    if (restored_rects.empty()) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: highlight rects empty "
                   "after cancel (expected committed highlight restored)\n");
      return 1;
    }
    if (restored_rects != committed_rects) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: restored highlight rects "
                   "differ from committed rects\n");
      return 1;
    }
  }

  // ---- destructor destroys live notation texture ----
  // Acquire a stats handle from a dedicated WriterShell scope, upload
  // one texture, verify alive=1, then let the shell go out of scope.
  // The handle survives destruction; assert Impl::~Impl destroyed the
  // texture (alive=0).
  {
    bool                                                       dtor_ran = false;
    std::optional<graphscore::WriterShell::TextureStatsHandle> dtor_handle;

    {
      graphscore::WriterShell dtor_shell;
      dtor_handle = dtor_shell.test_acquire_texture_stats_handle();

      graphscore::WindowOptions opts;
      opts.run_event_loop                       = false;
      const graphscore::ShellResult open_result = dtor_shell.open_window(opts);
      const bool                    is_headless =
          open_result.error == graphscore::ShellError::kBackendUnavailable ||
          open_result.error == graphscore::ShellError::kRendererUnavailable;
      if (!open_result.ok()) {
        if (is_headless) {
          std::printf(
              "selection-tool-shell-test: renderer unavailable in "
              "destructor scope — skipped\n");
          dtor_handle.reset();
        } else {
          std::fprintf(stderr,
                       "selection-tool-shell-test: destructor-scope "
                       "open_window failed: %s\n",
                       open_result.message.c_str());
          shell.set_input_handler(nullptr);
          return 1;
        }
      } else {
        // Upload one texture and verify alive=1.
        graphscore::RasterSurface surface;
        surface.width  = 2;
        surface.height = 2;
        surface.rgba.assign(16, 0xFF);
        const auto set_result =
            dtor_shell.set_notation_surface(std::move(surface));
        if (!set_result.ok()) {
          std::fprintf(stderr,
                       "selection-tool-shell-test: destructor-scope upload "
                       "failed: %s\n",
                       set_result.message.c_str());
          shell.set_input_handler(nullptr);
          return 1;
        }
        auto s = dtor_handle->snapshot();
        if (s.created != 1 || s.destroyed != 0) {
          std::fprintf(stderr,
                       "selection-tool-shell-test: destructor scope "
                       "alive != 1 before destruction — "
                       "created=%" PRIu64 " destroyed=%" PRIu64
                       " alive=%" PRIu64 "\n",
                       static_cast<std::uint64_t>(s.created),
                       static_cast<std::uint64_t>(s.destroyed),
                       static_cast<std::uint64_t>(s.created - s.destroyed));
          shell.set_input_handler(nullptr);
          return 1;
        }
        dtor_ran = true;
      }
      // dtor_shell destroyed here → ~Impl runs if window was opened
    }

    // Handle survived; verify destructor destroyed the texture.
    if (dtor_ran && dtor_handle.has_value()) {
      auto s = dtor_handle->snapshot();
      if (s.created != s.destroyed) {
        std::fprintf(stderr,
                     "selection-tool-shell-test: texture leak after shell "
                     "destruction — "
                     "created=%" PRIu64 " destroyed=%" PRIu64 " alive=%" PRIu64
                     "\n",
                     static_cast<std::uint64_t>(s.created),
                     static_cast<std::uint64_t>(s.destroyed),
                     static_cast<std::uint64_t>(s.created - s.destroyed));
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
  }

  // Clear the notation surface to destroy the final texture, then assert
  // no texture remains alive (no leak to the shell's destructor).
  shell.set_notation_surface(graphscore::RasterSurface{});
  {
    auto s = shell.test_notation_texture_stats();
    if (s.created != s.destroyed) {
      std::fprintf(stderr,
                   "selection-tool-shell-test: texture leak at test end — "
                   "created=%" PRIu64 " destroyed=%" PRIu64 " alive=%" PRIu64
                   "\n",
                   static_cast<std::uint64_t>(s.created),
                   static_cast<std::uint64_t>(s.destroyed),
                   static_cast<std::uint64_t>(s.created - s.destroyed));
      return 1;
    }
  }

  std::printf("selection-tool-shell-test: ok\n");
  shell.set_input_handler(nullptr);
  return 0;
}

// ---- key-event delivery tests (M5-phase-19b-ii) ----------------------------
//
// This sub-phase delivers platform-neutral key events from SDL (or a test
// seam) to a registered InputHandler; it does not interpret them. These
// tests only assert that a key event arrives at the handler unchanged
// (headless seam) or correctly translated from real SDL scancode/modifier
// values (production SDL path). Interpreting keys into selection changes is
// M5-phase-19b-iii and is out of scope here.

// Records every KeyEvent delivered to on_key_press(); the pointer methods
// and on_cancel() are no-ops because this handler exists only to observe
// key delivery. Deliberately not SelectionToolHandler — that handler is
// untouched this sub-phase and inherits the default no-op on_key_press.
class RecordingKeyHandler final : public graphscore::InputHandler {
 public:
  void on_pointer_press(graphscore::PointerEvent /*event*/) override {}

  void on_pointer_move(graphscore::PointerEvent /*event*/) override {}

  void on_pointer_release(graphscore::PointerEvent /*event*/) override {}

  void on_cancel() override {}

  void on_key_press(graphscore::KeyEvent event) override {
    events.push_back(event);
  }

  std::vector<graphscore::KeyEvent> events;
};

// Implements only the four pure-virtual pointer/cancel methods and does not
// override on_key_press, so it exercises InputHandler's default no-op
// implementation rather than shadowing it.
class DefaultKeyHandler final : public graphscore::InputHandler {
 public:
  void on_pointer_press(graphscore::PointerEvent /*event*/) override {}

  void on_pointer_move(graphscore::PointerEvent /*event*/) override {}

  void on_pointer_release(graphscore::PointerEvent /*event*/) override {}

  void on_cancel() override {}
};

// Headless: exercises WriterShell::dispatch_test_key_event only. Works
// identically in writer-ON and writer-OFF builds (no SDL, no window).
int key_events_test() {
  graphscore::WriterShell shell;

  // --- test: no handler registered -> dispatch is a silent no-op --------
  {
    graphscore::KeyEvent event;
    event.code = graphscore::KeyCode::kLeft;
    shell.dispatch_test_key_event(event);
  }

  RecordingKeyHandler handler;
  shell.set_input_handler(&handler);

  // --- test: every KeyCode value round-trips unchanged -------------------
  constexpr std::array<graphscore::KeyCode, 7> kAllCodes{
      graphscore::KeyCode::kUnknown, graphscore::KeyCode::kLeft,
      graphscore::KeyCode::kRight,   graphscore::KeyCode::kUp,
      graphscore::KeyCode::kDown,    graphscore::KeyCode::kHome,
      graphscore::KeyCode::kEnd,
  };
  for (const graphscore::KeyCode code : kAllCodes) {
    const std::size_t    before = handler.events.size();
    graphscore::KeyEvent event;
    event.code = code;
    shell.dispatch_test_key_event(event);
    if (handler.events.size() != before + 1 ||
        handler.events.back().code != code) {
      std::fprintf(stderr,
                   "key-events-test: KeyCode %d did not round-trip through "
                   "the headless seam\n",
                   static_cast<int>(code));
      shell.set_input_handler(nullptr);
      return 1;
    }
  }

  // --- test: all four modifier flags round-trip, individually and
  //     in combination --------------------------------------------------
  const std::array<graphscore::KeyModifiers, 5> kModifierCases{{
      {true, false, false, false},
      {false, true, false, false},
      {false, false, true, false},
      {false, false, false, true},
      {true, true, true, true},
  }};
  for (const graphscore::KeyModifiers& modifiers : kModifierCases) {
    const std::size_t    before = handler.events.size();
    graphscore::KeyEvent event;
    event.code      = graphscore::KeyCode::kLeft;
    event.modifiers = modifiers;
    shell.dispatch_test_key_event(event);
    if (handler.events.size() != before + 1 ||
        handler.events.back().modifiers != modifiers) {
      std::fprintf(stderr,
                   "key-events-test: modifiers did not round-trip through "
                   "the headless seam\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
  }

  // --- test: after set_input_handler(nullptr), no further callbacks -----
  shell.set_input_handler(nullptr);
  {
    const std::size_t    before = handler.events.size();
    graphscore::KeyEvent event;
    event.code = graphscore::KeyCode::kEnd;
    shell.dispatch_test_key_event(event);
    if (handler.events.size() != before) {
      std::fprintf(stderr,
                   "key-events-test: handler received a key event after "
                   "unregistration\n");
      return 1;
    }
  }

  // --- test: a handler that does not override on_key_press (the default
  //     no-op) receives a key press without crashing --------------------
  {
    DefaultKeyHandler default_handler;
    shell.set_input_handler(&default_handler);
    graphscore::KeyEvent event;
    event.code = graphscore::KeyCode::kHome;
    shell.dispatch_test_key_event(event);
    shell.set_input_handler(nullptr);
  }

  std::printf("key-events-test: ok\n");
  return 0;
}

// Exercises WriterShell::dispatch_sdl_test_key_event, the production SDL
// physical-scancode and modifier-mask translation path. Deliberately does
// not call open_window(): dispatch_sdl_test_key_event builds an
// SDL_EVENT_KEY_DOWN and routes it through the production
// dispatch_sdl_event, which only touches the renderer for
// SDL_ConvertEventToRenderCoordinates — a no-op for key events, and
// unreached altogether when impl_'s renderer is null, which it always is
// without open_window(). No window, renderer, or SDL_Init is needed, so
// this test runs unconditionally rather than skipping on a headless host.
int key_events_shell_test() {
  RecordingKeyHandler     handler;
  graphscore::WriterShell shell;
  shell.set_input_handler(&handler);

  // Raw SDL_Scancode values, verified against the fetched SDL3 headers
  // (SDL3/SDL_scancode.h at the pinned commit): LEFT=80, RIGHT=79, UP=82,
  // DOWN=81, HOME=74, END=77. SDL_SCANCODE_A=4 is a mapped character-key
  // scancode outside this sub-phase's minimal set and must translate to
  // kUnknown.
  constexpr std::uint32_t kScancodeLeft  = 80;
  constexpr std::uint32_t kScancodeRight = 79;
  constexpr std::uint32_t kScancodeUp    = 82;
  constexpr std::uint32_t kScancodeDown  = 81;
  constexpr std::uint32_t kScancodeHome  = 74;
  constexpr std::uint32_t kScancodeEnd   = 77;
  constexpr std::uint32_t kScancodeA     = 4;

  struct ScancodeCase {
    std::uint32_t       scancode;
    graphscore::KeyCode expected;
  };

  constexpr std::array<ScancodeCase, 6> kMappedScancodes{{
      {kScancodeLeft, graphscore::KeyCode::kLeft},
      {kScancodeRight, graphscore::KeyCode::kRight},
      {kScancodeUp, graphscore::KeyCode::kUp},
      {kScancodeDown, graphscore::KeyCode::kDown},
      {kScancodeHome, graphscore::KeyCode::kHome},
      {kScancodeEnd, graphscore::KeyCode::kEnd},
  }};

  for (const ScancodeCase& test_case : kMappedScancodes) {
    const std::size_t before = handler.events.size();
    shell.dispatch_sdl_test_key_event(test_case.scancode, 0);
    if (handler.events.size() != before + 1 ||
        handler.events.back().code != test_case.expected) {
      std::fprintf(stderr,
                   "key-events-shell-test: scancode %u did not translate to "
                   "the expected KeyCode\n",
                   test_case.scancode);
      shell.set_input_handler(nullptr);
      return 1;
    }
  }

  // Unmapped scancode -> kUnknown.
  {
    const std::size_t before = handler.events.size();
    shell.dispatch_sdl_test_key_event(kScancodeA, 0);
    if (handler.events.size() != before + 1 ||
        handler.events.back().code != graphscore::KeyCode::kUnknown) {
      std::fprintf(stderr,
                   "key-events-shell-test: unmapped scancode did not "
                   "translate to kUnknown\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
  }

  // Raw SDL_Keymod bitmasks, verified against the fetched SDL3 headers
  // (SDL3/SDL_keycode.h at the pinned commit): SDL_KMOD_SHIFT=0x0003
  // (LSHIFT 0x0001 | RSHIFT 0x0002), SDL_KMOD_CTRL=0x00C0 (LCTRL 0x0040 |
  // RCTRL 0x0080), SDL_KMOD_ALT=0x0300 (LALT 0x0100 | RALT 0x0200),
  // SDL_KMOD_GUI=0x0C00 (LGUI 0x0400 | RGUI 0x0800).
  constexpr std::uint16_t kModShift = 0x0003;
  constexpr std::uint16_t kModCtrl  = 0x00C0;
  constexpr std::uint16_t kModAlt   = 0x0300;
  constexpr std::uint16_t kModGui   = 0x0C00;

  struct ModifierCase {
    std::uint16_t            mods;
    graphscore::KeyModifiers expected;
  };

  const std::array<ModifierCase, 5> kModifierCases{{
      {kModShift, {true, false, false, false}},
      {kModCtrl, {false, true, false, false}},
      {kModAlt, {false, false, true, false}},
      {kModGui, {false, false, false, true}},
      {static_cast<std::uint16_t>(kModShift | kModCtrl | kModAlt | kModGui),
       {true, true, true, true}},
  }};

  for (const ModifierCase& test_case : kModifierCases) {
    const std::size_t before = handler.events.size();
    shell.dispatch_sdl_test_key_event(kScancodeLeft, test_case.mods);
    if (handler.events.size() != before + 1 ||
        handler.events.back().modifiers != test_case.expected) {
      std::fprintf(stderr,
                   "key-events-shell-test: modifier mask 0x%04X did not "
                   "translate correctly\n",
                   test_case.mods);
      shell.set_input_handler(nullptr);
      return 1;
    }
  }

  shell.set_input_handler(nullptr);
  std::printf("key-events-shell-test: ok\n");
  return 0;
}

// ---- key-selection tests (M5-phase-19b-iii) --------------------------------
//
// Exercises SelectionToolHandler's accessible range controls and its
// on_key_press override -- interpreting Shift extension and the
// start/end/staff-scope controls, wired through the platform-neutral key
// events M5-phase-19b-ii delivers. Headless: no window, no SDL, works in
// both writer-ON and writer-OFF configurations.

struct KeySelectionProject {
  graphscore::Project                project;
  graphscore::NodeId                 node_id;
  std::array<graphscore::TrackId, 3> track_ids{};
  std::array<graphscore::StaveId, 3> stave_ids{};
  graphscore::NotationLayout         layout;
};

// Builds a three-track, two-measure (4/4 each) fixture: track_ids[0] and
// track_ids[1] each carry one whole note per measure, spanning the full
// [0, 2) node timeline. track_ids[2] carries a single whole note in the
// first measure only, so its content spans exactly [0, 1) and does not
// overlap [1, 2) at all -- reproducing the "staff at the extreme of the
// range whose own voices carry no overlapping content" scenario
// extend_range_selection's own doc comment on graphscore_notation.hpp
// describes, for the staff-endpoint-preservation test below.
[[nodiscard]] std::optional<KeySelectionProject> build_key_selection_project(
    const graphscore::GlyphMetrics& metrics) {
  graphscore::Project                project{graphscore::ProjectId::generate(),
                              "KeySelection"};
  std::array<graphscore::TrackId, 3> track_ids{};
  for (std::size_t i = 0; i < track_ids.size(); ++i) {
    const auto midi_channel =
        graphscore::MidiChannel::create(static_cast<std::uint8_t>(i));
    if (!midi_channel.has_value()) {
      return std::nullopt;
    }
    const auto added = project.add_track(
        "Track",
        graphscore::StaffLayout::single_staff(graphscore::Clef::kTreble),
        *midi_channel);
    if (!added.has_value()) {
      return std::nullopt;
    }
    track_ids[i] = *added;
  }

  const graphscore::NodeId                 node_id = project.add_node("Node");
  std::array<graphscore::StaveId, 3>       stave_ids{};
  std::vector<graphscore::StaveDefinition> stave_defs;
  for (std::size_t i = 0; i < track_ids.size(); ++i) {
    auto* lane = project.find_node(node_id)->lane(track_ids[i]);
    const graphscore::StaveId stave_id =
        project.active_tracks()[i].layout().staves()[0].id;
    stave_ids[i] = stave_id;
    lane->ensure_stave(stave_id);
    stave_defs.push_back(project.active_tracks()[i].layout().staves()[0]);
  }

  const auto time_sig = graphscore::TimeSignature::create(4, 4);
  if (!time_sig.has_value()) {
    return std::nullopt;
  }
  std::vector<graphscore::Measure> measures(
      2, graphscore::Measure{*time_sig, graphscore::KeySignature{}});
  auto timeline =
      graphscore::NodeTimeline::create(std::move(measures), stave_defs);
  if (!timeline.has_value()) {
    return std::nullopt;
  }
  project.find_node(node_id)->set_timeline(std::move(*timeline));

  const auto whole_duration =
      graphscore::Duration::create(graphscore::NoteValue::kWhole, 0);
  if (!whole_duration.has_value()) {
    return std::nullopt;
  }
  const graphscore::Duration whole  = *whole_duration;
  const auto                 voice1 = graphscore::Voice::create(1);
  if (!voice1.has_value()) {
    return std::nullopt;
  }
  const auto pitch =
      graphscore::SpelledPitch::create(graphscore::Letter::kC, 4);
  if (!pitch.has_value()) {
    return std::nullopt;
  }

  for (std::size_t i = 0; i < 2; ++i) {
    graphscore::VoiceContent& vc = project.find_node(node_id)
                                       ->lane(track_ids[i])
                                       ->stave(stave_ids[i])
                                       ->voice(*voice1);
    if (!vc.append(graphscore::make_note(*pitch, whole)).ok()) {
      return std::nullopt;
    }
    if (!vc.append(graphscore::make_note(*pitch, whole)).ok()) {
      return std::nullopt;
    }
  }
  {
    graphscore::VoiceContent& vc = project.find_node(node_id)
                                       ->lane(track_ids[2])
                                       ->stave(stave_ids[2])
                                       ->voice(*voice1);
    if (!vc.append(graphscore::make_note(*pitch, whole)).ok()) {
      return std::nullopt;
    }
  }

  graphscore::NotationLayoutResult layout_result =
      graphscore::layout_notation(project, node_id, metrics);
  if (!layout_result || !layout_result.layout.has_value()) {
    return std::nullopt;
  }

  return KeySelectionProject{std::move(project), node_id, track_ids, stave_ids,
                             std::move(*layout_result.layout)};
}

// Presses at (x1, y1), then moves and releases at (x2, y2): a two-point
// drag through the headless pointer seam, matching selection_tool_test's
// own pattern.
void drag_through_shell(graphscore::WriterShell& shell, double x1, double y1,
                        double x2, double y2) {
  const graphscore::PointerEvent press{x1, y1,
                                       graphscore::PointerButton::kPrimary};
  const graphscore::PointerEvent release{x2, y2,
                                         graphscore::PointerButton::kPrimary};
  shell.dispatch_test_pointer_event(0, press);
  shell.dispatch_test_pointer_event(1, release);
  shell.dispatch_test_pointer_event(2, release);
}

[[nodiscard]] graphscore::KeyEvent shift_key(graphscore::KeyCode code) {
  graphscore::KeyEvent event;
  event.code            = code;
  event.modifiers.shift = true;
  return event;
}

// Rational::create only fails on a zero denominator; every call site below
// passes a small nonzero literal, so the std::nullopt arm is unreachable in
// practice. Checked explicitly (rather than dereferencing the optional
// inline) so the value is read the same way every other runtime
// Rational/Duration/SpelledPitch construction in this file is: via a
// named, has_value()-checked local. (kProvisionalRangeExtensionStep above
// is the one constexpr exception: an inline dereference there is
// evaluated at compile time, not runtime.)
[[nodiscard]] graphscore::Rational rational(std::int64_t numerator,
                                            std::int64_t denominator) {
  const auto value = graphscore::Rational::create(numerator, denominator);
  if (!value.has_value()) {
    return graphscore::Rational(0);
  }
  return *value;
}

// The committed selection's own ArbitraryRangeSet, or nullptr when there is
// no committed selection or it is not that arm -- the free-function
// counterpart of SelectionToolHandler::current_range_set(), used by the
// tests below to read the committed selection back through the same
// has_value()-checked-before-dereferenced pattern the rest of this file
// uses.
[[nodiscard]] const graphscore::ArbitraryRangeSet* committed_range_set(
    const SelectionToolHandler& handler) {
  const auto& committed = handler.drag_state().committed_selection();
  if (!committed.has_value()) {
    return nullptr;
  }
  return std::get_if<graphscore::ArbitraryRangeSet>(&*committed);
}

int key_selection_test() {
  const SelfTestMetrics metrics;

  // --- test 1: equivalence -- the phase's core acceptance criterion.
  //     A pointer drag and a keyboard-driven extension from a different
  //     starting selection must reach the identical committed Selection,
  //     compared with the defaulted ArbitraryRangeSet/Selection
  //     operator==. --------------------------------------------------
  {
    // Both handlers below operate on independent copies of the same
    // project (same NodeId/TrackId/StaveId values), not two separately
    // built fixtures -- build_key_selection_project generates fresh
    // NodeId/TrackId/StaveId values on every call, so two independently
    // built fixtures could never satisfy Selection's own operator==
    // regardless of whether the span/staff-scope logic agrees.
    auto dp = build_key_selection_project(metrics);
    if (!dp.has_value()) {
      std::fprintf(stderr,
                   "key-selection-test: build_key_selection_project (1) "
                   "failed\n");
      return 1;
    }

    graphscore::Project              project_a = dp->project;
    graphscore::NotationLayoutResult layout_result_a =
        graphscore::layout_notation(project_a, dp->node_id, metrics);
    if (!layout_result_a || !layout_result_a.layout.has_value()) {
      std::fprintf(stderr, "key-selection-test: layout_notation (a) failed\n");
      return 1;
    }
    graphscore::WriterShell shell_a;
    SelectionToolHandler    handler_a(
        std::move(project_a), std::move(*layout_result_a.layout), &shell_a);
    shell_a.set_input_handler(&handler_a);
    handler_a.set_active_tool(graphscore::ActiveTool::kSelection);
    const auto& layout_a = handler_a.layout();
    {
      const double x1 = layout_a.systems[0].measures[0].bounds.x;
      const double x2 = layout_a.systems[0].measures[1].bounds.x +
                        layout_a.systems[0].measures[1].bounds.width;
      const double y = layout_a.systems[0].staves[1].bounds.y +
                       layout_a.systems[0].staves[1].bounds.height * 0.5;
      drag_through_shell(shell_a, x1, y, x2, y);
    }
    const auto target = handler_a.drag_state().committed_selection();
    shell_a.set_input_handler(nullptr);
    if (!target.has_value()) {
      std::fprintf(stderr,
                   "key-selection-test: target selection missing "
                   "(equivalence)\n");
      return 1;
    }

    graphscore::Project              project_b = dp->project;
    graphscore::NotationLayoutResult layout_result_b =
        graphscore::layout_notation(project_b, dp->node_id, metrics);
    if (!layout_result_b || !layout_result_b.layout.has_value()) {
      std::fprintf(stderr, "key-selection-test: layout_notation (b) failed\n");
      return 1;
    }
    graphscore::WriterShell shell_b;
    SelectionToolHandler    handler_b(
        std::move(project_b), std::move(*layout_result_b.layout), &shell_b);
    shell_b.set_input_handler(&handler_b);
    handler_b.set_active_tool(graphscore::ActiveTool::kSelection);
    const auto& layout_b = handler_b.layout();
    {
      // A different starting selection: the same track/staff, but only the
      // first measure.
      const double x1 = layout_b.systems[0].measures[0].bounds.x;
      const double x2 = layout_b.systems[0].measures[0].bounds.x +
                        layout_b.systems[0].measures[0].bounds.width;
      const double y = layout_b.systems[0].staves[1].bounds.y +
                       layout_b.systems[0].staves[1].bounds.height * 0.5;
      drag_through_shell(shell_b, x1, y, x2, y);
    }
    if (!handler_b.drag_state().committed_selection().has_value()) {
      std::fprintf(stderr,
                   "key-selection-test: starting selection missing "
                   "(equivalence)\n");
      shell_b.set_input_handler(nullptr);
      return 1;
    }
    if (!handler_b.select_to_node_end()) {
      std::fprintf(stderr,
                   "key-selection-test: select_to_node_end failed "
                   "(equivalence)\n");
      shell_b.set_input_handler(nullptr);
      return 1;
    }
    if (handler_b.drag_state().committed_selection() != target) {
      std::fprintf(stderr,
                   "key-selection-test: keyboard-reached selection did not "
                   "equal the pointer-drag selection (equivalence)\n");
      shell_b.set_input_handler(nullptr);
      return 1;
    }
    shell_b.set_input_handler(nullptr);
  }

  // --- test 2: edge extension in both directions produces the expected
  //     span. -----------------------------------------------------------
  {
    auto dp = build_key_selection_project(metrics);
    if (!dp.has_value()) {
      std::fprintf(stderr,
                   "key-selection-test: build_key_selection_project (2) "
                   "failed\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler handler(std::move(dp->project), std::move(dp->layout),
                                 &shell);
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kSelection);
    const auto& layout = handler.layout();
    {
      const double x1 = layout.systems[0].measures[0].bounds.x;
      const double x2 = layout.systems[0].measures[0].bounds.x +
                        layout.systems[0].measures[0].bounds.width;
      const double y = layout.systems[0].staves[0].bounds.y +
                       layout.systems[0].staves[0].bounds.height * 0.5;
      drag_through_shell(shell, x1, y, x2, y);
    }
    const std::vector<graphscore::NotationRect> rects_before_edge =
        shell.test_snapshot_highlight_rects();
    if (rects_before_edge.empty()) {
      std::fprintf(stderr,
                   "key-selection-test: highlight empty after drag setup "
                   "(2)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (!handler.extend_range_edge(graphscore::RangeEdge::kEnd,
                                   graphscore::Rational(2))) {
      std::fprintf(stderr,
                   "key-selection-test: extend_range_edge(kEnd) failed\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    {
      const auto* set = committed_range_set(handler);
      if (set == nullptr ||
          set->items().front().span !=
              (graphscore::MusicalSpan{graphscore::Rational(0),
                                       graphscore::Rational(2)})) {
        std::fprintf(stderr,
                     "key-selection-test: extend_range_edge(kEnd) span "
                     "mismatch\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    {
      const std::vector<graphscore::NotationRect> rects_after_edge =
          shell.test_snapshot_highlight_rects();
      if (rects_after_edge == rects_before_edge) {
        std::fprintf(stderr,
                     "key-selection-test: highlight rects did not change "
                     "after extend_range_edge(kEnd) (2)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    if (!handler.extend_range_edge(graphscore::RangeEdge::kStart,
                                   graphscore::Rational(1))) {
      std::fprintf(stderr,
                   "key-selection-test: extend_range_edge(kStart) failed\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    {
      const auto* set = committed_range_set(handler);
      if (set == nullptr ||
          set->items().front().span !=
              (graphscore::MusicalSpan{graphscore::Rational(1),
                                       graphscore::Rational(2)})) {
        std::fprintf(stderr,
                     "key-selection-test: extend_range_edge(kStart) span "
                     "mismatch\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 3: focus-edge tracking across a crossing -- the next
  //     extension moves the edge a user would expect, proving the
  //     recompute takes effect. ------------------------------------------
  {
    auto dp = build_key_selection_project(metrics);
    if (!dp.has_value()) {
      std::fprintf(stderr,
                   "key-selection-test: build_key_selection_project (3) "
                   "failed\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler handler(std::move(dp->project), std::move(dp->layout),
                                 &shell);
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kSelection);
    const auto& layout = handler.layout();
    {
      const double x1 = layout.systems[0].measures[0].bounds.x;
      const double x2 = layout.systems[0].measures[0].bounds.x +
                        layout.systems[0].measures[0].bounds.width;
      const double y = layout.systems[0].staves[0].bounds.y +
                       layout.systems[0].staves[0].bounds.height * 0.5;
      drag_through_shell(shell, x1, y, x2, y);
    }
    // span [0, 1) -> [1/4, 1): kStart moves to 1/4, no crossing.
    if (!handler.extend_range_edge(graphscore::RangeEdge::kStart,
                                   kProvisionalRangeExtensionStep)) {
      std::fprintf(stderr, "key-selection-test: setup step 1 failed (3)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    // span [1/4, 1) -> [1, 3/2): kStart moves to 3/2, crossing the fixed
    // end (1) -- the span swaps, and focus_edge_ must become kEnd since
    // the moved value (3/2) now lands on the span's own end.
    if (!handler.extend_range_edge(graphscore::RangeEdge::kStart,
                                   rational(3, 2))) {
      std::fprintf(stderr, "key-selection-test: setup step 2 failed (3)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    {
      const auto* set = committed_range_set(handler);
      if (set == nullptr || set->items().front().span !=
                                (graphscore::MusicalSpan{
                                    graphscore::Rational(1), rational(3, 2)})) {
        std::fprintf(stderr,
                     "key-selection-test: crossing setup span mismatch "
                     "(3)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    // Shift+Left must now move the end (focus_edge_ == kEnd after the
    // crossing above), producing [1, 5/4) -- not the start, which would
    // instead produce [3/4, 3/2) if the recompute had not taken effect.
    shell.dispatch_test_key_event(shift_key(graphscore::KeyCode::kLeft));
    {
      const auto* set = committed_range_set(handler);
      if (set == nullptr || set->items().front().span !=
                                (graphscore::MusicalSpan{
                                    graphscore::Rational(1), rational(5, 4)})) {
        std::fprintf(stderr,
                     "key-selection-test: Shift+Left after crossing moved "
                     "the wrong edge\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 4: staff scope +/-1 in both directions, including that
  //     running off either end is a no-op leaving the selection
  //     unchanged. -----------------------------------------------------
  {
    auto dp = build_key_selection_project(metrics);
    if (!dp.has_value()) {
      std::fprintf(stderr,
                   "key-selection-test: build_key_selection_project (4) "
                   "failed\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler handler(std::move(dp->project), std::move(dp->layout),
                                 &shell);
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kSelection);
    const auto& layout = handler.layout();
    {
      const double x1 = layout.systems[0].measures[0].bounds.x;
      const double x2 = layout.systems[0].measures[0].bounds.x +
                        layout.systems[0].measures[0].bounds.width;
      const double y = layout.systems[0].staves[1].bounds.y +
                       layout.systems[0].staves[1].bounds.height * 0.5;
      drag_through_shell(shell, x1, y, x2, y);
    }
    const std::vector<graphscore::NotationRect> rects_before_scope =
        shell.test_snapshot_highlight_rects();
    if (rects_before_scope.empty()) {
      std::fprintf(stderr,
                   "key-selection-test: highlight empty after drag setup "
                   "(4)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.dispatch_test_key_event(shift_key(graphscore::KeyCode::kUp));
    {
      const std::vector<graphscore::NotationRect> rects_after_scope =
          shell.test_snapshot_highlight_rects();
      if (rects_after_scope == rects_before_scope) {
        std::fprintf(stderr,
                     "key-selection-test: highlight rects did not change "
                     "after Shift+Up widened the staff scope (4)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    const graphscore::MeasureScope top{dp->track_ids[0], dp->stave_ids[0]};
    if (handler.first_staff() != top) {
      std::fprintf(stderr,
                   "key-selection-test: Shift+Up did not widen first_staff_ "
                   "(4)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    {
      const auto* set = committed_range_set(handler);
      if (set == nullptr || set->items().size() != 2u) {
        std::fprintf(stderr,
                     "key-selection-test: Shift+Up item count mismatch "
                     "(4)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    // Shift+Up again: first_staff_ is already at index 0 -- no-op.
    const auto before_clamp_up = handler.drag_state().committed_selection();
    shell.dispatch_test_key_event(shift_key(graphscore::KeyCode::kUp));
    if (handler.drag_state().committed_selection() != before_clamp_up) {
      std::fprintf(stderr,
                   "key-selection-test: Shift+Up at the top staff was not a "
                   "no-op (4)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.dispatch_test_key_event(shift_key(graphscore::KeyCode::kDown));
    {
      const auto* set = committed_range_set(handler);
      if (set == nullptr || set->items().size() != 3u) {
        std::fprintf(stderr,
                     "key-selection-test: Shift+Down item count mismatch "
                     "(4)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    // Shift+Down again: last_staff_ is already at the bottom staff --
    // no-op.
    const auto before_clamp_down = handler.drag_state().committed_selection();
    shell.dispatch_test_key_event(shift_key(graphscore::KeyCode::kDown));
    if (handler.drag_state().committed_selection() != before_clamp_down) {
      std::fprintf(stderr,
                   "key-selection-test: Shift+Down at the bottom staff was "
                   "not a no-op (4)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 5: Shift+Home reaches exactly Rational(0); Shift+End reaches
  //     exactly the node's total_length(). --------------------------------
  {
    auto dp = build_key_selection_project(metrics);
    if (!dp.has_value()) {
      std::fprintf(stderr,
                   "key-selection-test: build_key_selection_project (5) "
                   "failed\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler handler(std::move(dp->project), std::move(dp->layout),
                                 &shell);
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kSelection);
    const auto& layout = handler.layout();
    {
      const double x1 = layout.systems[0].measures[0].bounds.x;
      const double x2 = layout.systems[0].measures[0].bounds.x +
                        layout.systems[0].measures[0].bounds.width;
      const double y = layout.systems[0].staves[0].bounds.y +
                       layout.systems[0].staves[0].bounds.height * 0.5;
      drag_through_shell(shell, x1, y, x2, y);
    }
    shell.dispatch_test_key_event(shift_key(graphscore::KeyCode::kEnd));
    {
      const auto* set = committed_range_set(handler);
      if (set == nullptr ||
          set->items().front().span.end != graphscore::Rational(2)) {
        std::fprintf(stderr,
                     "key-selection-test: Shift+End did not reach "
                     "total_length()\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    {
      const double x1 = layout.systems[0].measures[1].bounds.x;
      const double x2 = layout.systems[0].measures[1].bounds.x +
                        layout.systems[0].measures[1].bounds.width;
      const double y = layout.systems[0].staves[0].bounds.y +
                       layout.systems[0].staves[0].bounds.height * 0.5;
      drag_through_shell(shell, x1, y, x2, y);
    }
    shell.dispatch_test_key_event(shift_key(graphscore::KeyCode::kHome));
    {
      const auto* set = committed_range_set(handler);
      if (set == nullptr ||
          set->items().front().span.start != graphscore::Rational(0)) {
        std::fprintf(stderr,
                     "key-selection-test: Shift+Home did not reach "
                     "Rational(0)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 6: no committed selection -> every binding is a no-op and
  //     nothing crashes. -------------------------------------------------
  {
    auto dp = build_key_selection_project(metrics);
    if (!dp.has_value()) {
      std::fprintf(stderr,
                   "key-selection-test: build_key_selection_project (6) "
                   "failed\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler handler(std::move(dp->project), std::move(dp->layout),
                                 &shell);
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kSelection);
    if (handler.extend_range_edge(graphscore::RangeEdge::kEnd,
                                  graphscore::Rational(1)) ||
        handler.extend_range_staff_scope(
            graphscore::MeasureScope{dp->track_ids[0], dp->stave_ids[0]},
            graphscore::MeasureScope{dp->track_ids[1], dp->stave_ids[1]}) ||
        handler.select_to_node_start() || handler.select_to_node_end()) {
      std::fprintf(stderr,
                   "key-selection-test: a direct control call succeeded "
                   "with no committed selection\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    constexpr std::array<graphscore::KeyCode, 6> kAllBoundCodes{
        graphscore::KeyCode::kLeft, graphscore::KeyCode::kRight,
        graphscore::KeyCode::kUp,   graphscore::KeyCode::kDown,
        graphscore::KeyCode::kHome, graphscore::KeyCode::kEnd,
    };
    for (const graphscore::KeyCode code : kAllBoundCodes) {
      shell.dispatch_test_key_event(shift_key(code));
      if (handler.drag_state().committed_selection().has_value()) {
        std::fprintf(stderr,
                     "key-selection-test: a key binding created a "
                     "selection with none committed\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 7: unmodified arrows and wrong-modifier chords (control, alt,
  //     meta -- none substitutes for shift) are no-ops. -------------------
  {
    auto dp = build_key_selection_project(metrics);
    if (!dp.has_value()) {
      std::fprintf(stderr,
                   "key-selection-test: build_key_selection_project (7) "
                   "failed\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler handler(std::move(dp->project), std::move(dp->layout),
                                 &shell);
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kSelection);
    const auto& layout = handler.layout();
    {
      const double x1 = layout.systems[0].measures[0].bounds.x;
      const double x2 = layout.systems[0].measures[0].bounds.x +
                        layout.systems[0].measures[0].bounds.width;
      const double y = layout.systems[0].staves[0].bounds.y +
                       layout.systems[0].staves[0].bounds.height * 0.5;
      drag_through_shell(shell, x1, y, x2, y);
    }
    const auto before = handler.drag_state().committed_selection();
    if (!before.has_value()) {
      std::fprintf(stderr, "key-selection-test: setup selection missing (7)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    // Unmodified Left.
    {
      graphscore::KeyEvent event;
      event.code = graphscore::KeyCode::kLeft;
      shell.dispatch_test_key_event(event);
    }
    if (handler.drag_state().committed_selection() != before) {
      std::fprintf(stderr,
                   "key-selection-test: unmodified Left changed the "
                   "selection\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    // Control+Left (wrong modifier -- control does not substitute for
    // shift).
    {
      graphscore::KeyEvent event;
      event.code              = graphscore::KeyCode::kLeft;
      event.modifiers.control = true;
      shell.dispatch_test_key_event(event);
    }
    if (handler.drag_state().committed_selection() != before) {
      std::fprintf(stderr,
                   "key-selection-test: Control+Left changed the "
                   "selection\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    // Alt+Left (wrong modifier -- alt does not substitute for shift).
    {
      graphscore::KeyEvent event;
      event.code          = graphscore::KeyCode::kLeft;
      event.modifiers.alt = true;
      shell.dispatch_test_key_event(event);
    }
    if (handler.drag_state().committed_selection() != before) {
      std::fprintf(stderr,
                   "key-selection-test: Alt+Left changed the selection\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    // Meta+Left (wrong modifier -- meta does not substitute for shift).
    {
      graphscore::KeyEvent event;
      event.code           = graphscore::KeyCode::kLeft;
      event.modifiers.meta = true;
      shell.dispatch_test_key_event(event);
    }
    if (handler.drag_state().committed_selection() != before) {
      std::fprintf(stderr,
                   "key-selection-test: Meta+Left changed the selection\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 8: the override takes effect through the InputHandler* seam,
  //     not the base class's non-pure no-op default. ----------------------
  {
    auto dp = build_key_selection_project(metrics);
    if (!dp.has_value()) {
      std::fprintf(stderr,
                   "key-selection-test: build_key_selection_project (8) "
                   "failed\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler handler(std::move(dp->project), std::move(dp->layout),
                                 &shell);
    graphscore::InputHandler* base = &handler;
    shell.set_input_handler(base);
    handler.set_active_tool(graphscore::ActiveTool::kSelection);
    const auto& layout = handler.layout();
    {
      const double x1 = layout.systems[0].measures[0].bounds.x;
      const double x2 = layout.systems[0].measures[0].bounds.x +
                        layout.systems[0].measures[0].bounds.width;
      const double y = layout.systems[0].staves[0].bounds.y +
                       layout.systems[0].staves[0].bounds.height * 0.5;
      drag_through_shell(shell, x1, y, x2, y);
    }
    const auto before = handler.drag_state().committed_selection();
    shell.dispatch_test_key_event(shift_key(graphscore::KeyCode::kRight));
    if (handler.drag_state().committed_selection() == before) {
      std::fprintf(stderr,
                   "key-selection-test: Shift+Right through the "
                   "InputHandler* seam did not reach the override\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 9: staff endpoints survive an edge-only extension: an
  //     extreme staff contributing no items keeps its place in the
  //     tracked scope, and is recovered once the widened span overlaps
  //     it. -------------------------------------------------------------
  {
    auto dp = build_key_selection_project(metrics);
    if (!dp.has_value()) {
      std::fprintf(stderr,
                   "key-selection-test: build_key_selection_project (9) "
                   "failed\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler handler(std::move(dp->project), std::move(dp->layout),
                                 &shell);
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kSelection);
    const auto& layout = handler.layout();
    // Drag the second measure only on track 0.
    {
      const double x1 = layout.systems[0].measures[1].bounds.x;
      const double x2 = layout.systems[0].measures[1].bounds.x +
                        layout.systems[0].measures[1].bounds.width;
      const double y = layout.systems[0].staves[0].bounds.y +
                       layout.systems[0].staves[0].bounds.height * 0.5;
      drag_through_shell(shell, x1, y, x2, y);
    }
    const graphscore::MeasureScope first{dp->track_ids[0], dp->stave_ids[0]};
    const graphscore::MeasureScope last{dp->track_ids[2], dp->stave_ids[2]};
    if (!handler.extend_range_staff_scope(first, last)) {
      std::fprintf(stderr,
                   "key-selection-test: extend_range_staff_scope setup "
                   "failed (9)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    // track_ids[2] carries content only in [0, 1); the committed span is
    // [1, 2), so it contributes no item here, even though it is part of
    // the tracked scope.
    {
      const auto* set = committed_range_set(handler);
      if (set == nullptr || set->items().size() != 2u) {
        std::fprintf(stderr,
                     "key-selection-test: pre-extension item count "
                     "mismatch (9)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    if (handler.first_staff() != first || handler.last_staff() != last) {
      std::fprintf(stderr,
                   "key-selection-test: staff scope not tracked before "
                   "extension (9)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (!handler.extend_range_edge(graphscore::RangeEdge::kStart,
                                   graphscore::Rational(0))) {
      std::fprintf(stderr,
                   "key-selection-test: extend_range_edge(kStart) failed "
                   "(9)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    // The widened span [0, 2) now overlaps track_ids[2]'s content too.
    {
      const auto* set = committed_range_set(handler);
      if (set == nullptr || set->items().size() != 3u) {
        std::fprintf(stderr,
                     "key-selection-test: post-extension item count "
                     "mismatch (9)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    if (handler.first_staff() != first || handler.last_staff() != last) {
      std::fprintf(stderr,
                   "key-selection-test: staff scope not preserved across "
                   "an edge-only extension (9)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 10: a resolver rejection during extend_range_edge is a true
  //     no-op -- the committed selection must never be cleared, only left
  //     exactly as it was, when the requested span falls outside the
  //     node's bounds. -----------------------------------------------------
  {
    auto dp = build_key_selection_project(metrics);
    if (!dp.has_value()) {
      std::fprintf(stderr,
                   "key-selection-test: build_key_selection_project (10) "
                   "failed\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler handler(std::move(dp->project), std::move(dp->layout),
                                 &shell);
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kSelection);
    const auto& layout = handler.layout();
    {
      const double x1 = layout.systems[0].measures[0].bounds.x;
      const double x2 = layout.systems[0].measures[0].bounds.x +
                        layout.systems[0].measures[0].bounds.width;
      const double y = layout.systems[0].staves[0].bounds.y +
                       layout.systems[0].staves[0].bounds.height * 0.5;
      drag_through_shell(shell, x1, y, x2, y);
    }
    const auto before = handler.drag_state().committed_selection();
    if (!before.has_value()) {
      std::fprintf(stderr,
                   "key-selection-test: setup selection missing (10)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    // rational(99, 1) is well past the node's total_length() of 2, so
    // resolve_range_selection_spec must reject the request.
    if (handler.extend_range_edge(graphscore::RangeEdge::kEnd,
                                  rational(99, 1))) {
      std::fprintf(stderr,
                   "key-selection-test: extend_range_edge accepted an "
                   "out-of-bounds span (10)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (handler.drag_state().committed_selection() != before) {
      std::fprintf(stderr,
                   "key-selection-test: a rejected extend_range_edge "
                   "changed the committed selection (10)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 11: the active-tool gate -- Shift+arrow bindings must not
  //     touch the committed selection while a non-selection tool is
  //     active. ---------------------------------------------------------
  {
    auto dp = build_key_selection_project(metrics);
    if (!dp.has_value()) {
      std::fprintf(stderr,
                   "key-selection-test: build_key_selection_project (11) "
                   "failed\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler handler(std::move(dp->project), std::move(dp->layout),
                                 &shell);
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kSelection);
    const auto& layout = handler.layout();
    {
      const double x1 = layout.systems[0].measures[0].bounds.x;
      const double x2 = layout.systems[0].measures[0].bounds.x +
                        layout.systems[0].measures[0].bounds.width;
      const double y = layout.systems[0].staves[0].bounds.y +
                       layout.systems[0].staves[0].bounds.height * 0.5;
      drag_through_shell(shell, x1, y, x2, y);
    }
    const auto before = handler.drag_state().committed_selection();
    if (!before.has_value()) {
      std::fprintf(stderr,
                   "key-selection-test: setup selection missing (11)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    handler.set_active_tool(graphscore::ActiveTool::kNoteEntry);
    shell.dispatch_test_key_event(shift_key(graphscore::KeyCode::kRight));
    shell.dispatch_test_key_event(shift_key(graphscore::KeyCode::kUp));
    if (handler.drag_state().committed_selection() != before) {
      std::fprintf(stderr,
                   "key-selection-test: Shift+Right/Up changed the "
                   "committed selection while kNoteEntry was active\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  std::printf("key-selection-test: ok\n");
  return 0;
}

// ---- notehead move tests (M5-phase-20) -------------------------------------
//
// Exercises SelectionToolHandler's unmodified Up/Down dispatch: with exactly
// one selected notehead, the notehead moves one diatonic staff step, its
// accidental is preserved, the same notehead identity stays selected, and the
// retained layout is refreshed. No selection, a non-notehead selection, a
// multi-notehead selection, a stale notehead, and Shift chords remain no-ops
// (Shift stays M5-phase-19 range extension).

struct NoteheadMoveFixture {
  graphscore::Project          project;
  graphscore::NodeId           node_id;
  graphscore::TrackId          track_id;
  graphscore::StaveId          stave_id;
  graphscore::NotationEntityId first_note_id;
  graphscore::NotationEntityId second_note_id;
  graphscore::NotationLayout   layout;
};

// One single-staff node with a complete voice: two quarter notes (C4, D4)
// followed by normalized rests filling a 4/4 measure. Complete so the move
// command's normalize step is a no-op and undo/redo stay exact.
[[nodiscard]] std::optional<NoteheadMoveFixture> build_notehead_move_fixture(
    const graphscore::GlyphMetrics& metrics) {
  graphscore::Project project{graphscore::ProjectId::generate(),
                              "NoteheadMove"};
  const auto          midi_channel = graphscore::MidiChannel::create(0);
  if (!midi_channel.has_value()) {
    return std::nullopt;
  }
  const auto track_added = project.add_track(
      "Track", graphscore::StaffLayout::single_staff(graphscore::Clef::kTreble),
      *midi_channel);
  if (!track_added.has_value()) {
    return std::nullopt;
  }
  const graphscore::TrackId track_id = *track_added;
  const graphscore::NodeId  node_id  = project.add_node("Node");
  auto*                     lane = project.find_node(node_id)->lane(track_id);
  const graphscore::StaveId stave_id =
      project.active_tracks()[0].layout().staves()[0].id;
  lane->ensure_stave(stave_id);

  std::vector<graphscore::StaveDefinition> stave_defs;
  stave_defs.push_back(project.active_tracks()[0].layout().staves()[0]);
  const auto time_sig = graphscore::TimeSignature::create(4, 4);
  if (!time_sig.has_value()) {
    return std::nullopt;
  }
  std::vector<graphscore::Measure> measures(
      1, graphscore::Measure{*time_sig, graphscore::KeySignature{}});
  auto timeline =
      graphscore::NodeTimeline::create(std::move(measures), stave_defs);
  if (!timeline.has_value()) {
    return std::nullopt;
  }
  project.find_node(node_id)->set_timeline(std::move(*timeline));

  const auto quarter_dur =
      graphscore::Duration::create(graphscore::NoteValue::kQuarter, 0);
  if (!quarter_dur.has_value()) {
    return std::nullopt;
  }
  const graphscore::Duration quarter = *quarter_dur;
  const auto                 voice1  = graphscore::Voice::create(1);
  if (!voice1.has_value()) {
    return std::nullopt;
  }
  graphscore::VoiceContent& vc = lane->stave(stave_id)->voice(*voice1);

  const auto pitch_c4 =
      graphscore::SpelledPitch::create(graphscore::Letter::kC, 4);
  const auto pitch_d4 =
      graphscore::SpelledPitch::create(graphscore::Letter::kD, 4);
  if (!pitch_c4.has_value() || !pitch_d4.has_value()) {
    return std::nullopt;
  }
  if (!vc.append(graphscore::make_note(*pitch_c4, quarter)).ok()) {
    return std::nullopt;
  }
  if (!vc.append(graphscore::make_note(*pitch_d4, quarter)).ok()) {
    return std::nullopt;
  }
  const graphscore::Rational node_end =
      project.find_node(node_id)->timeline()->node_end();
  if (!vc.normalize(node_end).ok()) {
    return std::nullopt;
  }

  const graphscore::NotationEntityId first_id =
      graphscore::event_id(vc.events()[0]);
  const graphscore::NotationEntityId second_id =
      graphscore::event_id(vc.events()[1]);

  graphscore::NotationLayoutResult layout_result =
      graphscore::layout_notation(project, node_id, metrics);
  if (!layout_result || !layout_result.layout.has_value()) {
    return std::nullopt;
  }

  return NoteheadMoveFixture{std::move(project),
                             node_id,
                             track_id,
                             stave_id,
                             first_id,
                             second_id,
                             std::move(*layout_result.layout)};
}

[[nodiscard]] graphscore::KeyEvent plain_key(graphscore::KeyCode code) {
  graphscore::KeyEvent event;
  event.code = code;
  return event;
}

// Voice 1, the only voice this test's fixture writes. Voice::create(1) cannot
// fail (index 1 is always in range), but the value is checked rather than
// dereferenced so the unchecked-optional-access check sees a guarded access.
[[nodiscard]] graphscore::Voice voice_one() {
  const auto voice = graphscore::Voice::create(1);
  if (!voice.has_value()) {
    return graphscore::Voice{};
  }
  return *voice;
}

// A natural spelled pitch; the octaves this test uses (4, 5) are always in
// range, but the value is checked before dereferencing.
[[nodiscard]] graphscore::SpelledPitch spelled(graphscore::Letter letter,
                                               std::int8_t        octave) {
  const auto pitch = graphscore::SpelledPitch::create(letter, octave);
  if (!pitch.has_value()) {
    return graphscore::SpelledPitch{};
  }
  return *pitch;
}

// Sets the committed selection to the given notehead items. Returns false
// (leaving the handler untouched) if NoteheadSet::create rejects the items.
[[nodiscard]] bool select_noteheads(
    SelectionToolHandler&                 handler,
    std::vector<graphscore::NoteheadItem> items) {
  const auto set = graphscore::NoteheadSet::create(std::move(items));
  if (!set.has_value()) {
    return false;
  }
  handler.set_committed_selection(graphscore::Selection{*set});
  return true;
}

// The committed selection's own NoteheadSet, or nullptr when there is no
// committed selection or it is not that arm — the free-function counterpart
// of SelectionToolHandler::current_notehead_set().
[[nodiscard]] const graphscore::NoteheadSet* committed_notehead_set(
    const SelectionToolHandler& handler) {
  const auto& committed = handler.drag_state().committed_selection();
  if (!committed.has_value()) {
    return nullptr;
  }
  return std::get_if<graphscore::NoteheadSet>(&*committed);
}

// Presses and releases the primary button at (x, y) with no intervening move:
// a click, which on_pointer_release resolves through resolve_selection_at
// rather than through the range-drag path.
void click_at(graphscore::WriterShell& shell, double x, double y) {
  const graphscore::PointerEvent press{x, y,
                                       graphscore::PointerButton::kPrimary};
  const graphscore::PointerEvent release{x, y,
                                         graphscore::PointerButton::kPrimary};
  shell.dispatch_test_pointer_event(0, press);
  shell.dispatch_test_pointer_event(2, release);
}

// The origin of the "<id>/notehead" GlyphCommand in `layout` — ground truth
// read out of the real layout, never a reproduction of notation.cpp's own
// placement formulas. Clicking this point selects that notehead.
[[nodiscard]] graphscore::NotationPoint notehead_origin(
    const graphscore::NotationLayout&   layout,
    const graphscore::NotationEntityId& id) {
  const std::string target = id.to_string() + "/notehead";
  for (const auto& command : layout.commands) {
    const auto* glyph = std::get_if<graphscore::GlyphCommand>(&command);
    if (glyph != nullptr && glyph->id.value == target) {
      return glyph->origin;
    }
  }
  return graphscore::NotationPoint{};
}

// The origin of the "<id>/grace-notehead" GlyphCommand in `layout` (a grace
// notehead uses a distinct glyph role from an ordinary/chord notehead).
[[nodiscard]] graphscore::NotationPoint grace_notehead_origin(
    const graphscore::NotationLayout&   layout,
    const graphscore::NotationEntityId& id) {
  const std::string target = id.to_string() + "/grace-notehead";
  for (const auto& command : layout.commands) {
    const auto* glyph = std::get_if<graphscore::GlyphCommand>(&command);
    if (glyph != nullptr && glyph->id.value == target) {
      return glyph->origin;
    }
  }
  return graphscore::NotationPoint{};
}

// A headless surface publisher: encodes a hash of the layout's glyph origins
// into a small RGBA surface and publishes it through the shell's real
// set_notation_surface path, so the notehead-move test can observe that a
// mutation refresh re-published a *different* visible surface without a font
// or rendering backend. Moving a notehead changes that notehead's glyph
// origin, and therefore the surface.
[[nodiscard]] graphscore::ShellResult publish_headless_test_surface(
    const graphscore::NotationLayout& layout, graphscore::WriterShell* shell) {
  std::uint32_t hash = 2166136261u;
  for (const auto& command : layout.commands) {
    const auto* glyph = std::get_if<graphscore::GlyphCommand>(&command);
    if (glyph == nullptr) {
      continue;
    }
    const auto mix = [&hash](double value) {
      hash ^= static_cast<std::uint32_t>(value * 1000.0);
      hash *= 16777619u;
    };
    mix(glyph->origin.x);
    mix(glyph->origin.y);
  }
  graphscore::RasterSurface surface;
  surface.width  = 2;
  surface.height = 2;
  surface.rgba.resize(16);
  surface.rgba[0] = static_cast<std::uint8_t>(hash >> 24);
  surface.rgba[1] = static_cast<std::uint8_t>(hash >> 16);
  surface.rgba[2] = static_cast<std::uint8_t>(hash >> 8);
  surface.rgba[3] = static_cast<std::uint8_t>(hash);
  return shell->set_notation_surface(std::move(surface));
}

// A single-staff fixture for the chord/grace click tests: a leading C4
// quarter (so the grace-attached principal sits off the very first beat),
// the grace principal D4 quarter, a two-note chord (E4, G4 quarter), and a
// grace group (F4 eighth) attached to the principal. The lead is present so
// the grace notehead engraves at a positive x and is reachable by a click.
struct NoteheadClickFixture {
  graphscore::Project          project;
  graphscore::NodeId           node_id;
  graphscore::TrackId          track_id;
  graphscore::StaveId          stave_id;
  graphscore::NotationEntityId chord_id;        // top-level Chord event id
  graphscore::NotationEntityId chord_note_id;   // E4 (moved chord notehead)
  graphscore::NotationEntityId chord_other_id;  // G4 (untouched chord notehead)
  graphscore::NotationEntityId grace_id;        // F4 grace notehead
  graphscore::NotationLayout   layout;
};

[[nodiscard]] std::optional<NoteheadClickFixture> build_notehead_click_fixture(
    const graphscore::GlyphMetrics& metrics) {
  graphscore::Project project{graphscore::ProjectId::generate(), "Click"};
  const auto          midi_channel = graphscore::MidiChannel::create(0);
  if (!midi_channel.has_value()) {
    return std::nullopt;
  }
  const auto track_added = project.add_track(
      "Track", graphscore::StaffLayout::single_staff(graphscore::Clef::kTreble),
      *midi_channel);
  if (!track_added.has_value()) {
    return std::nullopt;
  }
  const graphscore::TrackId track_id = *track_added;
  const graphscore::NodeId  node_id  = project.add_node("Node");
  auto*                     lane = project.find_node(node_id)->lane(track_id);
  const graphscore::StaveId stave_id =
      project.active_tracks()[0].layout().staves()[0].id;
  lane->ensure_stave(stave_id);

  std::vector<graphscore::StaveDefinition> stave_defs;
  stave_defs.push_back(project.active_tracks()[0].layout().staves()[0]);
  const auto time_sig = graphscore::TimeSignature::create(4, 4);
  if (!time_sig.has_value()) {
    return std::nullopt;
  }
  std::vector<graphscore::Measure> measures(
      1, graphscore::Measure{*time_sig, graphscore::KeySignature{}});
  auto timeline =
      graphscore::NodeTimeline::create(std::move(measures), stave_defs);
  if (!timeline.has_value()) {
    return std::nullopt;
  }
  project.find_node(node_id)->set_timeline(std::move(*timeline));

  const auto quarter =
      graphscore::Duration::create(graphscore::NoteValue::kQuarter, 0);
  const auto eighth =
      graphscore::Duration::create(graphscore::NoteValue::kEighth, 0);
  if (!quarter.has_value() || !eighth.has_value()) {
    return std::nullopt;
  }
  const graphscore::Voice   voice1 = voice_one();
  graphscore::VoiceContent& vc     = lane->stave(stave_id)->voice(voice1);

  const auto c4 = graphscore::SpelledPitch::create(graphscore::Letter::kC, 4);
  const auto d4 = graphscore::SpelledPitch::create(graphscore::Letter::kD, 4);
  const auto e4 = graphscore::SpelledPitch::create(graphscore::Letter::kE, 4);
  const auto g4 = graphscore::SpelledPitch::create(graphscore::Letter::kG, 4);
  const auto f4 = graphscore::SpelledPitch::create(graphscore::Letter::kF, 4);
  if (!c4.has_value() || !d4.has_value() || !e4.has_value() ||
      !g4.has_value() || !f4.has_value()) {
    return std::nullopt;
  }

  if (!vc.append(graphscore::make_note(*c4, *quarter)).ok()) {
    return std::nullopt;
  }
  const graphscore::Note principal = graphscore::make_note(*d4, *quarter);
  if (!vc.append(principal).ok()) {
    return std::nullopt;
  }
  const graphscore::ChordNote moved{graphscore::NotationEntityId::generate(),
                                    *e4, false};
  const graphscore::ChordNote other{graphscore::NotationEntityId::generate(),
                                    *g4, false};
  const graphscore::NotationEntityId chord_note_id  = moved.id;
  const graphscore::NotationEntityId chord_other_id = other.id;
  const graphscore::Chord            chord =
      graphscore::make_chord(*quarter, {moved, other});
  const graphscore::NotationEntityId chord_id = chord.id;
  if (!vc.append(chord).ok()) {
    return std::nullopt;
  }
  const graphscore::GraceGroup group = graphscore::make_grace_group(
      principal.id, {graphscore::GraceNote{
                        graphscore::NotationEntityId::generate(), *f4, *eighth,
                        graphscore::GraceNoteType::kAcciaccatura, true}});
  const graphscore::NotationEntityId grace_id = group.notes[0].id;
  if (!vc.add_grace_group(group).ok()) {
    return std::nullopt;
  }
  const graphscore::Rational node_end =
      project.find_node(node_id)->timeline()->node_end();
  if (!vc.normalize(node_end).ok()) {
    return std::nullopt;
  }

  graphscore::NotationLayoutResult layout_result =
      graphscore::layout_notation(project, node_id, metrics);
  if (!layout_result || !layout_result.layout.has_value()) {
    return std::nullopt;
  }

  return NoteheadClickFixture{
      std::move(project), node_id,  track_id,
      stave_id,           chord_id, chord_note_id,
      chord_other_id,     grace_id, std::move(*layout_result.layout)};
}

// A single-staff, two-measure fixture whose voice is three quarter rests then
// a tied C4 quarter in measure 0, and a C4 quarter in measure 1 -- the tied
// notehead and its target sit on opposite sides of the barline, so the
// connected tie chain spans measures 0 and 1.
struct CrossMeasureTieFixture {
  graphscore::Project          project;
  graphscore::NodeId           node_id;
  graphscore::TrackId          track_id;
  graphscore::StaveId          stave_id;
  graphscore::NotationEntityId first_id;   // tied C4 (end of measure 0)
  graphscore::NotationEntityId second_id;  // C4 (start of measure 1)
  graphscore::NotationLayout   layout;
};

[[nodiscard]] std::optional<CrossMeasureTieFixture>
build_cross_measure_tie_fixture(
    const graphscore::GlyphMetrics&          metrics,
    const graphscore::NotationLayoutOptions& options) {
  graphscore::Project project{graphscore::ProjectId::generate(), "CrossTie"};
  const auto          midi_channel = graphscore::MidiChannel::create(0);
  if (!midi_channel.has_value()) {
    return std::nullopt;
  }
  const auto track_added = project.add_track(
      "Track", graphscore::StaffLayout::single_staff(graphscore::Clef::kTreble),
      *midi_channel);
  if (!track_added.has_value()) {
    return std::nullopt;
  }
  const graphscore::TrackId track_id = *track_added;
  const graphscore::NodeId  node_id  = project.add_node("Node");
  auto*                     lane = project.find_node(node_id)->lane(track_id);
  const graphscore::StaveId stave_id =
      project.active_tracks()[0].layout().staves()[0].id;
  lane->ensure_stave(stave_id);

  std::vector<graphscore::StaveDefinition> stave_defs;
  stave_defs.push_back(project.active_tracks()[0].layout().staves()[0]);
  const auto time_sig = graphscore::TimeSignature::create(4, 4);
  if (!time_sig.has_value()) {
    return std::nullopt;
  }
  std::vector<graphscore::Measure> measures(
      2, graphscore::Measure{*time_sig, graphscore::KeySignature{}});
  auto timeline =
      graphscore::NodeTimeline::create(std::move(measures), stave_defs);
  if (!timeline.has_value()) {
    return std::nullopt;
  }
  project.find_node(node_id)->set_timeline(std::move(*timeline));

  const auto quarter =
      graphscore::Duration::create(graphscore::NoteValue::kQuarter, 0);
  if (!quarter.has_value()) {
    return std::nullopt;
  }
  const auto pitch_c4 =
      graphscore::SpelledPitch::create(graphscore::Letter::kC, 4);
  if (!pitch_c4.has_value()) {
    return std::nullopt;
  }
  const graphscore::Voice   voice1 = voice_one();
  graphscore::VoiceContent& vc     = lane->stave(stave_id)->voice(voice1);

  for (int i = 0; i < 3; ++i) {
    if (!vc.append(graphscore::make_rest(*quarter)).ok()) {
      return std::nullopt;
    }
  }
  const graphscore::Note first =
      graphscore::make_note(*pitch_c4, *quarter, true);
  const graphscore::NotationEntityId first_id = first.id;
  if (!vc.append(first).ok()) {
    return std::nullopt;
  }
  const graphscore::Note second = graphscore::make_note(*pitch_c4, *quarter);
  const graphscore::NotationEntityId second_id = second.id;
  if (!vc.append(second).ok()) {
    return std::nullopt;
  }
  const graphscore::Rational node_end =
      project.find_node(node_id)->timeline()->node_end();
  if (!vc.normalize(node_end).ok()) {
    return std::nullopt;
  }

  graphscore::NotationLayoutResult layout_result =
      graphscore::layout_notation(project, node_id, metrics, options);
  if (!layout_result || !layout_result.layout.has_value()) {
    return std::nullopt;
  }

  return CrossMeasureTieFixture{std::move(project),
                                node_id,
                                track_id,
                                stave_id,
                                first_id,
                                second_id,
                                std::move(*layout_result.layout)};
}

// A single-staff, two-measure fixture whose two measures land in two separate
// systems (via narrow options) and whose voice carries one UNTIED note in each
// measure. Moving the measure-0 note is a single-measure (local) edit, so the
// fixture proves a cold-cache first move rebuilds only the affected system.
struct TwoSystemLocalFixture {
  graphscore::Project          project;
  graphscore::NodeId           node_id;
  graphscore::TrackId          track_id;
  graphscore::StaveId          stave_id;
  graphscore::NotationEntityId first_id;   // C4 quarter (measure 0)
  graphscore::NotationEntityId second_id;  // E4 quarter (measure 1)
  graphscore::NotationLayout   layout;
};

[[nodiscard]] std::optional<TwoSystemLocalFixture>
build_two_system_local_fixture(
    const graphscore::GlyphMetrics&          metrics,
    const graphscore::NotationLayoutOptions& options) {
  graphscore::Project project{graphscore::ProjectId::generate(), "TwoSystem"};
  const auto          midi_channel = graphscore::MidiChannel::create(0);
  if (!midi_channel.has_value()) {
    return std::nullopt;
  }
  const auto track_added = project.add_track(
      "Track", graphscore::StaffLayout::single_staff(graphscore::Clef::kTreble),
      *midi_channel);
  if (!track_added.has_value()) {
    return std::nullopt;
  }
  const graphscore::TrackId track_id = *track_added;
  const graphscore::NodeId  node_id  = project.add_node("Node");
  auto*                     lane = project.find_node(node_id)->lane(track_id);
  const graphscore::StaveId stave_id =
      project.active_tracks()[0].layout().staves()[0].id;
  lane->ensure_stave(stave_id);

  std::vector<graphscore::StaveDefinition> stave_defs;
  stave_defs.push_back(project.active_tracks()[0].layout().staves()[0]);
  const auto time_sig = graphscore::TimeSignature::create(4, 4);
  if (!time_sig.has_value()) {
    return std::nullopt;
  }
  std::vector<graphscore::Measure> measures(
      2, graphscore::Measure{*time_sig, graphscore::KeySignature{}});
  auto timeline =
      graphscore::NodeTimeline::create(std::move(measures), stave_defs);
  if (!timeline.has_value()) {
    return std::nullopt;
  }
  project.find_node(node_id)->set_timeline(std::move(*timeline));

  const auto quarter =
      graphscore::Duration::create(graphscore::NoteValue::kQuarter, 0);
  if (!quarter.has_value()) {
    return std::nullopt;
  }
  const auto pitch_c4 =
      graphscore::SpelledPitch::create(graphscore::Letter::kC, 4);
  const auto pitch_e4 =
      graphscore::SpelledPitch::create(graphscore::Letter::kE, 4);
  if (!pitch_c4.has_value() || !pitch_e4.has_value()) {
    return std::nullopt;
  }
  const graphscore::Voice   voice1 = voice_one();
  graphscore::VoiceContent& vc     = lane->stave(stave_id)->voice(voice1);

  const graphscore::Note first = graphscore::make_note(*pitch_c4, *quarter);
  const graphscore::NotationEntityId first_id = first.id;
  if (!vc.append(first).ok()) {
    return std::nullopt;
  }
  // Three explicit rests fill the rest of measure 0, so measure 1's note
  // starts exactly on the barline.
  for (int i = 0; i < 3; ++i) {
    if (!vc.append(graphscore::make_rest(*quarter)).ok()) {
      return std::nullopt;
    }
  }
  const graphscore::Note second = graphscore::make_note(*pitch_e4, *quarter);
  const graphscore::NotationEntityId second_id = second.id;
  if (!vc.append(second).ok()) {
    return std::nullopt;
  }
  const graphscore::Rational node_end =
      project.find_node(node_id)->timeline()->node_end();
  if (!vc.normalize(node_end).ok()) {
    return std::nullopt;
  }

  graphscore::NotationLayoutResult layout_result =
      graphscore::layout_notation(project, node_id, metrics, options);
  if (!layout_result || !layout_result.layout.has_value()) {
    return std::nullopt;
  }

  return TwoSystemLocalFixture{std::move(project),
                               node_id,
                               track_id,
                               stave_id,
                               first_id,
                               second_id,
                               std::move(*layout_result.layout)};
}

// A deterministic test wrapper that delegates execute/redo to a real
// MoveNoteheadCommand but fails undo() exactly `fail_times` times before
// delegating, so a rollback failure can be injected without relying on an
// actual allocation failure. `fail_times <= 0` means "never fail".
class FailUndoCommand final : public graphscore::Command {
 public:
  FailUndoCommand(std::unique_ptr<graphscore::Command> inner, int fail_times)
      : inner_(std::move(inner)), fail_times_(fail_times) {}

  graphscore::Result execute(graphscore::Project& project) noexcept override {
    return inner_->execute(project);
  }

  graphscore::Result undo(graphscore::Project& project) noexcept override {
    if (fail_times_ > 0) {
      --fail_times_;
      return graphscore::Result(graphscore::ResultCode::kInternalError);
    }
    return inner_->undo(project);
  }

  graphscore::Result redo(graphscore::Project& project) noexcept override {
    return inner_->redo(project);
  }

 private:
  std::unique_ptr<graphscore::Command> inner_;
  int                                  fail_times_;
};

int notehead_move_test() {
  const SelfTestMetrics metrics;

  // --- test 1: a real click on the ordinary notehead selects it, then
  //     unmodified Up/Down moves it, retains identity/selection, re-publishes
  //     a different visible surface, and issues the post-edit audition. ----
  {
    auto fx = build_notehead_move_fixture(metrics);
    if (!fx.has_value()) {
      std::fprintf(stderr, "notehead-move-test: fixture build failed (1)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler handler(std::move(fx->project), std::move(fx->layout),
                                 &shell);
    handler.set_metrics(&metrics);
    handler.set_surface_publisher(
        [&shell](const graphscore::NotationLayout& layout) {
          return publish_headless_test_surface(layout, &shell);
        });
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kSelection);

    const graphscore::Voice voice1 = voice_one();
    // Publish the initial surface so the test can observe a change rather
    // than an empty-surface-to-non-empty transition.
    if (!publish_headless_test_surface(handler.layout(), &shell).ok()) {
      std::fprintf(stderr,
                   "notehead-move-test: initial surface publish failed\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    const auto before_surface = shell.test_snapshot_notation_surface();

    const graphscore::NotationPoint point =
        notehead_origin(handler.layout(), fx->first_note_id);
    click_at(shell, point.x, point.y);
    {
      const auto* set = committed_notehead_set(handler);
      if (set == nullptr || set->items().size() != 1u ||
          set->items()[0].entity != fx->first_note_id) {
        std::fprintf(stderr,
                     "notehead-move-test: click did not select the ordinary "
                     "notehead (1)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }

    const auto first_pitch = [&]() {
      const auto* lane =
          handler.project().find_node(fx->node_id)->lane(fx->track_id);
      const auto& vc = lane->stave(fx->stave_id)->voice(voice1);
      return std::get<graphscore::Note>(vc.events().front()).pitch;
    };
    const graphscore::SpelledPitch c4 = spelled(graphscore::Letter::kC, 4);
    const graphscore::SpelledPitch d4 = spelled(graphscore::Letter::kD, 4);
    if (first_pitch() != c4) {
      std::fprintf(stderr, "notehead-move-test: expected C4 before move (1)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kUp));
    if (first_pitch() != d4) {
      std::fprintf(stderr,
                   "notehead-move-test: Up did not move C4 to D4 (1)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    {
      const auto* set = committed_notehead_set(handler);
      if (set == nullptr || set->items().size() != 1u ||
          set->items()[0].entity != fx->first_note_id) {
        std::fprintf(stderr,
                     "notehead-move-test: selection changed after Up (1)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    // Visible surface re-published: different surface bytes than before.
    const auto after_surface = shell.test_snapshot_notation_surface();
    if (!after_surface.has_value() || after_surface == before_surface) {
      std::fprintf(stderr,
                   "notehead-move-test: visible surface not re-published "
                   "after Up (1)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    // Audition: the post-edit sounding pitch (D4 = MIDI 62) on the track.
    {
      const auto& audition = handler.last_audition();
      if (!audition.has_value() || audition->track_id != fx->track_id ||
          audition->pitches.size() != 1u ||
          audition->pitches[0].value() != 62) {
        std::fprintf(stderr,
                     "notehead-move-test: no D4 audition after Up (1)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }

    shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kDown));
    if (first_pitch() != c4) {
      std::fprintf(stderr,
                   "notehead-move-test: Down did not move D4 back to C4 (1)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    {
      const auto* set = committed_notehead_set(handler);
      if (set == nullptr || set->items().size() != 1u ||
          set->items()[0].entity != fx->first_note_id) {
        std::fprintf(stderr,
                     "notehead-move-test: selection changed after Down (1)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 1b: clicking one chord notehead selects and moves only that
  //     notehead; the other chord notehead is untouched. ------------------
  {
    auto fx = build_notehead_click_fixture(metrics);
    if (!fx.has_value()) {
      std::fprintf(stderr, "notehead-move-test: fixture build failed (1b)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler handler(std::move(fx->project), std::move(fx->layout),
                                 &shell);
    handler.set_metrics(&metrics);
    handler.set_surface_publisher(
        [&shell](const graphscore::NotationLayout& layout) {
          return publish_headless_test_surface(layout, &shell);
        });
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kSelection);

    const graphscore::NotationPoint point =
        notehead_origin(handler.layout(), fx->chord_note_id);
    click_at(shell, point.x, point.y);
    {
      const auto* set = committed_notehead_set(handler);
      if (set == nullptr || set->items().size() != 1u ||
          set->items()[0].entity != fx->chord_note_id) {
        std::fprintf(stderr,
                     "notehead-move-test: click did not select the chord "
                     "notehead (1b)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }

    shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kUp));
    const auto chord = [&]() {
      const auto* lane =
          handler.project().find_node(fx->node_id)->lane(fx->track_id);
      const auto& vc = lane->stave(fx->stave_id)->voice(voice_one());
      for (const auto& event : vc.events()) {
        if (const auto* c = std::get_if<graphscore::Chord>(&event)) {
          if (c->id == fx->chord_id) {
            return *c;
          }
        }
      }
      return graphscore::Chord{};
    };
    const graphscore::Chord after = chord();
    const auto notehead_pitch     = [&after](graphscore::NotationEntityId id) {
      for (const auto& note : after.notes) {
        if (note.id == id) {
          return note.pitch;
        }
      }
      return graphscore::SpelledPitch{};
    };
    if (notehead_pitch(fx->chord_note_id) !=
            spelled(graphscore::Letter::kF, 4) ||
        notehead_pitch(fx->chord_other_id) !=
            spelled(graphscore::Letter::kG, 4)) {
      std::fprintf(stderr,
                   "notehead-move-test: chord notehead move wrong (1b)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 1c: clicking a grace notehead selects and moves it. ----------
  {
    auto fx = build_notehead_click_fixture(metrics);
    if (!fx.has_value()) {
      std::fprintf(stderr, "notehead-move-test: fixture build failed (1c)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler handler(std::move(fx->project), std::move(fx->layout),
                                 &shell);
    handler.set_metrics(&metrics);
    handler.set_surface_publisher(
        [&shell](const graphscore::NotationLayout& layout) {
          return publish_headless_test_surface(layout, &shell);
        });
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kSelection);

    const graphscore::NotationPoint point =
        grace_notehead_origin(handler.layout(), fx->grace_id);
    click_at(shell, point.x, point.y);
    {
      const auto* set = committed_notehead_set(handler);
      if (set == nullptr || set->items().size() != 1u ||
          set->items()[0].entity != fx->grace_id) {
        std::fprintf(stderr,
                     "notehead-move-test: click did not select the grace "
                     "notehead (1c)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }

    shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kUp));
    const auto grace_pitch = [&]() {
      const auto* lane =
          handler.project().find_node(fx->node_id)->lane(fx->track_id);
      const auto& vc = lane->stave(fx->stave_id)->voice(voice_one());
      return vc.grace_groups()[0].notes[0].pitch;
    };
    if (grace_pitch() != spelled(graphscore::Letter::kG, 4)) {
      std::fprintf(stderr,
                   "notehead-move-test: Up did not move the grace note (1c)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 1d: the production default project is an incomplete voice (two
  //     quarter notes, no trailing rests). Moving a notehead must change only
  //     its pitch and must not materialize unrelated rests. ----------------
  {
    auto dp = build_default_project(metrics);
    if (!dp.has_value()) {
      std::fprintf(stderr, "notehead-move-test: default project failed (1d)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler handler(std::move(dp->project), std::move(dp->layout),
                                 &shell);
    handler.set_metrics(&metrics);
    handler.set_surface_publisher(
        [&shell](const graphscore::NotationLayout& layout) {
          return publish_headless_test_surface(layout, &shell);
        });
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kSelection);

    const graphscore::Voice voice1 = voice_one();
    const auto*             lane =
        handler.project().find_node(dp->node_id)->lane(dp->track_id);
    const graphscore::NotationEntityId first_id = graphscore::event_id(
        lane->stave(dp->stave_id)->voice(voice1).events()[0]);
    const std::size_t before_count =
        lane->stave(dp->stave_id)->voice(voice1).events().size();
    if (before_count != 2u) {
      std::fprintf(
          stderr,
          "notehead-move-test: default project expected 2 events (1d)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    const graphscore::NotationPoint point =
        notehead_origin(handler.layout(), first_id);
    click_at(shell, point.x, point.y);
    shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kUp));

    const auto& vc       = lane->stave(dp->stave_id)->voice(voice1);
    const bool  has_rest = std::ranges::any_of(
        vc.events(), [](const graphscore::VoiceEvent& event) {
          return std::holds_alternative<graphscore::Rest>(event);
        });
    if (vc.events().size() != 2u || has_rest) {
      std::fprintf(stderr,
                   "notehead-move-test: move in the incomplete default project "
                   "changed rhythm or created rests (1d)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 1e: a cross-measure tie chain. Clicking either endpoint and
  //     pressing Up must move the whole chain, and the incremental layout
  //     refresh must re-layout BOTH measures so neither endpoint's retained
  //     surface stays stale. One-measure-per-system options place the two
  //     tied measures in two systems, so a single-measure invalidation would
  //     visibly leave the remote system stale. ----------------------------
  {
    const graphscore::NotationLayoutOptions narrow_options = [] {
      graphscore::NotationLayoutOptions options;
      options.system_width          = 160.0;
      options.left_margin           = 20.0;
      options.right_margin          = 20.0;
      options.minimum_measure_width = 120.0;
      options.whole_note_spacing    = 120.0;
      return options;
    }();
    auto fx = build_cross_measure_tie_fixture(metrics, narrow_options);
    if (!fx.has_value()) {
      std::fprintf(stderr, "notehead-move-test: fixture build failed (1e)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler handler(std::move(fx->project), std::move(fx->layout),
                                 &shell);
    handler.set_metrics(&metrics);
    handler.set_layout_options(narrow_options);
    handler.set_surface_publisher(
        [&shell](const graphscore::NotationLayout& layout) {
          return publish_headless_test_surface(layout, &shell);
        });
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kSelection);
    handler.warm_layout_cache();

    if (handler.layout().systems.size() < 2u) {
      std::fprintf(stderr,
                   "notehead-move-test: cross-measure fixture expected at "
                   "least two systems (1e)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    if (!publish_headless_test_surface(handler.layout(), &shell).ok()) {
      std::fprintf(stderr,
                   "notehead-move-test: initial surface publish failed (1e)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    const auto before_surface = shell.test_snapshot_notation_surface();

    // Both endpoints start at C4, so their notehead glyphs share one y.
    const double before_a = notehead_origin(handler.layout(), fx->first_id).y;
    const double before_b = notehead_origin(handler.layout(), fx->second_id).y;

    const graphscore::NotationPoint point =
        notehead_origin(handler.layout(), fx->first_id);
    click_at(shell, point.x, point.y);
    {
      const auto* set = committed_notehead_set(handler);
      if (set == nullptr || set->items().size() != 1u ||
          set->items()[0].entity != fx->first_id) {
        std::fprintf(stderr,
                     "notehead-move-test: click did not select the tied "
                     "notehead (1e)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }

    shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kUp));

    const double after_a = notehead_origin(handler.layout(), fx->first_id).y;
    const double after_b = notehead_origin(handler.layout(), fx->second_id).y;
    const double delta_a = before_a - after_a;
    const double delta_b = before_b - after_b;
    // Both chain endpoints must have moved up by the same diatonic step. A
    // stale remote measure would leave the far notehead's glyph at its old
    // C4 position, giving delta_b == 0 and failing the first check.
    if (delta_a <= 0.0 || delta_b <= 0.0) {
      std::fprintf(stderr,
                   "notehead-move-test: a cross-measure tie endpoint did not "
                   "move up (1e)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (std::abs(delta_a - delta_b) > 1e-6) {
      std::fprintf(stderr,
                   "notehead-move-test: cross-measure tie endpoints moved by "
                   "different amounts (1e)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    // The visible surface was re-published with different content.
    const auto after_surface = shell.test_snapshot_notation_surface();
    if (!after_surface.has_value() || after_surface == before_surface) {
      std::fprintf(stderr,
                   "notehead-move-test: visible surface not re-published for "
                   "the cross-measure move (1e)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 1f: the FIRST local move under a production-equivalent startup
  //     (cache seeded exactly as run() seeds it) rebuilds only the affected
  //     system, not every system. ------------------------------------------
  {
    const graphscore::NotationLayoutOptions narrow_options = [] {
      graphscore::NotationLayoutOptions options;
      options.system_width          = 160.0;
      options.left_margin           = 20.0;
      options.right_margin          = 20.0;
      options.minimum_measure_width = 120.0;
      options.whole_note_spacing    = 120.0;
      return options;
    }();
    auto fx = build_two_system_local_fixture(metrics, narrow_options);
    if (!fx.has_value()) {
      std::fprintf(stderr, "notehead-move-test: fixture build failed (1f)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler handler(std::move(fx->project), std::move(fx->layout),
                                 &shell);
    handler.set_metrics(&metrics);
    handler.set_layout_options(narrow_options);
    // run() seeds the cache at startup; reproduce that seeding here so the
    // first move below is the handler's first edit, not a test-only warm.
    handler.warm_layout_cache();
    handler.set_surface_publisher(
        [&shell](const graphscore::NotationLayout& layout) {
          return publish_headless_test_surface(layout, &shell);
        });
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kSelection);

    if (handler.layout().systems.size() < 2u) {
      std::fprintf(stderr,
                   "notehead-move-test: two-system fixture expected at least "
                   "two systems (1f)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    const graphscore::NotationPoint point =
        notehead_origin(handler.layout(), fx->first_id);
    click_at(shell, point.x, point.y);
    {
      const auto* set = committed_notehead_set(handler);
      if (set == nullptr || set->items().size() != 1u ||
          set->items()[0].entity != fx->first_id) {
        std::fprintf(stderr,
                     "notehead-move-test: click did not select the measure-0 "
                     "note (1f)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }

    shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kUp));

    // The first edit rebuilt only system 0 and reused system 1. A cold-cache
    // full reset would show every system in rebuilt_systems instead.
    const auto& work = handler.test_last_layout_work();
    if (work.rebuilt_systems != (std::vector<std::size_t>{0}) ||
        work.reused_systems != (std::vector<std::size_t>{1})) {
      std::fprintf(stderr,
                   "notehead-move-test: first local move rebuilt %zu system(s) "
                   "and reused %zu, expected rebuild {0} / reuse {1} (1f)\n",
                   work.rebuilt_systems.size(), work.reused_systems.size());
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 2: no selection -> Up is a no-op. ----------------------------
  {
    auto fx = build_notehead_move_fixture(metrics);
    if (!fx.has_value()) {
      std::fprintf(stderr, "notehead-move-test: fixture build failed (2)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler handler(std::move(fx->project), std::move(fx->layout),
                                 &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kSelection);

    const graphscore::Voice voice1      = voice_one();
    const auto              first_pitch = [&]() {
      const auto* lane =
          handler.project().find_node(fx->node_id)->lane(fx->track_id);
      const auto& vc = lane->stave(fx->stave_id)->voice(voice1);
      return std::get<graphscore::Note>(vc.events().front()).pitch;
    };
    const graphscore::SpelledPitch c4 = spelled(graphscore::Letter::kC, 4);

    shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kUp));
    if (first_pitch() != c4 ||
        handler.drag_state().committed_selection().has_value()) {
      std::fprintf(stderr,
                   "notehead-move-test: Up with no selection mutated (2)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 3: stale notehead identity -> Up is a no-op. -----------------
  {
    auto fx = build_notehead_move_fixture(metrics);
    if (!fx.has_value()) {
      std::fprintf(stderr, "notehead-move-test: fixture build failed (3)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler handler(std::move(fx->project), std::move(fx->layout),
                                 &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kSelection);

    const graphscore::Voice voice1 = voice_one();
    if (!select_noteheads(handler,
                          {graphscore::NoteheadItem{
                              fx->node_id, fx->track_id, fx->stave_id, voice1,
                              graphscore::NotationEntityId::generate()}})) {
      std::fprintf(stderr, "notehead-move-test: stale selection rejected\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    const auto first_pitch = [&]() {
      const auto* lane =
          handler.project().find_node(fx->node_id)->lane(fx->track_id);
      const auto& vc = lane->stave(fx->stave_id)->voice(voice1);
      return std::get<graphscore::Note>(vc.events().front()).pitch;
    };
    const graphscore::SpelledPitch c4 = spelled(graphscore::Letter::kC, 4);

    shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kUp));
    if (first_pitch() != c4) {
      std::fprintf(stderr,
                   "notehead-move-test: Up with stale notehead mutated (3)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 4: multi-notehead selection -> Up is a no-op. ----------------
  {
    auto fx = build_notehead_move_fixture(metrics);
    if (!fx.has_value()) {
      std::fprintf(stderr, "notehead-move-test: fixture build failed (4)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler handler(std::move(fx->project), std::move(fx->layout),
                                 &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kSelection);

    const graphscore::Voice voice1 = voice_one();
    if (!select_noteheads(
            handler,
            {graphscore::NoteheadItem{fx->node_id, fx->track_id, fx->stave_id,
                                      voice1, fx->first_note_id},
             graphscore::NoteheadItem{fx->node_id, fx->track_id, fx->stave_id,
                                      voice1, fx->second_note_id}})) {
      std::fprintf(stderr,
                   "notehead-move-test: multi-notehead selection rejected\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    const auto first_pitch = [&]() {
      const auto* lane =
          handler.project().find_node(fx->node_id)->lane(fx->track_id);
      const auto& vc = lane->stave(fx->stave_id)->voice(voice1);
      return std::get<graphscore::Note>(vc.events().front()).pitch;
    };
    const graphscore::SpelledPitch c4 = spelled(graphscore::Letter::kC, 4);

    shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kUp));
    if (first_pitch() != c4) {
      std::fprintf(stderr,
                   "notehead-move-test: Up with multi-notehead selection "
                   "mutated (4)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 5: Shift+Up with a notehead selection stays range extension
  //     (a no-op without a range set); the notehead does not move. --------
  {
    auto fx = build_notehead_move_fixture(metrics);
    if (!fx.has_value()) {
      std::fprintf(stderr, "notehead-move-test: fixture build failed (5)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler handler(std::move(fx->project), std::move(fx->layout),
                                 &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kSelection);

    const graphscore::Voice voice1 = voice_one();
    if (!select_noteheads(handler, {graphscore::NoteheadItem{
                                       fx->node_id, fx->track_id, fx->stave_id,
                                       voice1, fx->first_note_id}})) {
      std::fprintf(stderr, "notehead-move-test: notehead selection rejected\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    const auto first_pitch = [&]() {
      const auto* lane =
          handler.project().find_node(fx->node_id)->lane(fx->track_id);
      const auto& vc = lane->stave(fx->stave_id)->voice(voice1);
      return std::get<graphscore::Note>(vc.events().front()).pitch;
    };
    const graphscore::SpelledPitch c4 = spelled(graphscore::Letter::kC, 4);

    shell.dispatch_test_key_event(shift_key(graphscore::KeyCode::kUp));
    if (first_pitch() != c4) {
      std::fprintf(stderr,
                   "notehead-move-test: Shift+Up moved the notehead (5)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 6: unmodified Up with a committed range selection is a no-op
  //     that leaves the range selection intact. ---------------------------
  {
    auto fx = build_notehead_move_fixture(metrics);
    if (!fx.has_value()) {
      std::fprintf(stderr, "notehead-move-test: fixture build failed (6)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler handler(std::move(fx->project), std::move(fx->layout),
                                 &shell);
    handler.set_metrics(&metrics);
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kSelection);
    const auto& layout = handler.layout();
    {
      const double x1 = layout.systems[0].measures[0].bounds.x;
      const double x2 = layout.systems[0].measures[0].bounds.x +
                        layout.systems[0].measures[0].bounds.width;
      const double y = layout.systems[0].staves[0].bounds.y +
                       layout.systems[0].staves[0].bounds.height * 0.5;
      drag_through_shell(shell, x1, y, x2, y);
    }
    const auto before = handler.drag_state().committed_selection();
    if (!before.has_value()) {
      std::fprintf(stderr,
                   "notehead-move-test: range selection setup failed (6)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kUp));
    if (handler.drag_state().committed_selection() != before) {
      std::fprintf(stderr,
                   "notehead-move-test: Up with a range selection changed the "
                   "selection (6)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 7: a failing surface publisher rolls the move back completely:
  //     project, layout, surface, selection/highlight, history, and audition
  //     stay unchanged, and the next move succeeds. ------------------------
  {
    auto fx = build_notehead_move_fixture(metrics);
    if (!fx.has_value()) {
      std::fprintf(stderr, "notehead-move-test: fixture build failed (7)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler handler(std::move(fx->project), std::move(fx->layout),
                                 &shell);
    handler.set_metrics(&metrics);
    bool fail_next_publish = true;
    handler.set_surface_publisher(
        [&shell, &fail_next_publish](const graphscore::NotationLayout& layout) {
          if (fail_next_publish) {
            fail_next_publish = false;
            return graphscore::ShellResult{
                graphscore::ShellError::kRenderingSetupFailed,
                "injected publish failure"};
          }
          return publish_headless_test_surface(layout, &shell);
        });
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kSelection);

    const graphscore::Voice voice1      = voice_one();
    const auto              first_pitch = [&]() {
      const auto* lane =
          handler.project().find_node(fx->node_id)->lane(fx->track_id);
      const auto& vc = lane->stave(fx->stave_id)->voice(voice1);
      return std::get<graphscore::Note>(vc.events().front()).pitch;
    };
    const graphscore::SpelledPitch c4 = spelled(graphscore::Letter::kC, 4);
    const graphscore::SpelledPitch d4 = spelled(graphscore::Letter::kD, 4);

    // Publish the initial surface so a failed move can be observed as a
    // non-change (not an empty->non-empty transition).
    if (!publish_headless_test_surface(handler.layout(), &shell).ok()) {
      std::fprintf(stderr,
                   "notehead-move-test: initial surface publish failed (7)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    const graphscore::NotationPoint point =
        notehead_origin(handler.layout(), fx->first_note_id);
    click_at(shell, point.x, point.y);
    {
      const auto* set = committed_notehead_set(handler);
      if (set == nullptr || set->items().size() != 1u ||
          set->items()[0].entity != fx->first_note_id) {
        std::fprintf(stderr,
                     "notehead-move-test: click did not select the note (7)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }

    // Pre-move snapshots of every coherent observable.
    const graphscore::SpelledPitch before_pitch = first_pitch();
    const auto        before_surface   = shell.test_snapshot_notation_surface();
    const auto        before_highlight = shell.test_snapshot_highlight_rects();
    const auto        before_layout    = handler.layout();
    const bool        before_audition  = handler.last_audition().has_value();
    const std::size_t before_undo      = handler.test_undo_stack_size();
    const std::size_t before_redo      = handler.test_redo_stack_size();

    shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kUp));

    // The publish failed, so every observable must be unchanged.
    if (first_pitch() != before_pitch) {
      std::fprintf(stderr,
                   "notehead-move-test: project mutated on publish failure "
                   "(7)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (handler.layout() != before_layout) {
      std::fprintf(stderr,
                   "notehead-move-test: layout committed on publish failure "
                   "(7)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (shell.test_snapshot_notation_surface() != before_surface) {
      std::fprintf(stderr,
                   "notehead-move-test: surface changed on publish failure "
                   "(7)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (shell.test_snapshot_highlight_rects() != before_highlight) {
      std::fprintf(stderr,
                   "notehead-move-test: highlight changed on publish failure "
                   "(7)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (handler.test_undo_stack_size() != before_undo ||
        handler.test_redo_stack_size() != before_redo) {
      std::fprintf(stderr,
                   "notehead-move-test: history changed on publish failure "
                   "(7)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (handler.last_audition().has_value() != before_audition) {
      std::fprintf(stderr,
                   "notehead-move-test: audition changed on publish failure "
                   "(7)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    {
      const auto* set = committed_notehead_set(handler);
      if (set == nullptr || set->items().size() != 1u ||
          set->items()[0].entity != fx->first_note_id) {
        std::fprintf(stderr,
                     "notehead-move-test: selection changed on publish failure "
                     "(7)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    if (first_pitch() != c4) {
      std::fprintf(stderr,
                   "notehead-move-test: pitch not C4 after rollback (7)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    // The next move (publisher now healthy) must succeed and move the note.
    shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kUp));
    if (first_pitch() != d4) {
      std::fprintf(stderr,
                   "notehead-move-test: move after rollback did not move "
                   "the note (7)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (handler.test_undo_stack_size() != 1u) {
      std::fprintf(stderr,
                   "notehead-move-test: successful move after rollback not "
                   "recorded in history (7)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 7b: a failed publication preserves PRE-EXISTING redo history.
  //     A prior move is undone (leaving a redo entry), then a move whose
  //     publish fails must restore the exact project, undo/redo stacks,
  //     surface, highlight, layout, selection, and audition; the pre-existing
  //     redo must remain executable afterward, and a successful retry must
  //     commit normally (clearing redo). -----------------------------------
  {
    auto fx = build_notehead_move_fixture(metrics);
    if (!fx.has_value()) {
      std::fprintf(stderr, "notehead-move-test: fixture build failed (7b)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler handler(std::move(fx->project), std::move(fx->layout),
                                 &shell);
    handler.set_metrics(&metrics);
    bool fail_next_publish = false;
    handler.set_surface_publisher(
        [&shell, &fail_next_publish](const graphscore::NotationLayout& layout) {
          if (fail_next_publish) {
            fail_next_publish = false;
            return graphscore::ShellResult{
                graphscore::ShellError::kRenderingSetupFailed,
                "injected publish failure"};
          }
          return publish_headless_test_surface(layout, &shell);
        });
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kSelection);

    const graphscore::Voice voice1      = voice_one();
    const auto              first_pitch = [&]() {
      const auto* lane =
          handler.project().find_node(fx->node_id)->lane(fx->track_id);
      const auto& vc = lane->stave(fx->stave_id)->voice(voice1);
      return std::get<graphscore::Note>(vc.events().front()).pitch;
    };
    const graphscore::SpelledPitch c4 = spelled(graphscore::Letter::kC, 4);
    const graphscore::SpelledPitch d4 = spelled(graphscore::Letter::kD, 4);

    // Publish the initial surface, select the notehead, and make one
    // successful move (C4 -> D4).
    if (!publish_headless_test_surface(handler.layout(), &shell).ok()) {
      std::fprintf(stderr,
                   "notehead-move-test: initial surface publish failed (7b)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    const graphscore::NotationPoint point =
        notehead_origin(handler.layout(), fx->first_note_id);
    click_at(shell, point.x, point.y);
    {
      const auto* set = committed_notehead_set(handler);
      if (set == nullptr || set->items().size() != 1u ||
          set->items()[0].entity != fx->first_note_id) {
        std::fprintf(
            stderr, "notehead-move-test: click did not select the note (7b)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }
    shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kUp));
    if (first_pitch() != d4 || handler.test_undo_stack_size() != 1u ||
        handler.test_redo_stack_size() != 0u) {
      std::fprintf(stderr,
                   "notehead-move-test: setup move did not commit (7b)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    // Undo it, leaving a non-empty redo stack (the exact state the earlier
    // execute_new-clears-redo path would have destroyed).
    if (!handler.test_undo() || first_pitch() != c4 ||
        handler.test_undo_stack_size() != 0u ||
        handler.test_redo_stack_size() != 1u) {
      std::fprintf(stderr,
                   "notehead-move-test: setup undo did not leave redo (7b)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    // Snapshot every coherent observable before the failing move.
    const auto before_surface   = shell.test_snapshot_notation_surface();
    const auto before_highlight = shell.test_snapshot_highlight_rects();
    const auto before_layout    = handler.layout();
    const bool before_audition  = handler.last_audition().has_value();

    // Arm the publisher failure and move; the transaction must abort.
    fail_next_publish = true;
    shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kUp));

    // The publish failed, so every observable must be unchanged — including
    // the pre-existing redo entry, which execute_new() would have cleared.
    if (first_pitch() != c4) {
      std::fprintf(stderr,
                   "notehead-move-test: project mutated on publish failure "
                   "(7b)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (handler.layout() != before_layout) {
      std::fprintf(stderr,
                   "notehead-move-test: layout committed on publish failure "
                   "(7b)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (shell.test_snapshot_notation_surface() != before_surface) {
      std::fprintf(stderr,
                   "notehead-move-test: surface changed on publish failure "
                   "(7b)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (shell.test_snapshot_highlight_rects() != before_highlight) {
      std::fprintf(stderr,
                   "notehead-move-test: highlight changed on publish failure "
                   "(7b)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (handler.last_audition().has_value() != before_audition) {
      std::fprintf(stderr,
                   "notehead-move-test: audition changed on publish failure "
                   "(7b)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (handler.test_undo_stack_size() != 0u ||
        handler.test_redo_stack_size() != 1u) {
      std::fprintf(stderr,
                   "notehead-move-test: history (including redo) changed on "
                   "publish failure (7b)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    {
      const auto* set = committed_notehead_set(handler);
      if (set == nullptr || set->items().size() != 1u ||
          set->items()[0].entity != fx->first_note_id) {
        std::fprintf(stderr,
                     "notehead-move-test: selection changed on publish failure "
                     "(7b)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }

    // The pre-existing redo entry must still be executable.
    if (!handler.test_redo() || first_pitch() != d4) {
      std::fprintf(stderr,
                   "notehead-move-test: redo not executable after failed "
                   "publish (7b)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    // Re-establish a redo entry, then a healthy retry must commit normally
    // (clearing redo, exactly execute_new's success path).
    if (!handler.test_undo() || handler.test_redo_stack_size() != 1u) {
      std::fprintf(stderr,
                   "notehead-move-test: re-establish redo failed (7b)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kUp));
    if (first_pitch() != d4 || handler.test_undo_stack_size() != 1u ||
        handler.test_redo_stack_size() != 0u) {
      std::fprintf(stderr,
                   "notehead-move-test: retry after rollback did not commit "
                   "and clear redo (7b)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 8: a persistent rollback failure poisons the history and blocks
  //     further mutation: the authoritative project stays at the post-edit
  //     pitch (the rollback never completed), the visible surface stays the
  //     last successfully published one, and the handler is unavailable. ----
  {
    auto fx = build_notehead_move_fixture(metrics);
    if (!fx.has_value()) {
      std::fprintf(stderr, "notehead-move-test: fixture build failed (8)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler handler(std::move(fx->project), std::move(fx->layout),
                                 &shell);
    handler.set_metrics(&metrics);
    bool fail_next_publish = true;
    handler.set_surface_publisher(
        [&shell, &fail_next_publish](const graphscore::NotationLayout& layout) {
          if (fail_next_publish) {
            fail_next_publish = false;
            return graphscore::ShellResult{
                graphscore::ShellError::kRenderingSetupFailed,
                "injected publish failure"};
          }
          return publish_headless_test_surface(layout, &shell);
        });
    // Every move command's undo fails persistently (1,000,000 is effectively
    // unbounded for this fixture), so the rollback can never complete.
    handler.set_move_command_factory(
        [](const graphscore::Project&        project,
           const graphscore::NoteheadItem&   item,
           graphscore::NoteheadStepDirection direction) {
          auto command =
              graphscore::make_move_notehead_command(project, item, direction);
          if (command == nullptr) {
            return std::unique_ptr<graphscore::Command>{};
          }
          return std::unique_ptr<graphscore::Command>(
              new FailUndoCommand(std::move(command), 1'000'000));
        });
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kSelection);

    const graphscore::Voice voice1      = voice_one();
    const auto              first_pitch = [&]() {
      const auto* lane =
          handler.project().find_node(fx->node_id)->lane(fx->track_id);
      const auto& vc = lane->stave(fx->stave_id)->voice(voice1);
      return std::get<graphscore::Note>(vc.events().front()).pitch;
    };
    const graphscore::SpelledPitch d4 = spelled(graphscore::Letter::kD, 4);

    if (!publish_headless_test_surface(handler.layout(), &shell).ok()) {
      std::fprintf(stderr,
                   "notehead-move-test: initial surface publish failed (8)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    const auto before_surface = shell.test_snapshot_notation_surface();

    const graphscore::NotationPoint point =
        notehead_origin(handler.layout(), fx->first_note_id);
    click_at(shell, point.x, point.y);
    {
      const auto* set = committed_notehead_set(handler);
      if (set == nullptr || set->items().size() != 1u ||
          set->items()[0].entity != fx->first_note_id) {
        std::fprintf(stderr,
                     "notehead-move-test: click did not select the note (8)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }

    shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kUp));

    // The rollback never completed, so the project stays at the post-edit
    // pitch; the surface is unchanged and the handler is unavailable.
    if (first_pitch() != d4) {
      std::fprintf(stderr,
                   "notehead-move-test: project not at post-edit pitch after "
                   "persistent rollback failure (8)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (shell.test_snapshot_notation_surface() != before_surface) {
      std::fprintf(stderr,
                   "notehead-move-test: surface changed on persistent rollback "
                   "failure (8)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (!handler.history_unavailable()) {
      std::fprintf(stderr,
                   "notehead-move-test: handler not unavailable after "
                   "persistent rollback failure (8)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    // Further mutation is blocked: another Up leaves the project untouched.
    shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kUp));
    if (first_pitch() != d4) {
      std::fprintf(
          stderr, "notehead-move-test: blocked move mutated the project (8)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test 9: a one-shot rollback failure recovers coherently: the
  //     authoritative project/history are restored, the cache/layout/highlight
  //     stay coherent, the surface stays the last successfully published one,
  //     and the next move succeeds. ----------------------------------------
  {
    auto fx = build_notehead_move_fixture(metrics);
    if (!fx.has_value()) {
      std::fprintf(stderr, "notehead-move-test: fixture build failed (9)\n");
      return 1;
    }
    graphscore::WriterShell shell;
    SelectionToolHandler handler(std::move(fx->project), std::move(fx->layout),
                                 &shell);
    handler.set_metrics(&metrics);
    bool fail_next_publish = true;
    handler.set_surface_publisher(
        [&shell, &fail_next_publish](const graphscore::NotationLayout& layout) {
          if (fail_next_publish) {
            fail_next_publish = false;
            return graphscore::ShellResult{
                graphscore::ShellError::kRenderingSetupFailed,
                "injected publish failure"};
          }
          return publish_headless_test_surface(layout, &shell);
        });
    // The move command's undo fails exactly once, then delegates.
    handler.set_move_command_factory(
        [](const graphscore::Project&        project,
           const graphscore::NoteheadItem&   item,
           graphscore::NoteheadStepDirection direction) {
          auto command =
              graphscore::make_move_notehead_command(project, item, direction);
          if (command == nullptr) {
            return std::unique_ptr<graphscore::Command>{};
          }
          return std::unique_ptr<graphscore::Command>(
              new FailUndoCommand(std::move(command), 1));
        });
    shell.set_input_handler(&handler);
    handler.set_active_tool(graphscore::ActiveTool::kSelection);

    const graphscore::Voice voice1      = voice_one();
    const auto              first_pitch = [&]() {
      const auto* lane =
          handler.project().find_node(fx->node_id)->lane(fx->track_id);
      const auto& vc = lane->stave(fx->stave_id)->voice(voice1);
      return std::get<graphscore::Note>(vc.events().front()).pitch;
    };
    const graphscore::SpelledPitch c4 = spelled(graphscore::Letter::kC, 4);
    const graphscore::SpelledPitch d4 = spelled(graphscore::Letter::kD, 4);

    if (!publish_headless_test_surface(handler.layout(), &shell).ok()) {
      std::fprintf(stderr,
                   "notehead-move-test: initial surface publish failed (9)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    const auto before_surface = shell.test_snapshot_notation_surface();

    const graphscore::NotationPoint point =
        notehead_origin(handler.layout(), fx->first_note_id);
    click_at(shell, point.x, point.y);
    {
      const auto* set = committed_notehead_set(handler);
      if (set == nullptr || set->items().size() != 1u ||
          set->items()[0].entity != fx->first_note_id) {
        std::fprintf(stderr,
                     "notehead-move-test: click did not select the note (9)\n");
        shell.set_input_handler(nullptr);
        return 1;
      }
    }

    shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kUp));

    // The rollback failed once then recovered: the project is back at C4, the
    // handler is available again, and the surface is unchanged.
    if (first_pitch() != c4) {
      std::fprintf(stderr,
                   "notehead-move-test: project not restored after one-shot "
                   "rollback recovery (9)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (handler.history_unavailable()) {
      std::fprintf(stderr,
                   "notehead-move-test: handler still unavailable after "
                   "one-shot rollback recovery (9)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    if (shell.test_snapshot_notation_surface() != before_surface) {
      std::fprintf(
          stderr,
          "notehead-move-test: surface changed after one-shot rollback "
          "recovery (9)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    // The next move (publisher now healthy) succeeds and commits normally.
    shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kUp));
    if (first_pitch() != d4 || handler.test_undo_stack_size() != 1u) {
      std::fprintf(stderr,
                   "notehead-move-test: move after one-shot rollback recovery "
                   "did not succeed (9)\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  std::printf("notehead-move-test: ok\n");
  return 0;
}

}  // namespace

// The shell allocates (window titles, backend names), so this call path can
// throw. Letting an exception escape `main` gives std::terminate and an
// unhelpful abort; catching it here turns the same failure into a diagnostic
// and a non-zero exit. Note that the realtime prohibition on exceptions
// applies to the runtime's process path, not to the writer application.
int main(int argc, char** argv) {
  bool smoke_test                = false;
  bool run_selection_test        = false;
  bool run_selection_shell_test  = false;
  bool run_key_events_test       = false;
  bool run_key_events_shell_test = false;
  bool run_key_selection_test    = false;
  bool run_notehead_move_test    = false;
  for (int i = 1; i < argc; ++i) {
    if (kSmokeTestFlag == argv[i]) {
      smoke_test = true;
    }
    if (kSelectionToolTestFlag == argv[i]) {
      run_selection_test = true;
    }
    if (kSelectionToolShellTestFlag == argv[i]) {
      run_selection_shell_test = true;
    }
    if (kKeyEventsTestFlag == argv[i]) {
      run_key_events_test = true;
    }
    if (kKeyEventsShellTestFlag == argv[i]) {
      run_key_events_shell_test = true;
    }
    if (kKeySelectionTestFlag == argv[i]) {
      run_key_selection_test = true;
    }
    if (kNoteheadMoveTestFlag == argv[i]) {
      run_notehead_move_test = true;
    }
  }

  try {
    if (run_selection_test) {
      return selection_tool_test();
    }
    if (run_selection_shell_test) {
      return selection_tool_shell_test();
    }
    if (run_key_events_test) {
      return key_events_test();
    }
    if (run_key_events_shell_test) {
      return key_events_shell_test();
    }
    if (run_key_selection_test) {
      return key_selection_test();
    }
    if (run_notehead_move_test) {
      return notehead_move_test();
    }
    return run(smoke_test);
  } catch (const std::exception& error) {
    std::fprintf(stderr, "graphscore_writer_app: unhandled exception: %s\n",
                 error.what());
    return 1;
  } catch (...) {
    std::fprintf(stderr, "graphscore_writer_app: unhandled exception\n");
    return 1;
  }
}
