// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/graph.hpp>

#include <optional>
#include <vector>

#include <graphscore/domain/connector.hpp>

namespace graphscore {

Result Graph::connect(NodeId source_node, ConnectorId source_output,
                      NodeId dest_node, ConnectorId dest_input) {
  Node* source = project_->find_node(source_node);
  if (source == nullptr)
    return Result(ResultCode::kInvalidArgument);

  OutputConnector* output = source->find_output(source_output);
  if (output == nullptr || output->destination().has_value())
    return Result(ResultCode::kInvalidArgument);

  const Node* dest = project_->find_node(dest_node);
  if (dest == nullptr || dest->find_input(dest_input) == nullptr)
    return Result(ResultCode::kInvalidArgument);

  output->set_destination(ConnectorDestination{dest_node, dest_input});
  return Result();
}

Result Graph::disconnect(NodeId source_node, ConnectorId source_output) {
  Node* source = project_->find_node(source_node);
  if (source == nullptr)
    return Result(ResultCode::kInvalidArgument);

  OutputConnector* output = source->find_output(source_output);
  if (output == nullptr || !output->destination().has_value())
    return Result(ResultCode::kInvalidArgument);

  output->set_destination(std::nullopt);
  return Result();
}

Result Graph::remove_input(NodeId node_id, ConnectorId connector_id) {
  Node* target = project_->find_node(node_id);
  if (target == nullptr || target->find_input(connector_id) == nullptr)
    return Result(ResultCode::kInvalidArgument);

  std::vector<NodeId> node_ids;
  node_ids.reserve(project_->nodes().size());
  for (const Node& node : project_->nodes())
    node_ids.push_back(node.id());

  for (const NodeId id : node_ids) {
    project_->find_node(id)->clear_destinations_to(node_id, connector_id);
  }

  return target->remove_input(connector_id);
}

Result Graph::bind_output_event(NodeId node_id, ConnectorId output_id,
                                std::optional<EventId> event) {
  Node* node = project_->find_node(node_id);
  if (node == nullptr)
    return Result(ResultCode::kInvalidArgument);

  if (event.has_value() && project_->events().find_by_id(*event) == nullptr)
    return Result(ResultCode::kInvalidArgument);

  return node->bind_output_event(output_id, event);
}

Result Graph::remove_event(EventId event) {
  if (project_->events().find_by_id(event) == nullptr)
    return Result(ResultCode::kInvalidArgument);

  std::vector<NodeId> node_ids;
  node_ids.reserve(project_->nodes().size());
  for (const Node& node : project_->nodes())
    node_ids.push_back(node.id());

  for (const NodeId id : node_ids) {
    Node* node = project_->find_node(id);

    std::vector<ConnectorId> bound_outputs;
    for (const OutputConnector& output : node->outputs()) {
      if (output.event_binding() == event)
        bound_outputs.push_back(output.id());
    }

    for (const ConnectorId output_id : bound_outputs) {
      // Cannot fail: output_id came from this node's outputs and nullopt
      // skips the vertical/sequential clash check.
      [[maybe_unused]] const Result unbind_result =
          node->bind_output_event(output_id, std::nullopt);
    }
  }

  return project_->events().remove_event(event);
}

bool Graph::is_export_ready() const {
  for (const Node& node : project_->nodes()) {
    for (const OutputConnector& output : node.outputs()) {
      if (output.export_enabled() && !output.destination().has_value())
        return false;
    }
  }
  return true;
}

}  // namespace graphscore
