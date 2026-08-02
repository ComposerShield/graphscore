// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/measure_map.hpp>

#include <cassert>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace graphscore {

namespace {

Rational measure_length_of(const Measure& measure) {
  const TimeSignature& signature = measure.time_signature;
  // TimeSignature guarantees a non-zero denominator, and Rational::create
  // fails only on a zero denominator, so this is always engaged.
  const std::optional<Rational> length =
      Rational::create(signature.numerator(), signature.denominator());
  assert(length.has_value());
  return *length;
}

struct DerivedState {
  std::vector<Rational> starts;
  Rational              total_length;
};

DerivedState compute_derived_state(const std::vector<Measure>& measures) {
  DerivedState state;
  state.starts.reserve(measures.size());
  Rational position(0);
  for (const Measure& measure : measures) {
    state.starts.push_back(position);
    position = position + measure_length_of(measure);
  }
  state.total_length = position;
  return state;
}

}  // namespace

std::optional<MeasureMap> MeasureMap::create(std::vector<Measure> measures) {
  if (measures.empty())
    return std::nullopt;
  return MeasureMap(std::move(measures));
}

MeasureMap::MeasureMap(std::vector<Measure> measures)
    : measures_(std::move(measures)) {
  DerivedState state = compute_derived_state(measures_);
  starts_            = std::move(state.starts);
  total_length_      = state.total_length;
}

const Measure& MeasureMap::measure(std::size_t index) const {
  assert(index < measures_.size());
  return measures_[index];
}

Rational MeasureMap::measure_length(std::size_t index) const {
  assert(index < measures_.size());
  return measure_length_of(measures_[index]);
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

Result MeasureMap::insert_measure(std::size_t index, Measure measure) {
  if (index > measures_.size())
    return Result(ResultCode::kInvalidArgument);

  std::vector<Measure> updated = measures_;
  updated.insert(updated.begin() + static_cast<std::ptrdiff_t>(index), measure);

  DerivedState state = compute_derived_state(updated);
  measures_          = std::move(updated);
  starts_            = std::move(state.starts);
  total_length_      = state.total_length;
  return Result();
}

Result MeasureMap::insert_measure(std::size_t index) {
  if (index > measures_.size())
    return Result(ResultCode::kInvalidArgument);

  assert(!measures_.empty());
  const Measure source =
      index < measures_.size() ? measures_[index] : measures_.back();
  return insert_measure(index, source);
}

Result MeasureMap::remove_measure(std::size_t index) {
  if (index >= measures_.size())
    return Result(ResultCode::kInvalidArgument);
  if (measures_.size() == 1)
    return Result(ResultCode::kInvalidArgument);

  std::vector<Measure> updated = measures_;
  updated.erase(updated.begin() + static_cast<std::ptrdiff_t>(index));

  DerivedState state = compute_derived_state(updated);
  measures_          = std::move(updated);
  starts_            = std::move(state.starts);
  total_length_      = state.total_length;
  return Result();
}

Result MeasureMap::set_measure(std::size_t index, Measure measure) {
  if (index >= measures_.size())
    return Result(ResultCode::kInvalidArgument);

  std::vector<Measure> updated = measures_;
  updated[index]               = measure;

  DerivedState state = compute_derived_state(updated);
  measures_          = std::move(updated);
  starts_            = std::move(state.starts);
  total_length_      = state.total_length;
  return Result();
}

}  // namespace graphscore
