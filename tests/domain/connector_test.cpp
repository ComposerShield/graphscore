// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstddef>
#include <optional>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>

using graphscore::ConnectorId;
using graphscore::ConnectorType;
using graphscore::EventId;
using graphscore::EventListener;
using graphscore::InputConnector;
using graphscore::Node;
using graphscore::NodeId;
using graphscore::OutputConnector;
using graphscore::QueuePolicy;
using graphscore::Rational;

namespace {

Node make_node() {
  return Node(NodeId::generate(), "Node");
}

}  // namespace

TEST(ConnectorTest, ConnectorsHaveStableUniqueIds) {
  Node node = make_node();

  const auto input_a  = node.add_input("In A");
  const auto input_b  = node.add_input("In B");
  const auto output_a = node.add_output("Out A");

  EXPECT_NE(input_a, input_b);
  EXPECT_NE(input_a, output_a);

  const InputConnector* found = node.find_input(input_a);
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found->id(), input_a);
  EXPECT_EQ(found->name(), "In A");
}

TEST(ConnectorTest, ArbitraryNumberOfConnectorsPerNode) {
  Node node = make_node();

  constexpr int            kCount = 25;
  std::vector<ConnectorId> input_ids;
  std::vector<ConnectorId> output_ids;
  for (int i = 0; i < kCount; ++i) {
    input_ids.push_back(node.add_input("In"));
    output_ids.push_back(node.add_output("Out"));
  }

  EXPECT_EQ(node.inputs().size(), static_cast<std::size_t>(kCount));
  EXPECT_EQ(node.outputs().size(), static_cast<std::size_t>(kCount));

  for (std::size_t i = 0; i < input_ids.size(); ++i) {
    for (std::size_t j = i + 1; j < input_ids.size(); ++j) {
      EXPECT_NE(input_ids[i], input_ids[j]);
      EXPECT_NE(output_ids[i], output_ids[j]);
    }
  }
}

TEST(ConnectorTest, NewOutputHasNoDestinationAndIsExportEnabledByDefault) {
  Node        node   = make_node();
  const auto  out_id = node.add_output("Out");
  const auto* output = node.find_output(out_id);
  ASSERT_NE(output, nullptr);

  EXPECT_FALSE(output->destination().has_value());
  EXPECT_TRUE(output->export_enabled());
  EXPECT_TRUE(output->route().is_automatic());
}

TEST(ConnectorTest, SequentialAndVerticalFieldsAreIndependentlySettable) {
  Node       node   = make_node();
  const auto out_id = node.add_output("Out", ConnectorType::kSequential);
  auto*      output = node.find_output(out_id);
  ASSERT_NE(output, nullptr);

  EXPECT_EQ(output->type(), ConnectorType::kSequential);
  // 5/2 (2.5) is an out-of-range weight (see OutputConnector::set_weight's
  // doc comment) but still permitted storage: only negative weights are
  // rejected outright.
  ASSERT_TRUE(output->set_weight(*Rational::create(5, 2)).ok());
  output->set_priority(3);
  EXPECT_EQ(output->weight(), *Rational::create(5, 2));
  EXPECT_EQ(output->priority(), 3);

  ASSERT_TRUE(node.set_output_type(out_id, ConnectorType::kVertical).ok());
  EXPECT_EQ(output->type(), ConnectorType::kVertical);
  // Retyping never discards the fields entered under the previous type.
  EXPECT_EQ(output->weight(), *Rational::create(5, 2));
  EXPECT_EQ(output->priority(), 3);
}

TEST(ConnectorTest, NewOutputDefaultsToWeightOfOne) {
  Node        node   = make_node();
  const auto  out_id = node.add_output("Out");
  const auto* output = node.find_output(out_id);
  ASSERT_NE(output, nullptr);

  EXPECT_EQ(output->weight(), Rational(1));
}

TEST(ConnectorTest, SetWeightRejectsNegativeAndLeavesStoredWeightUnchanged) {
  Node       node   = make_node();
  const auto out_id = node.add_output("Out");
  auto*      output = node.find_output(out_id);
  ASSERT_NE(output, nullptr);

  ASSERT_TRUE(output->set_weight(*Rational::create(1, 3)).ok());
  EXPECT_FALSE(output->set_weight(Rational(-1)).ok());
  EXPECT_EQ(output->weight(), *Rational::create(1, 3));
}

TEST(ConnectorTest, RemoveOutputFailsForUnknownId) {
  Node node = make_node();
  EXPECT_FALSE(node.remove_output(ConnectorId::generate()).ok());
  // Node::remove_input is private -- see Node::remove_input's doc comment
  // and GraphTest.RemoveInputFailsForUnknownNodeOrConnector, which covers
  // the same failure through the only public entry point, Graph::remove_input.
}

TEST(ConnectorTest, RemoveOutputRemovesItAndAnyListenerItSoleyHeld) {
  Node       node   = make_node();
  const auto out_id = node.add_output("Out", ConnectorType::kVertical);
  const auto event  = EventId::generate();

  ASSERT_TRUE(node.bind_output_event(out_id, event).ok());
  ASSERT_NE(node.find_listener(event), nullptr);

  ASSERT_TRUE(node.remove_output(out_id).ok());
  EXPECT_EQ(node.find_output(out_id), nullptr);
  EXPECT_EQ(node.find_listener(event), nullptr);
}

TEST(ConnectorTest, BindOutputEventRejectsVerticalSequentialClashOnSameNode) {
  Node       node  = make_node();
  const auto event = EventId::generate();
  const auto vertical =
      node.add_output("Vertical Out", ConnectorType::kVertical);
  ASSERT_TRUE(node.bind_output_event(vertical, event).ok());

  const auto sequential =
      node.add_output("Sequential Out", ConnectorType::kSequential);
  EXPECT_FALSE(node.bind_output_event(sequential, event).ok());

  // The existing vertical binding is unaffected by the rejected attempt.
  const auto* vertical_output = node.find_output(vertical);
  ASSERT_NE(vertical_output, nullptr);
  EXPECT_EQ(vertical_output->event_binding(), event);
  const auto* sequential_output = node.find_output(sequential);
  ASSERT_NE(sequential_output, nullptr);
  EXPECT_FALSE(sequential_output->event_binding().has_value());
}

TEST(ConnectorTest, TwoOutputsOfTheSameTypeCanShareOneEventListener) {
  Node       node  = make_node();
  const auto event = EventId::generate();

  const auto first  = node.add_output("First", ConnectorType::kVertical);
  const auto second = node.add_output("Second", ConnectorType::kVertical);
  ASSERT_TRUE(node.bind_output_event(first, event).ok());
  ASSERT_TRUE(node.bind_output_event(second, event).ok());

  const auto* listener = node.find_listener(event);
  ASSERT_NE(listener, nullptr);
  EXPECT_EQ(listener->bound_type(), ConnectorType::kVertical);

  // Configuring the policy once, after both outputs are bound, proves it
  // is one shared listener instance rather than one stored per connector.
  ASSERT_TRUE(node.set_listener_policy(event, QueuePolicy::kFifo, 3).ok());
  const auto* shared = node.find_listener(event);
  ASSERT_NE(shared, nullptr);
  EXPECT_EQ(shared->policy(), QueuePolicy::kFifo);
  EXPECT_EQ(shared->capacity(), 3u);
}

TEST(ConnectorTest, UnbindingOneOfTwoConnectorsKeepsTheListenerAlive) {
  Node       node  = make_node();
  const auto event = EventId::generate();

  const auto first  = node.add_output("First", ConnectorType::kVertical);
  const auto second = node.add_output("Second", ConnectorType::kVertical);
  ASSERT_TRUE(node.bind_output_event(first, event).ok());
  ASSERT_TRUE(node.bind_output_event(second, event).ok());

  ASSERT_TRUE(node.bind_output_event(first, std::nullopt).ok());

  const auto* listener = node.find_listener(event);
  ASSERT_NE(listener, nullptr);
  const auto* first_output = node.find_output(first);
  ASSERT_NE(first_output, nullptr);
  EXPECT_FALSE(first_output->event_binding().has_value());
  const auto* second_output = node.find_output(second);
  ASSERT_NE(second_output, nullptr);
  EXPECT_EQ(second_output->event_binding(), event);
}

TEST(ConnectorTest, RebindingAnOutputMovesItToTheNewEventsListener) {
  Node       node = make_node();
  const auto e1   = EventId::generate();
  const auto e2   = EventId::generate();
  const auto out  = node.add_output("Out", ConnectorType::kVertical);

  ASSERT_TRUE(node.bind_output_event(out, e1).ok());
  ASSERT_NE(node.find_listener(e1), nullptr);

  ASSERT_TRUE(node.bind_output_event(out, e2).ok());
  EXPECT_EQ(node.find_listener(e1), nullptr);
  ASSERT_NE(node.find_listener(e2), nullptr);
  const auto* output = node.find_output(out);
  ASSERT_NE(output, nullptr);
  EXPECT_EQ(output->event_binding(), e2);
}

TEST(ConnectorTest, UnbindRebindRoundTripLosesListenerPolicyConfiguration) {
  Node       node  = make_node();
  const auto event = EventId::generate();
  const auto out   = node.add_output("Out", ConnectorType::kVertical);

  ASSERT_TRUE(node.bind_output_event(out, event).ok());
  ASSERT_TRUE(node.set_listener_policy(event, QueuePolicy::kFifo, 8).ok());

  ASSERT_TRUE(node.bind_output_event(out, std::nullopt).ok());
  ASSERT_TRUE(node.bind_output_event(out, event).ok());

  // Documented destructive behavior (Node::bind_output_event, node.hpp):
  // the round trip resets the listener to defaults with no diagnostic.
  const auto* listener = node.find_listener(event);
  ASSERT_NE(listener, nullptr);
  EXPECT_EQ(listener->policy(), QueuePolicy::kLatestValidWins);
  EXPECT_EQ(listener->capacity(), 1u);
}

TEST(ConnectorTest, ListenerPolicyDefaultsToLatestValidWinsAndIsSettable) {
  Node       node  = make_node();
  const auto event = EventId::generate();
  const auto out   = node.add_output("Out", ConnectorType::kSequential);
  ASSERT_TRUE(node.bind_output_event(out, event).ok());

  const auto* listener = node.find_listener(event);
  ASSERT_NE(listener, nullptr);
  EXPECT_EQ(listener->policy(), QueuePolicy::kLatestValidWins);

  ASSERT_TRUE(node.set_listener_policy(event, QueuePolicy::kFifo, 4).ok());
  const auto* updated = node.find_listener(event);
  ASSERT_NE(updated, nullptr);
  EXPECT_EQ(updated->policy(), QueuePolicy::kFifo);
  EXPECT_EQ(updated->capacity(), 4u);
}

TEST(ConnectorTest, SetListenerPolicyFailsWhenNoListenerExists) {
  Node node = make_node();
  EXPECT_FALSE(
      node.set_listener_policy(EventId::generate(), QueuePolicy::kFirstWins, 1)
          .ok());
}

TEST(ConnectorTest, UnbindingLastConnectorRemovesTheListener) {
  Node       node  = make_node();
  const auto event = EventId::generate();
  const auto out   = node.add_output("Out", ConnectorType::kVertical);
  ASSERT_TRUE(node.bind_output_event(out, event).ok());
  ASSERT_NE(node.find_listener(event), nullptr);

  ASSERT_TRUE(node.bind_output_event(out, std::nullopt).ok());
  EXPECT_EQ(node.find_listener(event), nullptr);
}

TEST(ConnectorTest, SetListenerPolicyRejectsZeroCapacityForFifo) {
  Node       node  = make_node();
  const auto event = EventId::generate();
  const auto out   = node.add_output("Out", ConnectorType::kSequential);
  ASSERT_TRUE(node.bind_output_event(out, event).ok());

  EXPECT_FALSE(node.set_listener_policy(event, QueuePolicy::kFifo, 0).ok());
  // The rejected call leaves the existing policy/capacity unchanged.
  const auto* listener = node.find_listener(event);
  ASSERT_NE(listener, nullptr);
  EXPECT_EQ(listener->policy(), QueuePolicy::kLatestValidWins);
  EXPECT_EQ(listener->capacity(), 1u);
}

TEST(ConnectorTest, SetOutputTypeRejectsClashWithOppositeTypeOnSameEvent) {
  Node       node  = make_node();
  const auto event = EventId::generate();

  const auto sequential =
      node.add_output("Sequential Out", ConnectorType::kSequential);
  const auto other =
      node.add_output("Other Sequential Out", ConnectorType::kSequential);
  ASSERT_TRUE(node.bind_output_event(sequential, event).ok());
  ASSERT_TRUE(node.bind_output_event(other, event).ok());

  // Retyping `other` to kVertical would leave a kSequential (sequential)
  // and a kVertical (other) output both bound to `event`: rejected.
  EXPECT_FALSE(node.set_output_type(other, ConnectorType::kVertical).ok());
  const auto* other_output = node.find_output(other);
  ASSERT_NE(other_output, nullptr);
  EXPECT_EQ(other_output->type(), ConnectorType::kSequential);
  const auto* listener = node.find_listener(event);
  ASSERT_NE(listener, nullptr);
  EXPECT_EQ(listener->bound_type(), ConnectorType::kSequential);
}

TEST(ConnectorTest, SetOutputTypeUpdatesListenerBoundTypeOnSuccess) {
  Node       node  = make_node();
  const auto event = EventId::generate();
  const auto out   = node.add_output("Out", ConnectorType::kSequential);
  ASSERT_TRUE(node.bind_output_event(out, event).ok());
  ASSERT_EQ(node.find_listener(event)->bound_type(),
            ConnectorType::kSequential);

  ASSERT_TRUE(node.set_output_type(out, ConnectorType::kVertical).ok());

  const auto* output = node.find_output(out);
  ASSERT_NE(output, nullptr);
  EXPECT_EQ(output->type(), ConnectorType::kVertical);
  const auto* listener = node.find_listener(event);
  ASSERT_NE(listener, nullptr);
  EXPECT_EQ(listener->bound_type(), ConnectorType::kVertical);

  // A later bind of an opposite (kSequential) output to the same event is
  // now correctly rejected against the up-to-date bound_type().
  const auto second = node.add_output("Second", ConnectorType::kSequential);
  EXPECT_FALSE(node.bind_output_event(second, event).ok());
}

TEST(ConnectorTest, SetOutputTypeFailsForUnknownId) {
  Node node = make_node();
  EXPECT_FALSE(
      node.set_output_type(ConnectorId::generate(), ConnectorType::kVertical)
          .ok());
}

TEST(ConnectorTest, SetOutputTypeIsANoOpWhenTypeIsUnchanged) {
  Node       node  = make_node();
  const auto event = EventId::generate();
  const auto out   = node.add_output("Out", ConnectorType::kSequential);
  ASSERT_TRUE(node.bind_output_event(out, event).ok());

  EXPECT_TRUE(node.set_output_type(out, ConnectorType::kSequential).ok());
  EXPECT_EQ(node.find_listener(event)->bound_type(),
            ConnectorType::kSequential);
}

// =========================================================================
// Phase 8d-i — Node::restore_input / Node::restore_output
// =========================================================================

TEST(ConnectorTest, RestoreInputRejectsDuplicateIdWithNoMutation) {
  Node        node   = make_node();
  const auto  in_id  = node.add_input("In");
  const auto* before = node.find_input(in_id);
  ASSERT_NE(before, nullptr);
  const InputConnector snapshot = *before;

  EXPECT_FALSE(node.restore_input(snapshot).ok());
  EXPECT_EQ(node.inputs().size(), 1u);
  EXPECT_EQ(node.find_input(in_id)->name(), "In");
}

TEST(ConnectorTest, RestoreOutputRejectsDuplicateIdWithNoMutation) {
  Node        node   = make_node();
  const auto  out_id = node.add_output("Out");
  const auto* before = node.find_output(out_id);
  ASSERT_NE(before, nullptr);
  const OutputConnector snapshot = *before;

  EXPECT_FALSE(node.restore_output(snapshot, std::nullopt).ok());
  EXPECT_EQ(node.outputs().size(), 1u);
  EXPECT_EQ(node.find_output(out_id)->name(), "Out");
}

TEST(ConnectorTest, RestoreOutputRecreatesDestroyedListenerExactly) {
  Node       node   = make_node();
  const auto event  = EventId::generate();
  const auto out_id = node.add_output("Out", ConnectorType::kVertical);
  ASSERT_TRUE(node.bind_output_event(out_id, event).ok());
  ASSERT_TRUE(node.set_listener_policy(event, QueuePolicy::kFifo, 5).ok());

  const OutputConnector removed  = *node.find_output(out_id);
  const EventListener   listener = *node.find_listener(event);

  ASSERT_TRUE(node.remove_output(out_id).ok());
  ASSERT_EQ(node.find_listener(event), nullptr);

  ASSERT_TRUE(node.restore_output(removed, listener).ok());
  const auto* restored = node.find_output(out_id);
  ASSERT_NE(restored, nullptr);
  EXPECT_EQ(restored->event_binding(), event);

  const auto* restored_listener = node.find_listener(event);
  ASSERT_NE(restored_listener, nullptr);
  EXPECT_EQ(restored_listener->policy(), QueuePolicy::kFifo);
  EXPECT_EQ(restored_listener->capacity(), 5u);
  EXPECT_EQ(restored_listener->bound_type(), ConnectorType::kVertical);
}

TEST(ConnectorTest, RestoreOutputLeavesSurvivingListenerUntouched) {
  Node       node   = make_node();
  const auto event  = EventId::generate();
  const auto first  = node.add_output("First", ConnectorType::kVertical);
  const auto second = node.add_output("Second", ConnectorType::kVertical);
  ASSERT_TRUE(node.bind_output_event(first, event).ok());
  ASSERT_TRUE(node.bind_output_event(second, event).ok());
  ASSERT_TRUE(node.set_listener_policy(event, QueuePolicy::kFifo, 3).ok());

  const OutputConnector removed = *node.find_output(first);
  ASSERT_TRUE(node.remove_output(first).ok());

  const auto* surviving = node.find_listener(event);
  ASSERT_NE(surviving, nullptr);
  EXPECT_EQ(surviving->policy(), QueuePolicy::kFifo);
  EXPECT_EQ(surviving->capacity(), 3u);

  ASSERT_TRUE(node.restore_output(removed, std::nullopt).ok());
  const auto* after_restore = node.find_listener(event);
  ASSERT_NE(after_restore, nullptr);
  EXPECT_EQ(after_restore->policy(), QueuePolicy::kFifo);
  EXPECT_EQ(after_restore->capacity(), 3u);
}
