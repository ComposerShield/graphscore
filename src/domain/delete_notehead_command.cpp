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

// Removes every slur whose start or end event is `deleted_id` and every
// grace group whose principal event is `deleted_id`, and normalizes every
// beam override that references `deleted_id`: the deleted id is dropped from
// the override's run, and the override is removed when the surviving run is
// empty, fewer than two events, or no longer a contiguous beamable run.
// Unaffected records keep their ids and relative order. `deleted_id` is the
// event id the delete replaced with a Rest; only references whose validity
// depends on that event staying sounding or beamable are touched.
Result normalize_references_for_deleted_event(VoiceContent&    voice,
                                              NotationEntityId deleted_id) {
  std::vector<NotationEntityId> slurs_to_remove;
  for (const Slur& slur : voice.slurs()) {
    if (slur.start_event == deleted_id || slur.end_event == deleted_id)
      slurs_to_remove.push_back(slur.id);
  }
  for (const NotationEntityId id : slurs_to_remove) {
    const Result result = voice.remove_slur(id);
    if (!result.ok())
      return result;
  }

  std::vector<NotationEntityId> groups_to_remove;
  for (const GraceGroup& group : voice.grace_groups()) {
    if (group.principal_event == deleted_id)
      groups_to_remove.push_back(group.id);
  }
  for (const NotationEntityId id : groups_to_remove) {
    const Result result = voice.remove_grace_group(id);
    if (!result.ok())
      return result;
  }

  std::unordered_map<NotationEntityId, std::size_t> positions;
  const std::vector<VoiceEvent>&                    events = voice.events();
  positions.reserve(events.size());
  for (std::size_t index = 0; index < events.size(); ++index)
    positions.emplace(event_id(events[index]), index);

  struct BeamPlan {
    NotationEntityId            id;
    std::optional<BeamOverride> replacement;
  };

  std::vector<BeamPlan> beam_plan;
  for (const BeamOverride& override : voice.beam_overrides()) {
    const auto hit =
        std::find(override.events.begin(), override.events.end(), deleted_id);
    if (hit == override.events.end())
      continue;

    std::vector<NotationEntityId> reduced;
    reduced.reserve(override.events.size());
    for (const NotationEntityId eid : override.events) {
      if (eid != deleted_id)
        reduced.push_back(eid);
    }

    bool keep = reduced.size() >= 2u;
    if (keep) {
      std::vector<std::size_t> indices;
      indices.reserve(reduced.size());
      for (const NotationEntityId eid : reduced) {
        const auto it = positions.find(eid);
        if (it == positions.end() || !event_is_beamable(events[it->second])) {
          keep = false;
          break;
        }
        indices.push_back(it->second);
      }
      if (keep) {
        for (std::size_t i = 1; i < indices.size(); ++i) {
          if (indices[i] != indices[i - 1] + 1u) {
            keep = false;
            break;
          }
        }
      }
    }

    if (keep) {
      BeamOverride updated = override;
      updated.events       = std::move(reduced);
      beam_plan.push_back(BeamPlan{override.id, std::move(updated)});
    } else {
      beam_plan.push_back(BeamPlan{override.id, std::nullopt});
    }
  }

  for (const BeamPlan& plan : beam_plan) {
    const Result result =
        plan.replacement.has_value()
            ? voice.replace_beam_override(plan.id, *plan.replacement)
            : voice.remove_beam_override(plan.id);
    if (!result.ok())
      return result;
  }
  return Result();
}

Result clear_incoming_ties(VoiceContent& voice, const EventLocation location,
                           const VoiceEvent& replacement, Rational node_end) {
  if (location.index == 0u)
    return Result();

  Rational previous_onset(0);
  for (std::size_t index = 0; index + 1u < location.index; ++index)
    previous_onset =
        previous_onset + event_duration(voice.events()[index]).resolved();

  const VoiceEvent& previous = voice.events()[location.index - 1u];
  VoiceEvent        modified = previous;
  bool              changed  = false;
  if (auto* note = std::get_if<Note>(&modified)) {
    if (note->tied_to_next && !event_sounds_pitch(replacement, note->pitch)) {
      note->tied_to_next = false;
      changed            = true;
    }
  } else if (auto* chord = std::get_if<Chord>(&modified)) {
    for (ChordNote& chord_note : chord->notes) {
      if (chord_note.tied_to_next &&
          !event_sounds_pitch(replacement, chord_note.pitch)) {
        chord_note.tied_to_next = false;
        changed                 = true;
      }
    }
  }
  if (!changed)
    return Result();
  return voice.replace_event(previous_onset, std::move(modified), node_end);
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
      result = clear_incoming_ties(candidate, *location, replacement,
                                   timeline->node_end());
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
        result = normalize_references_for_deleted_event(candidate, *rest_id);
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
