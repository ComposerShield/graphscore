// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "command_test_support.hpp"

#include <cstdint>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>

// A project with one node whose timeline is `measure_count` 4/4 measures,
// i.e. node_end == measure_count whole notes.
struct TempoSetup {
  Project project;
  NodeId  node_id;
};

TempoSetup make_tempo_setup(int measure_count = 4);

TempoPoint tempo_point(Rational position, std::int64_t bpm,
                       TempoSegmentKind kind);
TempoPoint tempo_point(Rational position, std::int64_t bpm);

const TempoLane* tempo_lane(const Project& project, NodeId node_id);

std::vector<TempoPoint> tempo_points(const Project& project, NodeId node_id);

void seed_lane(Project* project, NodeId node_id,
               const std::vector<TempoPoint>& points);
