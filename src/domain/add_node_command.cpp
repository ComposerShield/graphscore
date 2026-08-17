// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/add_node_command.hpp>

#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <graphscore/core/result.hpp>
#include <graphscore/domain/node_timeline.hpp>
#include <graphscore/domain/project.hpp>

namespace graphscore {
namespace {

[[nodiscard]] Tempo inherited_tempo(const Project& project,
                                    const Node*    source) {
  if (source == nullptr || source->timeline() == nullptr ||
      source->timeline()->tempo() == nullptr) {
    return project.default_tempo();
  }

  const NodeTimeline& timeline = *source->timeline();
  Tempo               result   = project.default_tempo();
  for (const TempoPoint& point : timeline.tempo()->points()) {
    if (point.position > timeline.boundary_position()) {
      break;
    }
    result = point.tempo;
  }
  return result;
}

[[nodiscard]] Node build_node(const Project& project, std::string name,
                              const Node* source) {
  Node node(NodeId::generate(), std::move(name));

  std::vector<StaveDefinition> staves;
  for (const Track& track : project.active_tracks()) {
    node.ensure_lane(track.id());
    TrackLane* const lane = node.lane(track.id());
    for (const StaveDefinition& stave : track.layout().staves()) {
      lane->ensure_stave(stave.id);
      staves.push_back(stave);
    }
  }

  const TimeSignature time_signature = *TimeSignature::create(4, 4);
  auto                timeline =
      *NodeTimeline::create({Measure{time_signature, KeySignature{}}}, staves);
  const Tempo tempo = inherited_tempo(project, source);
  static_cast<void>(timeline.set_tempo(
      {TempoPoint{Rational(0), tempo, TempoSegmentKind::kStep}}));
  node.set_timeline(std::move(timeline));
  return node;
}

}  // namespace

Result AddNodeCommand::execute(Project& project) noexcept {
  if (state_ != State::kFresh)
    return Result(ResultCode::kInvalidArgument);

  const Node* source = nullptr;
  if (source_node_.has_value()) {
    source = project.find_node(*source_node_);
    if (source == nullptr)
      return Result(ResultCode::kInvalidArgument);
  }

  try {
    created_node_       = build_node(project, name_, source);
    const Result result = project.restore_node(*created_node_);
    if (!result.ok()) {
      created_node_.reset();
      return result;
    }
  } catch (const std::bad_alloc&) {
    created_node_.reset();
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    created_node_.reset();
    return Result(ResultCode::kOutOfMemory);
  }

  state_ = State::kDone;
  return Result();
}

Result AddNodeCommand::undo(Project& project) noexcept {
  if (state_ != State::kDone)
    return Result(ResultCode::kInvalidArgument);
  if (!created_node_.has_value())
    return Result(ResultCode::kInternalError);

  Result result;
  try {
    result = project.remove_node(created_node_->id());
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }
  if (!result.ok())
    return result;

  state_ = State::kUndone;
  return Result();
}

Result AddNodeCommand::redo(Project& project) noexcept {
  if (state_ != State::kUndone)
    return Result(ResultCode::kInvalidArgument);
  if (!created_node_.has_value())
    return Result(ResultCode::kInternalError);

  Result result;
  try {
    result = project.restore_node(*created_node_);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }
  if (!result.ok())
    return result;

  state_ = State::kDone;
  return Result();
}

}  // namespace graphscore
