// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/vertical_transition.hpp>

#include <cstddef>
#include <optional>

namespace graphscore {

bool vertical_regions_compatible(const NodeTimeline& source,
                                 const NodeTimeline& destination) {
  const MeasureMap& source_measures      = source.measures();
  const MeasureMap& destination_measures = destination.measures();

  if (source_measures.measure_count() != destination_measures.measure_count())
    return false;

  for (std::size_t index = 0; index < source_measures.measure_count();
       ++index) {
    if (source_measures.measure(index).time_signature !=
        destination_measures.measure(index).time_signature)
      return false;
  }
  return true;
}

std::optional<Rational> map_vertical_position(const NodeTimeline& source,
                                              const NodeTimeline& destination,
                                              Rational            position) {
  if (!vertical_regions_compatible(source, destination))
    return std::nullopt;

  const std::optional<std::size_t> measure_index =
      source.measures().measure_index_at(position);
  if (!measure_index.has_value())
    return std::nullopt;

  const Rational offset_in_measure =
      position - source.measures().measure_start(*measure_index);
  return destination.measures().measure_start(*measure_index) +
         offset_in_measure;
}

}  // namespace graphscore
