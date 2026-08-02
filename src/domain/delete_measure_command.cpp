// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/delete_measure_command.hpp>

#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include <graphscore/core/result.hpp>
#include <graphscore/domain/node_timeline.hpp>
#include <graphscore/domain/project.hpp>
#include "measure_command_helpers.hpp"

namespace graphscore {

Result DeleteMeasureCommand::execute(Project& project) noexcept {
  if (state_ != State::kFresh)
    return Result(ResultCode::kInvalidArgument);
  Node* live = project.find_node(node_id_);
  if (live == nullptr || live->timeline() == nullptr)
    return Result(ResultCode::kInvalidArgument);
  const NodeTimeline& timeline = *live->timeline();
  if (index_ >= timeline.measures().measure_count())
    return Result(ResultCode::kInvalidArgument);

  try {
    const Rational start  = timeline.measures().measure_start(index_);
    const Rational length = timeline.measures().measure_length(index_);
    const internal::MeasureCascade edit{
        internal::MeasureCascadeKind::kDelete, start, timeline.node_end(),
        timeline.node_end() - length, start + length};
    Node         candidate    = *live;
    const Result build_result = internal::build_measure_candidate(
        project, *live, edit,
        [&](NodeTimeline& value) { return value.remove_measure(index_); },
        &candidate);
    if (!build_result.ok())
      return build_result;

    std::optional<Node> pre(*live);
    std::optional<Node> post(candidate);
    static_assert(std::is_nothrow_move_assignable_v<Node>);
    static_assert(std::is_nothrow_move_assignable_v<decltype(pre_snapshot_)>);
    pre_snapshot_  = std::move(pre);
    post_snapshot_ = std::move(post);
    *live          = std::move(candidate);
    state_         = State::kDone;
    return Result();
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (...) {
    return Result(ResultCode::kInternalError);
  }
}

Result DeleteMeasureCommand::undo(Project& project) noexcept {
  if (state_ != State::kDone || !pre_snapshot_.has_value() ||
      !post_snapshot_.has_value()) {
    return Result(ResultCode::kInvalidArgument);
  }
  const Result result = internal::restore_node_snapshot(
      project, node_id_, *pre_snapshot_, *post_snapshot_);
  if (!result.ok())
    return result;
  state_ = State::kUndone;
  return Result();
}

Result DeleteMeasureCommand::redo(Project& project) noexcept {
  if (state_ != State::kUndone || !pre_snapshot_.has_value() ||
      !post_snapshot_.has_value()) {
    return Result(ResultCode::kInvalidArgument);
  }
  const Result result = internal::restore_node_snapshot(
      project, node_id_, *post_snapshot_, *pre_snapshot_);
  if (!result.ok())
    return result;
  state_ = State::kDone;
  return Result();
}

}  // namespace graphscore
