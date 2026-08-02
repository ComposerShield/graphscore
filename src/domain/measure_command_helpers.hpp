// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <new>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <graphscore/core/result.hpp>
#include <graphscore/domain/measure_map.hpp>
#include <graphscore/domain/node.hpp>
#include <graphscore/domain/project.hpp>
#include <graphscore/domain/track.hpp>
#include "clipboard_command_helpers.hpp"
#include "command_snapshot_compare.hpp"

namespace graphscore::internal {

enum class MeasureCascadeKind { kInsert, kDelete };

struct MeasureCascade {
  MeasureCascadeKind kind;
  Rational           start;
  Rational           old_end;
  Rational           new_end;
  Rational           removed_end;
};

[[nodiscard]] inline bool boundary_cuts_tuplet_group(
    const std::vector<VoiceEvent>& events, const Rational boundary) {
  Rational    position(0);
  std::size_t index = 0;
  while (index < events.size()) {
    const std::optional<TupletRatio>& ratio =
        event_duration(events[index]).tuplet();
    if (!ratio.has_value()) {
      position = position + event_duration(events[index]).resolved();
      ++index;
      continue;
    }

    const Rational group_start = position;
    while (index < events.size()) {
      const std::optional<TupletRatio>& next =
          event_duration(events[index]).tuplet();
      if (!next.has_value() || *next != *ratio)
        break;
      position = position + event_duration(events[index]).resolved();
      ++index;
    }
    if (group_start < boundary && boundary < position)
      return true;
  }
  return false;
}

[[nodiscard]] inline Result add_repaired_beam_overrides(
    VoiceContent& content, const VoiceContent& source) {
  std::unordered_map<NotationEntityId, std::size_t> positions;
  positions.reserve(content.events().size());
  for (std::size_t index = 0; index < content.events().size(); ++index)
    positions.emplace(event_id(content.events()[index]), index);

  for (const BeamOverride& override_ : source.beam_overrides()) {
    std::vector<std::pair<std::size_t, NotationEntityId>> survivors;
    survivors.reserve(override_.events.size());
    for (const NotationEntityId id : override_.events) {
      const auto found = positions.find(id);
      if (found != positions.end() &&
          event_is_beamable(content.events()[found->second])) {
        survivors.emplace_back(found->second, id);
      }
    }
    std::sort(survivors.begin(), survivors.end(),
              [](const auto& left, const auto& right) {
                return left.first < right.first;
              });

    std::vector<std::vector<NotationEntityId>> groups;
    std::vector<NotationEntityId>              current;
    std::optional<std::size_t>                 previous_position;
    for (const auto& [position, id] : survivors) {
      if (previous_position.has_value() && position != *previous_position + 1) {
        groups.push_back(std::move(current));
        current.clear();
      }
      current.push_back(id);
      previous_position = position;
    }
    if (!current.empty())
      groups.push_back(std::move(current));

    bool kept_original_id = false;
    for (std::vector<NotationEntityId>& group : groups) {
      if (group.size() < 2)
        continue;
      BeamOverride repaired{
          kept_original_id ? NotationEntityId::generate() : override_.id,
          override_.kind, std::move(group)};
      const Result result = content.add_beam_override(std::move(repaired));
      if (!result.ok())
        return result;
      kept_original_id = true;
    }
  }
  return Result();
}

inline void sever_disconnected_successor(
    const VoiceContent&                         source,
    const std::unordered_set<NotationEntityId>& surviving,
    const MeasureCascadeKind kind, std::vector<VoiceEvent>* before) {
  if (before->empty())
    return;
  const NotationEntityId last_id = event_id(before->back());
  for (std::size_t i = 0; i < source.events().size(); ++i) {
    if (event_id(source.events()[i]) != last_id)
      continue;
    if (i + 1 >= source.events().size())
      return;
    if (kind == MeasureCascadeKind::kDelete &&
        surviving.contains(event_id(source.events()[i + 1]))) {
      return;
    }
    if (const auto* note = std::get_if<Note>(&before->back())) {
      Note severed         = *note;
      severed.tied_to_next = false;
      before->back()       = std::move(severed);
    } else if (const auto* chord = std::get_if<Chord>(&before->back())) {
      Chord severed = *chord;
      for (ChordNote& notehead : severed.notes)
        notehead.tied_to_next = false;
      before->back() = std::move(severed);
    }
    return;
  }
}

[[nodiscard]] inline VoiceRebuildResult rebuild_voice_for_measure_edit(
    const VoiceContent& source, const MeasureCascade& edit) {
  const Rational right_boundary =
      edit.kind == MeasureCascadeKind::kInsert ? edit.start : edit.removed_end;
  if (boundary_cuts_tuplet_group(source.events(), edit.start) ||
      (edit.kind == MeasureCascadeKind::kDelete &&
       boundary_cuts_tuplet_group(source.events(), right_boundary))) {
    return VoiceRebuildResult{Result(ResultCode::kInvalidArgument),
                              std::nullopt};
  }
  std::optional<ClippedRegion> before =
      clip_left_region(source.events(), edit.start);
  std::optional<ClippedRegion> after =
      clip_right_region(source.events(), right_boundary, edit.old_end);
  if (!before.has_value() || !after.has_value()) {
    return VoiceRebuildResult{Result(ResultCode::kInvalidArgument),
                              std::nullopt};
  }

  std::unordered_set<NotationEntityId> surviving = before->surviving_ids;
  surviving.insert(after->surviving_ids.begin(), after->surviving_ids.end());
  for (const VoiceEvent& event : source.events()) {
    if (!surviving.contains(event_id(event)))
      continue;
    if (const auto* chord = std::get_if<Chord>(&event)) {
      for (const ChordNote& notehead : chord->notes)
        surviving.insert(notehead.id);
    }
  }
  sever_disconnected_successor(source, surviving, edit.kind, &before->events);

  VoiceContent content;
  for (const VoiceEvent& event : before->events) {
    const Result result = content.append(event);
    if (!result.ok())
      return VoiceRebuildResult{result, std::nullopt};
  }

  if (edit.kind == MeasureCascadeKind::kInsert) {
    const Rational inserted_length = edit.new_end - edit.old_end;
    const std::optional<std::vector<Rest>> rests =
        decompose_rest(inserted_length);
    if (!rests.has_value()) {
      return VoiceRebuildResult{Result(ResultCode::kInvalidArgument),
                                std::nullopt};
    }
    for (const Rest& rest : *rests) {
      const Result result = content.append(rest);
      if (!result.ok())
        return VoiceRebuildResult{result, std::nullopt};
    }
  }

  for (const VoiceEvent& event : after->events) {
    const Result result = content.append(event);
    if (!result.ok())
      return VoiceRebuildResult{result, std::nullopt};
  }
  const Result markings_result =
      add_surviving_markings(content, source, surviving, false);
  if (!markings_result.ok())
    return VoiceRebuildResult{markings_result, std::nullopt};
  const Result beams_result = add_repaired_beam_overrides(content, source);
  if (!beams_result.ok())
    return VoiceRebuildResult{beams_result, std::nullopt};
  const Result normalize_result = content.normalize(edit.new_end);
  if (!normalize_result.ok())
    return VoiceRebuildResult{normalize_result, std::nullopt};
  return VoiceRebuildResult{Result(), std::move(content)};
}

[[nodiscard]] inline std::optional<PedalSpan> transform_pedal_span(
    const PedalSpan& span, const MeasureCascade& edit) {
  PedalSpan transformed = span;
  if (edit.kind == MeasureCascadeKind::kInsert) {
    const Rational delta = edit.new_end - edit.old_end;
    if (span.start >= edit.start) {
      transformed.start = transformed.start + delta;
      transformed.end   = transformed.end + delta;
    } else if (span.end > edit.start) {
      transformed.end = transformed.end + delta;
    }
    return transformed;
  }

  const Rational delta = edit.old_end - edit.new_end;
  if (span.end <= edit.start)
    return transformed;
  if (span.start >= edit.removed_end) {
    transformed.start = transformed.start - delta;
    transformed.end   = transformed.end - delta;
  } else {
    transformed.start = span.start < edit.start ? span.start : edit.start;
    transformed.end =
        span.end > edit.removed_end ? span.end - delta : edit.start;
  }
  if (transformed.end <= transformed.start)
    return std::nullopt;
  return transformed;
}

[[nodiscard]] inline Result transform_lane(TrackLane*            lane,
                                           const MeasureCascade& edit) {
  const std::vector<StaveId> stave_ids = lane->stave_ids();
  for (const StaveId stave_id : stave_ids) {
    StaveVoices* stave = lane->stave(stave_id);
    if (stave == nullptr)
      return Result(ResultCode::kInternalError);
    for (std::uint8_t value = Voice::kMin; value <= Voice::kMax; ++value) {
      const std::optional<Voice> voice = Voice::create(value);
      if (!voice.has_value())
        return Result(ResultCode::kInternalError);
      VoiceContent&      content = stave->voice(*voice);
      VoiceRebuildResult rebuilt =
          rebuild_voice_for_measure_edit(content, edit);
      if (!rebuilt.status.ok())
        return rebuilt.status;
      content = std::move(*rebuilt.content);
    }

    const std::vector<PedalSpan>* spans = lane->pedal_spans(stave_id);
    if (spans == nullptr)
      continue;
    const std::vector<PedalSpan> original = *spans;
    for (const PedalSpan& span : original) {
      const Result result = lane->remove_pedal_span(stave_id, span.id);
      if (!result.ok())
        return result;
    }
    for (const PedalSpan& span : original) {
      const std::optional<PedalSpan> transformed =
          transform_pedal_span(span, edit);
      if (!transformed.has_value())
        continue;
      const Result result = lane->add_pedal_span(stave_id, *transformed);
      if (!result.ok())
        return result;
    }
  }
  return validate_clipboard_lane_candidate(*lane, edit.new_end);
}

[[nodiscard]] inline Result transform_project_lanes(
    const Project& project, Node* candidate, const MeasureCascade& edit) {
  std::size_t transformed_count = 0;
  const auto  transform_tracks =
      [&](const std::vector<Track>& tracks) -> Result {
    for (const Track& track : tracks) {
      TrackLane* lane = candidate->lane(track.id());
      if (lane == nullptr)
        continue;
      for (const StaveDefinition& stave : track.layout().staves())
        lane->ensure_stave(stave.id);
      const Result result = transform_lane(lane, edit);
      if (!result.ok())
        return result;
      ++transformed_count;
    }
    return Result();
  };
  Result result = transform_tracks(project.active_tracks());
  if (!result.ok())
    return result;
  result = transform_tracks(project.archived_tracks());
  if (!result.ok())
    return result;
  return transformed_count == candidate->lane_count()
             ? Result()
             : Result(ResultCode::kInvalidArgument);
}

template <typename TimelineEdit>
[[nodiscard]] inline Result build_measure_candidate(
    const Project& project, const Node& source, const MeasureCascade& edit,
    TimelineEdit&& timeline_edit, Node* out) {
  Node          candidate = source;
  NodeTimeline* timeline  = candidate.timeline();
  if (timeline == nullptr)
    return Result(ResultCode::kInvalidArgument);
  const Result timeline_result = timeline_edit(*timeline);
  if (!timeline_result.ok())
    return timeline_result;
  if (timeline->node_end() != edit.new_end)
    return Result(ResultCode::kInternalError);
  const Result lanes_result =
      transform_project_lanes(project, &candidate, edit);
  if (!lanes_result.ok())
    return lanes_result;
  *out = std::move(candidate);
  return Result();
}

[[nodiscard]] inline Result restore_node_snapshot(
    Project& project, const NodeId node_id, const Node& target,
    const Node& expected_current) noexcept {
  Node* live = project.find_node(node_id);
  if (live == nullptr || !snapshot_matches(*live, expected_current))
    return Result(ResultCode::kInvalidArgument);
  try {
    Node prepared = target;
    static_assert(std::is_nothrow_move_assignable_v<Node>);
    *live = std::move(prepared);
    return Result();
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (...) {
    return Result(ResultCode::kInternalError);
  }
}

}  // namespace graphscore::internal
