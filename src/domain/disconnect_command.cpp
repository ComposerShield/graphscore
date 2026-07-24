// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/disconnect_command.hpp>

#include <graphscore/core/result.hpp>
#include <graphscore/domain/connector.hpp>
#include <graphscore/domain/graph.hpp>
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

Result DisconnectCommand::execute(Project& project) noexcept {
  if (state_ != State::kFresh)
    return Result(ResultCode::kInvalidArgument);

  Node* node = project.find_node(source_node_);
  if (node == nullptr)
    return Result(ResultCode::kInvalidArgument);

  OutputConnector* output = node->find_output(source_output_);
  if (output == nullptr)
    return Result(ResultCode::kInvalidArgument);

  const RouteGeometry saved_route     = output->route();
  const bool          had_destination = output->destination().has_value();
  const ConnectorDestination saved_dest =
      had_destination ? *output->destination() : ConnectorDestination{};

  Graph        graph(project);
  const Result result = graph.disconnect(source_node_, source_output_);
  if (!result.ok())
    return result;

  old_route_ = saved_route;
  old_dest_  = saved_dest;
  state_     = State::kDone;
  return Result();
}

Result DisconnectCommand::undo(Project& project) noexcept {
  if (state_ != State::kDone)
    return Result(ResultCode::kInvalidArgument);

  Graph        graph(project);
  const Result result = graph.connect(source_node_, source_output_,
                                      old_dest_.node, old_dest_.connector);
  if (!result.ok())
    return result;

  Node* node = project.find_node(source_node_);
  if (node == nullptr)
    return Result(ResultCode::kInvalidArgument);

  OutputConnector* output = node->find_output(source_output_);
  if (output == nullptr)
    return Result(ResultCode::kInvalidArgument);

  const Result restore_result = restore_route(*output, old_route_);
  if (!restore_result.ok())
    return restore_result;

  state_ = State::kUndone;
  return Result();
}

Result DisconnectCommand::redo(Project& project) noexcept {
  if (state_ != State::kUndone)
    return Result(ResultCode::kInvalidArgument);

  Graph        graph(project);
  const Result result = graph.disconnect(source_node_, source_output_);
  if (!result.ok())
    return result;

  state_ = State::kDone;
  return Result();
}

}  // namespace graphscore
