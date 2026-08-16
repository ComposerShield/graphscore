// SPDX-License-Identifier: Apache-2.0

#include "selection_tool_handler.hpp"

#include <graphscore/domain/graphscore_domain.hpp>
#include <graphscore/notation/graphscore_notation.hpp>
#include <graphscore/writer_shell/graphscore_writer_shell.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace graphscore::writer_app {

namespace {

// The engraver's diatonic index (letter+octave units), duplicated here as an
// app-owned helper because notation_engraving.cpp's own copy is not part of
// the notation layer's public API. These must stay byte-for-byte identical
// to src/notation/notation_engraving.cpp's `diatonic_index` and
// `clef_middle_line`; the action table §8.3 measures the nearest-octave rule
// in exactly these units.
[[nodiscard]] constexpr int diatonic_index(
    const graphscore::SpelledPitch& pitch) noexcept {
  constexpr int kFromC[7] = {5, 6, 0, 1, 2, 3, 4};  // A,B,C,D,E,F,G
  return (static_cast<int>(pitch.octave()) + 1) * 7 +
         kFromC[static_cast<std::size_t>(pitch.letter())];
}

[[nodiscard]] constexpr int clef_middle_line(graphscore::Clef clef) noexcept {
  switch (clef) {
    case graphscore::Clef::kTreble:
      return 41;  // B4
    case graphscore::Clef::kBass:
      return 29;  // D3
    case graphscore::Clef::kAlto:
      return 35;  // C4
    case graphscore::Clef::kTenor:
      return 33;  // A3
  }
  return 41;  // unreachable
}

[[nodiscard]] constexpr int letter_index_from_c(
    graphscore::Letter letter) noexcept {
  constexpr int kFromC[7] = {5, 6, 0, 1, 2, 3, 4};
  return kFromC[static_cast<std::size_t>(letter)];
}

}  // namespace

void SelectionToolHandler::toggle_tool() {
  set_active_tool(active_tool_ == graphscore::ActiveTool::kNoteEntry
                      ? graphscore::ActiveTool::kSelection
                      : graphscore::ActiveTool::kNoteEntry);
}

void SelectionToolHandler::enter_step_entry() {
  initialize_step_entry_cursor();
}

void SelectionToolHandler::exit_step_entry() {
  step_entry_cursor_.reset();
  reset_pitch_reference();
}

void SelectionToolHandler::reset_pitch_reference() noexcept {
  previous_pitch_.reset();
  octave_offset_             = 0;
  previous_pitch_undo_depth_ = 0;
}

// §8.2: deterministic cursor initialization from the committed Selection,
// first match wins, then snapped to the nearest legal caret boundary.
void SelectionToolHandler::initialize_step_entry_cursor() {
  step_entry_cursor_.reset();
  reset_pitch_reference();

  const graphscore::Node* node = project_.find_node(layout_.node_id);
  if (node == nullptr) {
    post_diagnostic("step entry: node is missing");
    return;
  }
  const graphscore::NodeTimeline* timeline = node->timeline();
  if (timeline == nullptr) {
    post_diagnostic("step entry: node has no timeline");
    return;
  }

  std::optional<StepEntryCursor> cursor;
  const auto&                    committed = drag_.committed_selection();
  if (committed.has_value()) {
    if (const auto* caret_set =
            std::get_if<graphscore::InsertionCaretSet>(&*committed)) {
      if (caret_set->items().size() == 1u) {
        const auto& item = caret_set->items().front();
        cursor = StepEntryCursor{item.node, item.track, item.stave, item.voice,
                                 item.position};
      }
    } else if (const auto* notehead_set =
                   std::get_if<graphscore::NoteheadSet>(&*committed)) {
      if (notehead_set->items().size() == 1u) {
        const auto& item  = notehead_set->items().front();
        const auto  onset = event_onset(item.node, item.track, item.stave,
                                        item.voice, item.entity);
        if (onset.has_value()) {
          cursor = StepEntryCursor{item.node, item.track, item.stave,
                                   item.voice, *onset};
        }
      }
    } else if (const auto* chord_set =
                   std::get_if<graphscore::ChordSet>(&*committed)) {
      if (chord_set->items().size() == 1u) {
        const auto& item  = chord_set->items().front();
        const auto  onset = event_onset(item.node, item.track, item.stave,
                                        item.voice, item.entity);
        if (onset.has_value()) {
          cursor = StepEntryCursor{item.node, item.track, item.stave,
                                   item.voice, *onset};
        }
      }
    } else if (const auto* rest_set =
                   std::get_if<graphscore::RestSet>(&*committed)) {
      if (rest_set->items().size() == 1u) {
        const auto& item  = rest_set->items().front();
        const auto  onset = event_onset(item.node, item.track, item.stave,
                                        item.voice, item.entity);
        if (onset.has_value()) {
          cursor = StepEntryCursor{item.node, item.track, item.stave,
                                   item.voice, *onset};
        }
      }
    } else if (const auto* range_set =
                   std::get_if<graphscore::ArbitraryRangeSet>(&*committed)) {
      if (!range_set->items().empty()) {
        const auto& item = range_set->items().front();
        cursor = StepEntryCursor{item.node, item.track, item.stave, item.voice,
                                 item.span.start};
      }
    } else if (const auto* measure_set =
                   std::get_if<graphscore::FullMeasureSet>(&*committed)) {
      if (!measure_set->items().empty()) {
        if (!graphscore::validate_selection(project_, *committed).empty()) {
          post_diagnostic("step entry: stale full-measure selection");
          return;
        }
        const auto& item          = measure_set->items().front();
        const auto  measure_start = measure_start_at(
            item.node, item.track, item.stave, item.measure_index);
        if (!measure_start.has_value()) {
          // A stale full-measure selection (out-of-range measure index,
          // archived track, or absent stave) is rejected rather than
          // indexing past measure_count() or re-deriving a wrong cursor.
          post_diagnostic("step entry: stale full-measure selection");
          return;
        }
        cursor = StepEntryCursor{item.node, item.track, item.stave,
                                 palette_.voice(), *measure_start};
      }
    }
    // MarkingSet / NodeSet / ConnectorSet name no voice+position of their
    // own; they fall through to rule 6 below.
  }

  if (!cursor.has_value()) {
    // Rule 6: the node's first staff in score order, the armed voice, and
    // position 0.
    for (const auto& scope : graphscore::score_ordered_staves(project_)) {
      const graphscore::TrackLane* lane = node->lane(scope.track_id);
      if (lane == nullptr || lane->stave(scope.stave_id) == nullptr) {
        continue;
      }
      cursor = StepEntryCursor{layout_.node_id, scope.track_id, scope.stave_id,
                               palette_.voice(), graphscore::Rational(0)};
      break;
    }
  }

  if (!cursor.has_value()) {
    post_diagnostic("step entry: no staff for the node");
    return;
  }

  // Archived/absent track or stave: no cursor, inert keys with a diagnostic.
  if (project_.find_active_track(cursor->track) == nullptr) {
    post_diagnostic("step entry: track is archived or absent");
    return;
  }
  const graphscore::TrackLane* lane = node->lane(cursor->track);
  if (lane == nullptr || lane->stave(cursor->stave) == nullptr) {
    post_diagnostic("step entry: stave is absent");
    return;
  }

  const auto snapped = snap_caret(cursor->node, cursor->track, cursor->stave,
                                  cursor->voice, cursor->position);
  if (!snapped.has_value()) {
    post_diagnostic("step entry: no legal caret");
    return;
  }
  cursor->position   = *snapped;
  step_entry_cursor_ = cursor;
}

// The nearest legal caret boundary in the target voice: position 0, an exact
// event boundary, or TrackLane::total_length(), ties resolving earlier.
std::optional<graphscore::Rational> SelectionToolHandler::snap_caret(
    graphscore::NodeId node_id, graphscore::TrackId track,
    graphscore::StaveId stave, graphscore::Voice voice,
    graphscore::Rational position) const {
  const graphscore::Node* node = project_.find_node(node_id);
  if (node == nullptr) {
    return std::nullopt;
  }
  const graphscore::TrackLane* lane = node->lane(track);
  if (lane == nullptr) {
    return std::nullopt;
  }
  const graphscore::StaveVoices* stave_voices = lane->stave(stave);
  if (stave_voices == nullptr) {
    return std::nullopt;
  }
  const graphscore::VoiceContent& voice_content = stave_voices->voice(voice);

  std::vector<graphscore::Rational> candidates;
  candidates.push_back(graphscore::Rational(0));
  graphscore::Rational onset(0);
  for (const auto& event : voice_content.events()) {
    candidates.push_back(onset);
    onset = onset + graphscore::event_duration(event).resolved();
  }

  graphscore::Rational best = candidates.front();
  graphscore::Rational best_dist =
      best >= position ? best - position : position - best;
  const auto consider = [&](graphscore::Rational candidate) {
    const graphscore::Rational dist =
        candidate >= position ? candidate - position : position - candidate;
    if (dist < best_dist || (dist == best_dist && candidate < best)) {
      best      = candidate;
      best_dist = dist;
    }
  };
  for (const graphscore::Rational candidate : candidates) {
    consider(candidate);
  }
  consider(lane->total_length());
  return best;
}

std::optional<graphscore::Rational> SelectionToolHandler::event_onset(
    graphscore::NodeId node_id, graphscore::TrackId track,
    graphscore::StaveId stave, graphscore::Voice voice,
    graphscore::NotationEntityId entity) const {
  const graphscore::VoiceContent* voice_content =
      resolve_voice(node_id, track, stave, voice);
  if (voice_content == nullptr) {
    return std::nullopt;
  }
  return voice_content->position_of_event(entity);
}

std::optional<graphscore::Clef> SelectionToolHandler::active_clef_at_cursor()
    const {
  if (!step_entry_cursor_.has_value()) {
    return std::nullopt;
  }
  const StepEntryCursor&  cursor = *step_entry_cursor_;
  const graphscore::Node* node   = project_.find_node(cursor.node);
  if (node == nullptr || node->timeline() == nullptr) {
    return std::nullopt;
  }
  const graphscore::NodeTimeline* timeline = node->timeline();
  if (timeline->has_clef_lane(cursor.stave)) {
    return timeline->clef_lane(cursor.stave)->clef_at(cursor.position);
  }
  const graphscore::Track* track = project_.find_active_track(cursor.track);
  if (track == nullptr) {
    return std::nullopt;
  }
  for (const auto& stave_def : track->layout().staves()) {
    if (stave_def.id == cursor.stave) {
      return stave_def.default_clef;
    }
  }
  return std::nullopt;
}

// §8.3 nearest-octave rule, in the engraver's diatonic_index units. The
// octave_offset is applied to the reference first; the nearest octave for the
// target letter is unique because one octave is seven diatonic steps (odd).
std::optional<graphscore::SpelledPitch>
SelectionToolHandler::resolve_pitch_for_letter(
    graphscore::Letter letter) const {
  int reference;
  if (previous_pitch_.has_value()) {
    reference = diatonic_index(*previous_pitch_) + 7 * octave_offset_;
  } else {
    const auto clef = active_clef_at_cursor();
    if (!clef.has_value()) {
      return std::nullopt;
    }
    reference = clef_middle_line(*clef) + 7 * octave_offset_;
  }

  const int offset     = 7 + letter_index_from_c(letter);
  const int delta      = reference - offset;
  int       octave_low = delta / 7;
  if (delta < 0 && delta % 7 != 0) {
    --octave_low;  // floor division for negative deltas
  }
  const int octave_high = octave_low + 1;
  const int dist_low    = std::abs(7 * octave_low + offset - reference);
  const int dist_high   = std::abs(7 * octave_high + offset - reference);
  const int octave      = dist_low <= dist_high ? octave_low : octave_high;

  const auto pitch =
      graphscore::SpelledPitch::create(letter, static_cast<std::int8_t>(octave),
                                       graphscore::Accidental::kNatural);
  if (!pitch.has_value() || !pitch->to_midi_pitch().has_value()) {
    return std::nullopt;
  }
  return pitch;
}

void SelectionToolHandler::record_step_entry_previous_pitch(
    const graphscore::SpelledPitch& pitch) {
  previous_pitch_ = graphscore::SpelledPitch::create(
      pitch.letter(), pitch.octave(), graphscore::Accidental::kNatural);
  // The producer of this reference is the command just committed, which sits
  // at index undo_stack_size() - 1; record the depth so a later undo knows
  // whether it removed that producer (see undo_action, §8.3).
  previous_pitch_undo_depth_ = history_.undo_stack_size();
}

std::optional<graphscore::NotationInvalidation>
SelectionToolHandler::step_entry_invalidation(
    graphscore::NodeId node_id, bool voice_was_empty,
    graphscore::Rational position) const {
  const graphscore::Node* node = project_.find_node(node_id);
  if (node == nullptr || node->timeline() == nullptr) {
    return std::nullopt;
  }
  const graphscore::MeasureMap& measures = node->timeline()->measures();
  const std::size_t             count    = measures.measure_count();
  if (count == 0) {
    return std::nullopt;
  }
  if (voice_was_empty) {
    // The empty-voice commit materializes the whole voice's measure-aligned
    // rest stream, so every measure's content can change.
    return graphscore::NotationInvalidation{
        graphscore::NotationInvalidationKind::kLocalContent, 0, count - 1};
  }
  const auto measure = measures.measure_index_at(position);
  if (!measure.has_value()) {
    return std::nullopt;
  }
  return graphscore::NotationInvalidation{
      graphscore::NotationInvalidationKind::kLocalContent, *measure, *measure};
}

std::optional<graphscore::Selection>
SelectionToolHandler::selection_after_step_entry_commit(
    graphscore::NodeId node, graphscore::TrackId track,
    graphscore::StaveId stave, graphscore::Voice voice,
    graphscore::Rational                    position,
    std::optional<graphscore::SpelledPitch> pitch) const {
  const graphscore::VoiceContent* voice_content =
      resolve_voice(node, track, stave, voice);
  if (voice_content == nullptr) {
    return std::nullopt;
  }
  const auto index = voice_content->find_event_index_at(position);
  if (!index.has_value()) {
    return std::nullopt;
  }
  const graphscore::VoiceEvent& event = voice_content->events()[*index];
  if (const auto* rest = std::get_if<graphscore::Rest>(&event)) {
    const auto set = graphscore::RestSet::create(
        {graphscore::RestItem{node, track, stave, voice, rest->id}});
    if (!set.has_value()) {
      return std::nullopt;
    }
    return graphscore::Selection{*set};
  }
  if (const auto* note = std::get_if<graphscore::Note>(&event)) {
    const auto set = graphscore::NoteheadSet::create(
        {graphscore::NoteheadItem{node, track, stave, voice, note->id}});
    if (!set.has_value()) {
      return std::nullopt;
    }
    return graphscore::Selection{*set};
  }
  if (const auto* chord = std::get_if<graphscore::Chord>(&event)) {
    if (!pitch.has_value()) {
      return std::nullopt;
    }
    for (const auto& chord_note : chord->notes) {
      if (chord_note.pitch == *pitch) {
        const auto set =
            graphscore::NoteheadSet::create({graphscore::NoteheadItem{
                node, track, stave, voice, chord_note.id}});
        if (!set.has_value()) {
          return std::nullopt;
        }
        return graphscore::Selection{*set};
      }
    }
  }
  return std::nullopt;
}

bool SelectionToolHandler::commit_step_entry(
    graphscore::NotePaletteEntryKind        kind,
    std::optional<graphscore::SpelledPitch> pitch) {
  if (history_.poisoned()) {
    post_diagnostic("step entry: history unavailable");
    return false;
  }
  if (!step_entry_cursor_.has_value()) {
    post_diagnostic("step entry: no cursor");
    return false;
  }
  const StepEntryCursor cursor = *step_entry_cursor_;
  return commit_note_entry_at(cursor.node, cursor.track, cursor.stave,
                              cursor.voice, cursor.position, kind, pitch,
                              /*advance_cursor=*/true);
}

std::unique_ptr<graphscore::Command>
SelectionToolHandler::make_note_entry_command_at(
    graphscore::NodeId node, graphscore::TrackId track,
    graphscore::StaveId stave, graphscore::Voice voice,
    graphscore::Rational position, graphscore::NotePaletteEntryKind kind,
    std::optional<graphscore::SpelledPitch> pitch) const {
  if (!live_stave(node, track, stave) ||
      (kind == graphscore::NotePaletteEntryKind::kNote && !pitch.has_value())) {
    return nullptr;
  }
  const graphscore::Node*         node_ptr = project_.find_node(node);
  const graphscore::NodeTimeline* timeline =
      node_ptr != nullptr ? node_ptr->timeline() : nullptr;
  if (timeline == nullptr || position < graphscore::Rational(0) ||
      position >= timeline->node_end()) {
    return nullptr;
  }
  graphscore::NotePaletteEntrySpec armed;
  armed.duration   = palette_.resolved_duration();
  armed.entry_kind = kind;
  armed.voice      = voice;
  return graphscore::make_note_entry_command(project_, node, track, stave,
                                             position, armed, pitch);
}

bool SelectionToolHandler::commit_note_entry_at(
    graphscore::NodeId node, graphscore::TrackId track,
    graphscore::StaveId stave, graphscore::Voice voice,
    graphscore::Rational position, graphscore::NotePaletteEntryKind kind,
    std::optional<graphscore::SpelledPitch> pitch, bool advance_cursor) {
  const graphscore::VoiceContent* voice_content =
      resolve_voice(node, track, stave, voice);
  if (voice_content == nullptr) {
    post_diagnostic("step entry: no such voice");
    return false;
  }
  const bool voice_was_empty = voice_content->events().empty();

  if (kind == graphscore::NotePaletteEntryKind::kNote && !pitch.has_value()) {
    post_diagnostic("step entry: pitch out of range");
    return false;
  }

  std::unique_ptr<graphscore::Command> command = make_note_entry_command_at(
      node, track, stave, voice, position, kind, pitch);
  if (command == nullptr) {
    // No event boundary at the caret and no fill can be produced (node end,
    // node has no timeline, etc.) — a rejected commit that consumes nothing.
    post_diagnostic("step entry: no event boundary or fill at the caret");
    return false;
  }

  const auto invalidation =
      step_entry_invalidation(node, voice_was_empty, position);
  if (!invalidation.has_value()) {
    post_diagnostic("step entry: cannot locate the affected measure");
    return false;
  }

  graphscore::CommandHistory::Transaction transaction =
      history_.begin_transaction(std::move(command), project_);
  if (!transaction.active()) {
    post_diagnostic("step entry: commit rejected");
    return false;
  }
  if (!refresh_layout(invalidation)) {
    const graphscore::Result rollback = transaction.abort();
    if (!rollback.ok()) {
      recover_from_failed_rollback();
      return false;
    }
    layout_cache_.reset();
    warm_layout_cache();
    post_diagnostic("step entry: layout refresh failed");
    return false;
  }
  if (!transaction.commit().ok()) {
    post_diagnostic("step entry: commit failed");
    return false;
  }

  // Success: select the inserted notehead/rest, update the pitch reference,
  // and position the cursor. The rejected path above already leaves the
  // cursor and pitch reference (octave_offset included) unchanged.
  const auto next_selection = selection_after_step_entry_commit(
      node, track, stave, voice, position, pitch);
  if (next_selection.has_value()) {
    set_committed_selection(std::move(next_selection));
  }

  if (kind == graphscore::NotePaletteEntryKind::kNote) {
    record_step_entry_previous_pitch(*pitch);
    octave_offset_ = 0;  // single-shot: consumed by this successful commit.
  }

  if (advance_cursor) {
    // §8.5: after a base commit the cursor advances by the armed duration's
    // resolved length and snaps to the nearest legal caret boundary.
    const graphscore::Node*      node_ptr = project_.find_node(node);
    const graphscore::TrackLane* lane =
        node_ptr != nullptr ? node_ptr->lane(track) : nullptr;
    const graphscore::Rational lane_end =
        lane != nullptr ? lane->total_length() : position;
    graphscore::Rational advanced =
        position + palette_.resolved_duration().resolved();
    if (advanced > lane_end) {
      advanced = lane_end;  // clamp to node end, itself a legal caret.
    }
    const auto snapped = snap_caret(node, track, stave, voice, advanced);
    if (snapped.has_value()) {
      step_entry_cursor_->position = *snapped;
    }
  } else {
    // §8.4: pointer note entry repositions the cursor to the click onset; the
    // pointer, not the armed duration, positions the next commit.
    step_entry_cursor_ = StepEntryCursor{node, track, stave, voice, position};
  }
  return true;
}

bool SelectionToolHandler::pointer_note_entry(graphscore::NotationPoint point) {
  if (active_tool_ != graphscore::ActiveTool::kNoteEntry) {
    return false;
  }
  if (history_.poisoned()) {
    post_diagnostic("note entry: history unavailable");
    return false;
  }
  const auto preview =
      graphscore::preview_note_entry(project_, layout_, palette_, point);
  if (!preview.has_value()) {
    post_diagnostic("note entry: no valid onset or pitch at the click");
    return false;
  }
  // Commit at the resolved onset, then reposition the cursor to that onset
  // (§8.4): the pointer, not the armed duration, positions the next commit.
  // The preview's own voice is the target (it is the armed palette voice the
  // preview resolved with), never the palette's current voice.
  return commit_note_entry_at(
      layout_.node_id, preview->track_id, preview->stave_id, preview->voice,
      preview->candidate_onset, preview->entry_kind, preview->candidate_pitch,
      /*advance_cursor=*/false);
}

bool SelectionToolHandler::step_entry_commit_pitch(graphscore::Letter letter) {
  // Distinguish the "no clef" rejection (a distinct §8.5 case) from the
  // out-of-range spelling/MIDI rejection, so the composer sees the right one.
  if (!previous_pitch_.has_value() && !active_clef_at_cursor().has_value()) {
    post_diagnostic("step entry: no clef for the cursor stave");
    return false;
  }
  const auto pitch = resolve_pitch_for_letter(letter);
  if (!pitch.has_value()) {
    post_diagnostic("step entry: pitch out of range");
    return false;
  }
  return commit_step_entry(graphscore::NotePaletteEntryKind::kNote, pitch);
}

bool SelectionToolHandler::step_entry_commit_rest() {
  return commit_step_entry(graphscore::NotePaletteEntryKind::kRest,
                           std::nullopt);
}

bool SelectionToolHandler::step_entry_arm_duration(
    graphscore::NoteValue value) {
  palette_ = palette_.with_note_value(value);
  return true;
}

bool SelectionToolHandler::step_entry_cycle_dots() {
  const auto next =
      palette_.with_dots(static_cast<std::uint8_t>((palette_.dots() + 1) % 3));
  if (!next.has_value()) {
    return false;
  }
  palette_ = *next;
  return true;
}

bool SelectionToolHandler::step_entry_arm_voice(graphscore::Voice voice) {
  if (palette_.voice() != voice) {
    palette_ = palette_.with_voice(voice);
    reset_pitch_reference();  // voice changed → invalidate the reference.
    // §8.4: the cursor's voice changes to the armed voice, so the next
    // commit targets the newly armed voice rather than the previous one.
    if (step_entry_cursor_.has_value()) {
      step_entry_cursor_->voice = voice;
    }
  }
  return true;
}

bool SelectionToolHandler::step_entry_step_octave(int direction) {
  // Checked, saturating single-octave steps in [-8, +8]; at a bound the chord
  // is a no-op (§7.5, §8.3).
  if (direction > 0) {
    if (octave_offset_ >= 8) {
      return false;
    }
    ++octave_offset_;
    return true;
  }
  if (octave_offset_ <= -8) {
    return false;
  }
  --octave_offset_;
  return true;
}

bool SelectionToolHandler::octave_step_available(int direction) const noexcept {
  if (active_tool_ != graphscore::ActiveTool::kNoteEntry) {
    return false;
  }
  return direction > 0 ? octave_offset_ < 8 : octave_offset_ > -8;
}

// §8.4 staff step in kNoteEntry: move the cursor to the same voice and
// position on the prior/next staff (wrapping), resetting the pitch reference.
bool SelectionToolHandler::step_cursor_staff(
    graphscore::StaffStepDirection direction) {
  if (!step_entry_cursor_.has_value()) {
    return false;
  }
  const graphscore::Node* node = project_.find_node(step_entry_cursor_->node);
  if (node == nullptr) {
    return false;
  }
  std::vector<graphscore::MeasureScope> filtered;
  for (const auto& scope : graphscore::score_ordered_staves(project_)) {
    const graphscore::TrackLane* lane = node->lane(scope.track_id);
    if (lane != nullptr && lane->stave(scope.stave_id) != nullptr) {
      filtered.push_back(scope);
    }
  }
  if (filtered.size() < 2) {
    return false;  // a single-staff node never steps.
  }
  const graphscore::MeasureScope current{step_entry_cursor_->track,
                                         step_entry_cursor_->stave};
  const auto it = std::find(filtered.begin(), filtered.end(), current);
  if (it == filtered.end()) {
    return false;
  }
  const std::size_t index =
      static_cast<std::size_t>(std::distance(filtered.begin(), it));
  const std::size_t count = filtered.size();
  const std::size_t target =
      direction == graphscore::StaffStepDirection::kPrevious
          ? (index + count - 1) % count
          : (index + 1) % count;
  step_entry_cursor_->track = filtered[target].track_id;
  step_entry_cursor_->stave = filtered[target].stave_id;
  reset_pitch_reference();  // staff changed → invalidate the reference.
  return true;
}

// Adds a diatonic interval above/below the single selected notehead and, when
// the step-entry cursor is live, updates the pitch reference to the inserted
// notehead's natural spelling (§8.3: "the inserted notehead of a 2-8 interval
// built on a step-entry notehead"). Reuses add_selected_interval so the two
// routes can never drift on the mutation itself.
bool SelectionToolHandler::run_interval_action(
    graphscore::IntervalDirection direction, std::uint8_t interval) {
  if (!add_selected_interval(direction, interval)) {
    return false;
  }
  if (!step_entry_cursor_.has_value()) {
    return true;
  }
  const auto* set = current_notehead_set();
  if (set == nullptr || set->items().size() != 1u) {
    return true;
  }
  const graphscore::NoteheadItem& item = set->items().front();
  const graphscore::VoiceContent* voice_content =
      resolve_voice(item.node, item.track, item.stave, item.voice);
  if (voice_content == nullptr) {
    return true;
  }
  const auto position = voice_content->position_of_event(item.entity);
  if (!position.has_value()) {
    return true;
  }
  const auto index = voice_content->find_event_index_at(*position);
  if (!index.has_value()) {
    return true;
  }
  const graphscore::VoiceEvent& event = voice_content->events()[*index];
  if (const auto* note = std::get_if<graphscore::Note>(&event)) {
    record_step_entry_previous_pitch(note->pitch);
  } else if (const auto* chord = std::get_if<graphscore::Chord>(&event)) {
    for (const auto& chord_note : chord->notes) {
      if (chord_note.id == item.entity) {
        record_step_entry_previous_pitch(chord_note.pitch);
        break;
      }
    }
  }
  return true;
}

// ---- test access -----------------------------------------------------------

std::optional<StepEntryCursor> SelectionToolHandler::step_entry_cursor()
    const noexcept {
  return step_entry_cursor_;
}

std::optional<graphscore::SpelledPitch> SelectionToolHandler::previous_pitch()
    const noexcept {
  return previous_pitch_;
}

int SelectionToolHandler::octave_offset() const noexcept {
  return octave_offset_;
}

graphscore::NoteValue SelectionToolHandler::armed_note_value() const noexcept {
  return palette_.note_value();
}

std::uint8_t SelectionToolHandler::armed_dots() const noexcept {
  return palette_.dots();
}

graphscore::Voice SelectionToolHandler::armed_voice() const noexcept {
  return palette_.voice();
}

}  // namespace graphscore::writer_app
