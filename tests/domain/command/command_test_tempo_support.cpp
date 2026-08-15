// SPDX-License-Identifier: Apache-2.0

#include "command_test_tempo_support.hpp"
#include "command_test_support.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <utility>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>

TempoSetup make_tempo_setup(int measure_count) {
  Project      project = make_project();
  const NodeId node_id = project.add_node("Tempo Node");

  std::vector<Measure> measures;
  for (int i = 0; i < measure_count; ++i) {
    measures.push_back(
        Measure{*TimeSignature::create(4, 4), *KeySignature::create(0)});
  }
  auto tl = NodeTimeline::create(std::move(measures), {});
  assert(tl.has_value());
  project.find_node(node_id)->set_timeline(std::move(*tl));

  return TempoSetup{std::move(project), node_id};
}

TempoPoint tempo_point(Rational position, std::int64_t bpm,
                       TempoSegmentKind kind) {
  return TempoPoint{position,
                    *Tempo::create(Rational(bpm), NoteValue::kQuarter), kind};
}

TempoPoint tempo_point(Rational position, std::int64_t bpm) {
  return tempo_point(position, bpm, TempoSegmentKind::kStep);
}

const TempoLane* tempo_lane(const Project& project, NodeId node_id) {
  const Node* node = project.find_node(node_id);
  if (node == nullptr)
    return nullptr;
  const NodeTimeline* timeline = node->timeline();
  if (timeline == nullptr)
    return nullptr;
  return timeline->tempo();
}

std::vector<TempoPoint> tempo_points(const Project& project, NodeId node_id) {
  const TempoLane* lane = tempo_lane(project, node_id);
  return lane == nullptr ? std::vector<TempoPoint>{} : lane->points();
}

void seed_lane(Project* project, NodeId node_id,
               const std::vector<TempoPoint>& points) {
  NodeTimeline* timeline = project->find_node(node_id)->timeline();
  ASSERT_NE(timeline, nullptr);
  ASSERT_TRUE(timeline->set_tempo(points).ok());
}
