// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <optional>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>

using graphscore::ConnectorType;
using graphscore::Graph;
using graphscore::KeySignature;
using graphscore::Measure;
using graphscore::NodeId;
using graphscore::NodeTimeline;
using graphscore::PickdownBoundResult;
using graphscore::PickdownBoundStatus;
using graphscore::Project;
using graphscore::ProjectId;
using graphscore::prove_pickdown_overlap_bound;
using graphscore::Rational;
using graphscore::TimeSignature;

namespace {

Project make_project() {
  return Project(ProjectId::generate(), "Project");
}

Measure make_measure() {
  return Measure{*TimeSignature::create(4, 4), *KeySignature::create(0)};
}

std::optional<NodeTimeline> make_timeline_with_pickdown() {
  auto timeline = NodeTimeline::create({make_measure()}, {});
  if (!timeline.has_value())
    return std::nullopt;
  if (!timeline->set_pickdown(*Rational::create(1, 4)).ok())
    return std::nullopt;
  return timeline;
}

std::optional<NodeTimeline> make_timeline_without_pickdown() {
  return NodeTimeline::create({make_measure()}, {});
}

}  // namespace

TEST(PickdownBoundOracleTest, ProjectWithNoNodesIsBoundedAtZero) {
  const Project project = make_project();

  const PickdownBoundResult result = prove_pickdown_overlap_bound(project);
  EXPECT_EQ(result.status, PickdownBoundStatus::kBounded);
  ASSERT_TRUE(result.bound.has_value());
  EXPECT_EQ(*result.bound, 0u);
  EXPECT_FALSE(result.unbounded_node.has_value());
}

TEST(PickdownBoundOracleTest, NodesWithoutAPickdownNeverAffectTheBound) {
  Project project = make_project();
  Graph   graph(project);

  const NodeId a_id     = project.add_node("A");
  const NodeId b_id     = project.add_node("B");
  auto         timeline = make_timeline_without_pickdown();
  ASSERT_TRUE(timeline.has_value());
  project.find_node(a_id)->set_timeline(*timeline);
  project.find_node(b_id)->set_timeline(*timeline);

  const auto out_id     = project.find_node(a_id)->add_output("Out");
  const auto in_id      = project.find_node(b_id)->add_input("In");
  const auto back_id    = project.find_node(b_id)->add_output("Back");
  const auto back_in_id = project.find_node(a_id)->add_input("In");
  ASSERT_TRUE(graph.connect(a_id, out_id, b_id, in_id).ok());
  ASSERT_TRUE(graph.connect(b_id, back_id, a_id, back_in_id).ok());

  const PickdownBoundResult result = prove_pickdown_overlap_bound(project);
  EXPECT_EQ(result.status, PickdownBoundStatus::kBounded);
  ASSERT_TRUE(result.bound.has_value());
  EXPECT_EQ(*result.bound, 0u);
}

TEST(PickdownBoundOracleTest, AnUnreachablePickdownNodeIsBoundedAtOne) {
  Project project = make_project();

  const NodeId a_id     = project.add_node("A");
  auto         timeline = make_timeline_with_pickdown();
  ASSERT_TRUE(timeline.has_value());
  project.find_node(a_id)->set_timeline(*timeline);

  const PickdownBoundResult result = prove_pickdown_overlap_bound(project);
  EXPECT_EQ(result.status, PickdownBoundStatus::kBounded);
  ASSERT_TRUE(result.bound.has_value());
  EXPECT_EQ(*result.bound, 1u);
}

TEST(PickdownBoundOracleTest, ADagOfPickdownNodesSumsToTheirCount) {
  Project project = make_project();
  Graph   graph(project);

  const NodeId a_id     = project.add_node("A");
  const NodeId b_id     = project.add_node("B");
  const NodeId c_id     = project.add_node("C");
  auto         timeline = make_timeline_with_pickdown();
  ASSERT_TRUE(timeline.has_value());
  project.find_node(a_id)->set_timeline(*timeline);
  project.find_node(b_id)->set_timeline(*timeline);
  project.find_node(c_id)->set_timeline(*timeline);

  const auto a_out = project.find_node(a_id)->add_output("Out");
  const auto b_in  = project.find_node(b_id)->add_input("In");
  const auto b_out = project.find_node(b_id)->add_output("Out");
  const auto c_in  = project.find_node(c_id)->add_input("In");
  ASSERT_TRUE(graph.connect(a_id, a_out, b_id, b_in).ok());
  ASSERT_TRUE(graph.connect(b_id, b_out, c_id, c_in).ok());

  const PickdownBoundResult result = prove_pickdown_overlap_bound(project);
  EXPECT_EQ(result.status, PickdownBoundStatus::kBounded);
  ASSERT_TRUE(result.bound.has_value());
  EXPECT_EQ(*result.bound, 3u);
}

TEST(PickdownBoundOracleTest, ASelfLoopingPickdownNodeIsBoundedAtOne) {
  Project project = make_project();
  Graph   graph(project);

  const NodeId a_id     = project.add_node("A");
  auto         timeline = make_timeline_with_pickdown();
  ASSERT_TRUE(timeline.has_value());
  project.find_node(a_id)->set_timeline(*timeline);

  const auto out_id = project.find_node(a_id)->add_output("Out");
  const auto in_id  = project.find_node(a_id)->add_input("In");
  ASSERT_TRUE(graph.connect(a_id, out_id, a_id, in_id).ok());

  const PickdownBoundResult result = prove_pickdown_overlap_bound(project);
  EXPECT_EQ(result.status, PickdownBoundStatus::kBounded);
  ASSERT_TRUE(result.bound.has_value());
  EXPECT_EQ(*result.bound, 1u);
  EXPECT_FALSE(result.unbounded_node.has_value());
}

TEST(PickdownBoundOracleTest, ATwoNodeSequentialCycleIsBoundedAtTwo) {
  Project project = make_project();
  Graph   graph(project);

  const NodeId a_id     = project.add_node("A");
  const NodeId b_id     = project.add_node("B");
  auto         timeline = make_timeline_with_pickdown();
  ASSERT_TRUE(timeline.has_value());
  project.find_node(a_id)->set_timeline(*timeline);
  project.find_node(b_id)->set_timeline(*timeline);

  const auto a_out = project.find_node(a_id)->add_output("Out");
  const auto b_in  = project.find_node(b_id)->add_input("In");
  const auto b_out = project.find_node(b_id)->add_output("Out");
  const auto a_in  = project.find_node(a_id)->add_input("In");
  ASSERT_TRUE(graph.connect(a_id, a_out, b_id, b_in).ok());
  ASSERT_TRUE(graph.connect(b_id, b_out, a_id, a_in).ok());

  const PickdownBoundResult result = prove_pickdown_overlap_bound(project);
  EXPECT_EQ(result.status, PickdownBoundStatus::kBounded);
  ASSERT_TRUE(result.bound.has_value());
  EXPECT_EQ(*result.bound, 2u);
}

TEST(PickdownBoundOracleTest,
     AVerticalEdgeParticipatingInACycleIsBoundedAtTwo) {
  Project project = make_project();
  Graph   graph(project);

  const NodeId a_id     = project.add_node("A");
  const NodeId b_id     = project.add_node("B");
  auto         timeline = make_timeline_with_pickdown();
  ASSERT_TRUE(timeline.has_value());
  project.find_node(a_id)->set_timeline(*timeline);
  project.find_node(b_id)->set_timeline(*timeline);

  // A -> B is a vertical output; B -> A is sequential. The cycle closes
  // through the vertical edge, and is still legal and bounded.
  const auto a_out =
      project.find_node(a_id)->add_output("Out", ConnectorType::kVertical);
  const auto b_in  = project.find_node(b_id)->add_input("In");
  const auto b_out = project.find_node(b_id)->add_output("Out");
  const auto a_in  = project.find_node(a_id)->add_input("In");
  ASSERT_TRUE(graph.connect(a_id, a_out, b_id, b_in).ok());
  ASSERT_TRUE(graph.connect(b_id, b_out, a_id, a_in).ok());

  const PickdownBoundResult result = prove_pickdown_overlap_bound(project);
  EXPECT_EQ(result.status, PickdownBoundStatus::kBounded);
  ASSERT_TRUE(result.bound.has_value());
  EXPECT_EQ(*result.bound, 2u);
}

TEST(PickdownBoundOracleTest, ADestinationLessOutputDoesNotAffectTheBound) {
  Project project = make_project();

  const NodeId a_id     = project.add_node("A");
  auto         timeline = make_timeline_with_pickdown();
  ASSERT_TRUE(timeline.has_value());
  project.find_node(a_id)->set_timeline(*timeline);

  // An output exists but is never connected -- it must not affect the
  // bound one way or the other.
  static_cast<void>(project.find_node(a_id)->add_output("Out"));

  const PickdownBoundResult result = prove_pickdown_overlap_bound(project);
  EXPECT_EQ(result.status, PickdownBoundStatus::kBounded);
  ASSERT_TRUE(result.bound.has_value());
  EXPECT_EQ(*result.bound, 1u);
}

TEST(PickdownBoundOracleTest,
     APickdownBearingNodeOnACycleWithNonPickdownNodesIsBounded) {
  // Adam's ruling (product decision, Phase 6b-i; docs/plan/M02_HANDOFF.md):
  // a pickdown on a cycle is legal and bounded -- the pickdown tail sounds
  // on every loop iteration, overlapping the start of the next iteration's
  // main region ("the pickdown measure still plays on every loop by
  // overlapping with the start of the loop").
  Project project = make_project();
  Graph   graph(project);

  const NodeId a_id             = project.add_node("A");
  const NodeId b_id             = project.add_node("B");
  auto         with_pickdown    = make_timeline_with_pickdown();
  auto         without_pickdown = make_timeline_without_pickdown();
  ASSERT_TRUE(with_pickdown.has_value());
  ASSERT_TRUE(without_pickdown.has_value());
  project.find_node(a_id)->set_timeline(*with_pickdown);
  project.find_node(b_id)->set_timeline(*without_pickdown);

  const auto a_out = project.find_node(a_id)->add_output("Out");
  const auto b_in  = project.find_node(b_id)->add_input("In");
  const auto b_out = project.find_node(b_id)->add_output("Out");
  const auto a_in  = project.find_node(a_id)->add_input("In");
  ASSERT_TRUE(graph.connect(a_id, a_out, b_id, b_in).ok());
  ASSERT_TRUE(graph.connect(b_id, b_out, a_id, a_in).ok());

  const PickdownBoundResult result = prove_pickdown_overlap_bound(project);
  EXPECT_EQ(result.status, PickdownBoundStatus::kBounded);
  ASSERT_TRUE(result.bound.has_value());
  EXPECT_EQ(*result.bound, 1u);
  EXPECT_FALSE(result.unbounded_node.has_value());
}
