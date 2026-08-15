// SPDX-License-Identifier: Apache-2.0

#include "command_test_support.hpp"

#include <gtest/gtest.h>

#include <bit>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>

// =========================================================================
// Phase 8b — noexcept static assertions for all 10 commands
// =========================================================================

TEST(CommandTest, NoexceptStaticAsserts8b) {
  static_assert(noexcept(std::declval<SetProjectNameCommand&>().execute(
      std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<SetProjectNameCommand&>().undo(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<SetProjectNameCommand&>().redo(std::declval<Project&>())));

  static_assert(noexcept(
      std::declval<SetStartNodeCommand&>().execute(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<SetStartNodeCommand&>().undo(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<SetStartNodeCommand&>().redo(std::declval<Project&>())));

  static_assert(noexcept(std::declval<SetProjectDynamicCommand&>().execute(
      std::declval<Project&>())));
  static_assert(noexcept(std::declval<SetProjectDynamicCommand&>().undo(
      std::declval<Project&>())));
  static_assert(noexcept(std::declval<SetProjectDynamicCommand&>().redo(
      std::declval<Project&>())));

  static_assert(noexcept(
      std::declval<SetTrackGainCommand&>().execute(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<SetTrackGainCommand&>().undo(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<SetTrackGainCommand&>().redo(std::declval<Project&>())));

  static_assert(noexcept(
      std::declval<SetTrackPanCommand&>().execute(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<SetTrackPanCommand&>().undo(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<SetTrackPanCommand&>().redo(std::declval<Project&>())));

  static_assert(noexcept(
      std::declval<SetTrackMuteCommand&>().execute(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<SetTrackMuteCommand&>().undo(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<SetTrackMuteCommand&>().redo(std::declval<Project&>())));

  static_assert(noexcept(
      std::declval<SetTrackSoloCommand&>().execute(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<SetTrackSoloCommand&>().undo(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<SetTrackSoloCommand&>().redo(std::declval<Project&>())));

  static_assert(noexcept(
      std::declval<SetNodeColorCommand&>().execute(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<SetNodeColorCommand&>().undo(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<SetNodeColorCommand&>().redo(std::declval<Project&>())));

  static_assert(noexcept(
      std::declval<SetNodeNotesCommand&>().execute(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<SetNodeNotesCommand&>().undo(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<SetNodeNotesCommand&>().redo(std::declval<Project&>())));

  static_assert(noexcept(std::declval<SetNodePositionCommand&>().execute(
      std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<SetNodePositionCommand&>().undo(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<SetNodePositionCommand&>().redo(std::declval<Project&>())));
}

// =========================================================================
// Phase 8b — Mixed 8a+8b CommandHistory full undo/redo and redo invalidation
// =========================================================================

TEST(CommandTest, MixedHistoryUndoRedo) {
  Project    project  = make_project();
  NodeId     node_id  = project.add_node("Node");
  const auto track_id = project.add_track("Track", StaffLayout::single_staff(),
                                          *MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());

  CommandHistory history;

  ASSERT_TRUE(history
                  .execute_new(std::make_unique<SetNodeColorCommand>(
                                   node_id, 0xFF0000FF),
                               project)
                  .ok());
  ASSERT_TRUE(
      history
          .execute_new(std::make_unique<SetTrackMuteCommand>(*track_id, true),
                       project)
          .ok());
  ASSERT_TRUE(history
                  .execute_new(std::make_unique<SetProjectNameCommand>("Mixed"),
                               project)
                  .ok());

  EXPECT_EQ(project.find_node(node_id)->color(), 0xFF0000FFu);
  EXPECT_TRUE(project.find_active_track(*track_id)->mix_settings().mute());
  EXPECT_EQ(project.name(), "Mixed");

  EXPECT_EQ(history.undo_stack_size(), 3u);

  ASSERT_TRUE(history.undo(project).ok());
  EXPECT_EQ(project.name(), "Test Project");

  ASSERT_TRUE(history.undo(project).ok());
  EXPECT_FALSE(project.find_active_track(*track_id)->mix_settings().mute());

  ASSERT_TRUE(history.undo(project).ok());
  EXPECT_EQ(project.find_node(node_id)->color(), 0xFFFFFFFF);

  EXPECT_EQ(history.undo_stack_size(), 0u);
  EXPECT_EQ(history.redo_stack_size(), 3u);

  // Redo in redo-stack order (color was undone last → redone first)
  ASSERT_TRUE(history.redo(project).ok());
  EXPECT_EQ(project.find_node(node_id)->color(), 0xFF0000FFu);

  ASSERT_TRUE(history.redo(project).ok());
  EXPECT_TRUE(project.find_active_track(*track_id)->mix_settings().mute());

  ASSERT_TRUE(history.redo(project).ok());
  EXPECT_EQ(project.name(), "Mixed");
}

TEST(CommandTest, MixedHistoryRedoInvalidatedByNewCommand) {
  Project    project  = make_project();
  NodeId     node_id  = project.add_node("Node");
  const auto track_id = project.add_track("Track", StaffLayout::single_staff(),
                                          *MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());

  CommandHistory history;

  ASSERT_TRUE(
      history
          .execute_new(std::make_unique<SetNodeNotesCommand>(node_id, "First"),
                       project)
          .ok());
  ASSERT_TRUE(history.undo(project).ok());
  EXPECT_EQ(history.redo_stack_size(), 1u);

  // New 8b command invalidates the redo stack
  ASSERT_TRUE(
      history
          .execute_new(std::make_unique<SetTrackGainCommand>(*track_id, 1.0F),
                       project)
          .ok());
  EXPECT_EQ(history.redo_stack_size(), 0u);
  EXPECT_EQ(history.undo_stack_size(), 1u);
}

// =========================================================================
// Phase 8b — CommandTransaction with 8b commands
// =========================================================================

TEST(CommandTest, TransactionWith8bCommandsRoundTrip) {
  Project    project  = make_project();
  NodeId     node_id  = project.add_node("Node");
  const auto track_id = project.add_track("Track", StaffLayout::single_staff(),
                                          *MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());

  auto tx = std::make_unique<CommandTransaction>();
  ASSERT_TRUE(tx->add_command(
                    std::make_unique<SetNodeColorCommand>(node_id, 0xAA00BB00))
                  .ok());
  ASSERT_TRUE(
      tx->add_command(std::make_unique<SetTrackPanCommand>(*track_id, -0.5F))
          .ok());
  ASSERT_TRUE(
      tx->add_command(std::make_unique<SetProjectDynamicCommand>(Dynamic::kPpp))
          .ok());

  ASSERT_TRUE(tx->execute(project).ok());
  EXPECT_EQ(project.find_node(node_id)->color(), 0xAA00BB00u);
  EXPECT_FLOAT_EQ(project.find_active_track(*track_id)->mix_settings().pan(),
                  -0.5F);
  EXPECT_EQ(project.default_dynamic(), Dynamic::kPpp);

  ASSERT_TRUE(tx->undo(project).ok());
  EXPECT_EQ(project.find_node(node_id)->color(), 0xFFFFFFFF);
  EXPECT_FLOAT_EQ(project.find_active_track(*track_id)->mix_settings().pan(),
                  0.0F);
  EXPECT_EQ(project.default_dynamic(), Dynamic::kMf);

  ASSERT_TRUE(tx->redo(project).ok());
  EXPECT_EQ(project.find_node(node_id)->color(), 0xAA00BB00u);
  EXPECT_FLOAT_EQ(project.find_active_track(*track_id)->mix_settings().pan(),
                  -0.5F);
  EXPECT_EQ(project.default_dynamic(), Dynamic::kPpp);
}

TEST(CommandTest, TransactionWith8bMiddleFailureRollsBack) {
  Project    project  = make_project();
  NodeId     node_id  = project.add_node("Node");
  const auto track_id = project.add_track("Track", StaffLayout::single_staff(),
                                          *MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());

  auto tx = std::make_unique<CommandTransaction>();
  ASSERT_TRUE(
      tx->add_command(std::make_unique<SetNodeNotesCommand>(node_id, "Before"))
          .ok());
  ASSERT_TRUE(tx->add_command(std::make_unique<SetTrackGainCommand>(
                                  TrackId::generate(), 0.5F))
                  .ok());
  ASSERT_TRUE(
      tx->add_command(std::make_unique<SetTrackMuteCommand>(*track_id, true))
          .ok());

  Result result = tx->execute(project);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.code(), ResultCode::kInvalidArgument);

  // Rollback: node notes restored, track mute never applied
  EXPECT_EQ(project.find_node(node_id)->notes(), "");
  EXPECT_FALSE(project.find_active_track(*track_id)->mix_settings().mute());
}

// =========================================================================
// Phase 8b — Deterministic replay across structurally equal Projects
// =========================================================================

TEST(CommandTest, DeterministicReplay8b) {
  // Build two structurally equal projects with stable IDs.
  Project proj_a = make_project();
  Project proj_b = make_project();
  proj_b.set_name(proj_a.name());

  // We need the same TrackId and NodeId in both. Rather than trying to
  // generate identical UUIDs, we add a node and track to proj_a, capture
  // their ids, then construct a deterministic command stream that modifies
  // project-level properties (name, dynamic, start node) — which don't
  // depend on per-project ids — plus commands that use ids resolved within
  // each project.

  // Establish identical start nodes by adding one to each.
  NodeId node_a = proj_a.add_node("N");
  NodeId node_b = proj_b.add_node("N");
  ASSERT_TRUE(proj_a.set_start_node(node_a).ok());
  ASSERT_TRUE(proj_b.set_start_node(node_b).ok());

  // Set project name to match
  proj_a.set_name("Base");
  proj_b.set_name("Base");

  // Apply the same command stream to both projects using freshly created
  // command objects (never reused stateful objects).
  {
    // Stream: set project name → "A", set dynamic → kF, set project name
    // → "B".
    auto c1 = std::make_unique<SetProjectNameCommand>("A");
    ASSERT_TRUE(c1->execute(proj_a).ok());
    auto c1b = std::make_unique<SetProjectNameCommand>("A");
    ASSERT_TRUE(c1b->execute(proj_b).ok());

    auto c2 = std::make_unique<SetProjectDynamicCommand>(Dynamic::kF);
    ASSERT_TRUE(c2->execute(proj_a).ok());
    auto c2b = std::make_unique<SetProjectDynamicCommand>(Dynamic::kF);
    ASSERT_TRUE(c2b->execute(proj_b).ok());

    auto c3 = std::make_unique<SetProjectNameCommand>("B");
    ASSERT_TRUE(c3->execute(proj_a).ok());
    auto c3b = std::make_unique<SetProjectNameCommand>("B");
    ASSERT_TRUE(c3b->execute(proj_b).ok());
  }

  EXPECT_EQ(proj_a.name(), proj_b.name());
  EXPECT_EQ(proj_a.default_dynamic(), proj_b.default_dynamic());
  EXPECT_TRUE(proj_a.start_node().has_value());
  EXPECT_TRUE(proj_b.start_node().has_value());
}

// =========================================================================
// Phase 8b — Unrelated fields remain unchanged
// =========================================================================

TEST(CommandTest, SetNodeColorUnrelatedFieldsUnchanged) {
  Project project = make_project();
  NodeId  node_id = project.add_node("OriginalName");
  auto*   node    = project.find_node(node_id);
  node->set_notes("Some notes");
  node->set_position(GraphPosition{1.0, 2.0});

  auto cmd = std::make_unique<SetNodeColorCommand>(node_id, 0x12345678);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(node->name(), "OriginalName");
  EXPECT_EQ(node->notes(), "Some notes");
  EXPECT_DOUBLE_EQ(node->position().x, 1.0);
  EXPECT_DOUBLE_EQ(node->position().y, 2.0);
}

TEST(CommandTest, SetTrackGainUnrelatedMixSettingsUnchanged) {
  Project    project  = make_project();
  const auto track_id = project.add_track("Track", StaffLayout::single_staff(),
                                          *MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());

  auto* track = project.find_active_track(*track_id);
  track->mix_settings().set_mute(true);
  track->mix_settings().set_solo(true);
  const bool mute_before = track->mix_settings().mute();
  const bool solo_before = track->mix_settings().solo();

  auto cmd = std::make_unique<SetTrackGainCommand>(*track_id, 0.3F);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(track->mix_settings().mute(), mute_before);
  EXPECT_EQ(track->mix_settings().solo(), solo_before);
}

TEST(CommandTest, SetProjectDynamicUnrelatedProjectFieldsUnchanged) {
  Project    project   = make_project();
  const auto old_name  = project.name();
  const auto old_tempo = project.default_tempo();

  auto cmd = std::make_unique<SetProjectDynamicCommand>(Dynamic::kFf);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(project.name(), old_name);
  EXPECT_EQ(project.default_tempo(), old_tempo);
}

// =========================================================================
// Finding 1: SetStartNodeCommand snapshot-invariant regression test
// =========================================================================

TEST(CommandTest, SetStartNodeFailedExecuteLeavesNoStaleSnapshot) {
  Project project = make_project();
  NodeId  node_id = project.add_node("Entry");
  ASSERT_TRUE(project.set_start_node(node_id).ok());

  // Execute with invalid target — must fail and leave model unchanged.
  auto cmd = std::make_unique<SetStartNodeCommand>(
      std::optional<NodeId>(NodeId::generate()));
  Result result = cmd->execute(project);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.code(), ResultCode::kInvalidArgument);
  ASSERT_TRUE(project.start_node().has_value());
  EXPECT_EQ(*project.start_node(), node_id);

  // Verify the failed command did not leak a stale snapshot: a subsequent
  // valid command must work correctly (no side effects from the failed one).
  NodeId node2 = project.add_node("Second");
  auto   cmd2 =
      std::make_unique<SetStartNodeCommand>(std::optional<NodeId>(node2));
  ASSERT_TRUE(cmd2->execute(project).ok());
  ASSERT_TRUE(project.start_node().has_value());
  EXPECT_EQ(*project.start_node(), node2);
}

// =========================================================================
// Phase 8b — Finding 2: bitwise IEEE round-trip for gain (already above as
// SetTrackGainBitwiseIeeeRoundTrip), pan (SetTrackPanBitwiseIeeeRoundTrip),
// and position (SetNodePositionBitwiseIeeeRoundTrip).
// =========================================================================

// =========================================================================
// Finding 3: DeterministicReplay8b — all ten 8b command types
// =========================================================================

TEST(CommandTest, DeterministicReplayAll8bCommands) {
  // Build one populated project.
  Project    proj = make_project();
  NodeId     n    = proj.add_node("N");
  const auto tid =
      proj.add_track("T", StaffLayout::single_staff(), *MidiChannel::create(0));
  ASSERT_TRUE(tid.has_value());
  ASSERT_TRUE(proj.set_start_node(n).ok());
  proj.set_name("Before");
  proj.set_default_dynamic(Dynamic::kF);

  // Set pre-state for node/track commands that will change them.
  proj.find_node(n)->set_color(0xAAAAAAAA);
  proj.find_node(n)->set_notes("Original notes");
  proj.find_node(n)->set_position(GraphPosition{10.0, 20.0});
  proj.find_active_track(*tid)->mix_settings().set_gain(0.5F);
  proj.find_active_track(*tid)->mix_settings().set_pan(-0.25F);
  proj.find_active_track(*tid)->mix_settings().set_mute(false);
  proj.find_active_track(*tid)->mix_settings().set_solo(false);

  // Copy: both copies have identical stable NodeId / TrackId.
  Project copy = proj;

  // Verify structural equality before commands.
  ASSERT_EQ(proj.id(), copy.id());
  ASSERT_EQ(proj.name(), copy.name());
  ASSERT_EQ(proj.start_node(), copy.start_node());
  ASSERT_EQ(proj.default_dynamic(), copy.default_dynamic());
  ASSERT_EQ(proj.active_tracks().size(), copy.active_tracks().size());
  ASSERT_EQ(proj.nodes().size(), copy.nodes().size());
  ASSERT_EQ(proj.nodes()[0].id(), copy.nodes()[0].id());
  ASSERT_EQ(proj.active_tracks()[0].id(), copy.active_tracks()[0].id());
  ASSERT_EQ(proj.nodes()[0].color(), copy.nodes()[0].color());
  ASSERT_EQ(proj.nodes()[0].notes(), copy.nodes()[0].notes());
  ASSERT_EQ(proj.find_node(n)->name(), copy.find_node(n)->name());

  // Helper: apply fresh command stream to both and compare every affected
  // field plus identity/order explicitly. All 10 command types exercised.
  {
    using Cmd = std::unique_ptr<Command>;

    auto apply = [&](Project& p, NodeId node, TrackId trk,
                     const GraphPosition& pos) {
      // 8b-1: SetProjectNameCommand
      Cmd c1 = std::make_unique<SetProjectNameCommand>("After");
      ASSERT_TRUE(c1->execute(p).ok());

      // 8b-2: SetProjectDynamicCommand
      Cmd c2 = std::make_unique<SetProjectDynamicCommand>(Dynamic::kPpp);
      ASSERT_TRUE(c2->execute(p).ok());

      // 8b-3: SetStartNodeCommand
      Cmd c3 =
          std::make_unique<SetStartNodeCommand>(std::optional<NodeId>(node));
      ASSERT_TRUE(c3->execute(p).ok());

      // 8b-4: SetNodeColorCommand
      Cmd c4 = std::make_unique<SetNodeColorCommand>(node, 0xDEADBEEF);
      ASSERT_TRUE(c4->execute(p).ok());

      // 8b-5: SetNodeNotesCommand
      Cmd c5 = std::make_unique<SetNodeNotesCommand>(node, "New notes");
      ASSERT_TRUE(c5->execute(p).ok());

      // 8b-6: SetNodePositionCommand
      Cmd c6 = std::make_unique<SetNodePositionCommand>(node, pos);
      ASSERT_TRUE(c6->execute(p).ok());

      // 8b-7: SetTrackGainCommand
      Cmd c7 = std::make_unique<SetTrackGainCommand>(trk, 0.75F);
      ASSERT_TRUE(c7->execute(p).ok());

      // 8b-8: SetTrackPanCommand
      Cmd c8 = std::make_unique<SetTrackPanCommand>(trk, 0.33F);
      ASSERT_TRUE(c8->execute(p).ok());

      // 8b-9: SetTrackMuteCommand
      Cmd c9 = std::make_unique<SetTrackMuteCommand>(trk, true);
      ASSERT_TRUE(c9->execute(p).ok());

      // 8b-10: SetTrackSoloCommand
      Cmd c10 = std::make_unique<SetTrackSoloCommand>(trk, true);
      ASSERT_TRUE(c10->execute(p).ok());

      // Also exercise the cross-cutting commands from 8a for completeness.
      // SetNodeNameCommand
      Cmd c11 = std::make_unique<SetNodeNameCommand>(node, "RenamedN");
      ASSERT_TRUE(c11->execute(p).ok());

      // SetProjectTempoCommand
      Cmd c12 = std::make_unique<SetProjectTempoCommand>(
          *Tempo::create(Rational(120), NoteValue::kQuarter));
      ASSERT_TRUE(c12->execute(p).ok());

      // SetTrackNameCommand
      Cmd c13 = std::make_unique<SetTrackNameCommand>(trk, "RenamedT");
      ASSERT_TRUE(c13->execute(p).ok());
    };

    apply(proj, n, *tid, GraphPosition{42.0, -7.0});
    apply(copy, n, *tid, GraphPosition{42.0, -7.0});
  }

  // Compare every affected field plus identity/order.
  EXPECT_EQ(proj.id(), copy.id());
  EXPECT_EQ(proj.name(), copy.name());
  EXPECT_EQ(proj.start_node(), copy.start_node());
  EXPECT_EQ(proj.default_dynamic(), copy.default_dynamic());
  EXPECT_EQ(proj.default_tempo(), copy.default_tempo());
  EXPECT_EQ(proj.active_tracks().size(), copy.active_tracks().size());
  EXPECT_EQ(proj.nodes().size(), copy.nodes().size());

  for (std::size_t i = 0; i < proj.nodes().size(); ++i) {
    SCOPED_TRACE(i);
    const auto& a = proj.nodes()[i];
    const auto& b = copy.nodes()[i];
    EXPECT_EQ(a.id(), b.id());
    EXPECT_EQ(a.name(), b.name());
    EXPECT_EQ(a.color(), b.color());
    EXPECT_EQ(a.notes(), b.notes());
    EXPECT_EQ(std::bit_cast<std::uint64_t>(a.position().x),
              std::bit_cast<std::uint64_t>(b.position().x));
    EXPECT_EQ(std::bit_cast<std::uint64_t>(a.position().y),
              std::bit_cast<std::uint64_t>(b.position().y));
  }

  for (std::size_t i = 0; i < proj.active_tracks().size(); ++i) {
    SCOPED_TRACE(i);
    const auto& a = proj.active_tracks()[i];
    const auto& b = copy.active_tracks()[i];
    EXPECT_EQ(a.id(), b.id());
    EXPECT_EQ(a.name(), b.name());
    EXPECT_EQ(std::bit_cast<std::uint32_t>(a.mix_settings().gain()),
              std::bit_cast<std::uint32_t>(b.mix_settings().gain()));
    EXPECT_EQ(std::bit_cast<std::uint32_t>(a.mix_settings().pan()),
              std::bit_cast<std::uint32_t>(b.mix_settings().pan()));
    EXPECT_EQ(a.mix_settings().mute(), b.mix_settings().mute());
    EXPECT_EQ(a.mix_settings().solo(), b.mix_settings().solo());
  }
}

// =========================================================================
// Finding 4: stable lookup / history retry — track archive/restore
// =========================================================================

TEST(CommandTest, HistoryUndoFailsWhenTrackArchivedRetryAfterRestore) {
  Project    project  = make_project();
  const auto track_id = project.add_track("Target", StaffLayout::single_staff(),
                                          *MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());

  auto*       track         = project.find_active_track(*track_id);
  const float original_gain = track->mix_settings().gain();
  const float new_gain      = 0.42F;
  ASSERT_NE(std::bit_cast<std::uint32_t>(original_gain),
            std::bit_cast<std::uint32_t>(new_gain));

  CommandHistory history;
  ASSERT_TRUE(history
                  .execute_new(std::make_unique<SetTrackGainCommand>(*track_id,
                                                                     new_gain),
                               project)
                  .ok());
  EXPECT_EQ(std::bit_cast<std::uint32_t>(track->mix_settings().gain()),
            std::bit_cast<std::uint32_t>(new_gain));
  EXPECT_EQ(history.undo_stack_size(), 1u);

  // Archive the target externally.
  ASSERT_TRUE(project.archive_track(*track_id).ok());

  // Undo must fail because the track is no longer active.
  Result undo_result = history.undo(project);
  EXPECT_FALSE(undo_result.ok());
  EXPECT_EQ(undo_result.code(), ResultCode::kInvalidArgument);
  // Command stays on undo stack; model unchanged from before undo attempt.
  EXPECT_EQ(history.undo_stack_size(), 1u);
  EXPECT_EQ(history.redo_stack_size(), 0u);
  // The track is still at the new_gain value in the archive (archive_track
  // moves it — verify it's still there in archived_tracks).
  ASSERT_NE(project.find_archived_track(*track_id), nullptr);

  // Restore the track externally.
  ASSERT_TRUE(project.restore_track(*track_id).ok());

  // Retry undo succeeds.
  ASSERT_TRUE(history.undo(project).ok());
  EXPECT_EQ(std::bit_cast<std::uint32_t>(
                project.find_active_track(*track_id)->mix_settings().gain()),
            std::bit_cast<std::uint32_t>(original_gain));
  EXPECT_EQ(history.undo_stack_size(), 0u);
  EXPECT_EQ(history.redo_stack_size(), 1u);

  // Now cover redo failure under archive.
  ASSERT_TRUE(project.archive_track(*track_id).ok());
  Result redo_result = history.redo(project);
  EXPECT_FALSE(redo_result.ok());
  EXPECT_EQ(redo_result.code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(history.redo_stack_size(), 1u);

  // Retry redo after restore succeeds.
  ASSERT_TRUE(project.restore_track(*track_id).ok());
  ASSERT_TRUE(history.redo(project).ok());
  EXPECT_EQ(std::bit_cast<std::uint32_t>(
                project.find_active_track(*track_id)->mix_settings().gain()),
            std::bit_cast<std::uint32_t>(new_gain));
}

// =========================================================================
// Finding 4: stable lookup — node commands survive vector reallocation
// =========================================================================

TEST(CommandTest, NodeCommandsSurviveReallocation) {
  Project project = make_project();
  NodeId  target  = project.add_node("Target");
  project.find_node(target)->set_name("Target");
  project.find_node(target)->set_color(0x11111111);

  // Execute two commands on the target node.
  CommandHistory history;
  ASSERT_TRUE(
      history
          .execute_new(std::make_unique<SetNodeNameCommand>(target, "Renamed"),
                       project)
          .ok());
  ASSERT_TRUE(history
                  .execute_new(
                      std::make_unique<SetNodeColorCommand>(target, 0xFFAABBCC),
                      project)
                  .ok());
  EXPECT_EQ(project.find_node(target)->name(), "Renamed");
  EXPECT_EQ(project.find_node(target)->color(), 0xFFAABBCCu);

  const auto initial_size = project.nodes().size();

  // Grow the node vector enough to force likely reallocation.
  constexpr int       kExtraNodes = 200;
  std::vector<NodeId> extra_ids;
  for (int i = 0; i < kExtraNodes; ++i) {
    extra_ids.push_back(project.add_node("Extra"));
  }
  EXPECT_EQ(project.nodes().size(), initial_size + kExtraNodes);

  // Verify the target node is still findable by ID and new nodes are
  // untouched (still have default name and color).
  auto* t = project.find_node(target);
  ASSERT_NE(t, nullptr);
  EXPECT_EQ(t->name(), "Renamed");
  EXPECT_EQ(t->color(), 0xFFAABBCCu);
  for (auto eid : extra_ids) {
    auto* en = project.find_node(eid);
    ASSERT_NE(en, nullptr);
    EXPECT_EQ(en->name(), "Extra");
    EXPECT_EQ(en->color(), 0xFFFFFFFF);
  }

  // Undo both commands by ID — intended node changes, new nodes do not.
  ASSERT_TRUE(history.undo(project).ok());  // color back
  t = project.find_node(target);
  ASSERT_NE(t, nullptr);
  EXPECT_EQ(t->color(), 0x11111111u);
  // New nodes unchanged.
  for (auto eid : extra_ids) {
    EXPECT_EQ(project.find_node(eid)->color(), 0xFFFFFFFF);
  }

  ASSERT_TRUE(history.undo(project).ok());  // name back
  t = project.find_node(target);
  ASSERT_NE(t, nullptr);
  EXPECT_EQ(t->name(), "Target");
  for (auto eid : extra_ids) {
    EXPECT_EQ(project.find_node(eid)->name(), "Extra");
  }

  // Redo both — still works after reallocation.
  ASSERT_TRUE(history.redo(project).ok());
  t = project.find_node(target);
  ASSERT_NE(t, nullptr);
  EXPECT_EQ(t->name(), "Renamed");

  ASSERT_TRUE(history.redo(project).ok());
  t = project.find_node(target);
  ASSERT_NE(t, nullptr);
  EXPECT_EQ(t->color(), 0xFFAABBCCu);

  // New nodes never changed.
  for (auto eid : extra_ids) {
    EXPECT_EQ(project.find_node(eid)->name(), "Extra");
    EXPECT_EQ(project.find_node(eid)->color(), 0xFFFFFFFF);
  }
}
