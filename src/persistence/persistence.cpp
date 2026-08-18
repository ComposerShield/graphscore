// SPDX-License-Identifier: Apache-2.0

#include <graphscore/persistence/graphscore_persistence.hpp>

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace graphscore {
namespace {
constexpr int kPersistenceVersion = 1;

struct PendingNodeMetadata {
  Node*         node         = nullptr;
  std::uint32_t custom_color = 0xFFFFFFFF;
  std::string   freeform_notes;
};

[[nodiscard]] bool graph_references_are_valid(
    const ProjectGraphSaveModel& save_model, const Project& project) {
  std::unordered_map<NodeId, std::unordered_set<ConnectorId>> inputs;
  inputs.reserve(save_model.nodes.size());
  std::unordered_set<ConnectorId> connector_ids;

  for (const Node& node : save_model.nodes) {
    const auto [entry, inserted] =
        inputs.emplace(node.id(), std::unordered_set<ConnectorId>{});
    if (!inserted)
      return false;
    entry->second.reserve(node.inputs().size());
    for (const InputConnector& input : node.inputs()) {
      if (!connector_ids.insert(input.id()).second ||
          !entry->second.insert(input.id()).second) {
        return false;
      }
    }
    for (const OutputConnector& output : node.outputs()) {
      if (!connector_ids.insert(output.id()).second)
        return false;
      if (output.event_binding().has_value() &&
          project.events().find_by_id(*output.event_binding()) == nullptr) {
        return false;
      }
    }
  }

  if (save_model.start_node.has_value() &&
      !inputs.contains(*save_model.start_node)) {
    return false;
  }
  for (const Node& node : save_model.nodes) {
    for (const OutputConnector& output : node.outputs()) {
      if (!output.destination().has_value())
        continue;
      const ConnectorDestination& destination = *output.destination();
      const auto                  node_it     = inputs.find(destination.node);
      if (node_it == inputs.end() ||
          !node_it->second.contains(destination.connector)) {
        return false;
      }
    }
  }
  return true;
}
}  // namespace

int persistence_version() {
  return kPersistenceVersion;
}

ProjectNodeMetadataSaveModel Persistence::capture_node_metadata(
    const Project& project) const {
  ProjectNodeMetadataSaveModel save_model;
  save_model.nodes.reserve(project.nodes().size());
  for (const Node& node : project.nodes()) {
    save_model.nodes.push_back({node.id(), node.color(), node.notes()});
  }
  return save_model;
}

Result Persistence::restore_node_metadata(
    const ProjectNodeMetadataSaveModel& save_model, Project& project) const {
  if (save_model.nodes.size() != project.nodes().size()) {
    return Result(ResultCode::kCorruptedData);
  }

  std::unordered_set<NodeId> seen_ids;
  seen_ids.reserve(save_model.nodes.size());
  std::vector<PendingNodeMetadata> pending;
  pending.reserve(save_model.nodes.size());
  for (const PersistedNodeMetadata& metadata : save_model.nodes) {
    const auto [unused, inserted] = seen_ids.insert(metadata.node_id);
    static_cast<void>(unused);
    Node* const node = project.find_node(metadata.node_id);
    if (!inserted || node == nullptr) {
      return Result(ResultCode::kCorruptedData);
    }
    pending.push_back({node, metadata.custom_color, metadata.freeform_notes});
  }

  for (PendingNodeMetadata& metadata : pending) {
    metadata.node->set_color(metadata.custom_color);
    metadata.node->set_notes(std::move(metadata.freeform_notes));
  }
  return Result();
}

ProjectGraphSaveModel Persistence::capture_graph(const Project& project) const {
  return {project.id(), project.start_node(), project.nodes()};
}

Result Persistence::restore_graph(const ProjectGraphSaveModel& save_model,
                                  Project&                     project) const {
  if (save_model.project_id != project.id() ||
      !graph_references_are_valid(save_model, project)) {
    return Result(ResultCode::kCorruptedData);
  }

  Project             replacement = project;
  std::vector<NodeId> existing_ids;
  existing_ids.reserve(replacement.nodes().size());
  for (const Node& node : replacement.nodes())
    existing_ids.push_back(node.id());
  for (const NodeId id : existing_ids) {
    if (!replacement.remove_node(id).ok())
      return Result(ResultCode::kCorruptedData);
  }
  for (const Node& node : save_model.nodes) {
    if (!replacement.restore_node(node).ok())
      return Result(ResultCode::kCorruptedData);
  }

  replacement.clear_start_node();
  if (save_model.start_node.has_value() &&
      !replacement.set_start_node(*save_model.start_node).ok()) {
    return Result(ResultCode::kCorruptedData);
  }
  project = std::move(replacement);
  return Result();
}
}  // namespace graphscore
