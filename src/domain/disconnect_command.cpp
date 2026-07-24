// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/disconnect_command.hpp>

#include <new>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include <graphscore/core/result.hpp>
#include <graphscore/domain/connector.hpp>
#include <graphscore/domain/graph.hpp>
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

Result DisconnectCommand::execute(Project& project) noexcept {
  if (state_ != State::kFresh)
    return Result(ResultCode::kInvalidArgument);

  Node* node = project.find_node(source_node_);
  if (node == nullptr)
    return Result(ResultCode::kInvalidArgument);

  OutputConnector* output = node->find_output(source_output_);
  if (output == nullptr)
    return Result(ResultCode::kInvalidArgument);

  const std::optional<ConnectorDestination>& destination =
      output->destination();
  if (!destination.has_value())
    return Result(ResultCode::kInvalidArgument);

  RouteGeometry saved_route;
  try {
    saved_route = output->route();
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }

  const ConnectorDestination saved_dest = *destination;

  Graph        graph(project);
  const Result result = graph.disconnect(source_node_, source_output_);
  if (!result.ok())
    return result;

  old_route_ = std::move(saved_route);
  old_dest_  = saved_dest;
  state_     = State::kDone;
  return Result();
}

Result DisconnectCommand::undo(Project& project) noexcept {
  if (state_ != State::kDone)
    return Result(ResultCode::kInvalidArgument);

  Node* node = project.find_node(source_node_);
  if (node == nullptr)
    return Result(ResultCode::kInvalidArgument);

  OutputConnector* output = node->find_output(source_output_);
  if (output == nullptr)
    return Result(ResultCode::kInvalidArgument);

  std::vector<RoutePoint> prepared_route;
  const Result            prepare_result =
      prepare_route_restore(old_route_, &prepared_route);
  if (!prepare_result.ok())
    return prepare_result;

  Graph        graph(project);
  const Result result = graph.connect(source_node_, source_output_,
                                      old_dest_.node, old_dest_.connector);
  if (!result.ok())
    return result;

  const Result restore_result =
      restore_route(*output, old_route_, std::move(prepared_route));
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
