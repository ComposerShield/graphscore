// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/reset_route_command.hpp>

#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

#include <graphscore/core/result.hpp>
#include <graphscore/domain/connector.hpp>
#include <graphscore/domain/node.hpp>
#include <graphscore/domain/project.hpp>
#include <graphscore/domain/route_geometry.hpp>

namespace graphscore {

namespace {

Result prepare_route_restore(const RouteGeometry&     saved,
                             std::vector<RoutePoint>* prepared) noexcept {
  if (saved.is_automatic())
    return Result();

  try {
    *prepared = saved.waypoints();
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }
  return Result();
}

Result restore_route(OutputConnector& output, const RouteGeometry& saved,
                     std::vector<RoutePoint> prepared) noexcept {
  if (saved.is_automatic()) {
    output.route().reset_to_automatic();
    return Result();
  }
  return output.route().set_custom_route(std::move(prepared));
}

}  // namespace

Result ResetRouteCommand::execute(Project& project) noexcept {
  if (state_ != State::kFresh)
    return Result(ResultCode::kInvalidArgument);

  Node* node = project.find_node(node_id_);
  if (node == nullptr)
    return Result(ResultCode::kInvalidArgument);

  OutputConnector* output = node->find_output(output_id_);
  if (output == nullptr)
    return Result(ResultCode::kInvalidArgument);

  RouteGeometry saved_route;
  try {
    saved_route = output->route();
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }

  old_route_ = std::move(saved_route);
  output->route().reset_to_automatic();
  state_ = State::kDone;
  return Result();
}

Result ResetRouteCommand::undo(Project& project) noexcept {
  if (state_ != State::kDone)
    return Result(ResultCode::kInvalidArgument);

  Node* node = project.find_node(node_id_);
  if (node == nullptr)
    return Result(ResultCode::kInvalidArgument);

  OutputConnector* output = node->find_output(output_id_);
  if (output == nullptr)
    return Result(ResultCode::kInvalidArgument);

  std::vector<RoutePoint> prepared_route;
  const Result            prepare_result =
      prepare_route_restore(old_route_, &prepared_route);
  if (!prepare_result.ok())
    return prepare_result;

  const Result result =
      restore_route(*output, old_route_, std::move(prepared_route));
  if (!result.ok())
    return result;

  state_ = State::kUndone;
  return Result();
}

Result ResetRouteCommand::redo(Project& project) noexcept {
  if (state_ != State::kUndone)
    return Result(ResultCode::kInvalidArgument);

  Node* node = project.find_node(node_id_);
  if (node == nullptr)
    return Result(ResultCode::kInvalidArgument);

  OutputConnector* output = node->find_output(output_id_);
  if (output == nullptr)
    return Result(ResultCode::kInvalidArgument);

  output->route().reset_to_automatic();
  state_ = State::kDone;
  return Result();
}

}  // namespace graphscore
