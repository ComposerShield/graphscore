// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/remove_input_connector_command.hpp>

#include <new>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include <graphscore/core/result.hpp>
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
  try {
    return output.route().set_custom_route(std::move(prepared));
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }
}

}  // namespace

Result RemoveInputConnectorCommand::execute(Project& project) noexcept {
  if (state_ != State::kFresh)
    return Result(ResultCode::kInvalidArgument);

  Node* node = project.find_node(node_id_);
  if (node == nullptr)
    return Result(ResultCode::kInvalidArgument);

  const InputConnector* input = node->find_input(input_id_);
  if (input == nullptr)
    return Result(ResultCode::kInvalidArgument);

  std::optional<InputConnector> snapshot;
  std::vector<ClearedEdge>      cleared;
  try {
    snapshot = *input;
    for (const Node& other : project.nodes()) {
      for (const OutputConnector& output : other.outputs()) {
        const auto& destination = output.destination();
        if (destination.has_value() && destination->node == node_id_ &&
            destination->connector == input_id_) {
          cleared.push_back(
              ClearedEdge{other.id(), output.id(), output.route()});
        }
      }
    }
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }

  Graph        graph(project);
  const Result result = graph.remove_input(node_id_, input_id_);
  if (!result.ok())
    return result;

  removed_ = std::move(snapshot);
  cleared_ = std::move(cleared);
  state_   = State::kDone;
  return Result();
}

Result RemoveInputConnectorCommand::undo(Project& project) noexcept {
  if (state_ != State::kDone)
    return Result(ResultCode::kInvalidArgument);
  if (!removed_.has_value())
    return Result(ResultCode::kInternalError);

  Node* node = project.find_node(node_id_);
  if (node == nullptr)
    return Result(ResultCode::kInvalidArgument);

  std::optional<InputConnector> prepared_input;
  try {
    prepared_input = *removed_;
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }

  Result restore_input_result;
  try {
    restore_input_result = node->restore_input(std::move(*prepared_input));
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }
  if (!restore_input_result.ok())
    return restore_input_result;

  for (const ClearedEdge& edge : cleared_) {
    Graph  graph(project);
    Result connect_result;
    try {
      connect_result = graph.connect(edge.source_node, edge.source_output,
                                     node_id_, input_id_);
    } catch (const std::bad_alloc&) {
      return Result(ResultCode::kOutOfMemory);
    } catch (const std::length_error&) {
      return Result(ResultCode::kOutOfMemory);
    }
    if (!connect_result.ok())
      return connect_result;

    Node* source_node = project.find_node(edge.source_node);
    if (source_node == nullptr)
      return Result(ResultCode::kInvalidArgument);

    OutputConnector* output = source_node->find_output(edge.source_output);
    if (output == nullptr)
      return Result(ResultCode::kInvalidArgument);

    std::vector<RoutePoint> prepared_route;
    const Result            prepare_result =
        prepare_route_restore(edge.route, &prepared_route);
    if (!prepare_result.ok())
      return prepare_result;

    const Result route_result =
        restore_route(*output, edge.route, std::move(prepared_route));
    if (!route_result.ok())
      return route_result;
  }

  state_ = State::kUndone;
  return Result();
}

Result RemoveInputConnectorCommand::redo(Project& project) noexcept {
  if (state_ != State::kUndone)
    return Result(ResultCode::kInvalidArgument);

  Graph        graph(project);
  const Result result = graph.remove_input(node_id_, input_id_);
  if (!result.ok())
    return result;

  state_ = State::kDone;
  return Result();
}

}  // namespace graphscore
