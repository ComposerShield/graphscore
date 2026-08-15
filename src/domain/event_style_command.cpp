// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/event_style_command.hpp>

#include "command_snapshot_compare.hpp"

#include <algorithm>
#include <new>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

#include <graphscore/domain/node.hpp>
#include <graphscore/domain/notation_validation.hpp>
#include <graphscore/domain/project.hpp>

namespace graphscore {
namespace {

VoiceContent* resolve_voice(Project& project, NodeId node_id, TrackId track_id,
                            StaveId stave_id, Voice voice_number) {
  Node*        node  = project.find_node(node_id);
  TrackLane*   lane  = node == nullptr ? nullptr : node->lane(track_id);
  StaveVoices* stave = lane == nullptr ? nullptr : lane->stave(stave_id);
  return stave == nullptr ? nullptr : &stave->voice(voice_number);
}

std::vector<Articulation>* mutable_articulations(VoiceEvent& event) {
  return std::visit(
      [](auto& value) -> std::vector<Articulation>* {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, Note> || std::is_same_v<T, Chord>)
          return &value.articulations;
        return nullptr;
      },
      event);
}

StemDirection* mutable_stem(VoiceEvent& event) {
  return std::visit(
      [](auto& value) -> StemDirection* {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, Note> || std::is_same_v<T, Chord>)
          return &value.stem;
        return nullptr;
      },
      event);
}

Result apply_articulation(VoiceEvent& event, ArticulationEdit edit,
                          Articulation                articulation,
                          std::optional<Articulation> replaced) {
  if (std::find(kAllArticulations.begin(), kAllArticulations.end(),
                articulation) == kAllArticulations.end() ||
      (edit != ArticulationEdit::kApply && edit != ArticulationEdit::kChange &&
       edit != ArticulationEdit::kRemove)) {
    return Result(ResultCode::kInvalidArgument);
  }
  std::vector<Articulation>* values = mutable_articulations(event);
  if (values == nullptr)
    return Result(ResultCode::kInvalidArgument);
  const auto existing = std::find(values->begin(), values->end(), articulation);
  if (edit == ArticulationEdit::kApply) {
    if (existing != values->end())
      return Result(ResultCode::kInvalidArgument);
    if (is_duration_articulation(articulation) &&
        std::ranges::any_of(*values, is_duration_articulation)) {
      return Result(ResultCode::kInvalidArgument);
    }
    values->push_back(articulation);
    return Result();
  }
  if (!replaced.has_value())
    return Result(ResultCode::kInvalidArgument);
  const auto old = std::find(values->begin(), values->end(), *replaced);
  if (old == values->end())
    return Result(ResultCode::kInvalidArgument);
  if (edit == ArticulationEdit::kRemove) {
    values->erase(old);
    return Result();
  }
  if (existing != values->end() && existing != old)
    return Result(ResultCode::kInvalidArgument);
  if (is_duration_articulation(articulation) &&
      std::any_of(values->begin(), values->end(),
                  [&](const Articulation& value) {
                    return &value != &*old && is_duration_articulation(value);
                  })) {
    return Result(ResultCode::kInvalidArgument);
  }
  *old = articulation;
  return Result();
}

}  // namespace

EventStyleCommand::EventStyleCommand(NodeId node, TrackId track, StaveId stave,
                                     Voice voice, NotationEntityId event,
                                     ArticulationEdit            edit,
                                     Articulation                articulation,
                                     std::optional<Articulation> replaced)
    : node_(node),
      track_(track),
      stave_(stave),
      voice_(voice),
      event_(event),
      kind_(Kind::kArticulation),
      articulation_edit_(edit),
      articulation_(articulation),
      replaced_(replaced) {}

EventStyleCommand::EventStyleCommand(NodeId node, TrackId track, StaveId stave,
                                     Voice voice, NotationEntityId event,
                                     StemDirection stem)
    : node_(node),
      track_(track),
      stave_(stave),
      voice_(voice),
      event_(event),
      kind_(Kind::kStem),
      stem_(stem) {}

Result EventStyleCommand::execute(Project& project) noexcept {
  if (state_ != State::kFresh)
    return Result(ResultCode::kInvalidArgument);
  VoiceContent* target = resolve_voice(project, node_, track_, stave_, voice_);
  if (target == nullptr)
    return Result(ResultCode::kInvalidArgument);
  try {
    VoiceContent candidate = *target;
    const auto   position  = candidate.position_of_event(event_);
    if (!position.has_value())
      return Result(ResultCode::kInvalidArgument);
    const auto index = candidate.find_event_index_at(*position);
    if (!index.has_value() || event_id(candidate.events()[*index]) != event_)
      return Result(ResultCode::kInvalidArgument);
    VoiceEvent replacement = candidate.events()[*index];
    Result     result;
    if (kind_ == Kind::kArticulation) {
      result = apply_articulation(replacement, articulation_edit_,
                                  articulation_, replaced_);
    } else {
      StemDirection* current    = mutable_stem(replacement);
      const bool     valid_stem = stem_ == StemDirection::kAuto ||
                              stem_ == StemDirection::kUp ||
                              stem_ == StemDirection::kDown;
      if (current == nullptr || !valid_stem || *current == stem_)
        return Result(ResultCode::kInvalidArgument);
      *current = stem_;
    }
    if (!result.ok())
      return result;
    result = candidate.replace_event(*position, std::move(replacement),
                                     candidate.total_length());
    if (!result.ok() || !validate_voice_references(candidate).empty())
      return Result(ResultCode::kInvalidArgument);
    pre_snapshot_  = *target;
    post_snapshot_ = candidate;
    *target        = std::move(candidate);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }
  state_ = State::kDone;
  return Result();
}

Result EventStyleCommand::undo(Project& project) noexcept {
  if (state_ != State::kDone || !pre_snapshot_.has_value() ||
      !post_snapshot_.has_value())
    return Result(ResultCode::kInvalidArgument);
  VoiceContent* target = resolve_voice(project, node_, track_, stave_, voice_);
  if (target == nullptr ||
      !internal::snapshot_matches(*target, *post_snapshot_))
    return Result(ResultCode::kInvalidArgument);
  try {
    VoiceContent candidate = *pre_snapshot_;
    VoiceContent displaced = std::move(*target);
    *target                = std::move(candidate);
    compensation_snapshot_ = std::move(displaced);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }
  state_ = State::kUndone;
  return Result();
}

Result EventStyleCommand::redo(Project& project) noexcept {
  if (state_ != State::kUndone || !pre_snapshot_.has_value() ||
      !post_snapshot_.has_value())
    return Result(ResultCode::kInvalidArgument);
  VoiceContent* target = resolve_voice(project, node_, track_, stave_, voice_);
  if (target == nullptr || !internal::snapshot_matches(*target, *pre_snapshot_))
    return Result(ResultCode::kInvalidArgument);
  try {
    VoiceContent candidate = *post_snapshot_;
    VoiceContent displaced = std::move(*target);
    *target                = std::move(candidate);
    compensation_snapshot_ = std::move(displaced);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }
  state_ = State::kDone;
  return Result();
}

Result EventStyleCommand::compensate_undo(Project& project) noexcept {
  if (state_ != State::kUndone || !compensation_snapshot_.has_value())
    return Result(ResultCode::kInvalidArgument);
  VoiceContent* target = resolve_voice(project, node_, track_, stave_, voice_);
  if (target == nullptr)
    return Result(ResultCode::kInvalidArgument);
  *target = std::move(*compensation_snapshot_);
  compensation_snapshot_.reset();
  state_ = State::kDone;
  return Result();
}

Result EventStyleCommand::compensate_redo(Project& project) noexcept {
  if (state_ != State::kDone || !compensation_snapshot_.has_value())
    return Result(ResultCode::kInvalidArgument);
  VoiceContent* target = resolve_voice(project, node_, track_, stave_, voice_);
  if (target == nullptr)
    return Result(ResultCode::kInvalidArgument);
  *target = std::move(*compensation_snapshot_);
  compensation_snapshot_.reset();
  state_ = State::kUndone;
  return Result();
}

}  // namespace graphscore
