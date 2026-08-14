// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/delete_notehead_command.hpp>

#include <algorithm>
#include <cstddef>
#include <new>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <graphscore/domain/node.hpp>
#include <graphscore/domain/node_timeline.hpp>
#include <graphscore/domain/notation_validation.hpp>
#include <graphscore/domain/project.hpp>
#include <graphscore/domain/track.hpp>
#include "event_replacement_helpers.hpp"
#include "marking_command_helpers.hpp"

namespace graphscore {
namespace {

struct EventLocation {
  std::size_t index = 0;
  Rational    onset;
};

std::optional<EventLocation> find_event(const VoiceContent& voice,
                                        NotationEntityId    id) {
  Rational onset(0);
  for (std::size_t index = 0; index < voice.events().size(); ++index) {
    const VoiceEvent& event = voice.events()[index];
    if (event_id(event) == id)
      return EventLocation{index, onset};
    if (const auto* chord = std::get_if<Chord>(&event)) {
      for (const ChordNote& note : chord->notes) {
        if (note.id == id)
          return EventLocation{index, onset};
      }
    }
    onset = onset + event_duration(event).resolved();
  }
  return std::nullopt;
}

}  // namespace

Result DeleteNoteheadCommand::execute(Project& project) noexcept {
  if (state_ != State::kFresh)
    return Result(ResultCode::kInvalidArgument);

  VoiceContent* voice =
      internal::resolve_voice(project, node_id_, track_id_, stave_id_, voice_);
  if (voice == nullptr)
    return Result(ResultCode::kInvalidArgument);

  Node*               node     = project.find_node(node_id_);
  const NodeTimeline* timeline = node->timeline();
  if (timeline == nullptr)
    return Result(ResultCode::kInvalidArgument);

  try {
    VoiceContent                       pre_snapshot = *voice;
    VoiceContent                       candidate    = pre_snapshot;
    const std::optional<EventLocation> location =
        find_event(candidate, notehead_id_);
    Result result;

    if (location.has_value()) {
      const VoiceEvent& event = candidate.events()[location->index];
      VoiceEvent        replacement;
      bool              found_notehead = false;

      if (const auto* note = std::get_if<Note>(&event)) {
        if (note->id != notehead_id_)
          return Result(ResultCode::kInvalidArgument);
        replacement    = VoiceEvent(Rest{note->id, note->duration});
        found_notehead = true;
      } else if (const auto* chord = std::get_if<Chord>(&event)) {
        std::vector<ChordNote> remaining;
        remaining.reserve(chord->notes.size());
        for (const ChordNote& chord_note : chord->notes) {
          if (chord_note.id == notehead_id_) {
            found_notehead = true;
          } else {
            remaining.push_back(chord_note);
          }
        }
        if (!found_notehead)
          return Result(ResultCode::kInvalidArgument);
        if (remaining.empty()) {
          replacement = VoiceEvent(Rest{chord->id, chord->duration});
        } else if (remaining.size() == 1u) {
          const ChordNote& remaining_note = remaining.front();
          replacement                     = VoiceEvent(Note{
              remaining_note.id, remaining_note.pitch, chord->duration,
              remaining_note.tied_to_next, chord->articulations, chord->stem});
        } else {
          Chord reduced = *chord;
          reduced.notes = std::move(remaining);
          replacement   = VoiceEvent(std::move(reduced));
        }
      }

      if (!found_notehead)
        return Result(ResultCode::kInvalidArgument);
      const std::optional<NotationEntityId> rest_id =
          std::holds_alternative<Rest>(replacement)
              ? std::optional<NotationEntityId>(event_id(replacement))
              : std::nullopt;
      result = internal::clear_incoming_ties(candidate, location->index,
                                             replacement, timeline->node_end());
      if (!result.ok())
        return result;
      result = candidate.replace_event(location->onset, std::move(replacement),
                                       timeline->node_end());
      if (!result.ok())
        return result;
      // Replacing a sounding event with a Rest invalidates every reference
      // whose validity depends on it staying sounding or beamable; normalize
      // them all before the candidate is validated.
      if (rest_id.has_value()) {
        result = internal::normalize_references_for_replaced_event(candidate,
                                                                   *rest_id);
        if (!result.ok())
          return result;
      }
    } else {
      result = candidate.remove_grace_group_note(notehead_id_);
    }

    if (!result.ok())
      return result;
    result = candidate.validate();
    if (!result.ok())
      return result;
    if (!validate_voice_references(candidate).empty())
      return Result(ResultCode::kInvalidArgument);

    pre_snapshot_  = std::move(pre_snapshot);
    post_snapshot_ = candidate;
    *voice         = std::move(candidate);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }

  state_ = State::kDone;
  return Result();
}

Result DeleteNoteheadCommand::undo(Project& project) noexcept {
  if (state_ != State::kDone)
    return Result(ResultCode::kInvalidArgument);
  if (!pre_snapshot_.has_value())
    return Result(ResultCode::kInternalError);

  const Result result =
      internal::voice_restore_snapshot(pre_snapshot_, post_snapshot_, node_id_,
                                       track_id_, stave_id_, voice_, project);
  if (!result.ok())
    return result;
  state_ = State::kUndone;
  return Result();
}

Result DeleteNoteheadCommand::redo(Project& project) noexcept {
  if (state_ != State::kUndone)
    return Result(ResultCode::kInvalidArgument);
  if (!post_snapshot_.has_value())
    return Result(ResultCode::kInternalError);

  const Result result =
      internal::voice_restore_snapshot(post_snapshot_, pre_snapshot_, node_id_,
                                       track_id_, stave_id_, voice_, project);
  if (!result.ok())
    return result;
  state_ = State::kDone;
  return Result();
}

}  // namespace graphscore
