// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <graphscore/compiler/graphscore_compiler.hpp>
#include <graphscore/cooked_format/graphscore_cooked_format.hpp>
#include <graphscore/domain/graphscore_domain.hpp>

namespace graphscore {

// Schema-neutral save-model projection for the writer-only node metadata that
// Milestone 06 authors. The project bundle and schema codec remain Milestone
// 03 work; this projection is the persistence boundary they consume.
struct PersistedNodeMetadata {
  NodeId        node_id;
  std::uint32_t custom_color = 0xFFFFFFFF;
  std::string   freeform_notes;

  [[nodiscard]] bool operator==(const PersistedNodeMetadata&) const = default;
};

struct ProjectNodeMetadataSaveModel {
  std::vector<PersistedNodeMetadata> nodes;

  [[nodiscard]] bool operator==(const ProjectNodeMetadataSaveModel&) const =
      default;
};

class Persistence {
 public:
  Persistence() = default;

  // Captures every node in project order and preserves metadata bytes exactly.
  [[nodiscard]] ProjectNodeMetadataSaveModel capture_node_metadata(
      const Project& project) const;

  // Restores a complete projection by stable NodeId. A missing, duplicate, or
  // extra record is rejected without modifying the project.
  [[nodiscard]] Result restore_node_metadata(
      const ProjectNodeMetadataSaveModel& save_model, Project& project) const;
};

}  // namespace graphscore
