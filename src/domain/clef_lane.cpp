// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/clef_lane.hpp>

#include <algorithm>

namespace graphscore {

Result ClefLane::add_change(Rational position, Clef clef) {
  if (position < Rational(0))
    return Result(ResultCode::kInvalidArgument);

  const auto it =
      std::lower_bound(changes_.begin(), changes_.end(), position,
                       [](const ClefChange& change, Rational value) {
                         return change.position < value;
                       });
  if (it != changes_.end() && it->position == position)
    return Result(ResultCode::kInvalidArgument);

  changes_.insert(it, ClefChange{position, clef});
  return Result();
}

Clef ClefLane::clef_at(Rational position) const {
  Clef result = default_clef_;
  for (const ClefChange& change : changes_) {
    if (change.position > position)
      break;
    result = change.clef;
  }
  return result;
}

}  // namespace graphscore
