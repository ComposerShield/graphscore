// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/node_timeline.hpp>

#include <optional>
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

Result NodeTimeline::set_tempo(std::vector<TempoPoint> points) {
  std::optional<TempoLane> lane =
      TempoLane::create(std::move(points), Rational(0), node_end());
  if (!lane.has_value())
    return Result(ResultCode::kInvalidArgument);

  tempo_ = std::move(lane);
  return Result();
}

std::optional<TempoLane> NodeTimeline::rebuild_tempo_for_end(
    Rational new_end) const {
  return TempoLane::create(tempo_->points(), tempo_->start(), new_end);
}

}  // namespace graphscore
