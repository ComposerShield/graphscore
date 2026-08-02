// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/set_measure_time_signature_command.hpp>

#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include <graphscore/core/result.hpp>
#include <graphscore/domain/node_timeline.hpp>
#include <graphscore/domain/project.hpp>
#include "measure_command_helpers.hpp"

namespace graphscore {

Result SetMeasureTimeSignatureCommand::execute(Project& project) noexcept {
  if (state_ != State::kFresh)
    return Result(ResultCode::kInvalidArgument);
  Node* live = project.find_node(node_id_);
  if (live == nullptr || live->timeline() == nullptr)
    return Result(ResultCode::kInvalidArgument);
  const NodeTimeline& timeline = *live->timeline();
  if (measure_index_ >= timeline.measures().measure_count())
    return Result(ResultCode::kInvalidArgument);

  try {
    const Rational old_length =
        timeline.measures().measure_length(measure_index_);
    const std::optional<Rational> new_length = Rational::create(
        time_signature_.numerator(), time_signature_.denominator());
    if (!new_length.has_value())
      return Result(ResultCode::kInternalError);
    Node   candidate = *live;
    Result build_result;
    if (*new_length == old_length) {
      build_result = candidate.timeline()->set_measure_time_signature(
          measure_index_, time_signature_);
    } else {
      const Rational measure_start =
          timeline.measures().measure_start(measure_index_);
      const bool     expands = *new_length > old_length;
      const Rational edit_start =
          measure_start + (expands ? old_length : *new_length);
      const internal::MeasureCascade edit{
          expands ? internal::MeasureCascadeKind::kInsert
                  : internal::MeasureCascadeKind::kDelete,
          edit_start, timeline.node_end(),
          timeline.node_end() + *new_length - old_length,
          measure_start + old_length};
      build_result = internal::build_measure_candidate(
          project, *live, edit,
          [&](NodeTimeline& value) {
            return value.set_measure_time_signature(measure_index_,
                                                    time_signature_);
          },
          &candidate);
    }
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

Result SetMeasureTimeSignatureCommand::undo(Project& project) noexcept {
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

Result SetMeasureTimeSignatureCommand::redo(Project& project) noexcept {
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
