// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>

#include <graphscore/core/result.hpp>
#include <graphscore/core/strong_id.hpp>
#include <graphscore/domain/command.hpp>
#include <graphscore/domain/event_listener.hpp>

namespace graphscore {

// Sets the QueuePolicy and capacity of a node's EventListener for one
// EventId (see Node::set_listener_policy). Snapshots the old policy and
// capacity on first successful execution.
//
// Missing NodeId returns kInvalidArgument without mutation. Missing
// EventListener (no output on the node is currently bound to the event)
// returns kInvalidArgument without mutation. Propagates
// Node::set_listener_policy's failure Result (kFifo with capacity 0)
// without changing state.
class SetListenerPolicyCommand : public Command {
 public:
  SetListenerPolicyCommand(NodeId node_id, EventId event,
                           QueuePolicy new_policy, std::size_t new_capacity)
      : node_id_(node_id),
        event_(event),
        new_policy_(new_policy),
        new_capacity_(new_capacity) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

 private:
  NodeId      node_id_;
  EventId     event_;
  QueuePolicy new_policy_;
  std::size_t new_capacity_;
  QueuePolicy old_policy_   = QueuePolicy::kLatestValidWins;
  std::size_t old_capacity_ = 0;
  State       state_        = State::kFresh;
};

}  // namespace graphscore
