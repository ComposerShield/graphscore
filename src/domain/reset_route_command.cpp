// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/reset_route_command.hpp>

#include <graphscore/core/result.hpp>
#include <graphscore/domain/connector.hpp>
#include <graphscore/domain/node.hpp>
#include <graphscore/domain/project.hpp>
#include <graphscore/domain/route_geometry.hpp>

namespace graphscore {

namespace {

Result restore_route(OutputConnector&     output,
                     const RouteGeometry& saved) noexcept {
  if (saved.is_automatic()) {
    output.route().reset_to_automatic();
    return Result();
  }
  return output.route().set_custom_route(saved.waypoints());
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

  old_route_ = output->route();
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

  const Result result = restore_route(*output, old_route_);
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
