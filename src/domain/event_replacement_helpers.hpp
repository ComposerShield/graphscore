// SPDX-License-Identifier: Apache-2.0
//
// Internal helpers shared by the commands that replace a sounding
// VoiceContent event (Note/Chord) with a same-duration Rest:
// DeleteNoteheadCommand (deleting a whole event, or the last pitch of a
// chord) and ConvertEventToRestCommand ("R"). Not installed; included only
// by src/domain/*_command.cpp files.

#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include <graphscore/core/result.hpp>
#include <graphscore/domain/voice_content.hpp>

namespace graphscore {
namespace internal {

// Clears the incoming tie on the event immediately preceding
// `event_index`, when that predecessor is tied into a pitch `replacement`
// no longer sounds. `event_index` is the index, in `voice.events()`, of
// the event being replaced; index 0 has no predecessor and is a no-op.
// event_sounds_pitch(replacement, pitch) decides survival per pitch, so
// replacing a Chord with a smaller Chord/Note only clears ties to the
// dropped pitches, and replacing anything with a Rest clears every
// incoming tie.
inline Result clear_incoming_ties(VoiceContent& voice, std::size_t event_index,
                                  const VoiceEvent& replacement,
                                  Rational          node_end) {
  if (event_index == 0u)
    return Result();

  Rational previous_onset(0);
  for (std::size_t index = 0; index + 1u < event_index; ++index)
    previous_onset =
        previous_onset + event_duration(voice.events()[index]).resolved();

  const VoiceEvent& previous = voice.events()[event_index - 1u];
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

// Removes every slur whose start or end event is `replaced_id` and every
// grace group whose principal event is `replaced_id`, and normalizes every
// beam override that references `replaced_id`: the id is dropped from the
// override's run, and the override is removed when the surviving run is
// empty, fewer than two events, or no longer a contiguous beamable run.
// Unaffected records keep their ids and relative order. `replaced_id` is
// the event id that a caller just replaced with a Rest (a full delete or
// an explicit "convert to rest"); only references whose validity depends
// on that event staying sounding or beamable are touched.
inline Result normalize_references_for_replaced_event(
    VoiceContent& voice, NotationEntityId replaced_id) {
  std::vector<NotationEntityId> slurs_to_remove;
  for (const Slur& slur : voice.slurs()) {
    if (slur.start_event == replaced_id || slur.end_event == replaced_id)
      slurs_to_remove.push_back(slur.id);
  }
  for (const NotationEntityId id : slurs_to_remove) {
    const Result result = voice.remove_slur(id);
    if (!result.ok())
      return result;
  }

  std::vector<NotationEntityId> groups_to_remove;
  for (const GraceGroup& group : voice.grace_groups()) {
    if (group.principal_event == replaced_id)
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
        std::find(override.events.begin(), override.events.end(), replaced_id);
    if (hit == override.events.end())
      continue;

    std::vector<NotationEntityId> reduced;
    reduced.reserve(override.events.size());
    for (const NotationEntityId eid : override.events) {
      if (eid != replaced_id)
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

}  // namespace internal
}  // namespace graphscore
