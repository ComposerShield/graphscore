// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/remove_node_command.hpp>

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

Result RemoveNodeCommand::execute(Project& project) noexcept {
  if (state_ != State::kFresh)
    return Result(ResultCode::kInvalidArgument);

  const Node* node = project.find_node(node_id_);
  if (node == nullptr)
    return Result(ResultCode::kInvalidArgument);

  std::optional<Node>             snapshot;
  std::vector<ClearedInboundEdge> cleared;
  try {
    snapshot = *node;
    for (const Node& other : project.nodes()) {
      if (other.id() == node_id_)
        continue;
      for (const OutputConnector& output : other.outputs()) {
        const auto& destination = output.destination();
        if (destination.has_value() && destination->node == node_id_) {
          cleared.push_back(ClearedInboundEdge{
              other.id(), output.id(), destination->connector, output.route()});
        }
      }
    }
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }

  const std::optional<NodeId> start = project.start_node();
  const bool was_start              = start.has_value() && *start == node_id_;

  Result result;
  try {
    result = project.remove_node(node_id_);
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }
  if (!result.ok())
    return result;

  removed_node_    = std::move(snapshot);
  was_start_       = was_start;
  cleared_inbound_ = std::move(cleared);
  state_           = State::kDone;
  return Result();
}

Result RemoveNodeCommand::undo(Project& project) noexcept {
  if (state_ != State::kDone)
    return Result(ResultCode::kInvalidArgument);
  if (!removed_node_.has_value())
    return Result(ResultCode::kInternalError);

  std::optional<Node> prepared_node;
  try {
    prepared_node = *removed_node_;
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }
  if (!prepared_node.has_value())
    return Result(ResultCode::kInternalError);

  Result restore_result;
  try {
    restore_result = project.restore_node(std::move(*prepared_node));
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }
  if (!restore_result.ok())
    return restore_result;

  if (was_start_) {
    const Result start_result = project.set_start_node(node_id_);
    if (!start_result.ok())
      return start_result;
  }

  for (const ClearedInboundEdge& edge : cleared_inbound_) {
    Graph  graph(project);
    Result connect_result;
    try {
      connect_result = graph.connect(edge.source_node, edge.source_output,
                                     node_id_, edge.dest_input);
    } catch (const std::bad_alloc&) {
      return Result(ResultCode::kOutOfMemory);
    } catch (const std::length_error&) {
      return Result(ResultCode::kOutOfMemory);
    }
    if (!connect_result.ok())
      return connect_result;

    Node* source_node = project.find_node(edge.source_node);
    if (source_node == nullptr)
      return Result(ResultCode::kInternalError);

    OutputConnector* output = source_node->find_output(edge.source_output);
    if (output == nullptr)
      return Result(ResultCode::kInternalError);

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

Result RemoveNodeCommand::redo(Project& project) noexcept {
  if (state_ != State::kUndone)
    return Result(ResultCode::kInvalidArgument);

  Result result;
  try {
    result = project.remove_node(node_id_);
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
