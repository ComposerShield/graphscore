// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <graphscore/core/result.hpp>
#include <graphscore/core/tempo.hpp>
#include <graphscore/domain/command.hpp>

namespace graphscore {

// Sets the project-wide default tempo. Snapshots the old tempo on first
// successful execution. No node-id or track-id lookup is required; the
// default tempo lives directly on Project.
class SetProjectTempoCommand : public Command {
 public:
  explicit SetProjectTempoCommand(Tempo new_tempo) : new_tempo_(new_tempo) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

 private:
  Tempo new_tempo_;
  Tempo old_tempo_;
  State state_ = State::kFresh;
};

}  // namespace graphscore
