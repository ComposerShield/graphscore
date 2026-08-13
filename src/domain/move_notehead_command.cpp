// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/move_notehead_command.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

#include <graphscore/core/result.hpp>
#include <graphscore/core/spelled_pitch.hpp>
#include <graphscore/domain/command.hpp>
#include <graphscore/domain/node.hpp>
#include <graphscore/domain/node_timeline.hpp>
#include <graphscore/domain/notation_validation.hpp>
#include <graphscore/domain/project.hpp>
#include <graphscore/domain/track.hpp>
#include "command_snapshot_compare.hpp"

namespace graphscore {

std::optional<SpelledPitch> step_notehead_pitch(
    const SpelledPitch& pitch, NoteheadStepDirection direction) {
  const int step = direction == NoteheadStepDirection::kUp ? 1 : -1;

  // Letter -> staff-step index in the C-anchored cycle (C=0 ... B=6). The same
  // mapping notation.cpp's diatonic_index uses, so pitch_y() and this step
  // agree on where a letter sits on the staff.
  constexpr std::array<int, 7>    kStepFromLetter = {5, 6, 0, 1, 2, 3, 4};
  constexpr std::array<Letter, 7> kLetterFromStep = {
      Letter::kC, Letter::kD, Letter::kE, Letter::kF,
      Letter::kG, Letter::kA, Letter::kB};

  const int   index = kStepFromLetter[static_cast<std::size_t>(pitch.letter())];
  int         next  = index + step;
  std::int8_t octave = pitch.octave();
  if (next > 6) {
    next = 0;  // B -> C upward.
    ++octave;
  } else if (next < 0) {
    next = 6;  // C -> B downward.
    --octave;
  }

  // `next` is now in [0, 6] and `octave` in [-2, 10]; SpelledPitch::create
  // rejects an out-of-range octave, and no intermediate value overflows
  // std::int8_t.
  const std::optional<SpelledPitch> spelled =
      SpelledPitch::create(kLetterFromStep[static_cast<std::size_t>(next)],
                           octave, pitch.accidental());
  if (!spelled.has_value())
    return std::nullopt;
  // The spelling must also sound: a double-sharp at the top of the range, for
  // example, has a valid octave but resolves past MIDI 127.
  if (!spelled->to_midi_pitch().has_value())
    return std::nullopt;
  return spelled;
}

namespace {

VoiceContent* resolve_voice(Project& project, const NodeId node_id,
                            const TrackId track_id, const StaveId stave_id,
                            const Voice voice) {
  Node* node = project.find_node(node_id);
  if (node == nullptr)
    return nullptr;

  TrackLane* lane = node->lane(track_id);
  if (lane == nullptr)
    return nullptr;

  StaveVoices* stave = lane->stave(stave_id);
  if (stave == nullptr)
    return nullptr;

  return &stave->voice(voice);
}

enum class NoteheadKind : std::uint8_t { kNote, kChordNote, kGraceNote };

struct NoteheadLocation {
  NoteheadKind kind        = NoteheadKind::kNote;
  std::size_t  event_index = 0;  // kNote/kChordNote: index into events()
  std::size_t  note_index  = 0;  // kChordNote/kGraceNote: index into notes
  std::size_t  grace_group_index = 0;  // kGraceNote: index into grace_groups()
  SpelledPitch pitch;
};

[[nodiscard]] std::optional<NoteheadLocation> find_notehead(
    const VoiceContent& voice, const NotationEntityId id) {
  const std::vector<VoiceEvent>& events = voice.events();
  for (std::size_t i = 0; i < events.size(); ++i) {
    if (const auto* note = std::get_if<Note>(&events[i])) {
      if (note->id == id)
        return NoteheadLocation{NoteheadKind::kNote, i, 0, 0, note->pitch};
    }
    if (const auto* chord = std::get_if<Chord>(&events[i])) {
      for (std::size_t j = 0; j < chord->notes.size(); ++j) {
        if (chord->notes[j].id == id)
          return NoteheadLocation{NoteheadKind::kChordNote, i, j, 0,
                                  chord->notes[j].pitch};
      }
    }
  }

  const std::vector<GraceGroup>& groups = voice.grace_groups();
  for (std::size_t g = 0; g < groups.size(); ++g) {
    for (std::size_t j = 0; j < groups[g].notes.size(); ++j) {
      if (groups[g].notes[j].id == id)
        return NoteheadLocation{NoteheadKind::kGraceNote, 0, j, g,
                                groups[g].notes[j].pitch};
    }
  }
  return std::nullopt;
}

// The notehead's tie state for a note/chordnote address. A grace note has no
// tied_to_next field, so it never participates in a tie chain.
[[nodiscard]] bool notehead_tied_to_next(const VoiceEvent& event,
                                         std::size_t       note_index) {
  if (const auto* note = std::get_if<Note>(&event))
    return note_index == 0 && note->tied_to_next;
  if (const auto* chord = std::get_if<Chord>(&event))
    return note_index < chord->notes.size() &&
           chord->notes[note_index].tied_to_next;
  return false;
}

// The number of addressable noteheads in `event`: 1 for a Note, the note
// count for a Chord, 0 for a Rest.
[[nodiscard]] std::size_t notehead_count(const VoiceEvent& event) {
  if (std::get_if<Note>(&event) != nullptr)
    return 1;
  if (const auto* chord = std::get_if<Chord>(&event))
    return chord->notes.size();
  return 0;
}

// The pitch of notehead `note_index` in `event`. `note_index` must be in
// range (callers guarantee it through notehead_count, which is 0 for a Rest,
// so this is never reached for one).
[[nodiscard]] SpelledPitch notehead_pitch(const VoiceEvent& event,
                                          std::size_t       note_index) {
  if (const auto* note = std::get_if<Note>(&event))
    return note->pitch;
  if (const auto* chord = std::get_if<Chord>(&event))
    return chord->notes[note_index].pitch;
  return SpelledPitch{};
}

// The persistent id of the notehead at `note_index` in `event` (a Note's own
// id, or the addressed ChordNote's id), or std::nullopt when the address is
// out of range or the event is a Rest.
[[nodiscard]] std::optional<NotationEntityId> notehead_id_at(
    const VoiceEvent& event, std::size_t note_index) {
  if (const auto* note = std::get_if<Note>(&event))
    return note_index == 0 ? std::optional<NotationEntityId>(note->id)
                           : std::nullopt;
  if (const auto* chord = std::get_if<Chord>(&event)) {
    if (note_index < chord->notes.size())
      return chord->notes[note_index].id;
  }
  return std::nullopt;
}

// One exact address of a note/chordnote notehead: the event it lives in and
// the notehead index within that event. Ties are traversed over these
// addresses rather than over a pitch's first occurrence, so duplicate-pitch
// chords resolve the notehead that actually carries the tie.
struct NoteheadAddress {
  std::size_t event_index = 0;
  std::size_t note_index  = 0;

  [[nodiscard]] bool operator==(const NoteheadAddress&) const = default;
};

// One addressable note/chordnote notehead in a tie chain: the event it lives
// in, the notehead index within that event, and its (chain-invariant) pitch.
struct ChainNotehead {
  std::size_t  event_index;
  std::size_t  note_index;
  SpelledPitch pitch;
};

// Builds the complete tie chain through the selected notehead. The selected
// notehead must be a Note or ChordNote (not a GraceNote, which never ties).
//
// A tie edge connects notehead (e, n) whose tied_to_next is set to every
// notehead (e+1, m) in the immediately following event at the same pitch;
// the same edge is traversed backward from (e+1, m) to (e, n). The chain is
// the connected component of the selected notehead under these edges,
// visited with exact-address deduplication so duplicate-pitch chords never
// re-visit a member or stop at the first equal pitch instead of the one that
// carries the tie. Returns the chain in ascending (event, notehead) order.
// A dangling tie (a tied_to_next flag with no matching following pitch)
// yields only the members a valid walk reaches; VoiceContent::validate later
// rejects that state.
[[nodiscard]] std::vector<ChainNotehead> build_tie_chain(
    const VoiceContent& voice, const NoteheadLocation& selected) {
  const std::vector<VoiceEvent>& events = voice.events();
  const SpelledPitch             pitch  = selected.pitch;

  std::vector<ChainNotehead>   chain;
  std::vector<NoteheadAddress> visited;

  const auto add_member = [&](std::size_t ei, std::size_t ni) {
    for (const NoteheadAddress& address : visited) {
      if (address.event_index == ei && address.note_index == ni)
        return false;
    }
    visited.push_back(NoteheadAddress{ei, ni});
    chain.push_back(ChainNotehead{ei, ni, pitch});
    return true;
  };

  std::vector<NoteheadAddress> frontier;
  add_member(selected.event_index, selected.note_index);
  frontier.push_back(
      NoteheadAddress{selected.event_index, selected.note_index});

  while (!frontier.empty()) {
    const NoteheadAddress current = frontier.back();
    frontier.pop_back();

    // Backward: any previous notehead whose tie flag is set at the same
    // pitch ties into the current notehead -- not merely the first previous
    // notehead at that pitch, so a duplicate carrying the tie is found even
    // when an earlier duplicate does not.
    if (current.event_index > 0) {
      const std::size_t previous = current.event_index - 1;
      for (std::size_t j = 0; j < notehead_count(events[previous]); ++j) {
        if (notehead_tied_to_next(events[previous], j) &&
            notehead_pitch(events[previous], j) == pitch) {
          if (add_member(previous, j))
            frontier.push_back(NoteheadAddress{previous, j});
        }
      }
    }

    // Forward: the current notehead's tie continues into every following
    // notehead at the same pitch.
    if (notehead_tied_to_next(events[current.event_index],
                              current.note_index) &&
        current.event_index + 1 < events.size()) {
      const std::size_t next = current.event_index + 1;
      for (std::size_t j = 0; j < notehead_count(events[next]); ++j) {
        if (notehead_pitch(events[next], j) == pitch) {
          if (add_member(next, j))
            frontier.push_back(NoteheadAddress{next, j});
        }
      }
    }
  }

  std::sort(chain.begin(), chain.end(),
            [](const ChainNotehead& a, const ChainNotehead& b) {
              if (a.event_index != b.event_index)
                return a.event_index < b.event_index;
              return a.note_index < b.note_index;
            });
  return chain;
}

}  // namespace

std::optional<NoteheadMoveScope> notehead_move_scope(
    const Project& project, const NoteheadItem& notehead) {
  const Node* node = project.find_node(notehead.node);
  if (node == nullptr)
    return std::nullopt;
  const TrackLane* lane = node->lane(notehead.track);
  if (lane == nullptr)
    return std::nullopt;
  const StaveVoices* stave = lane->stave(notehead.stave);
  if (stave == nullptr)
    return std::nullopt;
  const NodeTimeline* timeline = node->timeline();
  if (timeline == nullptr)
    return std::nullopt;
  const VoiceContent& voice = stave->voice(notehead.voice);

  const std::optional<NoteheadLocation> location =
      find_notehead(voice, notehead.entity);
  if (!location.has_value())
    return std::nullopt;

  // The measure each touched notehead lives in, resolved through
  // position_of_event (which maps a GraceNote to its principal's onset).
  std::vector<NotationEntityId> member_ids;
  if (location->kind == NoteheadKind::kGraceNote) {
    member_ids.push_back(notehead.entity);
  } else {
    const std::vector<ChainNotehead> chain = build_tie_chain(voice, *location);
    member_ids.reserve(chain.size());
    for (const ChainNotehead& member : chain) {
      const std::optional<NotationEntityId> id =
          notehead_id_at(voice.events()[member.event_index], member.note_index);
      if (!id.has_value())
        return std::nullopt;
      member_ids.push_back(*id);
    }
  }

  std::size_t first = std::numeric_limits<std::size_t>::max();
  std::size_t last  = 0;
  bool        any   = false;
  for (const NotationEntityId& id : member_ids) {
    const std::optional<Rational> onset = voice.position_of_event(id);
    if (!onset.has_value())
      return std::nullopt;
    const std::optional<std::size_t> measure =
        timeline->measures().measure_index_at(*onset);
    if (!measure.has_value())
      return std::nullopt;
    const std::size_t m = *measure;
    first               = std::min(first, m);
    last                = std::max(last, m);
    any                 = true;
  }
  if (!any)
    return std::nullopt;
  return NoteheadMoveScope{first, last};
}

Result MoveNoteheadCommand::execute(Project& project) noexcept {
  if (state_ != State::kFresh)
    return Result(ResultCode::kInvalidArgument);

  VoiceContent* voice =
      resolve_voice(project, node_id_, track_id_, stave_id_, voice_);
  if (voice == nullptr)
    return Result(ResultCode::kInvalidArgument);

  try {
    VoiceContent pre_snapshot = *voice;
    VoiceContent candidate    = pre_snapshot;

    const std::optional<NoteheadLocation> location =
        find_notehead(candidate, notehead_id_);
    if (!location.has_value())
      return Result(ResultCode::kInvalidArgument);

    if (location->kind == NoteheadKind::kGraceNote) {
      const std::optional<SpelledPitch> new_pitch =
          step_notehead_pitch(location->pitch, direction_);
      if (!new_pitch.has_value())
        return Result(ResultCode::kInvalidArgument);

      const Result result =
          candidate.set_notehead_pitch(notehead_id_, *new_pitch);
      if (!result.ok())
        return result;
    } else {
      // Tied noteheads step together: collect the whole chain and every
      // member's target pitch before mutating anything, then apply each
      // member's step through the narrow pitch-only primitive. All members
      // share one pitch, so one step_notehead_pitch result governs them; it
      // is still computed per member so a future per-member accidental
      // divergence fails loudly.
      const std::vector<ChainNotehead> chain =
          build_tie_chain(candidate, *location);

      std::vector<std::pair<NotationEntityId, SpelledPitch>> steps;
      steps.reserve(chain.size());
      for (const ChainNotehead& member : chain) {
        const std::optional<NotationEntityId> id = notehead_id_at(
            candidate.events()[member.event_index], member.note_index);
        if (!id.has_value())
          return Result(ResultCode::kInvalidArgument);
        const std::optional<SpelledPitch> new_pitch =
            step_notehead_pitch(member.pitch, direction_);
        if (!new_pitch.has_value())
          return Result(ResultCode::kInvalidArgument);
        steps.emplace_back(*id, *new_pitch);
      }

      for (const auto& [id, new_pitch] : steps) {
        const Result result = candidate.set_notehead_pitch(id, new_pitch);
        if (!result.ok())
          return result;
      }
    }

    const Result validation = candidate.validate();
    if (!validation.ok())
      return validation;

    const std::vector<NotationDiagnostic> diags =
        validate_voice_references(candidate);
    if (!diags.empty())
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

Result MoveNoteheadCommand::undo(Project& project) noexcept {
  if (state_ != State::kDone)
    return Result(ResultCode::kInvalidArgument);
  if (!pre_snapshot_.has_value())
    return Result(ResultCode::kInternalError);

  VoiceContent* voice =
      resolve_voice(project, node_id_, track_id_, stave_id_, voice_);
  if (voice == nullptr)
    return Result(ResultCode::kInvalidArgument);

  if (post_snapshot_.has_value() &&
      !internal::snapshot_matches(*voice, *post_snapshot_))
    return Result(ResultCode::kInvalidArgument);

  try {
    VoiceContent candidate = *pre_snapshot_;

    if (!candidate.validate().ok())
      return Result(ResultCode::kInvalidArgument);

    const std::vector<NotationDiagnostic> diags =
        validate_voice_references(candidate);
    if (!diags.empty())
      return Result(ResultCode::kInvalidArgument);

    *voice = std::move(candidate);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }

  state_ = State::kUndone;
  return Result();
}

Result MoveNoteheadCommand::redo(Project& project) noexcept {
  if (state_ != State::kUndone)
    return Result(ResultCode::kInvalidArgument);
  if (!post_snapshot_.has_value())
    return Result(ResultCode::kInternalError);

  VoiceContent* voice =
      resolve_voice(project, node_id_, track_id_, stave_id_, voice_);
  if (voice == nullptr)
    return Result(ResultCode::kInvalidArgument);

  if (pre_snapshot_.has_value() &&
      !internal::snapshot_matches(*voice, *pre_snapshot_))
    return Result(ResultCode::kInvalidArgument);

  try {
    VoiceContent candidate = *post_snapshot_;

    if (!candidate.validate().ok())
      return Result(ResultCode::kInvalidArgument);

    const std::vector<NotationDiagnostic> diags =
        validate_voice_references(candidate);
    if (!diags.empty())
      return Result(ResultCode::kInvalidArgument);

    *voice = std::move(candidate);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }

  state_ = State::kDone;
  return Result();
}

}  // namespace graphscore
