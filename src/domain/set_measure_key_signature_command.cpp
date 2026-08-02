// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/set_measure_key_signature_command.hpp>

#include <new>
#include <stdexcept>

#include <graphscore/core/result.hpp>
#include <graphscore/domain/node_timeline.hpp>
#include "timeline_command_helpers.hpp"

namespace graphscore {
namespace {

Result restore_measure(const NodeId node_id, const std::size_t measure_index,
                       const Measure& snapshot, const Measure& expected_current,
                       Project& project) {
  NodeTimeline* timeline = internal::resolve_node_timeline(project, node_id);
  if (timeline == nullptr ||
      measure_index >= timeline->measures().measure_count() ||
      timeline->measures().measure(measure_index) != expected_current) {
    return Result(ResultCode::kInvalidArgument);
  }

  try {
    return timeline->set_measure_key_signature(measure_index,
                                               snapshot.key_signature);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }
}

}  // namespace

Result SetMeasureKeySignatureCommand::execute(Project& project) noexcept {
  if (state_ != State::kFresh)
    return Result(ResultCode::kInvalidArgument);

  NodeTimeline* timeline = internal::resolve_node_timeline(project, node_id_);
  if (timeline == nullptr ||
      measure_index_ >= timeline->measures().measure_count()) {
    return Result(ResultCode::kInvalidArgument);
  }

  pre_snapshot_                = timeline->measures().measure(measure_index_);
  post_snapshot_               = pre_snapshot_;
  post_snapshot_.key_signature = key_signature_;

  try {
    const Result result =
        timeline->set_measure_key_signature(measure_index_, key_signature_);
    if (!result.ok())
      return result;
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }

  state_ = State::kDone;
  return Result();
}

Result SetMeasureKeySignatureCommand::undo(Project& project) noexcept {
  if (state_ != State::kDone)
    return Result(ResultCode::kInvalidArgument);
  const Result result = restore_measure(node_id_, measure_index_, pre_snapshot_,
                                        post_snapshot_, project);
  if (!result.ok())
    return result;
  state_ = State::kUndone;
  return Result();
}

Result SetMeasureKeySignatureCommand::redo(Project& project) noexcept {
  if (state_ != State::kUndone)
    return Result(ResultCode::kInvalidArgument);
  const Result result = restore_measure(node_id_, measure_index_,
                                        post_snapshot_, pre_snapshot_, project);
  if (!result.ok())
    return result;
  state_ = State::kDone;
  return Result();
}

}  // namespace graphscore
