// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <optional>
#include <string>
#include <utility>

#include <graphscore/core/result.hpp>
#include <graphscore/core/strong_id.hpp>
#include <graphscore/domain/command.hpp>

namespace graphscore {

// Registers a new event definition with the given name (see
// EventRegistry::add_event). Fails, leaving the registry unchanged, if
// `name` is already registered.
//
// Reversibility: execute mints the event's id and snapshots it on first
// success. Undo removes the event through Graph::remove_event -- the
// cascading entry point, though a freshly registered event has no bound
// outputs or listeners for that cascade to clear. Redo re-registers the
// exact same name through EventRegistry::add_event_with_id, preserving the
// same EventId rather than minting a fresh one.
class RegisterEventCommand : public Command {
 public:
  explicit RegisterEventCommand(std::string name) : name_(std::move(name)) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

 private:
  std::string            name_;
  std::optional<EventId> created_id_;
  State                  state_ = State::kFresh;
};

}  // namespace graphscore
