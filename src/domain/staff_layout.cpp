// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/staff_layout.hpp>

#include <utility>
#include <vector>

namespace graphscore {

std::optional<StaffLayout> StaffLayout::create(
    std::vector<StaveDefinition> staves) {
  if (staves.empty())
    return std::nullopt;
  return StaffLayout(std::move(staves));
}

StaffLayout StaffLayout::single_staff(Clef clef) {
  return StaffLayout(std::vector<StaveDefinition>{
      StaveDefinition{StaveId::generate(), clef},
  });
}

StaffLayout StaffLayout::grand_staff() {
  return StaffLayout(std::vector<StaveDefinition>{
      StaveDefinition{StaveId::generate(), Clef::kTreble},
      StaveDefinition{StaveId::generate(), Clef::kBass},
  });
}

}  // namespace graphscore
