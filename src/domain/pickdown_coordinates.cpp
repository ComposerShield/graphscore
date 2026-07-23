// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/pickdown_coordinates.hpp>

#include <cstddef>
#include <optional>

namespace graphscore {

namespace {

std::optional<PickdownCoordinate> coordinate_at_position(
    const NodeTimeline& timeline, Rational position) {
  if (!timeline.pickdown_duration().has_value())
    return std::nullopt;
  if (position < timeline.boundary_position() ||
      position >= timeline.node_end())
    return std::nullopt;

  const MeasureMap&      measures       = timeline.measures();
  const std::size_t      boundary_index = measures.measure_count() - 1;
  const TempoLane* const tempo          = timeline.tempo();

  PickdownCoordinate coordinate;
  coordinate.position               = position;
  coordinate.offset                 = position - timeline.boundary_position();
  coordinate.boundary_measure_index = boundary_index;
  coordinate.governing_time_signature =
      measures.measure(boundary_index).time_signature;
  coordinate.tempo_segment_index =
      tempo != nullptr ? tempo->segment_index_at(position) : std::nullopt;
  return coordinate;
}

}  // namespace

std::optional<PickdownCoordinate> pickdown_coordinate_at_position(
    const NodeTimeline& timeline, Rational position) {
  return coordinate_at_position(timeline, position);
}

std::optional<PickdownCoordinate> pickdown_coordinate_at_offset(
    const NodeTimeline& timeline, Rational offset) {
  if (offset < Rational(0))
    return std::nullopt;
  return coordinate_at_position(timeline,
                                timeline.boundary_position() + offset);
}

}  // namespace graphscore
