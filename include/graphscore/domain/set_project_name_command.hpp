// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <utility>

#include <graphscore/core/result.hpp>
#include <graphscore/domain/command.hpp>

namespace graphscore {

// Sets the project display name. Snapshots the old name on first successful
// execution.
class SetProjectNameCommand : public Command {
 public:
  explicit SetProjectNameCommand(std::string new_name)
      : new_name_(std::move(new_name)) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

 private:
  std::string new_name_;
  std::string old_name_;
  State       state_ = State::kFresh;
};

}  // namespace graphscore
