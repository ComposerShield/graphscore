// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/add_interval_command.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <new>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include <graphscore/core/accidental.hpp>
#include <graphscore/core/result.hpp>
#include <graphscore/domain/node.hpp>
#include <graphscore/domain/node_timeline.hpp>
#include <graphscore/domain/notation_validation.hpp>
#include <graphscore/domain/project.hpp>
#include <graphscore/domain/track.hpp>
#include "command_snapshot_compare.hpp"
#include "marking_command_helpers.hpp"
#include "notehead_edit_helpers.hpp"

namespace graphscore {

namespace {

using internal::find_notehead;
using internal::NoteheadKind;
using internal::NoteheadLocation;
using internal::resolve_voice;

// The standard accidental a key signature implies for a letter: sharp for
// every letter the key's sharp count covers (F C G D A E B order), flat for
// every letter its flat count covers (B E A D G C F order), natural
// otherwise. This is the same table the engraver's key_accidental
// (src/notation/notation_engraving.cpp) uses to decide whether a notehead needs
// a written accidental; it is duplicated here because graphscore_domain cannot
// depend on graphscore_notation, and it must not drift from that table.
[[nodiscard]] Accidental key_accidental(const KeySignature& key,
                                        Letter              letter) noexcept {
  constexpr std::array<Letter, 7> kSharps = {Letter::kF, Letter::kC, Letter::kG,
                                             Letter::kD, Letter::kA, Letter::kE,
                                             Letter::kB};
  constexpr std::array<Letter, 7> kFlats  = {Letter::kB, Letter::kE, Letter::kA,
                                             Letter::kD, Letter::kG, Letter::kC,
                                             Letter::kF};
  const std::uint8_t              encoded_fifths_key =
      static_cast<std::uint8_t>(key.fifths());
  const int fifths = encoded_fifths_key <= 7
                         ? static_cast<int>(encoded_fifths_key)
                         : static_cast<int>(encoded_fifths_key) - 256;
  if (fifths > 0 && std::ranges::find(kSharps.begin(), kSharps.begin() + fifths,
                                      letter) != kSharps.begin() + fifths) {
    return Accidental::kSharp;
  }
  if (fifths < 0 && std::ranges::find(kFlats.begin(), kFlats.begin() - fifths,
                                      letter) != kFlats.begin() - fifths) {
    return Accidental::kFlat;
  }
  return Accidental::kNatural;
}

}  // namespace

std::optional<SpelledPitch> interval_target_pitch(const SpelledPitch& source,
                                                  std::uint8_t        interval,
                                                  IntervalDirection   direction,
                                                  KeySignature        key) {
  if (interval < 2 || interval > 8)
    return std::nullopt;
  const int letter_steps = static_cast<int>(interval) - 1;  // 1..7
  const int sign         = direction == IntervalDirection::kAbove ? 1 : -1;

  // Letter -> staff-step index in the C-anchored cycle (C=0 ... B=6), the
  // same mapping step_notehead_pitch (move_notehead_command.cpp) and
  // notation_engraving.cpp's diatonic_index use, so a letter always sits at one
  // unambiguous diatonic position.
  constexpr std::array<int, 7>    kStepFromLetter = {5, 6, 0, 1, 2, 3, 4};
  constexpr std::array<Letter, 7> kLetterFromStep = {
      Letter::kC, Letter::kD, Letter::kE, Letter::kF,
      Letter::kG, Letter::kA, Letter::kB};

  // A single "white note" coordinate: C in octave 0 is 0, and each octave
  // contributes seven diatonic steps. Splitting the result back into
  // (octave, step) with floor division gives the correct wrap for both
  // directions and any step count in [1, 7].
  const int source_step =
      kStepFromLetter[static_cast<std::size_t>(source.letter())];
  const int source_diatonic =
      static_cast<int>(source.octave()) * 7 + source_step;
  const int target_diatonic = source_diatonic + sign * letter_steps;

  int target_octave = target_diatonic / 7;
  int target_step   = target_diatonic % 7;
  if (target_step < 0) {
    target_step += 7;
    --target_octave;
  }

  const Letter target_letter =
      kLetterFromStep[static_cast<std::size_t>(target_step)];
  const Accidental accidental = key_accidental(key, target_letter);

  const std::optional<SpelledPitch> spelled = SpelledPitch::create(
      target_letter, static_cast<std::int8_t>(target_octave), accidental);
  if (!spelled.has_value())
    return std::nullopt;
  // The spelling must also sound: an accidental at the top of the range can
  // resolve past MIDI 127.
  if (!spelled->to_midi_pitch().has_value())
    return std::nullopt;
  return spelled;
}

std::optional<std::size_t> notehead_measure_index(
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
  // A GraceNote has no rhythmic measure of its own; interval entry cannot
  // grow its event, so it is ineligible.
  if (location->kind == NoteheadKind::kGraceNote)
    return std::nullopt;

  const std::optional<Rational> onset =
      voice.position_of_event(notehead.entity);
  if (!onset.has_value())
    return std::nullopt;
  return timeline->measures().measure_index_at(*onset);
}

std::optional<KeySignature> notehead_key_signature(
    const Project& project, const NoteheadItem& notehead) {
  const std::optional<std::size_t> measure =
      notehead_measure_index(project, notehead);
  if (!measure.has_value())
    return std::nullopt;
  const Node* node = project.find_node(notehead.node);
  if (node == nullptr)
    return std::nullopt;
  const NodeTimeline* timeline = node->timeline();
  if (timeline == nullptr)
    return std::nullopt;
  return timeline->measures().measure(*measure).key_signature;
}

Result AddIntervalCommand::execute(Project& project) noexcept {
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
    // A GraceNote has no rhythmic event of its own to grow.
    if (location->kind == NoteheadKind::kGraceNote)
      return Result(ResultCode::kInvalidArgument);

    const NoteheadItem item{node_id_, track_id_, stave_id_, voice_,
                            notehead_id_};
    const std::optional<KeySignature> key =
        notehead_key_signature(project, item);
    if (!key.has_value())
      return Result(ResultCode::kInvalidArgument);

    const std::optional<SpelledPitch> target =
        interval_target_pitch(location->pitch, interval_, direction_, *key);
    if (!target.has_value())
      return Result(ResultCode::kInvalidArgument);

    const std::optional<Rational> position =
        candidate.position_of_event(notehead_id_);
    if (!position.has_value())
      return Result(ResultCode::kInvalidArgument);

    const Node* node = project.find_node(node_id_);
    if (node == nullptr)
      return Result(ResultCode::kInvalidArgument);
    const NodeTimeline* timeline = node->timeline();
    if (timeline == nullptr)
      return Result(ResultCode::kInvalidArgument);
    const Rational node_end = timeline->node_end();

    if (location->kind == NoteheadKind::kNote) {
      const Note* old_note =
          std::get_if<Note>(&candidate.events()[location->event_index]);
      if (old_note == nullptr)
        return Result(ResultCode::kInvalidArgument);
      std::vector<ChordNote> chord_notes;
      chord_notes.push_back(
          {old_note->id, old_note->pitch, old_note->tied_to_next});
      chord_notes.push_back({inserted_id_, *target, false});
      const Result result = candidate.replace_event(
          *position,
          make_chord(old_note->duration, std::move(chord_notes),
                     old_note->articulations, old_note->stem),
          node_end);
      if (!result.ok())
        return result;
    } else {
      const Chord* old_chord =
          std::get_if<Chord>(&candidate.events()[location->event_index]);
      if (old_chord == nullptr)
        return Result(ResultCode::kInvalidArgument);
      // Repeated interval input that would create the same spelled pitch is
      // a no-op, never an indistinguishable duplicate notehead.
      const bool duplicate = std::ranges::any_of(
          old_chord->notes, [&](const ChordNote& chord_note) {
            return chord_note.pitch == *target;
          });
      if (duplicate)
        return Result(ResultCode::kInvalidArgument);

      std::vector<ChordNote> new_notes = old_chord->notes;
      new_notes.push_back({inserted_id_, *target, false});
      Chord new_chord = make_chord(old_chord->duration, std::move(new_notes));
      new_chord.id    = old_chord->id;
      new_chord.stem  = old_chord->stem;
      new_chord.articulations = old_chord->articulations;
      const Result result =
          candidate.replace_event(*position, std::move(new_chord), node_end);
      if (!result.ok())
        return result;
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

Result AddIntervalCommand::undo(Project& project) noexcept {
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

  const Node* node = project.find_node(node_id_);
  if (node == nullptr)
    return Result(ResultCode::kInvalidArgument);
  const NodeTimeline* timeline = node->timeline();
  if (timeline == nullptr)
    return Result(ResultCode::kInvalidArgument);
  const Rational node_end = timeline->node_end();

  try {
    VoiceContent candidate = *pre_snapshot_;

    if (!candidate.check_complete(node_end).ok())
      return Result(ResultCode::kInvalidArgument);
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

Result AddIntervalCommand::redo(Project& project) noexcept {
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

  const Node* node = project.find_node(node_id_);
  if (node == nullptr)
    return Result(ResultCode::kInvalidArgument);
  const NodeTimeline* timeline = node->timeline();
  if (timeline == nullptr)
    return Result(ResultCode::kInvalidArgument);
  const Rational node_end = timeline->node_end();

  try {
    VoiceContent candidate = *post_snapshot_;

    if (!candidate.check_complete(node_end).ok())
      return Result(ResultCode::kInvalidArgument);
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
