// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/measure_map.hpp>

#include <cassert>
#include <optional>
#include <utility>
#include <vector>

namespace graphscore {

std::optional<MeasureMap> MeasureMap::create(std::vector<Measure> measures) {
  if (measures.empty())
    return std::nullopt;
  return MeasureMap(std::move(measures));
}

MeasureMap::MeasureMap(std::vector<Measure> measures)
    : measures_(std::move(measures)) {
  starts_.reserve(measures_.size());
  Rational position(0);
  for (std::size_t i = 0; i < measures_.size(); ++i) {
    starts_.push_back(position);
    position = position + measure_length(i);
  }
  total_length_ = position;
}

const Measure& MeasureMap::measure(std::size_t index) const {
  assert(index < measures_.size());
  return measures_[index];
}

Rational MeasureMap::measure_length(std::size_t index) const {
  assert(index < measures_.size());
  const TimeSignature& signature = measures_[index].time_signature;
  return *Rational::create(signature.numerator(), signature.denominator());
}

Rational MeasureMap::measure_start(std::size_t index) const {
  assert(index < starts_.size());
  return starts_[index];
}

std::optional<std::size_t> MeasureMap::measure_index_at(
    Rational position) const {
  if (position < Rational(0) || position >= total_length_)
    return std::nullopt;

  for (std::size_t i = 0; i < measures_.size(); ++i) {
    const Rational start = starts_[i];
    const Rational end   = start + measure_length(i);
    if (position >= start && position < end)
      return i;
  }
  return std::nullopt;
}

Rational MeasureMap::total_length() const {
  return total_length_;
}

}  // namespace graphscore
