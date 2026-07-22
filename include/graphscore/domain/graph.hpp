// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <optional>

#include <graphscore/core/graphscore_core.hpp>
#include <graphscore/domain/project.hpp>

namespace graphscore {

// A thin domain service over an existing Project's node/connector data for
// the concerns that genuinely need more than one node's worth of state:
// connecting/disconnecting an output to a destination input that may live
// on a *different* node, keeping every node's destinations consistent when
// an input connector is removed, and validating an event binding against
// the project-wide EventRegistry. Everything else -- adding/removing a
// node's own connectors, and configuring the queue policy/capacity of a
// node/event listener -- needs no cross-node knowledge and stays on Node
// itself (node.hpp); see Node's class comment for why connectors and
// listeners are stored there rather than in a second container here.
class Graph {
 public:
  explicit Graph(Project& project) noexcept : project_(&project) {}

  // Connects `source_output` (on `source_node`) to `dest_input` (on
  // `dest_node`). Fails, leaving both connectors unchanged, if either node
  // or connector id is unknown to the project, or if `source_output`
  // already has a destination -- call disconnect() first to retarget it.
  // Self-loops and cycles through any number of nodes are both accepted
  // (product decision: "Cycles are valid").
  [[nodiscard]] Result connect(NodeId source_node, ConnectorId source_output,
                               NodeId dest_node, ConnectorId dest_input);

  // Fails if `source_node`/`source_output` is unknown, or if the output
  // has no destination to remove.
  [[nodiscard]] Result disconnect(NodeId      source_node,
                                  ConnectorId source_output);

  // Removes input connector `connector_id` from `node_id`, first clearing
  // (to no destination) every output connector across the project whose
  // destination referenced it, so no dangling destination survives the
  // removal. Fails, leaving the graph unchanged, if `node_id` or
  // `connector_id` is unknown.
  [[nodiscard]] Result remove_input(NodeId node_id, ConnectorId connector_id);

  // Validates `event` (if set) against the project's EventRegistry, then
  // delegates to Node::bind_output_event, which rejects a vertical/
  // sequential clash for the same event on the same node. Fails if
  // `node_id`/`output_id` is unknown, if `event` is set but not registered,
  // or on that clash.
  [[nodiscard]] Result bind_output_event(NodeId node_id, ConnectorId output_id,
                                         std::optional<EventId> event);

  // Removes `event` from the project's EventRegistry, first unbinding
  // every output across every node currently bound to it (via
  // Node::bind_output_event, which also cleans up the affected node's
  // EventListener), so no output binding or listener is left referencing
  // a deregistered event. Fails, leaving the registry unchanged, if
  // `event` is not registered.
  [[nodiscard]] Result remove_event(EventId event);

  // True iff every export-enabled output connector, across every node in
  // the project, currently has exactly one destination. Editing-time
  // outputs with export_enabled() == false are not required to have one
  // (product decision: "Zero or one destination edge per output while
  // editing; enabled exportable outputs require exactly one").
  [[nodiscard]] bool is_export_ready() const;

 private:
  Project* project_;
};

}  // namespace graphscore
