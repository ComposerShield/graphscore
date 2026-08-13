// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/step_accidental_command.hpp>

#include <cstdint>
#include <new>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include <graphscore/core/accidental.hpp>
#include <graphscore/core/result.hpp>
#include <graphscore/core/spelled_pitch.hpp>
#include <graphscore/domain/command.hpp>
#include <graphscore/domain/node.hpp>
#include <graphscore/domain/notation_validation.hpp>
#include <graphscore/domain/project.hpp>
#include <graphscore/domain/track.hpp>
#include "command_snapshot_compare.hpp"
#include "marking_command_helpers.hpp"
#include "notehead_edit_helpers.hpp"

namespace graphscore {

std::optional<SpelledPitch> step_notehead_accidental(
    const SpelledPitch& pitch, AccidentalStepDirection direction) {
  const int rung = direction == AccidentalStepDirection::kRaise ? 1 : -1;

  // Accidental's underlying values are the semitone offsets -2..2, so one
  // rung of the ladder is one step of the offset. accidental_from_offset
  // rejects -3 and 3, which is exactly the hard reject at either end: the
  // step is never clamped back onto the ladder.
  const std::optional<Accidental> stepped = accidental_from_offset(
      static_cast<std::int8_t>(to_semitone_offset(pitch.accidental()) + rung));
  if (!stepped.has_value())
    return std::nullopt;

  const std::optional<SpelledPitch> spelled =
      SpelledPitch::create(pitch.letter(), pitch.octave(), *stepped);
  if (!spelled.has_value())
    return std::nullopt;
  // The spelling must also sound: a double-sharp at the top of the range, for
  // example, has a valid octave but resolves past MIDI 127.
  if (!spelled->to_midi_pitch().has_value())
    return std::nullopt;
  return spelled;
}

namespace {

using internal::build_tie_chain;
using internal::ChainNotehead;
using internal::find_notehead;
using internal::notehead_id_at;
using internal::NoteheadKind;
using internal::NoteheadLocation;
using internal::resolve_voice;

}  // namespace

Result StepAccidentalCommand::execute(Project& project) noexcept {
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
          step_notehead_accidental(location->pitch, direction_);
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
      // share one pitch, so one step_notehead_accidental result governs them;
      // it is still computed per member so a future per-member spelling
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
            step_notehead_accidental(member.pitch, direction_);
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

Result StepAccidentalCommand::undo(Project& project) noexcept {
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

Result StepAccidentalCommand::redo(Project& project) noexcept {
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
