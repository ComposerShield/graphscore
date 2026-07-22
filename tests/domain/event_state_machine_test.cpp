// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>

using graphscore::ArbitrationCandidate;
using graphscore::assemble_vertical_candidates;
using graphscore::BoundaryOutcome;
using graphscore::BoundaryResolution;
using graphscore::ConnectorDestination;
using graphscore::ConnectorId;
using graphscore::ConnectorType;
using graphscore::EventId;
using graphscore::EventStateMachine;
using graphscore::KeySignature;
using graphscore::Measure;
using graphscore::Node;
using graphscore::NodeId;
using graphscore::NodeTimeline;
using graphscore::Project;
using graphscore::ProjectId;
using graphscore::QueuePolicy;
using graphscore::Rational;
using graphscore::resolve_vertical_match;
using graphscore::select_winner;
using graphscore::SequentialTransitionResult;
using graphscore::SequentialTransitionTier;
using graphscore::TimelineRegion;
using graphscore::TimeSignature;
using graphscore::TransportInstant;

namespace {

Node make_node() {
  return Node(NodeId::generate(), "Node");
}

// A destination raw enough to satisfy OutputConnector::destination().
// has_value() for eligibility purposes; these tests never traverse it.
ConnectorDestination any_destination() {
  return ConnectorDestination{NodeId::generate(), ConnectorId::generate()};
}

Project make_project() {
  return Project(ProjectId::generate(), "Project");
}

NodeTimeline make_timeline(std::uint8_t  numerator   = 4,
                           std::uint16_t denominator = 4) {
  std::vector<Measure> measures = {
      Measure{*TimeSignature::create(numerator, denominator),
              *KeySignature::create(0)}};
  return *NodeTimeline::create(std::move(measures), {});
}

// A valid TransportInstant wrapping `value`, for tests that only care about
// the guard's comparison mechanics rather than construction failures.
TransportInstant at(std::int64_t value) {
  return *TransportInstant::create(value);
}

}  // namespace

// -- select_winner / resolve_vertical_match ---------------------------------

TEST(SelectWinnerTest, EmptyCandidatesYieldNoWinner) {
  EXPECT_FALSE(select_winner({}).has_value());
}

TEST(SelectWinnerTest, HigherPriorityWinsRegardlessOfOrdinalOrOrder) {
  const ConnectorId low  = ConnectorId::generate();
  const ConnectorId high = ConnectorId::generate();

  const std::vector<ArbitrationCandidate> candidates = {
      ArbitrationCandidate{low, 0, /*priority=*/1, /*arrival_ordinal=*/100},
      ArbitrationCandidate{high, 1, /*priority=*/5, /*arrival_ordinal=*/1},
  };

  EXPECT_EQ(select_winner(candidates), high);
}

TEST(SelectWinnerTest, EqualPriorityNewestArrivalOrdinalWins) {
  const ConnectorId older = ConnectorId::generate();
  const ConnectorId newer = ConnectorId::generate();

  const std::vector<ArbitrationCandidate> candidates = {
      ArbitrationCandidate{older, 0, 3, 10},
      ArbitrationCandidate{newer, 1, 3, 20},
  };

  EXPECT_EQ(select_winner(candidates), newer);
}

TEST(SelectWinnerTest, EqualPriorityAndOrdinalStableConnectorOrderDecides) {
  const ConnectorId first_added  = ConnectorId::generate();
  const ConnectorId second_added = ConnectorId::generate();

  const std::vector<ArbitrationCandidate> candidates = {
      ArbitrationCandidate{second_added, 1, 4, 7},
      ArbitrationCandidate{first_added, 0, 4, 7},
  };

  EXPECT_EQ(select_winner(candidates), first_added);
}

TEST(SelectWinnerTest, ResultIsIndependentOfInputVectorOrdering) {
  const ConnectorId a = ConnectorId::generate();
  const ConnectorId b = ConnectorId::generate();
  const ConnectorId c = ConnectorId::generate();

  const std::vector<ArbitrationCandidate> forward = {
      ArbitrationCandidate{a, 0, 1, 5},
      ArbitrationCandidate{b, 1, 2, 5},
      ArbitrationCandidate{c, 2, 2, 9},
  };
  std::vector<ArbitrationCandidate> shuffled = {forward[2], forward[0],
                                                forward[1]};

  EXPECT_EQ(select_winner(forward), select_winner(shuffled));
  EXPECT_EQ(select_winner(forward), c);
}

TEST(ResolveVerticalMatchTest,
     SimultaneousMatchesUseExplicitPriorityThenStableConnectorOrder) {
  const ConnectorId low_priority_first  = ConnectorId::generate();
  const ConnectorId low_priority_second = ConnectorId::generate();
  const ConnectorId high_priority       = ConnectorId::generate();

  // All arrival_ordinal fields default to 0: truly simultaneous vertical
  // matches carry no meaningful recency, so ties never resolve on it.
  const std::vector<ArbitrationCandidate> candidates = {
      ArbitrationCandidate{low_priority_first, 0, 1},
      ArbitrationCandidate{high_priority, 1, 9},
      ArbitrationCandidate{low_priority_second, 2, 1},
  };

  EXPECT_EQ(resolve_vertical_match(candidates), high_priority);

  const std::vector<ArbitrationCandidate> tied = {
      ArbitrationCandidate{low_priority_second, 2, 1},
      ArbitrationCandidate{low_priority_first, 0, 1},
  };
  EXPECT_EQ(resolve_vertical_match(tied), low_priority_first);
}

// -- EventStateMachine::record_sequential_occurrence ------------------------

TEST(EventStateMachineTest, RecordFailsWhenNodeHasNoListenerForEvent) {
  EventStateMachine state_machine;
  Node              node = make_node();

  EXPECT_FALSE(
      state_machine.record_sequential_occurrence(node, EventId::generate(), 0)
          .ok());
}

TEST(EventStateMachineTest, RecordFailsForAVerticallyBoundEvent) {
  EventStateMachine state_machine;
  Node              node  = make_node();
  const auto        event = EventId::generate();
  const auto        out   = node.add_output("Out", ConnectorType::kVertical);
  ASSERT_TRUE(node.bind_output_event(out, event).ok());

  EXPECT_FALSE(state_machine.record_sequential_occurrence(node, event, 0).ok());
  EXPECT_EQ(state_machine.find_queue(node.id(), event), nullptr);
}

TEST(EventStateMachineTest, RecordFailsForNegativeSampleOffset) {
  EventStateMachine state_machine;
  Node              node  = make_node();
  const auto        event = EventId::generate();
  const auto        out   = node.add_output("Out", ConnectorType::kSequential);
  ASSERT_TRUE(node.bind_output_event(out, event).ok());

  EXPECT_FALSE(
      state_machine.record_sequential_occurrence(node, event, -1).ok());
}

TEST(EventStateMachineTest, RecordPersistsAnOccurrenceUntilConsumed) {
  EventStateMachine state_machine;
  Node              node  = make_node();
  const auto        event = EventId::generate();
  const auto        out   = node.add_output("Out", ConnectorType::kSequential);
  ASSERT_TRUE(node.bind_output_event(out, event).ok());

  ASSERT_TRUE(
      state_machine.record_sequential_occurrence(node, event, 128).ok());

  const auto* queue = state_machine.find_queue(node.id(), event);
  ASSERT_NE(queue, nullptr);
  ASSERT_TRUE(queue->peek().has_value());
  EXPECT_EQ(queue->peek()->sample_offset(), 128);
}

TEST(EventStateMachineTest, ArrivalOrdinalsAreStrictlyMonotonic) {
  EventStateMachine state_machine;
  Node              node   = make_node();
  const auto        event1 = EventId::generate();
  const auto        event2 = EventId::generate();
  const auto        out1 = node.add_output("Out1", ConnectorType::kSequential);
  const auto        out2 = node.add_output("Out2", ConnectorType::kSequential);
  ASSERT_TRUE(node.bind_output_event(out1, event1).ok());
  ASSERT_TRUE(node.bind_output_event(out2, event2).ok());
  ASSERT_TRUE(node.set_listener_policy(event1, QueuePolicy::kFifo, 4).ok());

  ASSERT_TRUE(state_machine.record_sequential_occurrence(node, event1, 0).ok());
  ASSERT_TRUE(state_machine.record_sequential_occurrence(node, event2, 0).ok());
  ASSERT_TRUE(state_machine.record_sequential_occurrence(node, event1, 0).ok());

  const auto* queue1 = state_machine.find_queue(node.id(), event1);
  const auto* queue2 = state_machine.find_queue(node.id(), event2);
  ASSERT_NE(queue1, nullptr);
  ASSERT_NE(queue2, nullptr);
  ASSERT_EQ(queue1->size(), 2u);

  EXPECT_LT(queue1->peek()->arrival_ordinal(),
            queue2->peek()->arrival_ordinal());
}

// -- EventStateMachine::resolve_sequential_boundary -------------------------

TEST(EventStateMachineTest, NodeWithNoSequentialOutputsStopsPlayback) {
  EventStateMachine state_machine;
  Node              node = make_node();
  (void)node.add_output("Vertical Only", ConnectorType::kVertical);

  const BoundaryResolution resolution =
      state_machine.resolve_sequential_boundary(node);
  EXPECT_EQ(resolution.outcome, BoundaryOutcome::kStopPlayback);
  EXPECT_FALSE(resolution.winner.has_value());
}

TEST(EventStateMachineTest, EmptyNodeAlsoStopsPlayback) {
  EventStateMachine state_machine;
  Node              node = make_node();

  EXPECT_EQ(state_machine.resolve_sequential_boundary(node).outcome,
            BoundaryOutcome::kStopPlayback);
}

TEST(EventStateMachineTest, DestinationlessSequentialOutputAloneStopsPlayback) {
  EventStateMachine state_machine;
  Node              node  = make_node();
  const auto        event = EventId::generate();
  const auto        out   = node.add_output("Out", ConnectorType::kSequential);
  ASSERT_TRUE(node.bind_output_event(out, event).ok());
  ASSERT_TRUE(state_machine.record_sequential_occurrence(node, event, 0).ok());

  // `out` has no destination -- it must not count toward "the node holds a
  // sequential output" even though it is event-bound with a pending
  // occurrence, so the boundary has literally nothing eligible to select
  // among and must stop playback rather than report kNoEventIntent.
  const BoundaryResolution resolution =
      state_machine.resolve_sequential_boundary(node);
  EXPECT_EQ(resolution.outcome, BoundaryOutcome::kStopPlayback);
  EXPECT_FALSE(resolution.winner.has_value());
}

TEST(EventStateMachineTest,
     DestinationlessEventBoundOutputCannotWinOrConsumeItsOccurrence) {
  EventStateMachine state_machine;
  Node              node  = make_node();
  const auto        event = EventId::generate();

  const auto manual_out = node.add_output("Manual", ConnectorType::kSequential);
  node.find_output(manual_out)->set_destination(any_destination());

  const auto dangling_out =
      node.add_output("Dangling", ConnectorType::kSequential);
  ASSERT_TRUE(node.bind_output_event(dangling_out, event).ok());

  ASSERT_TRUE(state_machine.record_sequential_occurrence(node, event, 0).ok());

  // `dangling_out` has a matching pending occurrence but no destination, so
  // it can never be followed and must never be offered as an arbitration
  // candidate. `manual_out` (the only eligible output) is unbound, so the
  // boundary correctly reports kNoEventIntent -- not kEventWinner naming an
  // unfollowable connector -- leaving the manual-queue/weighted-random
  // tiers (a later phase) to decide over Node::outputs() themselves.
  const BoundaryResolution resolution =
      state_machine.resolve_sequential_boundary(node);
  EXPECT_EQ(resolution.outcome, BoundaryOutcome::kNoEventIntent);
  EXPECT_FALSE(resolution.winner.has_value());

  // The event intent must survive untouched: it was never a valid winner,
  // so resolve_sequential_boundary() must not have consumed it.
  const auto* queue = state_machine.find_queue(node.id(), event);
  ASSERT_NE(queue, nullptr);
  EXPECT_FALSE(queue->empty());
}

TEST(EventStateMachineTest, UnboundSequentialOutputYieldsNoEventIntentNotStop) {
  EventStateMachine state_machine;
  Node              node = make_node();
  const auto manual = node.add_output("Manual", ConnectorType::kSequential);
  node.find_output(manual)->set_destination(any_destination());

  const BoundaryResolution resolution =
      state_machine.resolve_sequential_boundary(node);
  EXPECT_EQ(resolution.outcome, BoundaryOutcome::kNoEventIntent);
  EXPECT_FALSE(resolution.winner.has_value());
}

TEST(EventStateMachineTest,
     EventBoundOutputWithNoRecordedOccurrenceYieldsNoEventIntent) {
  EventStateMachine state_machine;
  Node              node  = make_node();
  const auto        event = EventId::generate();
  const auto        out   = node.add_output("Out", ConnectorType::kSequential);
  node.find_output(out)->set_destination(any_destination());
  ASSERT_TRUE(node.bind_output_event(out, event).ok());

  EXPECT_EQ(state_machine.resolve_sequential_boundary(node).outcome,
            BoundaryOutcome::kNoEventIntent);
}

TEST(EventStateMachineTest,
     FirstWinsResolvesToTheEarliestAndConsumesItEntirely) {
  EventStateMachine state_machine;
  Node              node  = make_node();
  const auto        event = EventId::generate();
  const auto        out   = node.add_output("Out", ConnectorType::kSequential);
  node.find_output(out)->set_destination(any_destination());
  ASSERT_TRUE(node.bind_output_event(out, event).ok());
  ASSERT_TRUE(node.set_listener_policy(event, QueuePolicy::kFirstWins, 1).ok());

  ASSERT_TRUE(state_machine.record_sequential_occurrence(node, event, 0).ok());
  ASSERT_TRUE(state_machine.record_sequential_occurrence(node, event, 10).ok());

  const BoundaryResolution resolution =
      state_machine.resolve_sequential_boundary(node);
  EXPECT_EQ(resolution.outcome, BoundaryOutcome::kEventWinner);
  EXPECT_EQ(resolution.winner, out);

  const auto* queue = state_machine.find_queue(node.id(), event);
  ASSERT_NE(queue, nullptr);
  EXPECT_TRUE(queue->empty());
}

TEST(EventStateMachineTest,
     LatestValidWinsIsTheDefaultAndConsumesTheSingleRemainingOccurrence) {
  EventStateMachine state_machine;
  Node              node  = make_node();
  const auto        event = EventId::generate();
  const auto        out   = node.add_output("Out", ConnectorType::kSequential);
  node.find_output(out)->set_destination(any_destination());
  ASSERT_TRUE(node.bind_output_event(out, event).ok());
  ASSERT_EQ(node.find_listener(event)->policy(), QueuePolicy::kLatestValidWins);

  ASSERT_TRUE(state_machine.record_sequential_occurrence(node, event, 0).ok());
  ASSERT_TRUE(state_machine.record_sequential_occurrence(node, event, 99).ok());

  const auto* queue = state_machine.find_queue(node.id(), event);
  ASSERT_NE(queue, nullptr);
  ASSERT_TRUE(queue->peek().has_value());
  EXPECT_EQ(queue->peek()->sample_offset(), 99);

  const BoundaryResolution resolution =
      state_machine.resolve_sequential_boundary(node);
  EXPECT_EQ(resolution.outcome, BoundaryOutcome::kEventWinner);
  EXPECT_EQ(resolution.winner, out);
  EXPECT_TRUE(queue->empty());
}

TEST(EventStateMachineTest,
     FifoConsumesOnlyTheEarliestAcrossSuccessiveBoundaries) {
  EventStateMachine state_machine;
  Node              node  = make_node();
  const auto        event = EventId::generate();
  const auto        out   = node.add_output("Out", ConnectorType::kSequential);
  node.find_output(out)->set_destination(any_destination());
  ASSERT_TRUE(node.bind_output_event(out, event).ok());
  ASSERT_TRUE(node.set_listener_policy(event, QueuePolicy::kFifo, 4).ok());

  ASSERT_TRUE(state_machine.record_sequential_occurrence(node, event, 1).ok());
  ASSERT_TRUE(state_machine.record_sequential_occurrence(node, event, 2).ok());
  ASSERT_TRUE(state_machine.record_sequential_occurrence(node, event, 3).ok());

  const auto* queue = state_machine.find_queue(node.id(), event);
  ASSERT_NE(queue, nullptr);

  ASSERT_EQ(state_machine.resolve_sequential_boundary(node).outcome,
            BoundaryOutcome::kEventWinner);
  EXPECT_EQ(queue->size(), 2u);

  ASSERT_EQ(state_machine.resolve_sequential_boundary(node).outcome,
            BoundaryOutcome::kEventWinner);
  EXPECT_EQ(queue->size(), 1u);
  ASSERT_TRUE(queue->peek().has_value());
  EXPECT_EQ(queue->peek()->sample_offset(), 3);
}

TEST(EventStateMachineTest, CrossEventArbitrationHigherConnectorPriorityWins) {
  EventStateMachine state_machine;
  Node              node       = make_node();
  const auto        low_event  = EventId::generate();
  const auto        high_event = EventId::generate();

  const auto low_out =
      node.add_output("Low Priority", ConnectorType::kSequential);
  const auto high_out =
      node.add_output("High Priority", ConnectorType::kSequential);
  node.find_output(low_out)->set_destination(any_destination());
  node.find_output(high_out)->set_destination(any_destination());
  ASSERT_TRUE(node.bind_output_event(low_out, low_event).ok());
  ASSERT_TRUE(node.bind_output_event(high_out, high_event).ok());
  node.find_output(low_out)->set_priority(1);
  node.find_output(high_out)->set_priority(5);

  ASSERT_TRUE(
      state_machine.record_sequential_occurrence(node, low_event, 0).ok());
  ASSERT_TRUE(
      state_machine.record_sequential_occurrence(node, high_event, 0).ok());

  const BoundaryResolution resolution =
      state_machine.resolve_sequential_boundary(node);
  EXPECT_EQ(resolution.outcome, BoundaryOutcome::kEventWinner);
  EXPECT_EQ(resolution.winner, high_out);

  // The winner's queue was consumed; the loser's occurrence was untouched
  // and remains queued to compete again at the next boundary.
  EXPECT_TRUE(state_machine.find_queue(node.id(), high_event)->empty());
  EXPECT_FALSE(state_machine.find_queue(node.id(), low_event)->empty());
}

TEST(EventStateMachineTest, LosingCandidateRemainsQueuedForTheNextBoundary) {
  EventStateMachine state_machine;
  Node              node       = make_node();
  const auto        low_event  = EventId::generate();
  const auto        high_event = EventId::generate();

  const auto low_out =
      node.add_output("Low Priority", ConnectorType::kSequential);
  const auto high_out =
      node.add_output("High Priority", ConnectorType::kSequential);
  node.find_output(low_out)->set_destination(any_destination());
  node.find_output(high_out)->set_destination(any_destination());
  ASSERT_TRUE(node.bind_output_event(low_out, low_event).ok());
  ASSERT_TRUE(node.bind_output_event(high_out, high_event).ok());
  node.find_output(low_out)->set_priority(1);
  node.find_output(high_out)->set_priority(5);

  ASSERT_TRUE(
      state_machine.record_sequential_occurrence(node, low_event, 0).ok());
  ASSERT_TRUE(
      state_machine.record_sequential_occurrence(node, high_event, 0).ok());

  ASSERT_EQ(state_machine.resolve_sequential_boundary(node).winner, high_out);

  // No new high-priority occurrence arrives; the previously-queued low
  // event now wins the following boundary.
  const BoundaryResolution second =
      state_machine.resolve_sequential_boundary(node);
  EXPECT_EQ(second.outcome, BoundaryOutcome::kEventWinner);
  EXPECT_EQ(second.winner, low_out);
}

TEST(EventStateMachineTest,
     TwoConnectorsSharingOneEventBreakTiesByStableConnectorOrder) {
  EventStateMachine state_machine;
  Node              node  = make_node();
  const auto        event = EventId::generate();

  const auto first  = node.add_output("First", ConnectorType::kSequential);
  const auto second = node.add_output("Second", ConnectorType::kSequential);
  node.find_output(first)->set_destination(any_destination());
  node.find_output(second)->set_destination(any_destination());
  ASSERT_TRUE(node.bind_output_event(first, event).ok());
  ASSERT_TRUE(node.bind_output_event(second, event).ok());

  ASSERT_TRUE(state_machine.record_sequential_occurrence(node, event, 0).ok());

  // Equal priority (default 0) and the same shared occurrence: the earlier-
  // added connector (`first`) wins by stable connector order.
  EXPECT_EQ(state_machine.resolve_sequential_boundary(node).winner, first);
}

TEST(EventStateMachineTest,
     StableConnectorOrderSurvivesRemovalOfAnUnrelatedOutput) {
  EventStateMachine state_machine;
  Node              node  = make_node();
  const auto        event = EventId::generate();

  const auto decoy  = node.add_output("Decoy", ConnectorType::kSequential);
  const auto first  = node.add_output("First", ConnectorType::kSequential);
  const auto second = node.add_output("Second", ConnectorType::kSequential);
  node.find_output(first)->set_destination(any_destination());
  node.find_output(second)->set_destination(any_destination());
  ASSERT_TRUE(node.bind_output_event(first, event).ok());
  ASSERT_TRUE(node.bind_output_event(second, event).ok());

  // Removing the unrelated, unbound decoy shifts First/Second's absolute
  // outputs() indices down by one but must not change their relative
  // order, since they both still exist.
  ASSERT_TRUE(node.remove_output(decoy).ok());

  ASSERT_TRUE(state_machine.record_sequential_occurrence(node, event, 0).ok());
  EXPECT_EQ(state_machine.resolve_sequential_boundary(node).winner, first);
}

TEST(EventStateMachineTest,
     ArrivalOrdinalDecidesBetweenTwoDifferentEventsAtEqualPriority) {
  EventStateMachine state_machine;
  Node              node         = make_node();
  const auto        first_event  = EventId::generate();
  const auto        second_event = EventId::generate();

  // Added in this order, so stable connector order alone would favor
  // `first_out`; both share the default priority (0), so only the arrival
  // ordinal wired through from each event's own occurrence can correctly
  // pick `second_out` instead.
  const auto first_out  = node.add_output("First", ConnectorType::kSequential);
  const auto second_out = node.add_output("Second", ConnectorType::kSequential);
  node.find_output(first_out)->set_destination(any_destination());
  node.find_output(second_out)->set_destination(any_destination());
  ASSERT_TRUE(node.bind_output_event(first_out, first_event).ok());
  ASSERT_TRUE(node.bind_output_event(second_out, second_event).ok());

  ASSERT_TRUE(
      state_machine.record_sequential_occurrence(node, first_event, 0).ok());
  ASSERT_TRUE(
      state_machine.record_sequential_occurrence(node, second_event, 0).ok());

  // If resolve_sequential_boundary() passed a literal 0 for arrival_ordinal
  // instead of each occurrence's own value, this would fall through to
  // stable connector order and `first_out` would win instead -- the
  // opposite of what a correctly wired newest-occurrence tier produces.
  const BoundaryResolution resolution =
      state_machine.resolve_sequential_boundary(node);
  EXPECT_EQ(resolution.outcome, BoundaryOutcome::kEventWinner);
  EXPECT_EQ(resolution.winner, second_out);
}

// -- Transport clearing: pause preserves; stop/reset/node-play clear -------

TEST(EventStateMachineTest,
     PauseIsANoOpAndArrivalOrdinalContinuityIsPreservedAcrossResume) {
  EventStateMachine state_machine;
  Node              node          = make_node();
  Node              other_node    = make_node();
  const auto        paused_event  = EventId::generate();
  const auto        resumed_event = EventId::generate();
  const auto        other_event   = EventId::generate();

  const auto paused_out = node.add_output("Paused", ConnectorType::kSequential);
  const auto resumed_out =
      node.add_output("Resumed", ConnectorType::kSequential);
  node.find_output(paused_out)->set_destination(any_destination());
  node.find_output(resumed_out)->set_destination(any_destination());
  ASSERT_TRUE(node.bind_output_event(paused_out, paused_event).ok());
  ASSERT_TRUE(node.bind_output_event(resumed_out, resumed_event).ok());

  const auto other_out =
      other_node.add_output("Other", ConnectorType::kSequential);
  other_node.find_output(other_out)->set_destination(any_destination());
  ASSERT_TRUE(other_node.bind_output_event(other_out, other_event).ok());

  ASSERT_TRUE(
      state_machine.record_sequential_occurrence(node, paused_event, 0).ok());
  ASSERT_TRUE(
      state_machine.record_sequential_occurrence(other_node, other_event, 0)
          .ok());

  // Resolving the unrelated other node's boundary consumes only its own
  // queue; `node`'s paused_event occurrence must survive, still queued.
  ASSERT_EQ(state_machine.resolve_sequential_boundary(other_node).outcome,
            BoundaryOutcome::kEventWinner);

  // "Pausing" transport calls nothing on EventStateMachine at all -- that
  // absence is the entire implementation of pause semantics.
  const auto* paused_queue = state_machine.find_queue(node.id(), paused_event);
  ASSERT_NE(paused_queue, nullptr);
  ASSERT_TRUE(paused_queue->peek().has_value());

  // Recording a fresh occurrence "after resume" must continue the shared
  // arrival ordinal sequence rather than restart it: at equal priority,
  // the newer, just-resumed occurrence must still outrank the one
  // recorded before the pause, exactly as it would have had the pause
  // never happened. If pause had silently reset the ordinal counter (or
  // any other state), `paused_out` (added first, so it would win by
  // stable connector order on a tie) would win this boundary instead.
  ASSERT_TRUE(
      state_machine.record_sequential_occurrence(node, resumed_event, 0).ok());

  const BoundaryResolution resolution =
      state_machine.resolve_sequential_boundary(node);
  EXPECT_EQ(resolution.outcome, BoundaryOutcome::kEventWinner);
  EXPECT_EQ(resolution.winner, resumed_out);
}

TEST(EventStateMachineTest, StopClearsAllPendingOccurrencesProjectWide) {
  EventStateMachine state_machine;
  Node              a     = make_node();
  Node              b     = make_node();
  const auto        event = EventId::generate();
  const auto        a_out = a.add_output("Out", ConnectorType::kSequential);
  const auto        b_out = b.add_output("Out", ConnectorType::kSequential);
  ASSERT_TRUE(a.bind_output_event(a_out, event).ok());
  ASSERT_TRUE(b.bind_output_event(b_out, event).ok());
  ASSERT_TRUE(state_machine.record_sequential_occurrence(a, event, 0).ok());
  ASSERT_TRUE(state_machine.record_sequential_occurrence(b, event, 0).ok());

  state_machine.clear_all();  // Transport "stop".

  EXPECT_EQ(state_machine.find_queue(a.id(), event)->size(), 0u);
  EXPECT_EQ(state_machine.find_queue(b.id(), event)->size(), 0u);
}

TEST(EventStateMachineTest, ResetClearsAllPendingOccurrencesProjectWide) {
  EventStateMachine state_machine;
  Node              node  = make_node();
  const auto        event = EventId::generate();
  const auto        out   = node.add_output("Out", ConnectorType::kSequential);
  ASSERT_TRUE(node.bind_output_event(out, event).ok());
  ASSERT_TRUE(state_machine.record_sequential_occurrence(node, event, 0).ok());

  state_machine.clear_all();  // Transport "reset".

  EXPECT_TRUE(state_machine.find_queue(node.id(), event)->empty());
}

TEST(EventStateMachineTest, NodePlayClearsAllPendingOccurrencesProjectWide) {
  EventStateMachine state_machine;
  Node              node  = make_node();
  const auto        event = EventId::generate();
  const auto        out   = node.add_output("Out", ConnectorType::kSequential);
  ASSERT_TRUE(node.bind_output_event(out, event).ok());
  ASSERT_TRUE(state_machine.record_sequential_occurrence(node, event, 0).ok());

  // Activating a node's play button "clears tails/queues" project-wide and
  // starts fresh from that node.
  state_machine.clear_all();

  EXPECT_TRUE(state_machine.find_queue(node.id(), event)->empty());
}

TEST(EventStateMachineTest, ClearNodeOnlyClearsThatNodesQueues) {
  EventStateMachine state_machine;
  Node              a     = make_node();
  Node              b     = make_node();
  const auto        event = EventId::generate();
  const auto        a_out = a.add_output("Out", ConnectorType::kSequential);
  const auto        b_out = b.add_output("Out", ConnectorType::kSequential);
  ASSERT_TRUE(a.bind_output_event(a_out, event).ok());
  ASSERT_TRUE(b.bind_output_event(b_out, event).ok());
  ASSERT_TRUE(state_machine.record_sequential_occurrence(a, event, 0).ok());
  ASSERT_TRUE(state_machine.record_sequential_occurrence(b, event, 0).ok());

  state_machine.clear_node(a.id());

  EXPECT_TRUE(state_machine.find_queue(a.id(), event)->empty());
  EXPECT_FALSE(state_machine.find_queue(b.id(), event)->empty());
}

TEST(EventStateMachineTest,
     ClearEventPreventsAStaleOccurrenceFromResurfacingAfterUnbindAndRebind) {
  EventStateMachine state_machine;
  Node              node  = make_node();
  const auto        event = EventId::generate();
  const auto        out   = node.add_output("Out", ConnectorType::kSequential);
  node.find_output(out)->set_destination(any_destination());
  ASSERT_TRUE(node.bind_output_event(out, event).ok());

  ASSERT_TRUE(state_machine.record_sequential_occurrence(node, event, 0).ok());

  // Unbinding the last output referencing `event` erases the node's
  // EventListener entirely (node.hpp). Per this class's documented
  // invariant, the caller performing that edit must clear_event() before
  // `event` can ever be rebound, or the pre-unbind occurrence recorded
  // above would still be sitting in this queue to resurface later.
  ASSERT_TRUE(node.bind_output_event(out, std::nullopt).ok());
  state_machine.clear_event(node.id(), event);

  // Rebinding creates a fresh EventListener with default policy/capacity,
  // with no memory of the occurrence recorded before the unbind.
  ASSERT_TRUE(node.bind_output_event(out, event).ok());

  const auto* queue = state_machine.find_queue(node.id(), event);
  ASSERT_NE(queue, nullptr);
  EXPECT_TRUE(queue->empty());

  const BoundaryResolution resolution =
      state_machine.resolve_sequential_boundary(node);
  EXPECT_EQ(resolution.outcome, BoundaryOutcome::kNoEventIntent);
  EXPECT_FALSE(resolution.winner.has_value());
}

TEST(EventStateMachineTest,
     ClearAllDoesNotResetPerQueueDiagnosticDropCounters) {
  EventStateMachine state_machine;
  Node              node  = make_node();
  const auto        event = EventId::generate();
  const auto        out   = node.add_output("Out", ConnectorType::kSequential);
  ASSERT_TRUE(node.bind_output_event(out, event).ok());
  ASSERT_TRUE(node.set_listener_policy(event, QueuePolicy::kFifo, 1).ok());
  ASSERT_TRUE(state_machine.record_sequential_occurrence(node, event, 0).ok());
  ASSERT_TRUE(state_machine.record_sequential_occurrence(node, event, 1).ok());
  ASSERT_EQ(state_machine.find_queue(node.id(), event)->dropped_count(), 1u);

  state_machine.clear_all();

  EXPECT_EQ(state_machine.find_queue(node.id(), event)->dropped_count(), 1u);
}

// -- Determinism -------------------------------------------------------------

TEST(EventStateMachineTest,
     IdenticalArrivalSequenceProducesIdenticalArbitrationAcrossInstances) {
  auto run = [](EventStateMachine& state_machine, Node& node, EventId low,
                EventId high, ConnectorId low_out, ConnectorId high_out) {
    node.find_output(low_out)->set_priority(1);
    node.find_output(high_out)->set_priority(3);
    (void)state_machine.record_sequential_occurrence(node, low, 0);
    (void)state_machine.record_sequential_occurrence(node, high, 0);
    return state_machine.resolve_sequential_boundary(node);
  };

  const auto low_event  = EventId::generate();
  const auto high_event = EventId::generate();

  Node       node_a = make_node();
  const auto low_a  = node_a.add_output("Low", ConnectorType::kSequential);
  const auto high_a = node_a.add_output("High", ConnectorType::kSequential);
  node_a.find_output(low_a)->set_destination(any_destination());
  node_a.find_output(high_a)->set_destination(any_destination());
  ASSERT_TRUE(node_a.bind_output_event(low_a, low_event).ok());
  ASSERT_TRUE(node_a.bind_output_event(high_a, high_event).ok());
  EventStateMachine machine_a;

  Node       node_b = make_node();
  const auto low_b  = node_b.add_output("Low", ConnectorType::kSequential);
  const auto high_b = node_b.add_output("High", ConnectorType::kSequential);
  node_b.find_output(low_b)->set_destination(any_destination());
  node_b.find_output(high_b)->set_destination(any_destination());
  ASSERT_TRUE(node_b.bind_output_event(low_b, low_event).ok());
  ASSERT_TRUE(node_b.bind_output_event(high_b, high_event).ok());
  EventStateMachine machine_b;

  const BoundaryResolution result_a =
      run(machine_a, node_a, low_event, high_event, low_a, high_a);
  const BoundaryResolution result_b =
      run(machine_b, node_b, low_event, high_event, low_b, high_b);

  EXPECT_EQ(result_a.outcome, result_b.outcome);
  ASSERT_TRUE(result_a.winner.has_value());
  ASSERT_TRUE(result_b.winner.has_value());
  // Both winners are each node's own "High" output, constructed
  // independently but identically.
  EXPECT_EQ(result_a.winner, high_a);
  EXPECT_EQ(result_b.winner, high_b);
}

// -- Manual queue (tier 2) ----------------------------------------------

TEST(EventStateMachineTest, QueueManualTransitionRejectsANonActiveSource) {
  EventStateMachine state_machine;
  Node              node = make_node();
  const auto        out  = node.add_output("Out", ConnectorType::kSequential);
  node.find_output(out)->set_destination(any_destination());

  EXPECT_FALSE(
      state_machine.queue_manual_transition(NodeId::generate(), node, out)
          .ok());
  EXPECT_FALSE(state_machine.find_manual_queue(node.id()).has_value());
}

TEST(EventStateMachineTest, QueueManualTransitionRejectsAnUnknownConnector) {
  EventStateMachine state_machine;
  Node              node = make_node();

  EXPECT_FALSE(
      state_machine
          .queue_manual_transition(node.id(), node, ConnectorId::generate())
          .ok());
}

TEST(EventStateMachineTest, QueueManualTransitionRejectsAVerticalOutput) {
  EventStateMachine state_machine;
  Node              node = make_node();
  const auto        out  = node.add_output("Out", ConnectorType::kVertical);
  node.find_output(out)->set_destination(any_destination());

  EXPECT_FALSE(
      state_machine.queue_manual_transition(node.id(), node, out).ok());
}

TEST(EventStateMachineTest,
     QueueManualTransitionRejectsADestinationlessOutput) {
  EventStateMachine state_machine;
  Node              node = make_node();
  const auto        out  = node.add_output("Out", ConnectorType::kSequential);

  EXPECT_FALSE(
      state_machine.queue_manual_transition(node.id(), node, out).ok());
}

TEST(EventStateMachineTest, QueueManualTransitionSucceedsAndIsFindable) {
  EventStateMachine state_machine;
  Node              node = make_node();
  const auto        out  = node.add_output("Out", ConnectorType::kSequential);
  node.find_output(out)->set_destination(any_destination());

  ASSERT_TRUE(state_machine.queue_manual_transition(node.id(), node, out).ok());
  EXPECT_EQ(state_machine.find_manual_queue(node.id()), out);
}

TEST(EventStateMachineTest, QueueManualTransitionReplacesAPreviousEntry) {
  EventStateMachine state_machine;
  Node              node = make_node();
  const auto first       = node.add_output("First", ConnectorType::kSequential);
  const auto second = node.add_output("Second", ConnectorType::kSequential);
  node.find_output(first)->set_destination(any_destination());
  node.find_output(second)->set_destination(any_destination());

  ASSERT_TRUE(
      state_machine.queue_manual_transition(node.id(), node, first).ok());
  ASSERT_TRUE(
      state_machine.queue_manual_transition(node.id(), node, second).ok());

  EXPECT_EQ(state_machine.find_manual_queue(node.id()), second);
}

// -- resolve_sequential_transition: precedence ---------------------------

TEST(EventStateMachineTest, EventIntentBeatsManualQueueAndWeightedRandom) {
  EventStateMachine state_machine;
  Node              node  = make_node();
  const auto        event = EventId::generate();
  const auto event_out  = node.add_output("Event", ConnectorType::kSequential);
  const auto manual_out = node.add_output("Manual", ConnectorType::kSequential);
  node.find_output(event_out)->set_destination(any_destination());
  node.find_output(manual_out)->set_destination(any_destination());
  ASSERT_TRUE(node.bind_output_event(event_out, event).ok());

  ASSERT_TRUE(
      state_machine.queue_manual_transition(node.id(), node, manual_out).ok());
  ASSERT_TRUE(state_machine.record_sequential_occurrence(node, event, 0).ok());

  const SequentialTransitionResult result =
      state_machine.resolve_sequential_transition(node);
  EXPECT_EQ(result.winner, event_out);
  EXPECT_EQ(result.tier, SequentialTransitionTier::kEventIntent);

  // Crossing the boundary via tier 1 discards the now-moot manual queue.
  EXPECT_FALSE(state_machine.find_manual_queue(node.id()).has_value());
}

TEST(EventStateMachineTest, ManualQueueBeatsWeightedRandomWhenNoEventIntent) {
  EventStateMachine state_machine;
  Node              node = make_node();
  const auto manual_out = node.add_output("Manual", ConnectorType::kSequential);
  const auto other_out  = node.add_output("Other", ConnectorType::kSequential);
  node.find_output(manual_out)->set_destination(any_destination());
  node.find_output(other_out)->set_destination(any_destination());
  ASSERT_TRUE(node.find_output(manual_out)->set_weight(Rational(0)).ok());
  ASSERT_TRUE(node.find_output(other_out)->set_weight(Rational(1)).ok());

  ASSERT_TRUE(
      state_machine.queue_manual_transition(node.id(), node, manual_out).ok());

  const SequentialTransitionResult result =
      state_machine.resolve_sequential_transition(node);
  EXPECT_EQ(result.winner, manual_out);
  EXPECT_EQ(result.tier, SequentialTransitionTier::kManualQueue);
  EXPECT_FALSE(state_machine.find_manual_queue(node.id()).has_value());
}

TEST(EventStateMachineTest,
     WeightedRandomOnlyAppliesWhenNeitherEventIntentNorManualQueueApply) {
  EventStateMachine state_machine(42);
  Node              node = make_node();
  const auto        only = node.add_output("Only", ConnectorType::kSequential);
  node.find_output(only)->set_destination(any_destination());
  ASSERT_TRUE(node.find_output(only)->set_weight(Rational(1)).ok());

  const SequentialTransitionResult result =
      state_machine.resolve_sequential_transition(node);
  EXPECT_EQ(result.winner, only);
  EXPECT_EQ(result.tier, SequentialTransitionTier::kWeightedRandom);
}

TEST(EventStateMachineTest,
     AStaleManualQueueEntryFallsThroughToWeightedRandom) {
  EventStateMachine state_machine(42);
  Node              node = make_node();
  const auto stale       = node.add_output("Stale", ConnectorType::kSequential);
  const auto winner = node.add_output("Winner", ConnectorType::kSequential);
  node.find_output(stale)->set_destination(any_destination());
  node.find_output(winner)->set_destination(any_destination());
  ASSERT_TRUE(node.find_output(winner)->set_weight(Rational(1)).ok());

  ASSERT_TRUE(
      state_machine.queue_manual_transition(node.id(), node, stale).ok());

  // The queued connector is disconnected (loses eligibility) after being
  // queued, without ever crossing a boundary in between.
  node.find_output(stale)->set_destination(std::nullopt);

  const SequentialTransitionResult result =
      state_machine.resolve_sequential_transition(node);
  EXPECT_EQ(result.winner, winner);
  EXPECT_EQ(result.tier, SequentialTransitionTier::kWeightedRandom);
}

TEST(EventStateMachineTest,
     AStaleManualQueueEntryFromARemovedOutputFallsThroughToWeightedRandom) {
  EventStateMachine state_machine(42);
  Node              node = make_node();
  const auto stale       = node.add_output("Stale", ConnectorType::kSequential);
  const auto winner = node.add_output("Winner", ConnectorType::kSequential);
  node.find_output(stale)->set_destination(any_destination());
  node.find_output(winner)->set_destination(any_destination());
  ASSERT_TRUE(node.find_output(winner)->set_weight(Rational(1)).ok());

  ASSERT_TRUE(
      state_machine.queue_manual_transition(node.id(), node, stale).ok());

  // The queued connector is removed from the node entirely after being
  // queued, without ever crossing a boundary in between.
  ASSERT_TRUE(node.remove_output(stale).ok());

  const SequentialTransitionResult result =
      state_machine.resolve_sequential_transition(node);
  EXPECT_EQ(result.winner, winner);
  EXPECT_EQ(result.tier, SequentialTransitionTier::kWeightedRandom);
}

TEST(EventStateMachineTest,
     ARetypedStaleManualQueueEntryFallsThroughToWeightedRandom) {
  EventStateMachine state_machine(42);
  Node              node = make_node();
  const auto stale       = node.add_output("Stale", ConnectorType::kSequential);
  const auto winner = node.add_output("Winner", ConnectorType::kSequential);
  node.find_output(stale)->set_destination(any_destination());
  node.find_output(winner)->set_destination(any_destination());
  ASSERT_TRUE(node.find_output(winner)->set_weight(Rational(1)).ok());

  ASSERT_TRUE(
      state_machine.queue_manual_transition(node.id(), node, stale).ok());

  // The queued connector is retyped away from kSequential after being
  // queued, without ever crossing a boundary in between.
  ASSERT_TRUE(node.set_output_type(stale, ConnectorType::kVertical).ok());

  const SequentialTransitionResult result =
      state_machine.resolve_sequential_transition(node);
  EXPECT_EQ(result.winner, winner);
  EXPECT_EQ(result.tier, SequentialTransitionTier::kWeightedRandom);
}

TEST(EventStateMachineTest,
     StopsPlaybackWhenTier1FindsNoEligibleSequentialOutputAtAll) {
  EventStateMachine state_machine;
  Node              node = make_node();
  (void)node.add_output("Vertical Only", ConnectorType::kVertical);

  const SequentialTransitionResult result =
      state_machine.resolve_sequential_transition(node);
  EXPECT_FALSE(result.winner.has_value());
  EXPECT_FALSE(result.tier.has_value());
}

TEST(EventStateMachineTest,
     StopsPlaybackWhenTheWeightGroupTotalIsNotExactlyOne) {
  EventStateMachine state_machine;
  Node              node = make_node();
  const auto        out  = node.add_output("Out", ConnectorType::kSequential);
  node.find_output(out)->set_destination(any_destination());
  ASSERT_TRUE(node.find_output(out)->set_weight(*Rational::create(3, 5)).ok());
  // Sole candidate, sums to 3/5.

  const SequentialTransitionResult result =
      state_machine.resolve_sequential_transition(node);
  EXPECT_FALSE(result.winner.has_value());
  EXPECT_FALSE(result.tier.has_value());
}

TEST(EventStateMachineTest, StopsPlaybackWhenEveryEligibleOutputIsZeroWeight) {
  EventStateMachine state_machine;
  Node              node = make_node();
  const auto        a    = node.add_output("A", ConnectorType::kSequential);
  const auto        b    = node.add_output("B", ConnectorType::kSequential);
  node.find_output(a)->set_destination(any_destination());
  node.find_output(b)->set_destination(any_destination());
  ASSERT_TRUE(node.find_output(a)->set_weight(Rational(0)).ok());
  ASSERT_TRUE(node.find_output(b)->set_weight(Rational(0)).ok());

  const SequentialTransitionResult result =
      state_machine.resolve_sequential_transition(node);
  EXPECT_FALSE(result.winner.has_value());
  EXPECT_FALSE(result.tier.has_value());
}

TEST(EventStateMachineTest,
     ResolveSequentialTransitionDrawsTheDocumentedProportionOfWeightedPicks) {
  Node       node = make_node();
  const auto low  = node.add_output("Low", ConnectorType::kSequential);
  const auto high = node.add_output("High", ConnectorType::kSequential);
  node.find_output(low)->set_destination(any_destination());
  node.find_output(high)->set_destination(any_destination());
  ASSERT_TRUE(node.find_output(low)->set_weight(*Rational::create(1, 5)).ok());
  ASSERT_TRUE(node.find_output(high)->set_weight(*Rational::create(4, 5)).ok());

  // 1/5 + 4/5's common denominator is 5, so the roll bound is 5, not 100.
  // Golden rolls for DeterministicPrng(42).next_below(5), six times:
  // 3, 1, 3, 4, 0, 2 -- exactly one (the fifth) falls below low's bucket
  // width of 1.
  EventStateMachine state_machine(42);
  int               low_count  = 0;
  int               high_count = 0;
  for (int i = 0; i < 6; ++i) {
    const SequentialTransitionResult result =
        state_machine.resolve_sequential_transition(node);
    ASSERT_EQ(result.tier, SequentialTransitionTier::kWeightedRandom);
    if (result.winner == low)
      ++low_count;
    else if (result.winner == high)
      ++high_count;
  }
  EXPECT_EQ(low_count, 1);
  EXPECT_EQ(high_count, 5);
}

TEST(EventStateMachineTest, InvalidGroupResolutionsNeverAdvanceThePrngStream) {
  constexpr std::uint64_t kSeed = 42;
  Node                    node  = make_node();
  const auto low  = node.add_output("Low", ConnectorType::kSequential);
  const auto high = node.add_output("High", ConnectorType::kSequential);
  node.find_output(low)->set_destination(any_destination());
  node.find_output(high)->set_destination(any_destination());
  // Both default to weight Rational(1), summing to 2: kInvalidTotal, so
  // every resolution below reaches tier 3, finds the group invalid, and
  // stops playback without ever drawing from the PRNG
  // (weighted_selection.hpp's "the roll bound, and drawing exactly one
  // PRNG value per selection").

  EventStateMachine state_machine(kSeed);
  for (int i = 0; i < 5; ++i) {
    EXPECT_FALSE(
        state_machine.resolve_sequential_transition(node).winner.has_value());
  }

  // Now assign weights that make the group valid and resolve once: since
  // none of the calls above drew from the PRNG, this must draw exactly the
  // same first roll a fresh, identically seeded machine draws on its very
  // first resolution against the same group.
  ASSERT_TRUE(node.find_output(low)->set_weight(*Rational::create(1, 5)).ok());
  ASSERT_TRUE(node.find_output(high)->set_weight(*Rational::create(4, 5)).ok());

  const SequentialTransitionResult result =
      state_machine.resolve_sequential_transition(node);
  ASSERT_EQ(result.tier, SequentialTransitionTier::kWeightedRandom);

  EventStateMachine                fresh(kSeed);
  const SequentialTransitionResult fresh_result =
      fresh.resolve_sequential_transition(node);
  EXPECT_EQ(result.winner, fresh_result.winner);
}

// -- EventStateMachine::reset_random ---------------------------------------

TEST(EventStateMachineTest, ClearNodeAndClearAllDoNotReseedThePrng) {
  EventStateMachine state_machine(42);
  Node              node = make_node();
  const auto        low  = node.add_output("Low", ConnectorType::kSequential);
  const auto        high = node.add_output("High", ConnectorType::kSequential);
  node.find_output(low)->set_destination(any_destination());
  node.find_output(high)->set_destination(any_destination());
  ASSERT_TRUE(node.find_output(low)->set_weight(*Rational::create(1, 5)).ok());
  ASSERT_TRUE(node.find_output(high)->set_weight(*Rational::create(4, 5)).ok());

  const SequentialTransitionResult first =
      state_machine.resolve_sequential_transition(node);
  ASSERT_EQ(first.tier, SequentialTransitionTier::kWeightedRandom);

  // Neither primitive is documented to touch the PRNG stream -- only
  // transport-level queue/manual-queue/vertical-guard state.
  state_machine.clear_node(node.id());
  state_machine.clear_all();

  EventStateMachine                fresh(42);
  const SequentialTransitionResult fresh_first =
      fresh.resolve_sequential_transition(node);
  ASSERT_EQ(fresh_first.winner, first.winner);

  // If clear_node()/clear_all() had reseeded (or otherwise disturbed) the
  // stream, this second draw would diverge from the fresh machine's second
  // draw at the same seed.
  const SequentialTransitionResult second =
      state_machine.resolve_sequential_transition(node);
  const SequentialTransitionResult fresh_second =
      fresh.resolve_sequential_transition(node);
  EXPECT_EQ(second.winner, fresh_second.winner);
}

TEST(EventStateMachineTest,
     ResetRandomProducesAStreamBitIdenticalToAFreshInstance) {
  EventStateMachine state_machine(1);
  Node              node = make_node();
  const auto        low  = node.add_output("Low", ConnectorType::kSequential);
  const auto        high = node.add_output("High", ConnectorType::kSequential);
  node.find_output(low)->set_destination(any_destination());
  node.find_output(high)->set_destination(any_destination());
  ASSERT_TRUE(node.find_output(low)->set_weight(*Rational::create(1, 5)).ok());
  ASSERT_TRUE(node.find_output(high)->set_weight(*Rational::create(4, 5)).ok());

  // Draw a few rolls from the original seed, then reseed to a different
  // one: the sequence from here on must be bit-for-bit identical to a
  // freshly constructed machine seeded directly with that seed, regardless
  // of how many rolls this instance already drew before the call.
  for (int i = 0; i < 3; ++i) {
    (void)state_machine.resolve_sequential_transition(node);
  }
  state_machine.reset_random(42);

  EventStateMachine fresh(42);
  for (int i = 0; i < 6; ++i) {
    const SequentialTransitionResult result =
        state_machine.resolve_sequential_transition(node);
    const SequentialTransitionResult fresh_result =
        fresh.resolve_sequential_transition(node);
    EXPECT_EQ(result.winner, fresh_result.winner);
  }
}

// -- assemble_vertical_candidates -----------------------------------------

TEST(AssembleVerticalCandidatesTest, DestinationlessVerticalOutputIsExcluded) {
  Project    project   = make_project();
  const auto active_id = project.add_node("Active");
  Node*      active    = project.find_node(active_id);
  active->set_timeline(make_timeline());

  const auto event = EventId::generate();
  const auto out   = active->add_output("Out", ConnectorType::kVertical);
  ASSERT_TRUE(active->bind_output_event(out, event).ok());

  const auto candidates = assemble_vertical_candidates(
      project, *active, TimelineRegion::kMain, std::vector{event});
  EXPECT_TRUE(candidates.empty());
}

TEST(AssembleVerticalCandidatesTest, OnlyFiredEventsAreCandidates) {
  Project    project   = make_project();
  const auto active_id = project.add_node("Active");
  const auto dest_id   = project.add_node("Destination");
  Node*      active    = project.find_node(active_id);
  Node*      dest      = project.find_node(dest_id);
  active->set_timeline(make_timeline());
  dest->set_timeline(make_timeline());

  const auto event       = EventId::generate();
  const auto other_event = EventId::generate();
  const auto out         = active->add_output("Out", ConnectorType::kVertical);
  ASSERT_TRUE(active->bind_output_event(out, event).ok());
  const auto in = dest->add_input("In");
  active->find_output(out)->set_destination(ConnectorDestination{dest_id, in});

  EXPECT_TRUE(assemble_vertical_candidates(project, *active,
                                           TimelineRegion::kMain,
                                           std::vector{other_event})
                  .empty());

  const auto matched = assemble_vertical_candidates(
      project, *active, TimelineRegion::kMain, std::vector{event});
  ASSERT_EQ(matched.size(), 1u);
  EXPECT_EQ(matched.front().connector, out);
}

TEST(AssembleVerticalCandidatesTest, PickdownRegionYieldsNoCandidates) {
  Project    project   = make_project();
  const auto active_id = project.add_node("Active");
  const auto dest_id   = project.add_node("Destination");
  Node*      active    = project.find_node(active_id);
  Node*      dest      = project.find_node(dest_id);
  active->set_timeline(make_timeline());
  dest->set_timeline(make_timeline());

  const auto event = EventId::generate();
  const auto out   = active->add_output("Out", ConnectorType::kVertical);
  ASSERT_TRUE(active->bind_output_event(out, event).ok());
  const auto in = dest->add_input("In");
  active->find_output(out)->set_destination(ConnectorDestination{dest_id, in});

  EXPECT_TRUE(assemble_vertical_candidates(project, *active,
                                           TimelineRegion::kPickdown,
                                           std::vector{event})
                  .empty());
}

TEST(AssembleVerticalCandidatesTest, IncompatibleDestinationIsExcluded) {
  Project    project   = make_project();
  const auto active_id = project.add_node("Active");
  const auto dest_id   = project.add_node("Destination");
  Node*      active    = project.find_node(active_id);
  Node*      dest      = project.find_node(dest_id);
  active->set_timeline(make_timeline(4, 4));
  dest->set_timeline(make_timeline(3, 4));  // Incompatible time signature.

  const auto event = EventId::generate();
  const auto out   = active->add_output("Out", ConnectorType::kVertical);
  ASSERT_TRUE(active->bind_output_event(out, event).ok());
  const auto in = dest->add_input("In");
  active->find_output(out)->set_destination(ConnectorDestination{dest_id, in});

  EXPECT_TRUE(assemble_vertical_candidates(
                  project, *active, TimelineRegion::kMain, std::vector{event})
                  .empty());
}

TEST(AssembleVerticalCandidatesTest,
     DestinationNodeMissingFromProjectIsExcluded) {
  Project    project   = make_project();
  const auto active_id = project.add_node("Active");
  Node*      active    = project.find_node(active_id);
  active->set_timeline(make_timeline());

  const auto event = EventId::generate();
  const auto out   = active->add_output("Out", ConnectorType::kVertical);
  ASSERT_TRUE(active->bind_output_event(out, event).ok());
  active->find_output(out)->set_destination(any_destination());

  EXPECT_TRUE(assemble_vertical_candidates(
                  project, *active, TimelineRegion::kMain, std::vector{event})
                  .empty());
}

TEST(AssembleVerticalCandidatesTest,
     ActiveNodeWithNoTimelineYieldsNoCandidates) {
  Project    project   = make_project();
  const auto active_id = project.add_node("Active");
  Node*      active    = project.find_node(active_id);

  const auto event = EventId::generate();
  const auto out   = active->add_output("Out", ConnectorType::kVertical);
  ASSERT_TRUE(active->bind_output_event(out, event).ok());
  active->find_output(out)->set_destination(any_destination());

  EXPECT_TRUE(assemble_vertical_candidates(
                  project, *active, TimelineRegion::kMain, std::vector{event})
                  .empty());
}

TEST(AssembleVerticalCandidatesTest, SequentialOutputsAreNeverCandidates) {
  Project    project   = make_project();
  const auto active_id = project.add_node("Active");
  const auto dest_id   = project.add_node("Destination");
  Node*      active    = project.find_node(active_id);
  Node*      dest      = project.find_node(dest_id);
  active->set_timeline(make_timeline());
  dest->set_timeline(make_timeline());

  const auto event = EventId::generate();
  const auto out   = active->add_output("Out", ConnectorType::kSequential);
  ASSERT_TRUE(active->bind_output_event(out, event).ok());
  const auto in = dest->add_input("In");
  active->find_output(out)->set_destination(ConnectorDestination{dest_id, in});

  EXPECT_TRUE(assemble_vertical_candidates(
                  project, *active, TimelineRegion::kMain, std::vector{event})
                  .empty());
}

TEST(AssembleVerticalCandidatesTest,
     OnlyTheActiveNodesOwnOutputsAreEverConsidered) {
  Project    project   = make_project();
  const auto active_id = project.add_node("Active");
  const auto other_id  = project.add_node("Other");
  const auto dest_id   = project.add_node("Destination");
  Node*      active    = project.find_node(active_id);
  Node*      other     = project.find_node(other_id);
  Node*      dest      = project.find_node(dest_id);
  active->set_timeline(make_timeline());
  other->set_timeline(make_timeline());
  dest->set_timeline(make_timeline());

  const auto event = EventId::generate();
  const auto in    = dest->add_input("In");

  const auto other_out =
      other->add_output("Other Out", ConnectorType::kVertical);
  ASSERT_TRUE(other->bind_output_event(other_out, event).ok());
  other->find_output(other_out)->set_destination(
      ConnectorDestination{dest_id, in});

  // `active` itself has no vertical output bound to `event` at all -- only
  // `other` does -- so even though `other`'s connector matches every
  // eligibility rule, it must never appear when assembling candidates for
  // `active`.
  EXPECT_TRUE(assemble_vertical_candidates(
                  project, *active, TimelineRegion::kMain, std::vector{event})
                  .empty());
}

// -- TransportInstant -----------------------------------------------------

TEST(TransportInstantTest, RejectsANegativeValue) {
  EXPECT_FALSE(TransportInstant::create(-1).has_value());
}

TEST(TransportInstantTest, AcceptsZeroAndPositiveValues) {
  ASSERT_TRUE(TransportInstant::create(0).has_value());
  EXPECT_EQ(TransportInstant::create(0)->value(), 0);
  ASSERT_TRUE(TransportInstant::create(42).has_value());
  EXPECT_EQ(TransportInstant::create(42)->value(), 42);
}

// -- resolve_vertical_transition: one jump per transport instant ----------

TEST(EventStateMachineTest, FirstVerticalJumpAtATransportInstantSucceeds) {
  EventStateMachine                       state_machine;
  const ConnectorId                       winner     = ConnectorId::generate();
  const std::vector<ArbitrationCandidate> candidates = {
      ArbitrationCandidate{winner, 0, 0, 0},
  };

  EXPECT_EQ(state_machine.resolve_vertical_transition(at(100), candidates),
            winner);
}

TEST(EventStateMachineTest,
     SecondVerticalJumpAtTheSameTransportInstantIsBlocked) {
  EventStateMachine                       state_machine;
  const ConnectorId                       winner     = ConnectorId::generate();
  const std::vector<ArbitrationCandidate> candidates = {
      ArbitrationCandidate{winner, 0, 0, 0},
  };

  ASSERT_TRUE(state_machine.resolve_vertical_transition(at(100), candidates)
                  .has_value());

  // Same transport instant, even a completely different (also winning)
  // candidate set: same-instant chaining must never be allowed.
  const ConnectorId                       other = ConnectorId::generate();
  const std::vector<ArbitrationCandidate> other_candidates = {
      ArbitrationCandidate{other, 0, 0, 0},
  };
  EXPECT_FALSE(
      state_machine.resolve_vertical_transition(at(100), other_candidates)
          .has_value());
}

// Review HIGH-1(a): node A jumps vertically to node B, which lands at its
// own, generally *different*, locally mapped sample position -- but that is
// still the same real project-wide instant as A's jump. The guard must key
// on the *shared* TransportInstant the caller supplies for both
// evaluations, not on either node's own local sample coordinate, or this
// exact scenario (README: "same-sample events cannot chain through a
// vertical cycle") would go unblocked. Passing the identical
// TransportInstant for what conceptually are two different nodes'
// candidate sets is exactly how a caller represents that.
TEST(EventStateMachineTest,
     SameTransportInstantBlocksChainingThroughADifferentNodesCandidates) {
  EventStateMachine                       state_machine;
  const ConnectorId                       from_node_a = ConnectorId::generate();
  const std::vector<ArbitrationCandidate> node_a_candidates = {
      ArbitrationCandidate{from_node_a, 0, 0, 0},
  };
  const ConnectorId                       from_node_b = ConnectorId::generate();
  const std::vector<ArbitrationCandidate> node_b_candidates = {
      ArbitrationCandidate{from_node_b, 0, 0, 0},
  };

  const TransportInstant shared_instant = at(500);
  ASSERT_EQ(state_machine.resolve_vertical_transition(shared_instant,
                                                      node_a_candidates),
            from_node_a);
  EXPECT_FALSE(
      state_machine
          .resolve_vertical_transition(shared_instant, node_b_candidates)
          .has_value());
}

// Review HIGH-1(b): a node's own local sample counter reading, say, 0 must
// never be conflated with the TransportInstant that identified an earlier
// jump also numbered 0 -- a later, genuinely distinct real instant that a
// caller correctly represents with a *different* (larger) TransportInstant
// must not be spuriously suppressed just because some node's local counter
// happens to reuse an earlier numeric value.
TEST(EventStateMachineTest,
     ALaterTransportInstantIsNotSuppressedByAnEarlierReusedLocalOffsetValue) {
  EventStateMachine                       state_machine;
  const ConnectorId                       first  = ConnectorId::generate();
  const ConnectorId                       second = ConnectorId::generate();
  const std::vector<ArbitrationCandidate> first_candidates = {
      ArbitrationCandidate{first, 0, 0, 0},
  };
  const std::vector<ArbitrationCandidate> second_candidates = {
      ArbitrationCandidate{second, 0, 0, 0},
  };

  ASSERT_EQ(state_machine.resolve_vertical_transition(at(0), first_candidates),
            first);
  // A later node's own local sample offset happens to also read 0, but the
  // real transport instant has legitimately advanced to 1.
  EXPECT_EQ(state_machine.resolve_vertical_transition(at(1), second_candidates),
            second);
}

TEST(EventStateMachineTest, ADifferentTransportInstantAllowsAnotherJump) {
  EventStateMachine                       state_machine;
  const ConnectorId                       first  = ConnectorId::generate();
  const ConnectorId                       second = ConnectorId::generate();
  const std::vector<ArbitrationCandidate> first_candidates = {
      ArbitrationCandidate{first, 0, 0, 0},
  };
  const std::vector<ArbitrationCandidate> second_candidates = {
      ArbitrationCandidate{second, 0, 0, 0},
  };

  EXPECT_EQ(
      state_machine.resolve_vertical_transition(at(100), first_candidates),
      first);
  EXPECT_EQ(
      state_machine.resolve_vertical_transition(at(200), second_candidates),
      second);
}

// The guard compares <=, not ==: a caller regression that supplies an
// instant not strictly greater than the last recorded jump's -- even one
// smaller than it, which a value-based node-local mix-up could plausibly
// produce -- fails closed rather than silently permitting an extra jump.
TEST(EventStateMachineTest,
     AnInstantNoGreaterThanTheLastRecordedJumpIsAlsoBlocked) {
  EventStateMachine                       state_machine;
  const ConnectorId                       winner     = ConnectorId::generate();
  const std::vector<ArbitrationCandidate> candidates = {
      ArbitrationCandidate{winner, 0, 0, 0},
  };

  ASSERT_TRUE(state_machine.resolve_vertical_transition(at(500), candidates)
                  .has_value());

  EXPECT_FALSE(state_machine.resolve_vertical_transition(at(300), candidates)
                   .has_value());
}

TEST(EventStateMachineTest, EmptyCandidatesNeverConsumeTheOneJumpGuard) {
  EventStateMachine                       state_machine;
  const ConnectorId                       winner     = ConnectorId::generate();
  const std::vector<ArbitrationCandidate> candidates = {
      ArbitrationCandidate{winner, 0, 0, 0},
  };

  EXPECT_FALSE(
      state_machine.resolve_vertical_transition(at(100), {}).has_value());
  // No jump actually happened above, so the same transport instant can
  // still produce one now.
  EXPECT_EQ(state_machine.resolve_vertical_transition(at(100), candidates),
            winner);
}

TEST(EventStateMachineTest, ClearAllResetsTheOneJumpPerTransportInstantGuard) {
  EventStateMachine                       state_machine;
  const ConnectorId                       winner     = ConnectorId::generate();
  const std::vector<ArbitrationCandidate> candidates = {
      ArbitrationCandidate{winner, 0, 0, 0},
  };

  ASSERT_TRUE(state_machine.resolve_vertical_transition(at(100), candidates)
                  .has_value());
  state_machine.clear_all();

  EXPECT_EQ(state_machine.resolve_vertical_transition(at(100), candidates),
            winner);
}

// -- clear_node / clear_all also own the manual queue --------------------

TEST(EventStateMachineTest, ClearNodeAlsoClearsThatNodesManualQueueEntry) {
  EventStateMachine state_machine;
  Node              node = make_node();
  const auto        out  = node.add_output("Out", ConnectorType::kSequential);
  node.find_output(out)->set_destination(any_destination());
  ASSERT_TRUE(state_machine.queue_manual_transition(node.id(), node, out).ok());

  state_machine.clear_node(node.id());

  EXPECT_FALSE(state_machine.find_manual_queue(node.id()).has_value());
}

TEST(EventStateMachineTest, ClearAllAlsoClearsEveryManualQueueEntry) {
  EventStateMachine state_machine;
  Node              node = make_node();
  const auto        out  = node.add_output("Out", ConnectorType::kSequential);
  node.find_output(out)->set_destination(any_destination());
  ASSERT_TRUE(state_machine.queue_manual_transition(node.id(), node, out).ok());

  state_machine.clear_all();

  EXPECT_FALSE(state_machine.find_manual_queue(node.id()).has_value());
}

TEST(EventStateMachineTest, ClearNodeDoesNotTouchTheVerticalJumpGuard) {
  EventStateMachine                       state_machine;
  Node                                    node       = make_node();
  const ConnectorId                       winner     = ConnectorId::generate();
  const std::vector<ArbitrationCandidate> candidates = {
      ArbitrationCandidate{winner, 0, 0, 0},
  };
  ASSERT_TRUE(state_machine.resolve_vertical_transition(at(100), candidates)
                  .has_value());

  // clear_node() is a per-node primitive; the vertical-jump guard is
  // project-wide, transport-level state that only clear_all() owns.
  state_machine.clear_node(node.id());

  EXPECT_FALSE(state_machine.resolve_vertical_transition(at(100), candidates)
                   .has_value());
}
