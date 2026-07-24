// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

#include <graphscore/core/result.hpp>
#include <graphscore/core/strong_id.hpp>
#include <graphscore/domain/command.hpp>
#include <graphscore/domain/event_listener.hpp>
#include <graphscore/domain/event_registry.hpp>

namespace graphscore {

// Removes a registered event, identified by its stable EventId, cascading
// to unbind every output across every node currently bound to it and to
// clear each affected node's EventListener for it (see Graph::remove_event).
// Fails, leaving the registry unchanged, if `event_id` is not registered.
//
// Reversibility: execute snapshots the removed EventDefinition, every
// (node, output) pair whose OutputConnector was bound to the event, and
// every affected node's listener policy/capacity, on first success. Undo
// re-registers the exact same id/name through
// EventRegistry::add_event_with_id, rebinds every snapshotted output
// (recreating each affected node's listener with the event's bound type),
// then reapplies every snapshotted listener's policy/capacity so the
// configuration is restored exactly rather than reset to defaults. Redo
// removes the event again.
class RemoveEventCommand : public Command {
 public:
  explicit RemoveEventCommand(EventId event_id) : event_id_(event_id) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

 private:
  struct BoundOutput {
    NodeId      node_id;
    ConnectorId output_id;
  };

  struct ListenerSnapshot {
    NodeId      node_id;
    QueuePolicy policy;
    std::size_t capacity;
  };

  EventId                        event_id_;
  std::optional<EventDefinition> removed_def_;
  std::vector<BoundOutput>       bound_outputs_;
  std::vector<ListenerSnapshot>  listeners_;
  State                          state_ = State::kFresh;
};

}  // namespace graphscore
