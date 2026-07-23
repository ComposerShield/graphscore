// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <graphscore/core/dynamic.hpp>
#include <graphscore/core/result.hpp>
#include <graphscore/domain/command.hpp>

namespace graphscore {

// Sets the project-wide default dynamic marking. Snapshots the old dynamic
// on first successful execution.
class SetProjectDynamicCommand : public Command {
 public:
  explicit SetProjectDynamicCommand(Dynamic new_dynamic)
      : new_dynamic_(new_dynamic) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

 private:
  Dynamic new_dynamic_;
  Dynamic old_dynamic_ = Dynamic::kMf;
  State   state_       = State::kFresh;
};

}  // namespace graphscore
