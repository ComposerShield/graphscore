// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/range_edit_command.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include <graphscore/core/accidental.hpp>
#include <graphscore/core/result.hpp>
#include <graphscore/core/spelled_pitch.hpp>
#include <graphscore/domain/node.hpp>
#include <graphscore/domain/node_timeline.hpp>
#include <graphscore/domain/notation_event.hpp>
#include <graphscore/domain/notation_markings.hpp>
#include <graphscore/domain/notation_validation.hpp>
#include <graphscore/domain/project.hpp>
#include <graphscore/domain/selection.hpp>
#include <graphscore/domain/tie_chain.hpp>
#include <graphscore/domain/track.hpp>
#include <graphscore/domain/voice_content.hpp>

#include "clipboard_command_helpers.hpp"

namespace graphscore {

namespace {

using LaneSnapshot = std::vector<std::pair<TrackId, TrackLane>>;

TrackLane* find_candidate(LaneSnapshot& candidates, TrackId track_id) {
  for (auto& entry : candidates) {
    if (entry.first == track_id)
      return &entry.second;
  }
  return nullptr;
}

[[nodiscard]] std::optional<SpelledPitch> diatonic_pitch(
    const SpelledPitch& source, std::int32_t amount) {
  constexpr std::array<int, 7>    kStepFromLetter = {5, 6, 0, 1, 2, 3, 4};
  constexpr std::array<Letter, 7> kLetterFromStep = {
      Letter::kC, Letter::kD, Letter::kE, Letter::kF,
      Letter::kG, Letter::kA, Letter::kB};

  const std::int64_t source_step =
      static_cast<std::int64_t>(source.octave()) * 7 +
      kStepFromLetter[static_cast<std::size_t>(source.letter())];
  const std::int64_t target_step = source_step + amount;
  const std::int64_t octave =
      target_step >= 0 ? target_step / 7 : (target_step - 6) / 7;
  const std::int64_t step = target_step - octave * 7;
  if (octave < SpelledPitch::kMinOctave || octave > SpelledPitch::kMaxOctave)
    return std::nullopt;

  const auto spelling = SpelledPitch::create(
      kLetterFromStep[static_cast<std::size_t>(step)],
      static_cast<std::int8_t>(octave), source.accidental());
  if (!spelling.has_value() || !spelling->to_midi_pitch().has_value())
    return std::nullopt;
  return spelling;
}

[[nodiscard]] std::optional<SpelledPitch> chromatic_pitch(
    const SpelledPitch& source, std::int32_t amount) {
  const std::optional<MidiPitch> source_midi = source.to_midi_pitch();
  if (!source_midi.has_value())
    return std::nullopt;
  const std::int64_t target =
      static_cast<std::int64_t>(source_midi->value()) + amount;
  if (target < MidiPitch::kMin || target > MidiPitch::kMax)
    return std::nullopt;

  std::optional<SpelledPitch> best;
  int                         best_accidental = std::numeric_limits<int>::max();
  int best_letter_distance                    = std::numeric_limits<int>::max();
  for (std::int8_t octave = SpelledPitch::kMinOctave;
       octave <= SpelledPitch::kMaxOctave; ++octave) {
    for (std::uint8_t letter_value = 0; letter_value < 7; ++letter_value) {
      const Letter letter = static_cast<Letter>(letter_value);
      for (const int offset : {-2, -1, 0, 1, 2}) {
        const auto accidental =
            accidental_from_offset(static_cast<std::int8_t>(offset));
        if (!accidental.has_value())
          continue;
        const auto candidate =
            SpelledPitch::create(letter, octave, *accidental);
        if (!candidate.has_value() || !candidate->to_midi_pitch().has_value() ||
            candidate->to_midi_pitch()->value() != target)
          continue;

        const int accidental_distance = std::abs(static_cast<int>(offset));
        const int letter_distance = std::abs(static_cast<int>(letter_value) -
                                             static_cast<int>(source.letter()));
        if (!best.has_value() || accidental_distance < best_accidental ||
            (accidental_distance == best_accidental &&
             letter_distance < best_letter_distance)) {
          best                 = *candidate;
          best_accidental      = accidental_distance;
          best_letter_distance = letter_distance;
        }
      }
    }
  }
  return best;
}

[[nodiscard]] std::optional<SpelledPitch> transposed_pitch(
    const SpelledPitch& source, RangeTransposeKind kind, std::int32_t amount) {
  return kind == RangeTransposeKind::kDiatonic
             ? diatonic_pitch(source, amount)
             : chromatic_pitch(source, amount);
}

[[nodiscard]] bool contains_id(const std::vector<NotationEntityId>& ids,
                               NotationEntityId                     id) {
  return std::ranges::find(ids, id) != ids.end();
}

[[nodiscard]] Result transpose_voice(VoiceContent& voice, Rational start,
                                     Rational end, RangeTransposeKind kind,
                                     std::int32_t amount, bool& changed) {
  struct PitchEdit {
    NotationEntityId id;
    SpelledPitch     pitch;
  };

  std::vector<PitchEdit> edits;
  const auto             has_edit = [&](NotationEntityId id) {
    return std::ranges::any_of(
        edits, [id](const PitchEdit& edit) { return edit.id == id; });
  };
  std::vector<NotationEntityId> selected_events;
  Rational                      onset(0);
  for (std::size_t event_index = 0; event_index < voice.events().size();
       ++event_index) {
    const VoiceEvent& event = voice.events()[event_index];
    const Rational    next  = onset + event_duration(event).resolved();
    if (onset >= start && onset < end) {
      const NotationEntityId event_entity = event_id(event);
      selected_events.push_back(event_entity);
      const std::size_t count = notehead_count(event);
      for (std::size_t note_index = 0; note_index < count; ++note_index) {
        const std::vector<ChainNotehead> chain =
            build_tie_chain(voice, event_index, note_index);
        for (const ChainNotehead& member : chain) {
          const std::optional<NotationEntityId> id = notehead_id_at(
              voice.events()[member.event_index], member.note_index);
          if (!id.has_value() || has_edit(*id))
            continue;
          const auto pitch = transposed_pitch(member.pitch, kind, amount);
          if (!pitch.has_value())
            return Result(ResultCode::kInvalidArgument);
          edits.push_back(PitchEdit{*id, *pitch});
        }
      }
    }
    onset = next;
  }

  // Grace notes belong to the selected principal event and have no rhythmic
  // onset of their own. They follow the same transpose as that event.
  for (const GraceGroup& group : voice.grace_groups()) {
    if (!contains_id(selected_events, group.principal_event))
      continue;
    for (const GraceNote& note : group.notes) {
      if (has_edit(note.id))
        continue;
      const auto pitch = transposed_pitch(note.pitch, kind, amount);
      if (!pitch.has_value())
        return Result(ResultCode::kInvalidArgument);
      edits.push_back(PitchEdit{note.id, *pitch});
    }
  }

  if (edits.empty())
    return Result();
  for (const PitchEdit& edit : edits) {
    const Result result = voice.set_notehead_pitch(edit.id, edit.pitch);
    if (!result.ok())
      return result;
  }
  changed = true;
  return Result();
}

}  // namespace

bool is_valid_range_edit_selection(const Project&   project,
                                   const Selection& selection) {
  if (!std::holds_alternative<FullMeasureSet>(selection) &&
      !std::holds_alternative<ArbitraryRangeSet>(selection))
    return false;
  if (!validate_selection(project, selection).empty())
    return false;

  if (const auto* measures = std::get_if<FullMeasureSet>(&selection);
      measures != nullptr) {
    if (measures->items().empty())
      return false;
    const FullMeasureItem& first = measures->items().front();
    if (first.measure_count != 1u)
      return false;
    for (const FullMeasureItem& item : measures->items()) {
      if (item.node != first.node ||
          item.measure_index != first.measure_index || item.measure_count != 1u)
        return false;
    }
    return true;
  }

  const auto* ranges = std::get_if<ArbitraryRangeSet>(&selection);
  if (ranges == nullptr || ranges->items().empty())
    return false;
  const ArbitraryRangeItem& first = ranges->items().front();
  if (!(first.span.end > first.span.start) || first.span.start < Rational(0))
    return false;
  const Node* node = project.find_node(first.node);
  if (node == nullptr || node->timeline() == nullptr ||
      first.span.end > node->timeline()->node_end())
    return false;
  for (std::size_t i = 0; i < ranges->items().size(); ++i) {
    const ArbitraryRangeItem& item = ranges->items()[i];
    if (item.node != first.node || item.span != first.span)
      return false;
    for (std::size_t j = 0; j < i; ++j) {
      const ArbitraryRangeItem& prior = ranges->items()[j];
      if (item.track == prior.track && item.stave == prior.stave &&
          item.voice == prior.voice)
        return false;
    }
  }
  return true;
}

Result RangeEditCommand::execute(Project& project) noexcept {
  if (state_ != State::kFresh || (!kind_.has_value() && amount_ != 0) ||
      (kind_.has_value() && amount_ == 0))
    return Result(ResultCode::kInvalidArgument);

  try {
    if (!is_valid_range_edit_selection(project, selection_))
      return Result(ResultCode::kInvalidArgument);

    const std::optional<internal::CutTarget> target =
        internal::resolve_cut_target(project, selection_);
    if (!target.has_value() || !(target->end > target->start))
      return Result(ResultCode::kInvalidArgument);
    Node* node = project.find_node(target->node);
    if (node == nullptr || node->timeline() == nullptr ||
        target->start < Rational(0) ||
        target->end > node->timeline()->node_end())
      return Result(ResultCode::kInvalidArgument);

    std::vector<TrackId> touched;
    bool                 any_transposed = false;
    for (const internal::AffectedVoice& voice : target->voices) {
      if (std::find(touched.begin(), touched.end(), voice.track) ==
          touched.end())
        touched.push_back(voice.track);
    }
    LaneSnapshot pre_snapshot;
    LaneSnapshot candidates;
    pre_snapshot.reserve(touched.size());
    candidates.reserve(touched.size());
    for (const TrackId track_id : touched) {
      TrackLane* lane = node->lane(track_id);
      if (lane == nullptr)
        return Result(ResultCode::kInvalidArgument);
      pre_snapshot.emplace_back(track_id, *lane);
      candidates.emplace_back(track_id, *lane);
    }

    for (const internal::AffectedVoice& voice : target->voices) {
      TrackLane* lane = find_candidate(candidates, voice.track);
      if (lane == nullptr)
        return Result(ResultCode::kInvalidArgument);
      StaveVoices* stave = lane->stave(voice.stave);
      if (stave == nullptr)
        return Result(ResultCode::kInvalidArgument);
      VoiceContent& content = stave->voice(voice.voice);
      Result        result;
      if (!kind_.has_value()) {
        internal::VoiceRebuildResult rebuild = internal::rebuild_voice_range(
            content, target->start, target->end, node->timeline()->node_end(),
            nullptr);
        if (!rebuild.status.ok())
          return rebuild.status;
        content = std::move(*rebuild.content);
        result  = Result();
      } else {
        bool changed = false;
        result = transpose_voice(content, target->start, target->end, *kind_,
                                 amount_, changed);
        any_transposed = any_transposed || changed;
      }
      if (!result.ok())
        return result;
    }
    if (kind_.has_value() && !any_transposed)
      return Result(ResultCode::kInvalidArgument);

    if (!kind_.has_value()) {
      std::vector<std::pair<TrackId, StaveId>> staves;
      for (const internal::AffectedVoice& voice : target->voices) {
        const auto key = std::make_pair(voice.track, voice.stave);
        if (std::find(staves.begin(), staves.end(), key) == staves.end())
          staves.push_back(key);
      }
      for (const auto& [track, stave] : staves) {
        TrackLane* lane = find_candidate(candidates, track);
        if (lane == nullptr)
          return Result(ResultCode::kInvalidArgument);
        const Result result = internal::clip_pedal_spans_in_range(
            *lane, stave, target->start, target->end);
        if (!result.ok())
          return result;
      }
    }

    for (const auto& entry : candidates) {
      const Result result = internal::validate_clipboard_lane_candidate(
          entry.second, node->timeline()->node_end());
      if (!result.ok())
        return result;
    }

    std::vector<TrackLane*> live_lanes;
    live_lanes.reserve(candidates.size());
    for (const auto& entry : candidates) {
      TrackLane* lane = node->lane(entry.first);
      if (lane == nullptr)
        return Result(ResultCode::kInvalidArgument);
      live_lanes.push_back(lane);
    }
    LaneSnapshot post_snapshot = candidates;
    node_id_                   = target->node;
    pre_snapshot_              = std::move(pre_snapshot);
    post_snapshot_             = std::move(post_snapshot);
    for (std::size_t i = 0; i < live_lanes.size(); ++i)
      *live_lanes[i] = std::move(candidates[i].second);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (...) {
    return Result(ResultCode::kInternalError);
  }

  state_ = State::kDone;
  return Result();
}

Result RangeEditCommand::undo(Project& project) noexcept {
  if (state_ != State::kDone || !pre_snapshot_.has_value() ||
      !post_snapshot_.has_value() || !node_id_.has_value())
    return Result(ResultCode::kInvalidArgument);
  const Result result = internal::multi_lane_restore_snapshot(
      *pre_snapshot_, *post_snapshot_, *node_id_, project);
  if (!result.ok())
    return result;
  state_ = State::kUndone;
  return Result();
}

Result RangeEditCommand::redo(Project& project) noexcept {
  if (state_ != State::kUndone || !pre_snapshot_.has_value() ||
      !post_snapshot_.has_value() || !node_id_.has_value())
    return Result(ResultCode::kInvalidArgument);
  const Result result = internal::multi_lane_restore_snapshot(
      *post_snapshot_, *pre_snapshot_, *node_id_, project);
  if (!result.ok())
    return result;
  state_ = State::kDone;
  return Result();
}

}  // namespace graphscore
