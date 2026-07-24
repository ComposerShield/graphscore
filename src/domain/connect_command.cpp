// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/connect_command.hpp>

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

Result ConnectCommand::execute(Project& project) noexcept {
  if (state_ != State::kFresh)
    return Result(ResultCode::kInvalidArgument);

  Graph        graph(project);
  const Result result =
      graph.connect(source_node_, source_output_, dest_node_, dest_input_);
  if (!result.ok())
    return result;

  const Node* node = project.find_node(source_node_);
  if (node == nullptr)
    return Result(ResultCode::kInvalidArgument);

  const OutputConnector* output = node->find_output(source_output_);
  if (output == nullptr)
    return Result(ResultCode::kInvalidArgument);

  old_route_ = output->route();
  state_     = State::kDone;
  return Result();
}

Result ConnectCommand::undo(Project& project) noexcept {
  if (state_ != State::kDone)
    return Result(ResultCode::kInvalidArgument);

  Graph        graph(project);
  const Result result = graph.disconnect(source_node_, source_output_);
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

Result ConnectCommand::redo(Project& project) noexcept {
  if (state_ != State::kUndone)
    return Result(ResultCode::kInvalidArgument);

  Graph        graph(project);
  const Result result =
      graph.connect(source_node_, source_output_, dest_node_, dest_input_);
  if (!result.ok())
    return result;

  state_ = State::kDone;
  return Result();
}

}  // namespace graphscore
