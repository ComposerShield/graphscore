// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/tempo_lane.hpp>

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace graphscore {

std::optional<TempoLane> TempoLane::create(std::vector<TempoPoint> points,
                                           Rational start, Rational end) {
  if (points.empty() || start >= end)
    return std::nullopt;
  if (points.front().position != start)
    return std::nullopt;

  for (std::size_t i = 0; i < points.size(); ++i) {
    if (points[i].position < start || points[i].position >= end)
      return std::nullopt;
    if (i > 0 && points[i - 1].position >= points[i].position)
      return std::nullopt;
  }

  return TempoLane(std::move(points), start, end);
}

TempoLane::TempoLane(std::vector<TempoPoint> points, Rational start,
                     Rational end) noexcept
    : points_(std::move(points)), start_(start), end_(end) {}

std::optional<std::size_t> TempoLane::segment_index_at(
    Rational position) const {
  if (position < start_ || position >= end_)
    return std::nullopt;

  std::optional<std::size_t> governing;
  for (std::size_t index = 0; index < points_.size(); ++index) {
    if (points_[index].position > position)
      break;
    governing = index;
  }
  return governing;
}

}  // namespace graphscore
