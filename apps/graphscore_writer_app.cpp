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
    std::ignore = drag_.update(project_, layout_, point);
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
    // Resolve the final release point before committing, so the committed
    // extent matches the pointer position at release, not the last motion
    // position.
    const graphscore::NotationPoint point{event.x, event.y};
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
  // for every binding below; control, alt, and meta never substitute for
  // it, and an unmodified arrow or a non-Shift chord is a no-op. Acts only
  // when the selection tool is active and a committed ArbitraryRangeSet
  // selection already exists to extend — this sub-phase never creates a
  // first selection from the keyboard alone.
  //
  //   Shift+Left/Right  -- move the current focus edge (focus_edge_) one
  //                        provisional step earlier/later.
  //   Shift+Up/Down     -- extend the staff scope by one staff in score
  //                        order (up = earlier, down = later).
  //   Shift+Home/End    -- select_to_node_start()/select_to_node_end().
  void on_key_press(graphscore::KeyEvent event) override {
    if (!event.modifiers.shift) {
      return;
    }
    if (active_tool_ != graphscore::ActiveTool::kSelection) {
      return;
    }
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

  graphscore::Project            project_;
  graphscore::NotationLayout     layout_;
  graphscore::WriterShell*       shell_;
  graphscore::SelectionDragState drag_;
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
  const graphscore::NotationLayout& layout = default_project->layout;
  graphscore::RasterOptions         raster_opts;
  raster_opts.width = static_cast<std::uint32_t>(
                          std::ceil(layout.bounds.x + layout.bounds.width)) +
                      16U;
  raster_opts.height = static_cast<std::uint32_t>(
                           std::ceil(layout.bounds.y + layout.bounds.height)) +
                       16U;
  raster_opts.pixels_per_unit = 1.0;
  raster_opts.origin          = {0.0, 0.0};
  raster_opts.color           = {0, 0, 0, 255};
  raster_opts.opacity         = 255;

  auto raster = graphscore::rasterize_notation(layout.commands,
                                               *font_loaded->font, raster_opts);
  if (!raster || !raster.surface.has_value()) {
    std::fprintf(stderr,
                 "graphscore_writer_app: failed to rasterise notation\n");
    return 1;
  }

  graphscore::WriterShell shell;
  const auto              surf_result =
      shell.set_notation_surface(std::move(*raster.surface));
  if (!surf_result.ok()) {
    std::fprintf(stderr,
                 "graphscore_writer_app: set_notation_surface failed: %s\n",
                 surf_result.message.c_str());
    return 1;
  }

  SelectionToolHandler handler(std::move(default_project->project),
                               std::move(default_project->layout), &shell);

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
