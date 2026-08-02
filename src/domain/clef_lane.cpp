// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/clef_lane.hpp>

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

namespace graphscore {

namespace {

bool position_less(const ClefChange& change, Rational value) {
  return change.position < value;
}

std::vector<ClefChange>::const_iterator find_at(
    const std::vector<ClefChange>& changes, Rational position) {
  const auto it =
      std::lower_bound(changes.begin(), changes.end(), position, position_less);
  if (it != changes.end() && it->position == position)
    return it;
  return changes.end();
}

}  // namespace

Result ClefLane::add_change(Rational position, Clef clef) {
  if (position < Rational(0))
    return Result(ResultCode::kInvalidArgument);

  const auto it = std::lower_bound(changes_.begin(), changes_.end(), position,
                                   position_less);
  if (it != changes_.end() && it->position == position)
    return Result(ResultCode::kInvalidArgument);

  changes_.insert(it, ClefChange{position, clef});
  return Result();
}

Result ClefLane::remove_change(Rational position) {
  const auto it = find_at(changes_, position);
  if (it == changes_.end())
    return Result(ResultCode::kInvalidArgument);

  std::vector<ClefChange> updated = changes_;
  updated.erase(updated.begin() + (it - changes_.begin()));
  changes_ = std::move(updated);
  return Result();
}

Result ClefLane::move_change(Rational from, Rational to) {
  if (to < Rational(0))
    return Result(ResultCode::kInvalidArgument);

  const auto from_it = find_at(changes_, from);
  if (from_it == changes_.end())
    return Result(ResultCode::kInvalidArgument);
  const Clef clef = from_it->clef;

  std::vector<ClefChange> updated = changes_;
  updated.erase(updated.begin() + (from_it - changes_.begin()));

  const auto to_it =
      std::lower_bound(updated.begin(), updated.end(), to, position_less);
  if (to_it != updated.end() && to_it->position == to)
    return Result(ResultCode::kInvalidArgument);

  updated.insert(to_it, ClefChange{to, clef});
  changes_ = std::move(updated);
  return Result();
}

Result ClefLane::set_change(Rational position, Clef clef) {
  const auto it = find_at(changes_, position);
  if (it == changes_.end())
    return Result(ResultCode::kInvalidArgument);

  std::vector<ClefChange> updated                               = changes_;
  updated[static_cast<std::size_t>(it - changes_.begin())].clef = clef;
  changes_ = std::move(updated);
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
