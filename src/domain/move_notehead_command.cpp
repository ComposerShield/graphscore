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
#include "notehead_edit_helpers.hpp"

namespace graphscore {

std::optional<SpelledPitch> step_notehead_pitch(
    const SpelledPitch& pitch, NoteheadStepDirection direction) {
  const int step = direction == NoteheadStepDirection::kUp ? 1 : -1;

  // Letter -> staff-step index in the C-anchored cycle (C=0 ... B=6). The same
  // mapping notation_engraving.cpp's diatonic_index uses, so pitch_y() and this
  // step agree on where a letter sits on the staff.
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

using internal::find_notehead;
using internal::NoteheadKind;
using internal::NoteheadLocation;

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
    const std::vector<ChainNotehead> chain =
        build_tie_chain(voice, location->event_index, location->note_index);
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
      const std::vector<ChainNotehead> chain = build_tie_chain(
          candidate, location->event_index, location->note_index);

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
