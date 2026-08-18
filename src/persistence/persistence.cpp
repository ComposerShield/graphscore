// SPDX-License-Identifier: Apache-2.0

#include <graphscore/persistence/graphscore_persistence.hpp>

#include <string>
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
}  // namespace graphscore
