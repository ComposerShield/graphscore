// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/range_edit_command.hpp>
#include <graphscore/domain/selection.hpp>
#include <graphscore/notation/notation_editing.hpp>

#include <memory>

#include <graphscore/domain/project.hpp>

namespace graphscore {

std::unique_ptr<Command> make_range_delete_command(const Project&   project,
                                                   const Selection& selection) {
  if (!is_valid_range_edit_selection(project, selection))
    return nullptr;
  return std::make_unique<RangeEditCommand>(selection, std::nullopt, 0);
}

std::unique_ptr<Command> make_range_transpose_command(
    const Project& project, const Selection& selection, RangeTransposeKind kind,
    std::int32_t amount) {
  if (amount == 0 || !is_valid_range_edit_selection(project, selection))
    return nullptr;
  return std::make_unique<RangeEditCommand>(selection, kind, amount);
}

}  // namespace graphscore
