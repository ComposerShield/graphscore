// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/node_timeline.hpp>

#include <new>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace graphscore {

std::optional<NodeTimeline> NodeTimeline::create(
    std::vector<Measure> measures, const std::vector<StaveDefinition>& staves) {
  std::optional<MeasureMap> measure_map =
      MeasureMap::create(std::move(measures));
  if (!measure_map.has_value())
    return std::nullopt;

  NodeTimeline timeline(std::move(*measure_map));
  for (const StaveDefinition& stave : staves) {
    timeline.clef_lanes_.try_emplace(stave.id, stave.default_clef);
  }
  return timeline;
}

NodeTimeline::NodeTimeline(MeasureMap measures)
    : measures_(std::move(measures)) {}

Result NodeTimeline::set_measure_key_signature(
    const std::size_t measure_index, const KeySignature key_signature) {
  if (measure_index >= measures_.measure_count())
    return Result(ResultCode::kInvalidArgument);

  Measure replacement       = measures_.measure(measure_index);
  replacement.key_signature = key_signature;
  return measures_.set_measure(measure_index, replacement);
}

Result NodeTimeline::insert_measure(const std::size_t index, Measure measure) {
  if (index > measures_.measure_count())
    return Result(ResultCode::kInvalidArgument);

  try {
    NodeTimeline                  candidate = *this;
    const std::optional<Rational> inserted_length =
        Rational::create(measure.time_signature.numerator(),
                         measure.time_signature.denominator());
    if (!inserted_length.has_value())
      return Result(ResultCode::kInternalError);
    const Rational start = index == measures_.measure_count()
                               ? measures_.total_length()
                               : measures_.measure_start(index);
    const Result   insert_result =
        candidate.measures_.insert_measure(index, measure);
    if (!insert_result.ok())
      return insert_result;
    const Result transform_result = candidate.transform_absolute_lanes(
        MeasureEditKind::kInsert, start, *inserted_length);
    if (!transform_result.ok())
      return transform_result;
    *this = std::move(candidate);
    return Result();
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }
}

Result NodeTimeline::insert_measure(const std::size_t index) {
  if (index > measures_.measure_count())
    return Result(ResultCode::kInvalidArgument);
  const std::size_t inherited_index =
      index == measures_.measure_count() ? index - 1 : index;
  return insert_measure(index, measures_.measure(inherited_index));
}

Result NodeTimeline::remove_measure(const std::size_t index) {
  if (index >= measures_.measure_count())
    return Result(ResultCode::kInvalidArgument);

  try {
    NodeTimeline   candidate = *this;
    const Rational start     = measures_.measure_start(index);
    const Rational length    = measures_.measure_length(index);

    // Deliberately delegate to MeasureMap so its sole-measure guard remains
    // the one source of truth for the non-empty main-region invariant.
    const Result remove_result = candidate.measures_.remove_measure(index);
    if (!remove_result.ok())
      return remove_result;
    const Result transform_result = candidate.transform_absolute_lanes(
        MeasureEditKind::kDelete, start, length);
    if (!transform_result.ok())
      return transform_result;
    *this = std::move(candidate);
    return Result();
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }
}

Result NodeTimeline::set_measure_time_signature(
    const std::size_t measure_index, const TimeSignature time_signature) {
  if (measure_index >= measures_.measure_count())
    return Result(ResultCode::kInvalidArgument);

  try {
    NodeTimeline   candidate   = *this;
    Measure        replacement = measures_.measure(measure_index);
    const Rational old_length  = measures_.measure_length(measure_index);
    replacement.time_signature = time_signature;
    const std::uint8_t            numerator   = time_signature.numerator();
    const std::uint16_t           denominator = time_signature.denominator();
    const std::optional<Rational> new_length =
        Rational::create(numerator, denominator);
    if (!new_length.has_value())
      return Result(ResultCode::kInternalError);
    const Rational start = measures_.measure_start(measure_index);

    const Result set_result =
        candidate.measures_.set_measure(measure_index, replacement);
    if (!set_result.ok())
      return set_result;
    if (*new_length > old_length) {
      const Result transform_result = candidate.transform_absolute_lanes(
          MeasureEditKind::kInsert, start + old_length,
          *new_length - old_length);
      if (!transform_result.ok())
        return transform_result;
    } else if (*new_length < old_length) {
      const Result transform_result = candidate.transform_absolute_lanes(
          MeasureEditKind::kDelete, start + *new_length,
          old_length - *new_length);
      if (!transform_result.ok())
        return transform_result;
    }
    *this = std::move(candidate);
    return Result();
  } catch (const std::bad_alloc&) {
    return Result(ResultCode::kOutOfMemory);
  } catch (const std::length_error&) {
    return Result(ResultCode::kOutOfMemory);
  }
}

Result NodeTimeline::set_pickdown(Rational duration) {
  const std::size_t last_index           = measures_.measure_count() - 1;
  const Rational boundary_measure_length = measures_.measure_length(last_index);
  if (duration <= Rational(0) || duration >= boundary_measure_length)
    return Result(ResultCode::kInvalidArgument);

  if (tempo_.has_value()) {
    std::optional<TempoLane> revalidated =
        rebuild_tempo_for_end(boundary_position() + duration);
    if (!revalidated.has_value())
      return Result(ResultCode::kInvalidArgument);

    pickdown_duration_ = duration;
    tempo_             = std::move(revalidated);
    return Result();
  }

  pickdown_duration_ = duration;
  return Result();
}

Result NodeTimeline::clear_pickdown() {
  if (tempo_.has_value()) {
    std::optional<TempoLane> revalidated =
        rebuild_tempo_for_end(boundary_position());
    if (!revalidated.has_value())
      return Result(ResultCode::kInvalidArgument);

    pickdown_duration_.reset();
    tempo_ = std::move(revalidated);
    return Result();
  }

  pickdown_duration_.reset();
  return Result();
}

Rational NodeTimeline::node_end() const {
  return boundary_position() + pickdown_duration_.value_or(Rational(0));
}

TimelineRegion NodeTimeline::classify(Rational position) const {
  return position < boundary_position() ? TimelineRegion::kMain
                                        : TimelineRegion::kPickdown;
}

SpanClassification NodeTimeline::classify(const MusicalSpan& span) const {
  const Rational boundary = boundary_position();

  if (span.end <= boundary)
    return SpanClassification{span, std::nullopt};
  if (span.start >= boundary)
    return SpanClassification{std::nullopt, span};

  return SpanClassification{MusicalSpan{span.start, boundary},
                            MusicalSpan{boundary, span.end}};
}

ClefLane* NodeTimeline::clef_lane(StaveId stave_id) {
  const auto it = clef_lanes_.find(stave_id);
  return it == clef_lanes_.end() ? nullptr : &it->second;
}

const ClefLane* NodeTimeline::clef_lane(StaveId stave_id) const {
  const auto it = clef_lanes_.find(stave_id);
  return it == clef_lanes_.end() ? nullptr : &it->second;
}

Result NodeTimeline::add_clef_change(const StaveId  stave_id,
                                     const Rational position, const Clef clef) {
  ClefLane* lane = clef_lane(stave_id);
  if (lane == nullptr)
    return Result(ResultCode::kInvalidArgument);
  return lane->add_change(position, clef);
}

Result NodeTimeline::remove_clef_change(const StaveId  stave_id,
                                        const Rational position) {
  ClefLane* lane = clef_lane(stave_id);
  if (lane == nullptr)
    return Result(ResultCode::kInvalidArgument);
  return lane->remove_change(position);
}

Result NodeTimeline::move_clef_change(const StaveId  stave_id,
                                      const Rational from, const Rational to) {
  ClefLane* lane = clef_lane(stave_id);
  if (lane == nullptr)
    return Result(ResultCode::kInvalidArgument);
  return lane->move_change(from, to);
}

Result NodeTimeline::restore_clef_lane(const StaveId stave_id,
                                       ClefLane      replacement) noexcept {
  ClefLane* lane = clef_lane(stave_id);
  if (lane == nullptr || lane->default_clef() != replacement.default_clef())
    return Result(ResultCode::kInvalidArgument);
  *lane = std::move(replacement);
  return Result();
}

Result NodeTimeline::set_tempo(std::vector<TempoPoint> points) {
  std::optional<TempoLane> lane =
      TempoLane::create(std::move(points), Rational(0), node_end());
  if (!lane.has_value())
    return Result(ResultCode::kInvalidArgument);

  tempo_ = std::move(lane);
  return Result();
}

void NodeTimeline::clear_tempo() noexcept {
  tempo_.reset();
}

std::optional<TempoLane> NodeTimeline::rebuild_tempo_for_end(
    Rational new_end) const {
  if (!tempo_.has_value())
    return std::nullopt;
  return TempoLane::create(tempo_->points(), tempo_->start(), new_end);
}

Result NodeTimeline::transform_absolute_lanes(const MeasureEditKind kind,
                                              const Rational        start,
                                              const Rational        length) {
  const Rational removed_end = start + length;

  for (auto& [stave_id, lane] : clef_lanes_) {
    static_cast<void>(stave_id);
    ClefLane transformed(lane.default_clef());
    for (const ClefChange& change : lane.changes()) {
      std::optional<Rational> position;
      if (kind == MeasureEditKind::kInsert) {
        position = change.position < start ? change.position
                                           : change.position + length;
      } else if (change.position < start) {
        position = change.position;
      } else if (change.position >= removed_end) {
        position = change.position - length;
      }
      if (position.has_value()) {
        const Result result = transformed.add_change(*position, change.clef);
        if (!result.ok())
          return result;
      }
    }
    lane = std::move(transformed);
  }

  if (pickdown_duration_.has_value()) {
    const std::size_t last_index = measures_.measure_count() - 1;
    if (*pickdown_duration_ <= Rational(0) ||
        *pickdown_duration_ >= measures_.measure_length(last_index)) {
      return Result(ResultCode::kInvalidArgument);
    }
  }

  if (!tempo_.has_value())
    return Result();

  std::vector<TempoPoint> transformed;
  transformed.reserve(tempo_->points().size() + 1);
  const TempoPoint original_origin = tempo_->points().front();
  for (const TempoPoint& point : tempo_->points()) {
    if (kind == MeasureEditKind::kInsert) {
      if (start == Rational(0) && point.position == Rational(0)) {
        transformed.push_back(point);
        TempoPoint shifted = point;
        shifted.position   = length;
        transformed.push_back(shifted);
      } else {
        TempoPoint shifted = point;
        if (shifted.position >= start)
          shifted.position = shifted.position + length;
        transformed.push_back(shifted);
      }
    } else if (point.position < start) {
      transformed.push_back(point);
    } else if (point.position >= removed_end) {
      TempoPoint shifted = point;
      shifted.position   = shifted.position - length;
      transformed.push_back(shifted);
    }
  }

  if (kind == MeasureEditKind::kDelete && start == Rational(0) &&
      (transformed.empty() || transformed.front().position != Rational(0))) {
    transformed.insert(transformed.begin(), original_origin);
  }

  return set_tempo(std::move(transformed));
}

}  // namespace graphscore
