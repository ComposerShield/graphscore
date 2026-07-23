// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>

#include <graphscore/domain/graphscore_domain.hpp>

using graphscore::MidiAttackId;
using graphscore::MidiChannel;
using graphscore::MidiNoteAttack;
using graphscore::MidiNoteReleaseOutcome;
using graphscore::MidiOwnerScope;
using graphscore::MidiOwnershipRelease;
using graphscore::MidiOwnershipTracker;
using graphscore::MidiPedalTransition;
using graphscore::MidiPitch;
using graphscore::NodeId;
using graphscore::NotationEntityId;
using graphscore::PickdownTailSnapshotId;

namespace {

MidiChannel channel(std::uint8_t value = 0) {
  return *MidiChannel::create(value);
}

MidiPitch pitch(std::uint8_t value = 60) {
  return *MidiPitch::create(value);
}

}  // namespace

// -- attack_note / release_note: same-pitch newest-owner retrigger ---------

TEST(MidiOwnershipTrackerTest, FirstAttackOnAnUnownedPitchSupersedesNothing) {
  MidiOwnershipTracker tracker;
  const MidiNoteAttack attack = tracker.attack_note(
      channel(), pitch(), MidiOwnerScope::main(NodeId::generate()));
  EXPECT_FALSE(attack.superseded.has_value());
  EXPECT_EQ(tracker.current_note_owner(channel(), pitch()), attack.attack_id);
}

TEST(MidiOwnershipTrackerTest,
     RetriggeringTheSamePitchSupersedesTheOlderAttackAndBecomesTheNewOwner) {
  MidiOwnershipTracker tracker;
  const NodeId         node = NodeId::generate();
  const MidiNoteAttack first =
      tracker.attack_note(channel(), pitch(), MidiOwnerScope::main(node));
  const MidiNoteAttack second =
      tracker.attack_note(channel(), pitch(), MidiOwnerScope::main(node));

  ASSERT_TRUE(second.superseded.has_value());
  EXPECT_EQ(*second.superseded, first.attack_id);
  EXPECT_NE(second.attack_id, first.attack_id);
  EXPECT_EQ(tracker.current_note_owner(channel(), pitch()), second.attack_id);
}

TEST(MidiOwnershipTrackerTest,
     OlderAttacksLaterReleaseIsSuppressedAndNewerOwnsTheEventualNoteOff) {
  MidiOwnershipTracker tracker;
  const NodeId         node = NodeId::generate();
  const MidiNoteAttack first =
      tracker.attack_note(channel(), pitch(), MidiOwnerScope::main(node));
  const MidiNoteAttack second =
      tracker.attack_note(channel(), pitch(), MidiOwnerScope::main(node));

  // The older logical note's own, originally notated end arrives later and
  // tries to release its attack; it must not cut the newer note off.
  EXPECT_EQ(tracker.release_note(first.attack_id),
            MidiNoteReleaseOutcome::kSuppressed);
  EXPECT_EQ(tracker.current_note_owner(channel(), pitch()), second.attack_id);

  // The newer attack's own natural end still emits the eventual note-off.
  EXPECT_EQ(tracker.release_note(second.attack_id),
            MidiNoteReleaseOutcome::kEmitsNoteOff);
  EXPECT_FALSE(tracker.current_note_owner(channel(), pitch()).has_value());
}

TEST(MidiOwnershipTrackerTest,
     ReleasingAnUnknownAttackIsIdempotentAndSuppressed) {
  MidiOwnershipTracker tracker;
  const MidiNoteAttack attack = tracker.attack_note(
      channel(), pitch(), MidiOwnerScope::main(NodeId::generate()));
  ASSERT_EQ(tracker.release_note(attack.attack_id),
            MidiNoteReleaseOutcome::kEmitsNoteOff);

  // Releasing the same attack twice must not resurrect ownership or crash.
  EXPECT_EQ(tracker.release_note(attack.attack_id),
            MidiNoteReleaseOutcome::kSuppressed);
}

TEST(MidiOwnershipTrackerTest,
     ReleasingAnAttackIdThisTrackerNeverIssuedIsIdempotentAndSuppressed) {
  MidiOwnershipTracker tracker;
  const MidiAttackId   foreign_attack_id = MidiAttackId::generate();
  EXPECT_EQ(tracker.release_note(foreign_attack_id),
            MidiNoteReleaseOutcome::kSuppressed);
}

TEST(MidiOwnershipTrackerTest,
     TripleRetriggerWithOutOfOrderReleasesOnlyTheCurrentOwnerEmitsNoteOff) {
  MidiOwnershipTracker tracker;
  const NodeId         node = NodeId::generate();
  const MidiNoteAttack first =
      tracker.attack_note(channel(), pitch(), MidiOwnerScope::main(node));
  const MidiNoteAttack second =
      tracker.attack_note(channel(), pitch(), MidiOwnerScope::main(node));
  const MidiNoteAttack third =
      tracker.attack_note(channel(), pitch(), MidiOwnerScope::main(node));

  ASSERT_TRUE(second.superseded.has_value());
  EXPECT_EQ(*second.superseded, first.attack_id);
  ASSERT_TRUE(third.superseded.has_value());
  EXPECT_EQ(*third.superseded, second.attack_id);
  EXPECT_EQ(tracker.current_note_owner(channel(), pitch()), third.attack_id);

  // Out-of-order releases: the two superseded attacks are suppressed
  // regardless of release order, and only the current owner emits.
  EXPECT_EQ(tracker.release_note(first.attack_id),
            MidiNoteReleaseOutcome::kSuppressed);
  EXPECT_EQ(tracker.release_note(second.attack_id),
            MidiNoteReleaseOutcome::kSuppressed);
  EXPECT_EQ(tracker.release_note(third.attack_id),
            MidiNoteReleaseOutcome::kEmitsNoteOff);
  EXPECT_FALSE(tracker.current_note_owner(channel(), pitch()).has_value());
}

TEST(MidiOwnershipTrackerTest, DifferentPitchesOnTheSameChannelNeverInteract) {
  MidiOwnershipTracker tracker;
  const NodeId         node = NodeId::generate();
  const MidiNoteAttack low =
      tracker.attack_note(channel(), pitch(60), MidiOwnerScope::main(node));
  const MidiNoteAttack high =
      tracker.attack_note(channel(), pitch(64), MidiOwnerScope::main(node));

  EXPECT_FALSE(low.superseded.has_value());
  EXPECT_FALSE(high.superseded.has_value());
  EXPECT_EQ(tracker.release_note(low.attack_id),
            MidiNoteReleaseOutcome::kEmitsNoteOff);
  EXPECT_EQ(tracker.current_note_owner(channel(), pitch(64)), high.attack_id);
}

TEST(MidiOwnershipTrackerTest,
     RetriggerAppliesUniformlyAcrossMainAndPickdownTailScopes) {
  MidiOwnershipTracker tracker;
  const NodeId         node     = NodeId::generate();
  const auto           snapshot = tracker.begin_pickdown_tail(node);

  const MidiNoteAttack main_attack =
      tracker.attack_note(channel(), pitch(), MidiOwnerScope::main(node));
  const MidiNoteAttack tail_attack = tracker.attack_note(
      channel(), pitch(), MidiOwnerScope::pickdown_tail(snapshot));

  ASSERT_TRUE(tail_attack.superseded.has_value());
  EXPECT_EQ(*tail_attack.superseded, main_attack.attack_id);
  EXPECT_EQ(tracker.current_note_owner(channel(), pitch()),
            tail_attack.attack_id);
}

// -- pedal_down / pedal_up: logical-OR CC64 ownership -----------------------

TEST(MidiOwnershipTrackerTest, FirstPedalDownOnAChannelBecomesActive) {
  MidiOwnershipTracker tracker;
  const auto           span = NotationEntityId::generate();
  EXPECT_EQ(tracker.pedal_down(channel(), span,
                               MidiOwnerScope::main(NodeId::generate())),
            MidiPedalTransition::kBecameActive);
  EXPECT_TRUE(tracker.pedal_active(channel()));
}

TEST(MidiOwnershipTrackerTest,
     OverlappingSpansCombineAsLogicalOrAndPedalUpOnlyAfterTheLastSpanReleases) {
  MidiOwnershipTracker tracker;
  const NodeId         node  = NodeId::generate();
  const auto           span1 = NotationEntityId::generate();
  const auto           span2 = NotationEntityId::generate();

  ASSERT_EQ(tracker.pedal_down(channel(), span1, MidiOwnerScope::main(node)),
            MidiPedalTransition::kBecameActive);
  // A second, overlapping span keeps the channel down but must not re-fire
  // a redundant CC64-down message.
  EXPECT_EQ(tracker.pedal_down(channel(), span2, MidiOwnerScope::main(node)),
            MidiPedalTransition::kNoChange);
  EXPECT_TRUE(tracker.pedal_active(channel()));

  // Releasing the first span alone must not emit CC64-up: span2 still holds
  // the channel down (the logical OR).
  EXPECT_EQ(tracker.pedal_up(channel(), span1), MidiPedalTransition::kNoChange);
  EXPECT_TRUE(tracker.pedal_active(channel()));

  // Only the last active span's release emits CC64-up.
  EXPECT_EQ(tracker.pedal_up(channel(), span2),
            MidiPedalTransition::kBecameInactive);
  EXPECT_FALSE(tracker.pedal_active(channel()));
}

TEST(MidiOwnershipTrackerTest, RedepressingTheIdenticalSpanNeverDoubleCounts) {
  MidiOwnershipTracker tracker;
  const NodeId         node = NodeId::generate();
  const auto           span = NotationEntityId::generate();

  ASSERT_EQ(tracker.pedal_down(channel(), span, MidiOwnerScope::main(node)),
            MidiPedalTransition::kBecameActive);
  EXPECT_EQ(tracker.pedal_down(channel(), span, MidiOwnerScope::main(node)),
            MidiPedalTransition::kNoChange);

  // A single pedal_up must be enough to release it: the duplicate
  // pedal_down never inflated the active count.
  EXPECT_EQ(tracker.pedal_up(channel(), span),
            MidiPedalTransition::kBecameInactive);
}

TEST(MidiOwnershipTrackerTest, ReleasingAnUnknownSpanIsIdempotentAndNoChange) {
  MidiOwnershipTracker tracker;
  EXPECT_EQ(tracker.pedal_up(channel(), NotationEntityId::generate()),
            MidiPedalTransition::kNoChange);
  EXPECT_FALSE(tracker.pedal_active(channel()));
}

TEST(MidiOwnershipTrackerTest, DifferentChannelsPedalIndependently) {
  MidiOwnershipTracker tracker;
  const NodeId         node = NodeId::generate();
  const auto           span = NotationEntityId::generate();
  ASSERT_EQ(tracker.pedal_down(channel(0), span, MidiOwnerScope::main(node)),
            MidiPedalTransition::kBecameActive);
  EXPECT_FALSE(tracker.pedal_active(channel(1)));
}

// -- transfer_main_to_pickdown_tail: boundary crossing ----------------------

TEST(MidiOwnershipTrackerTest,
     TransferredNoteSurvivesAVerticalJumpAgainstItsOwnSourceNode) {
  MidiOwnershipTracker tracker;
  const NodeId         source_node = NodeId::generate();
  const auto           snapshot    = tracker.begin_pickdown_tail(source_node);

  const MidiNoteAttack crossing = tracker.attack_note(
      channel(), pitch(), MidiOwnerScope::main(source_node));
  tracker.transfer_main_to_pickdown_tail(source_node, snapshot);

  // Simulates the pickdown-on-a-cycle scenario: source_node becomes active
  // again and fires a vertical jump against itself. The crossing note must
  // not be cut -- it is tail material now, not main material.
  const MidiOwnershipRelease release = tracker.vertical_jump(source_node);
  EXPECT_TRUE(release.notes.empty());
  EXPECT_EQ(tracker.current_note_owner(channel(), pitch()), crossing.attack_id);
}

TEST(MidiOwnershipTrackerTest,
     TransferredNoteIsReleasedByItsTailSnapshotsRetirement) {
  MidiOwnershipTracker tracker;
  const NodeId         source_node = NodeId::generate();
  const auto           snapshot    = tracker.begin_pickdown_tail(source_node);

  const MidiNoteAttack crossing = tracker.attack_note(
      channel(), pitch(), MidiOwnerScope::main(source_node));
  tracker.transfer_main_to_pickdown_tail(source_node, snapshot);

  const MidiOwnershipRelease release =
      tracker.retire_pickdown_tail_snapshot(snapshot);
  ASSERT_EQ(release.notes.size(), 1u);
  EXPECT_EQ(release.notes[0].attack_id, crossing.attack_id);
  EXPECT_FALSE(tracker.current_note_owner(channel(), pitch()).has_value());
}

TEST(MidiOwnershipTrackerTest,
     UntransferredNoteIsStillCutByAVerticalJumpOnItsSourceNode) {
  MidiOwnershipTracker tracker;
  const NodeId         source_node = NodeId::generate();
  (void)tracker.begin_pickdown_tail(source_node);

  const MidiNoteAttack note = tracker.attack_note(
      channel(), pitch(), MidiOwnerScope::main(source_node));
  // No transfer_main_to_pickdown_tail() call: this note never crossed the
  // boundary and remains ordinary main material.

  const MidiOwnershipRelease release = tracker.vertical_jump(source_node);
  ASSERT_EQ(release.notes.size(), 1u);
  EXPECT_EQ(release.notes[0].attack_id, note.attack_id);
  EXPECT_FALSE(tracker.current_note_owner(channel(), pitch()).has_value());
}

TEST(MidiOwnershipTrackerTest,
     TransferredPedalSpanSurvivesAVerticalJumpAgainstItsOwnSourceNode) {
  MidiOwnershipTracker tracker;
  const NodeId         source_node = NodeId::generate();
  const auto           snapshot    = tracker.begin_pickdown_tail(source_node);
  const auto           span        = NotationEntityId::generate();

  ASSERT_EQ(
      tracker.pedal_down(channel(0), span, MidiOwnerScope::main(source_node)),
      MidiPedalTransition::kBecameActive);
  tracker.transfer_main_to_pickdown_tail(source_node, snapshot);

  // The transfer itself moves no count: the channel is still down.
  EXPECT_TRUE(tracker.pedal_active(channel(0)));

  // Simulates the same pickdown-on-a-cycle scenario as the note test above,
  // but for a pedal span: the crossing span must survive a jump against its
  // own source_node -- it is tail material now.
  const MidiOwnershipRelease jump_release = tracker.vertical_jump(source_node);
  EXPECT_TRUE(jump_release.pedals.empty());
  EXPECT_TRUE(tracker.pedal_active(channel(0)));

  const MidiOwnershipRelease retirement =
      tracker.retire_pickdown_tail_snapshot(snapshot);
  ASSERT_EQ(retirement.pedals.size(), 1u);
  EXPECT_EQ(retirement.pedals[0].channel, channel(0));
  EXPECT_FALSE(tracker.pedal_active(channel(0)));
}

TEST(MidiOwnershipTrackerTest,
     TransferRewritesOnlyTheSourceNodesEntriesLeavingOthersUntouched) {
  MidiOwnershipTracker tracker;
  const NodeId         source_node = NodeId::generate();
  const NodeId         other_node  = NodeId::generate();
  const auto           snapshot    = tracker.begin_pickdown_tail(source_node);
  const auto           other_snapshot = tracker.begin_pickdown_tail(other_node);

  const MidiNoteAttack source_note = tracker.attack_note(
      channel(), pitch(60), MidiOwnerScope::main(source_node));
  const MidiNoteAttack other_main_note = tracker.attack_note(
      channel(), pitch(64), MidiOwnerScope::main(other_node));
  const MidiNoteAttack other_tail_note = tracker.attack_note(
      channel(), pitch(67), MidiOwnerScope::pickdown_tail(other_snapshot));

  tracker.transfer_main_to_pickdown_tail(source_node, snapshot);

  // other_node's own main note must still be released by a jump against
  // other_node, exactly as if the transfer above had never happened.
  const MidiOwnershipRelease other_jump = tracker.vertical_jump(other_node);
  ASSERT_EQ(other_jump.notes.size(), 1u);
  EXPECT_EQ(other_jump.notes[0].attack_id, other_main_note.attack_id);
  EXPECT_FALSE(tracker.current_note_owner(channel(), pitch(64)).has_value());

  // other_snapshot's own tail note must still be released by its own
  // retirement, untouched by the unrelated transfer above.
  const MidiOwnershipRelease other_retirement =
      tracker.retire_pickdown_tail_snapshot(other_snapshot);
  ASSERT_EQ(other_retirement.notes.size(), 1u);
  EXPECT_EQ(other_retirement.notes[0].attack_id, other_tail_note.attack_id);
  EXPECT_FALSE(tracker.current_note_owner(channel(), pitch(67)).has_value());

  // The transferred source-node entry was rewritten to tail scope by the
  // transfer, and released by neither release above.
  EXPECT_EQ(tracker.current_note_owner(channel(), pitch(60)),
            source_note.attack_id);
}

// -- Lifecycle: vertical jump releases main only, tails remain active ------

TEST(MidiOwnershipTrackerTest,
     VerticalJumpReleasesOnlySourceMainNotesAndLeavesConcurrentTailsActive) {
  MidiOwnershipTracker tracker;
  const NodeId         source_node = NodeId::generate();
  const NodeId         other_node  = NodeId::generate();
  const auto           snapshot    = tracker.begin_pickdown_tail(source_node);

  const MidiNoteAttack main_note = tracker.attack_note(
      channel(), pitch(60), MidiOwnerScope::main(source_node));
  const MidiNoteAttack tail_note = tracker.attack_note(
      channel(), pitch(64), MidiOwnerScope::pickdown_tail(snapshot));
  const MidiNoteAttack other_note = tracker.attack_note(
      channel(), pitch(67), MidiOwnerScope::main(other_node));

  const MidiOwnershipRelease release = tracker.vertical_jump(source_node);

  ASSERT_EQ(release.notes.size(), 1u);
  EXPECT_EQ(release.notes[0].attack_id, main_note.attack_id);
  EXPECT_EQ(release.notes[0].pitch, pitch(60));

  EXPECT_FALSE(tracker.current_note_owner(channel(), pitch(60)).has_value());
  EXPECT_EQ(tracker.current_note_owner(channel(), pitch(64)),
            tail_note.attack_id);
  EXPECT_EQ(tracker.current_note_owner(channel(), pitch(67)),
            other_note.attack_id);
}

TEST(MidiOwnershipTrackerTest,
     VerticalJumpAppliesTheSameMainOnlySplitToPedalSpans) {
  MidiOwnershipTracker tracker;
  const NodeId         source_node = NodeId::generate();
  const auto           snapshot    = tracker.begin_pickdown_tail(source_node);
  const auto           main_span   = NotationEntityId::generate();
  const auto           tail_span   = NotationEntityId::generate();

  ASSERT_EQ(tracker.pedal_down(channel(0), main_span,
                               MidiOwnerScope::main(source_node)),
            MidiPedalTransition::kBecameActive);
  ASSERT_EQ(tracker.pedal_down(channel(1), tail_span,
                               MidiOwnerScope::pickdown_tail(snapshot)),
            MidiPedalTransition::kBecameActive);

  const MidiOwnershipRelease release = tracker.vertical_jump(source_node);

  ASSERT_EQ(release.pedals.size(), 1u);
  EXPECT_EQ(release.pedals[0].channel, channel(0));
  EXPECT_FALSE(tracker.pedal_active(channel(0)));
  EXPECT_TRUE(tracker.pedal_active(channel(1)));
}

TEST(MidiOwnershipTrackerTest,
     MainAndTailSpansOnTheSameChannelLogicallyOrWithEachOtherAcrossRegions) {
  MidiOwnershipTracker tracker;
  const NodeId         source_node = NodeId::generate();
  const auto           snapshot    = tracker.begin_pickdown_tail(source_node);
  const auto           main_span   = NotationEntityId::generate();
  const auto           tail_span   = NotationEntityId::generate();

  ASSERT_EQ(tracker.pedal_down(channel(0), main_span,
                               MidiOwnerScope::main(source_node)),
            MidiPedalTransition::kBecameActive);
  // The tail span overlaps the SAME channel as the main span: the
  // cross-region logical OR must keep the channel down without a
  // redundant CC64-down.
  EXPECT_EQ(tracker.pedal_down(channel(0), tail_span,
                               MidiOwnerScope::pickdown_tail(snapshot)),
            MidiPedalTransition::kNoChange);

  const MidiOwnershipRelease release = tracker.vertical_jump(source_node);
  // The main span is released, but the tail span still holds channel 0
  // down, so no spurious CC64-up must be reported.
  EXPECT_TRUE(release.pedals.empty());
  EXPECT_TRUE(tracker.pedal_active(channel(0)));

  const MidiOwnershipRelease retirement =
      tracker.retire_pickdown_tail_snapshot(snapshot);
  ASSERT_EQ(retirement.pedals.size(), 1u);
  EXPECT_EQ(retirement.pedals[0].channel, channel(0));
  EXPECT_FALSE(tracker.pedal_active(channel(0)));
}

// -- Lifecycle: stop/reset/node-play clear everything, including tails -----

TEST(MidiOwnershipTrackerTest,
     ClearAllReleasesEveryNoteAndPedalAcrossRegionsAndNodes) {
  MidiOwnershipTracker tracker;
  const NodeId         node_a   = NodeId::generate();
  const NodeId         node_b   = NodeId::generate();
  const auto           snapshot = tracker.begin_pickdown_tail(node_a);

  (void)tracker.attack_note(channel(), pitch(60), MidiOwnerScope::main(node_a));
  (void)tracker.attack_note(channel(), pitch(64),
                            MidiOwnerScope::pickdown_tail(snapshot));
  (void)tracker.attack_note(channel(), pitch(67), MidiOwnerScope::main(node_b));
  ASSERT_EQ(tracker.pedal_down(channel(), NotationEntityId::generate(),
                               MidiOwnerScope::main(node_a)),
            MidiPedalTransition::kBecameActive);

  const MidiOwnershipRelease release = tracker.clear_all();

  EXPECT_EQ(release.notes.size(), 3u);
  EXPECT_EQ(release.pedals.size(), 1u);
  EXPECT_FALSE(tracker.current_note_owner(channel(), pitch(60)).has_value());
  EXPECT_FALSE(tracker.current_note_owner(channel(), pitch(64)).has_value());
  EXPECT_FALSE(tracker.current_note_owner(channel(), pitch(67)).has_value());
  EXPECT_FALSE(tracker.pedal_active(channel()));
}

TEST(MidiOwnershipTrackerTest, ClearAllForgetsEveryOpenSnapshotsSourceNode) {
  MidiOwnershipTracker tracker;
  const auto snapshot = tracker.begin_pickdown_tail(NodeId::generate());
  ASSERT_TRUE(tracker.pickdown_tail_source_node(snapshot).has_value());

  (void)tracker.clear_all();
  EXPECT_FALSE(tracker.pickdown_tail_source_node(snapshot).has_value());
}

TEST(MidiOwnershipTrackerTest,
     ClearAllWithTwoPedalSpansOnOneChannelReleasesOneChannel) {
  MidiOwnershipTracker tracker;
  const NodeId         node  = NodeId::generate();
  const auto           span1 = NotationEntityId::generate();
  const auto           span2 = NotationEntityId::generate();

  ASSERT_EQ(tracker.pedal_down(channel(), span1, MidiOwnerScope::main(node)),
            MidiPedalTransition::kBecameActive);
  ASSERT_EQ(tracker.pedal_down(channel(), span2, MidiOwnerScope::main(node)),
            MidiPedalTransition::kNoChange);

  const MidiOwnershipRelease release = tracker.clear_all();
  ASSERT_EQ(release.pedals.size(), 1u);
  EXPECT_EQ(release.pedals[0].channel, channel());
  EXPECT_FALSE(tracker.pedal_active(channel()));
}

TEST(MidiOwnershipTrackerTest, ClearAllAndPanicOnAnEmptyTrackerReleaseNothing) {
  MidiOwnershipTracker       tracker;
  const MidiOwnershipRelease cleared = tracker.clear_all();
  EXPECT_TRUE(cleared.notes.empty());
  EXPECT_TRUE(cleared.pedals.empty());

  const MidiOwnershipRelease panicked = tracker.panic();
  EXPECT_TRUE(panicked.notes.empty());
  EXPECT_TRUE(panicked.pedals.empty());
}

// -- MidiOwnershipRelease is sorted deterministically -----------------------

TEST(MidiOwnershipTrackerTest,
     ClearAllReturnsNotesSortedByChannelThenPitchRegardlessOfAttackOrder) {
  MidiOwnershipTracker tracker;
  const NodeId         node = NodeId::generate();

  // Attack in descending (channel, pitch) order -- the exact reverse of the
  // ascending order asserted below -- so a pass cannot be an accident of
  // insertion order happening to survive through the underlying hash map.
  (void)tracker.attack_note(channel(1), pitch(64), MidiOwnerScope::main(node));
  (void)tracker.attack_note(channel(1), pitch(60), MidiOwnerScope::main(node));
  (void)tracker.attack_note(channel(0), pitch(64), MidiOwnerScope::main(node));
  (void)tracker.attack_note(channel(0), pitch(60), MidiOwnerScope::main(node));

  const MidiOwnershipRelease release = tracker.clear_all();
  ASSERT_EQ(release.notes.size(), 4u);
  EXPECT_EQ(release.notes[0].channel, channel(0));
  EXPECT_EQ(release.notes[0].pitch, pitch(60));
  EXPECT_EQ(release.notes[1].channel, channel(0));
  EXPECT_EQ(release.notes[1].pitch, pitch(64));
  EXPECT_EQ(release.notes[2].channel, channel(1));
  EXPECT_EQ(release.notes[2].pitch, pitch(60));
  EXPECT_EQ(release.notes[3].channel, channel(1));
  EXPECT_EQ(release.notes[3].pitch, pitch(64));
}

// -- Lifecycle: panic is global, matching clear_all()'s ownership effect ---

TEST(MidiOwnershipTrackerTest,
     PanicReleasesEveryNoteAndPedalAcrossRegionsAndNodes) {
  MidiOwnershipTracker tracker;
  const NodeId         node     = NodeId::generate();
  const auto           snapshot = tracker.begin_pickdown_tail(node);

  (void)tracker.attack_note(channel(), pitch(60), MidiOwnerScope::main(node));
  (void)tracker.attack_note(channel(), pitch(64),
                            MidiOwnerScope::pickdown_tail(snapshot));

  const MidiOwnershipRelease release = tracker.panic();

  EXPECT_EQ(release.notes.size(), 2u);
  EXPECT_FALSE(tracker.current_note_owner(channel(), pitch(60)).has_value());
  EXPECT_FALSE(tracker.current_note_owner(channel(), pitch(64)).has_value());
}

// -- Lifecycle: pickdown-tail snapshot retirement ---------------------------

TEST(MidiOwnershipTrackerTest,
     RetiringASnapshotReleasesOnlyThatTailsOwnership) {
  MidiOwnershipTracker tracker;
  const NodeId         node        = NodeId::generate();
  const auto           first_tail  = tracker.begin_pickdown_tail(node);
  const auto           second_tail = tracker.begin_pickdown_tail(node);

  const MidiNoteAttack retiring_note = tracker.attack_note(
      channel(), pitch(60), MidiOwnerScope::pickdown_tail(first_tail));
  const MidiNoteAttack surviving_tail_note = tracker.attack_note(
      channel(), pitch(64), MidiOwnerScope::pickdown_tail(second_tail));
  const MidiNoteAttack surviving_main_note =
      tracker.attack_note(channel(), pitch(67), MidiOwnerScope::main(node));

  const MidiOwnershipRelease release =
      tracker.retire_pickdown_tail_snapshot(first_tail);

  ASSERT_EQ(release.notes.size(), 1u);
  EXPECT_EQ(release.notes[0].attack_id, retiring_note.attack_id);
  EXPECT_FALSE(tracker.current_note_owner(channel(), pitch(60)).has_value());
  EXPECT_EQ(tracker.current_note_owner(channel(), pitch(64)),
            surviving_tail_note.attack_id);
  EXPECT_EQ(tracker.current_note_owner(channel(), pitch(67)),
            surviving_main_note.attack_id);
  EXPECT_FALSE(tracker.pickdown_tail_source_node(first_tail).has_value());
}

TEST(MidiOwnershipTrackerTest,
     RetiringASnapshotWithNoRemainingOwnershipIsAnIdempotentNoOp) {
  MidiOwnershipTracker tracker;
  const auto snapshot = tracker.begin_pickdown_tail(NodeId::generate());

  const MidiOwnershipRelease release =
      tracker.retire_pickdown_tail_snapshot(snapshot);
  EXPECT_TRUE(release.notes.empty());
  EXPECT_TRUE(release.pedals.empty());

  // Retiring an already-retired (or never-known) snapshot is safe.
  const MidiOwnershipRelease second_release =
      tracker.retire_pickdown_tail_snapshot(snapshot);
  EXPECT_TRUE(second_release.notes.empty());
  EXPECT_TRUE(second_release.pedals.empty());
}

TEST(MidiOwnershipTrackerTest,
     BeginPickdownTailRecordsItsSourceNodeUntilRetired) {
  MidiOwnershipTracker tracker;
  const NodeId         node     = NodeId::generate();
  const auto           snapshot = tracker.begin_pickdown_tail(node);
  ASSERT_EQ(tracker.pickdown_tail_source_node(snapshot), node);

  (void)tracker.retire_pickdown_tail_snapshot(snapshot);
  EXPECT_FALSE(tracker.pickdown_tail_source_node(snapshot).has_value());
}

// -- Lifecycle: pause retains ownership -------------------------------------

TEST(MidiOwnershipTrackerTest, PauseIsANoOpAndOwnershipSurvivesAcrossIt) {
  MidiOwnershipTracker tracker;
  const NodeId         node = NodeId::generate();
  const MidiNoteAttack note =
      tracker.attack_note(channel(), pitch(), MidiOwnerScope::main(node));
  const auto span = NotationEntityId::generate();
  ASSERT_EQ(tracker.pedal_down(channel(), span, MidiOwnerScope::main(node)),
            MidiPedalTransition::kBecameActive);

  // "Pausing" transport calls nothing on MidiOwnershipTracker at all -- that
  // absence is the entire implementation of pause semantics, mirroring
  // EventStateMachine's own pause behavior (event_state_machine.hpp).
  EXPECT_EQ(tracker.current_note_owner(channel(), pitch()), note.attack_id);
  EXPECT_TRUE(tracker.pedal_active(channel()));

  // Ownership recorded before the "pause" resolves normally after it, with
  // no special resume step required.
  EXPECT_EQ(tracker.release_note(note.attack_id),
            MidiNoteReleaseOutcome::kEmitsNoteOff);
  EXPECT_EQ(tracker.pedal_up(channel(), span),
            MidiPedalTransition::kBecameInactive);
}
