// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <optional>

#include <graphscore/core/result.hpp>
#include <graphscore/core/strong_id.hpp>
#include <graphscore/domain/command.hpp>
#include <graphscore/domain/event_listener.hpp>

namespace graphscore {

// Binds (or clears, via nullopt) an output connector's event trigger (see
// Graph::bind_output_event). Fails, leaving the binding unchanged, if the
// node/output id is unknown, if the new event is set but not registered, or
// on a vertical/sequential clash with another output on the same node.
//
// Reversibility subtlety: unbinding the last output on a node referencing
// an event destroys that node's EventListener entirely (see
// Node::bind_output_event); a later rebind to the same event creates a
// fresh listener with default policy/capacity. This command snapshots the
// prior event binding and, if a listener for it existed, its exact
// policy/capacity, on first successful execute. Undo restores the binding
// and, if a listener existed before, reapplies the snapshotted
// policy/capacity to whichever listener now exists for it -- preserved or
// freshly recreated -- so the configuration is restored exactly rather than
// silently reset to defaults.
class BindOutputEventCommand : public Command {
 public:
  BindOutputEventCommand(NodeId node_id, ConnectorId output_id,
                         std::optional<EventId> new_event)
      : node_id_(node_id), output_id_(output_id), new_event_(new_event) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

 private:
  NodeId                 node_id_;
  ConnectorId            output_id_;
  std::optional<EventId> new_event_;
  std::optional<EventId> old_event_;
  bool                   old_listener_existed_ = false;
  QueuePolicy            old_policy_           = QueuePolicy::kLatestValidWins;
  std::size_t            old_capacity_         = 0;
  State                  state_                = State::kFresh;
};

}  // namespace graphscore
