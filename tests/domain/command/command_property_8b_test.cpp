// SPDX-License-Identifier: Apache-2.0

#include "command_test_support.hpp"

#include <gtest/gtest.h>

#include <bit>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include <graphscore/domain/graphscore_domain.hpp>

// =========================================================================
// Phase 8b — SetProjectNameCommand
// =========================================================================

TEST(CommandTest, SetProjectNameRoundTrip) {
  Project project = make_project();
  EXPECT_EQ(project.name(), "Test Project");

  auto cmd = std::make_unique<SetProjectNameCommand>("Renamed Project");

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(project.name(), "Renamed Project");

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_EQ(project.name(), "Test Project");

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_EQ(project.name(), "Renamed Project");
}

TEST(CommandTest, SetProjectNameEmptyString) {
  Project project = make_project();

  auto cmd = std::make_unique<SetProjectNameCommand>("");

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(project.name(), "");

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_EQ(project.name(), "Test Project");
}

TEST(CommandTest, SetProjectNameLongString) {
  Project project = make_project();

  std::string long_name(10'000, 'x');
  auto        cmd = std::make_unique<SetProjectNameCommand>(long_name);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(project.name(), long_name);

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_EQ(project.name(), "Test Project");
}

TEST(CommandTest, SetProjectNameUtf8Bytes) {
  Project project = make_project();

  auto cmd = std::make_unique<SetProjectNameCommand>(
      "\xc3\xa9"            // é
      "\xe2\x98\x83"        // snowman
      "\xf0\x9f\x8e\xb6");  // musical note

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(project.name(), "é☃🎶");

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_EQ(project.name(), "Test Project");
}

TEST(CommandTest, SetProjectNameDoubleExecuteRejected) {
  Project project = make_project();
  auto    cmd     = std::make_unique<SetProjectNameCommand>("X");

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_FALSE(cmd->execute(project).ok());
  EXPECT_EQ(project.name(), "X");
}

TEST(CommandTest, SetProjectNameUndoWithoutExecuteRejected) {
  Project project = make_project();
  auto    cmd     = std::make_unique<SetProjectNameCommand>("X");

  EXPECT_FALSE(cmd->undo(project).ok());
  EXPECT_EQ(project.name(), "Test Project");
}

TEST(CommandTest, SetProjectNameRedoWithoutUndoRejected) {
  Project project = make_project();
  auto    cmd     = std::make_unique<SetProjectNameCommand>("X");

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_FALSE(cmd->redo(project).ok());
  EXPECT_EQ(project.name(), "X");
}

// =========================================================================
// Phase 8b — SetStartNodeCommand
// =========================================================================

TEST(CommandTest, SetStartNodeValidSetRoundTrip) {
  Project project = make_project();
  NodeId  node_id = project.add_node("Entry");

  EXPECT_FALSE(project.start_node().has_value());

  auto cmd =
      std::make_unique<SetStartNodeCommand>(std::optional<NodeId>(node_id));

  ASSERT_TRUE(cmd->execute(project).ok());
  ASSERT_TRUE(project.start_node().has_value());
  EXPECT_EQ(*project.start_node(), node_id);

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_FALSE(project.start_node().has_value());

  ASSERT_TRUE(cmd->redo(project).ok());
  ASSERT_TRUE(project.start_node().has_value());
  EXPECT_EQ(*project.start_node(), node_id);
}

TEST(CommandTest, SetStartNodeClearRoundTrip) {
  Project project = make_project();
  NodeId  node_id = project.add_node("Entry");
  ASSERT_TRUE(project.set_start_node(node_id).ok());

  auto cmd = std::make_unique<SetStartNodeCommand>(std::nullopt);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_FALSE(project.start_node().has_value());

  ASSERT_TRUE(cmd->undo(project).ok());
  ASSERT_TRUE(project.start_node().has_value());
  EXPECT_EQ(*project.start_node(), node_id);

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_FALSE(project.start_node().has_value());
}

TEST(CommandTest, SetStartNodeReplaceExistingRoundTrip) {
  Project project = make_project();
  NodeId  old_id  = project.add_node("Old");
  NodeId  new_id  = project.add_node("New");
  ASSERT_TRUE(project.set_start_node(old_id).ok());

  auto cmd =
      std::make_unique<SetStartNodeCommand>(std::optional<NodeId>(new_id));

  ASSERT_TRUE(cmd->execute(project).ok());
  ASSERT_TRUE(project.start_node().has_value());
  EXPECT_EQ(*project.start_node(), new_id);

  ASSERT_TRUE(cmd->undo(project).ok());
  ASSERT_TRUE(project.start_node().has_value());
  EXPECT_EQ(*project.start_node(), old_id);

  ASSERT_TRUE(cmd->redo(project).ok());
  ASSERT_TRUE(project.start_node().has_value());
  EXPECT_EQ(*project.start_node(), new_id);
}

TEST(CommandTest, SetStartNodeInvalidTargetFailsNoMutation) {
  Project project = make_project();
  NodeId  node_id = project.add_node("Entry");
  ASSERT_TRUE(project.set_start_node(node_id).ok());

  auto cmd = std::make_unique<SetStartNodeCommand>(
      std::optional<NodeId>(NodeId::generate()));

  Result result = cmd->execute(project);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.code(), ResultCode::kInvalidArgument);
  ASSERT_TRUE(project.start_node().has_value());
  EXPECT_EQ(*project.start_node(), node_id);
}

TEST(CommandTest, SetStartNodeClearWhenAlreadyClear) {
  Project project = make_project();
  EXPECT_FALSE(project.start_node().has_value());

  auto cmd = std::make_unique<SetStartNodeCommand>(std::nullopt);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_FALSE(project.start_node().has_value());

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_FALSE(project.start_node().has_value());
}

TEST(CommandTest, SetStartNodeDoubleExecuteRejected) {
  Project project = make_project();
  NodeId  node_id = project.add_node("X");

  auto cmd =
      std::make_unique<SetStartNodeCommand>(std::optional<NodeId>(node_id));

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_FALSE(cmd->execute(project).ok());
}

TEST(CommandTest, SetStartNodeUndoWithoutExecuteRejected) {
  Project project = make_project();
  NodeId  node_id = project.add_node("X");

  auto cmd =
      std::make_unique<SetStartNodeCommand>(std::optional<NodeId>(node_id));

  EXPECT_FALSE(cmd->undo(project).ok());
  EXPECT_FALSE(project.start_node().has_value());
}

TEST(CommandTest, SetStartNodeRedoWithoutUndoRejected) {
  Project project = make_project();
  NodeId  node_id = project.add_node("X");

  auto cmd =
      std::make_unique<SetStartNodeCommand>(std::optional<NodeId>(node_id));

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_FALSE(cmd->redo(project).ok());
}

// =========================================================================
// Phase 8b — SetProjectDynamicCommand
// =========================================================================

TEST(CommandTest, SetProjectDynamicRoundTrip) {
  Project project = make_project();
  EXPECT_EQ(project.default_dynamic(), Dynamic::kMf);

  auto cmd = std::make_unique<SetProjectDynamicCommand>(Dynamic::kFff);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(project.default_dynamic(), Dynamic::kFff);

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_EQ(project.default_dynamic(), Dynamic::kMf);

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_EQ(project.default_dynamic(), Dynamic::kFff);
}

TEST(CommandTest, SetProjectDynamicAllValues) {
  Project project = make_project();

  for (auto d : {Dynamic::kPpp, Dynamic::kPp, Dynamic::kP, Dynamic::kMp,
                 Dynamic::kMf, Dynamic::kF, Dynamic::kFf, Dynamic::kFff}) {
    auto cmd = std::make_unique<SetProjectDynamicCommand>(d);
    ASSERT_TRUE(cmd->execute(project).ok());
    EXPECT_EQ(project.default_dynamic(), d);
  }
}

TEST(CommandTest, SetProjectDynamicDoubleExecuteRejected) {
  Project project = make_project();
  auto    cmd     = std::make_unique<SetProjectDynamicCommand>(Dynamic::kPp);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_FALSE(cmd->execute(project).ok());
  EXPECT_EQ(project.default_dynamic(), Dynamic::kPp);
}

// =========================================================================
// Phase 8b — SetTrackGainCommand
// =========================================================================

TEST(CommandTest, SetTrackGainRoundTrip) {
  Project    project  = make_project();
  const auto track_id = project.add_track("Track", StaffLayout::single_staff(),
                                          *MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());

  auto* track = project.find_active_track(*track_id);
  ASSERT_NE(track, nullptr);
  EXPECT_FLOAT_EQ(track->mix_settings().gain(), 0.8F);

  auto cmd = std::make_unique<SetTrackGainCommand>(*track_id, 1.0F);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_FLOAT_EQ(track->mix_settings().gain(), 1.0F);

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_FLOAT_EQ(track->mix_settings().gain(), 0.8F);

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_FLOAT_EQ(track->mix_settings().gain(), 1.0F);
}

TEST(CommandTest, SetTrackGainMissingIdFails) {
  Project project = make_project();

  auto cmd = std::make_unique<SetTrackGainCommand>(TrackId::generate(), 1.0F);
  EXPECT_FALSE(cmd->execute(project).ok());
}

TEST(CommandTest, SetTrackGainArchivedTrackFails) {
  Project    project  = make_project();
  const auto track_id = project.add_track("Track", StaffLayout::single_staff(),
                                          *MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());
  ASSERT_TRUE(project.archive_track(*track_id).ok());

  auto cmd = std::make_unique<SetTrackGainCommand>(*track_id, 1.0F);
  EXPECT_FALSE(cmd->execute(project).ok());
}

TEST(CommandTest, SetTrackGainBitwiseIeeeRoundTrip) {
  Project    project  = make_project();
  const auto track_id = project.add_track("Track", StaffLayout::single_staff(),
                                          *MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());
  auto* track = project.find_active_track(*track_id);

  constexpr std::uint32_t kDefaultBits = 0x3F4CCCCDu;  // 0.8F
  EXPECT_EQ(std::bit_cast<std::uint32_t>(track->mix_settings().gain()),
            kDefaultBits);

  struct Case {
    std::uint32_t bits;
    const char*   label;
  };

  const Case cases[] = {
      {0x00000000u, "+0.0f"},
      {0x80000000u, "-0.0f"},
      {0x3F800000u, "1.0f"},
      {0x40000000u, "2.0f"},
      {0x3F000000u, "0.5f"},
      {0x3E800000u, "0.25f"},
      {0x7F7FFFFFu, "max finite float"},
      {0x00800000u, "min positive normal"},
      {0x00000001u, "min positive subnormal"},
      {0x7F800000u, "+inf"},
      {0xFF800000u, "-inf"},
      {0x7FC00001u, "qNaN payload 1"},
      {0x7FC00000u, "canonical qNaN"},
      {0x7F800001u, "sNaN payload 1"},
      {0xFFC00000u, "negative qNaN"},
  };

  std::uint32_t expected_old_bits = kDefaultBits;
  for (const auto& c : cases) {
    const float v   = std::bit_cast<float>(c.bits);
    auto        cmd = std::make_unique<SetTrackGainCommand>(*track_id, v);

    ASSERT_TRUE(cmd->execute(project).ok()) << c.label;
    EXPECT_EQ(std::bit_cast<std::uint32_t>(track->mix_settings().gain()),
              c.bits)
        << c.label;

    ASSERT_TRUE(cmd->undo(project).ok()) << c.label;
    EXPECT_EQ(std::bit_cast<std::uint32_t>(track->mix_settings().gain()),
              expected_old_bits)
        << "undo " << c.label;

    ASSERT_TRUE(cmd->redo(project).ok()) << c.label;
    EXPECT_EQ(std::bit_cast<std::uint32_t>(track->mix_settings().gain()),
              c.bits)
        << "redo " << c.label;

    expected_old_bits = c.bits;
  }
}

TEST(CommandTest, SetTrackGainUnrelatedFieldsUnchanged) {
  Project    project  = make_project();
  const auto track_id = project.add_track("Track", StaffLayout::single_staff(),
                                          *MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());

  auto* track = project.find_active_track(*track_id);
  track->mix_settings().set_pan(0.75F);
  track->mix_settings().set_mute(true);
  const float pan_before  = track->mix_settings().pan();
  const bool  mute_before = track->mix_settings().mute();

  auto cmd = std::make_unique<SetTrackGainCommand>(*track_id, 0.42F);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_FLOAT_EQ(track->mix_settings().pan(), pan_before);
  EXPECT_EQ(track->mix_settings().mute(), mute_before);
}

// =========================================================================
// Phase 8b — SetTrackPanCommand
// =========================================================================

TEST(CommandTest, SetTrackPanRoundTrip) {
  Project    project  = make_project();
  const auto track_id = project.add_track("Track", StaffLayout::single_staff(),
                                          *MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());

  auto* track = project.find_active_track(*track_id);
  ASSERT_NE(track, nullptr);
  EXPECT_FLOAT_EQ(track->mix_settings().pan(), 0.0F);

  auto cmd = std::make_unique<SetTrackPanCommand>(*track_id, 0.5F);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_FLOAT_EQ(track->mix_settings().pan(), 0.5F);

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_FLOAT_EQ(track->mix_settings().pan(), 0.0F);

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_FLOAT_EQ(track->mix_settings().pan(), 0.5F);
}

TEST(CommandTest, SetTrackPanMissingIdFails) {
  Project project = make_project();

  auto cmd = std::make_unique<SetTrackPanCommand>(TrackId::generate(), 0.5F);
  EXPECT_FALSE(cmd->execute(project).ok());
}

TEST(CommandTest, SetTrackPanArchivedTrackFails) {
  Project    project  = make_project();
  const auto track_id = project.add_track("Track", StaffLayout::single_staff(),
                                          *MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());
  ASSERT_TRUE(project.archive_track(*track_id).ok());

  auto cmd = std::make_unique<SetTrackPanCommand>(*track_id, 0.5F);
  EXPECT_FALSE(cmd->execute(project).ok());
}

TEST(CommandTest, SetTrackPanBitwiseIeeeRoundTrip) {
  Project    project  = make_project();
  const auto track_id = project.add_track("Track", StaffLayout::single_staff(),
                                          *MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());
  auto* track = project.find_active_track(*track_id);

  constexpr std::uint32_t kDefaultBits = 0x00000000u;  // 0.0F
  EXPECT_EQ(std::bit_cast<std::uint32_t>(track->mix_settings().pan()),
            kDefaultBits);

  struct Case {
    std::uint32_t bits;
    const char*   label;
  };

  const Case cases[] = {
      {0x00000000u, "+0.0f"},
      {0x80000000u, "-0.0f"},
      {0x3F800000u, "1.0f"},
      {0xBF800000u, "-1.0f"},
      {0x3F000000u, "0.5f"},
      {0xBF000000u, "-0.5f"},
      {0x7F7FFFFFu, "max finite float"},
      {0xFF7FFFFFu, "-max finite float"},
      {0x7F800000u, "+inf"},
      {0xFF800000u, "-inf"},
      {0x7FC00001u, "qNaN payload 1"},
      {0x7FC00000u, "canonical qNaN"},
      {0x7F800001u, "sNaN payload 1"},
  };

  std::uint32_t expected_old_bits = kDefaultBits;
  for (const auto& c : cases) {
    const float v   = std::bit_cast<float>(c.bits);
    auto        cmd = std::make_unique<SetTrackPanCommand>(*track_id, v);

    ASSERT_TRUE(cmd->execute(project).ok()) << c.label;
    EXPECT_EQ(std::bit_cast<std::uint32_t>(track->mix_settings().pan()), c.bits)
        << c.label;

    ASSERT_TRUE(cmd->undo(project).ok()) << c.label;
    EXPECT_EQ(std::bit_cast<std::uint32_t>(track->mix_settings().pan()),
              expected_old_bits)
        << "undo " << c.label;

    ASSERT_TRUE(cmd->redo(project).ok()) << c.label;
    EXPECT_EQ(std::bit_cast<std::uint32_t>(track->mix_settings().pan()), c.bits)
        << "redo " << c.label;

    expected_old_bits = c.bits;
  }
}

// =========================================================================
// Phase 8b — SetTrackMuteCommand
// =========================================================================

TEST(CommandTest, SetTrackMuteRoundTrip) {
  Project    project  = make_project();
  const auto track_id = project.add_track("Track", StaffLayout::single_staff(),
                                          *MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());

  auto* track = project.find_active_track(*track_id);
  EXPECT_FALSE(track->mix_settings().mute());

  auto cmd = std::make_unique<SetTrackMuteCommand>(*track_id, true);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_TRUE(track->mix_settings().mute());

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_FALSE(track->mix_settings().mute());

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_TRUE(track->mix_settings().mute());
}

TEST(CommandTest, SetTrackMuteMissingIdFails) {
  Project project = make_project();

  auto cmd = std::make_unique<SetTrackMuteCommand>(TrackId::generate(), true);
  EXPECT_FALSE(cmd->execute(project).ok());
}

TEST(CommandTest, SetTrackMuteArchivedTrackFails) {
  Project    project  = make_project();
  const auto track_id = project.add_track("Track", StaffLayout::single_staff(),
                                          *MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());
  ASSERT_TRUE(project.archive_track(*track_id).ok());

  auto cmd = std::make_unique<SetTrackMuteCommand>(*track_id, true);
  EXPECT_FALSE(cmd->execute(project).ok());
}

// =========================================================================
// Phase 8b — SetTrackSoloCommand
// =========================================================================

TEST(CommandTest, SetTrackSoloRoundTrip) {
  Project    project  = make_project();
  const auto track_id = project.add_track("Track", StaffLayout::single_staff(),
                                          *MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());

  auto* track = project.find_active_track(*track_id);
  EXPECT_FALSE(track->mix_settings().solo());

  auto cmd = std::make_unique<SetTrackSoloCommand>(*track_id, true);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_TRUE(track->mix_settings().solo());

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_FALSE(track->mix_settings().solo());

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_TRUE(track->mix_settings().solo());
}

TEST(CommandTest, SetTrackSoloMissingIdFails) {
  Project project = make_project();

  auto cmd = std::make_unique<SetTrackSoloCommand>(TrackId::generate(), true);
  EXPECT_FALSE(cmd->execute(project).ok());
}

TEST(CommandTest, SetTrackSoloArchivedTrackFails) {
  Project    project  = make_project();
  const auto track_id = project.add_track("Track", StaffLayout::single_staff(),
                                          *MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());
  ASSERT_TRUE(project.archive_track(*track_id).ok());

  auto cmd = std::make_unique<SetTrackSoloCommand>(*track_id, true);
  EXPECT_FALSE(cmd->execute(project).ok());
}

// =========================================================================
// Phase 8b — SetNodeColorCommand
// =========================================================================

TEST(CommandTest, SetNodeColorRoundTrip) {
  Project project = make_project();
  NodeId  node_id = project.add_node("Node");

  auto* node = project.find_node(node_id);
  EXPECT_EQ(node->color(), 0xFFFFFFFF);

  auto cmd = std::make_unique<SetNodeColorCommand>(node_id, 0xFF00FF00);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(node->color(), 0xFF00FF00u);

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_EQ(node->color(), 0xFFFFFFFF);

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_EQ(node->color(), 0xFF00FF00u);
}

TEST(CommandTest, SetNodeColorMissingIdFails) {
  Project project = make_project();

  auto cmd =
      std::make_unique<SetNodeColorCommand>(NodeId::generate(), 0xFF0000FF);
  EXPECT_FALSE(cmd->execute(project).ok());
}

TEST(CommandTest, SetNodeColorMissingIdDoesNotChangeProject) {
  Project project = make_project();
  NodeId  node_id = project.add_node("Node");

  auto cmd =
      std::make_unique<SetNodeColorCommand>(NodeId::generate(), 0x01234567);
  EXPECT_FALSE(cmd->execute(project).ok());
  EXPECT_EQ(project.find_node(node_id)->color(), 0xFFFFFFFF);
}

TEST(CommandTest, SetNodeColorAllBytessExact) {
  Project project = make_project();
  NodeId  node_id = project.add_node("Node");
  auto*   node    = project.find_node(node_id);

  const std::uint32_t values[] = {0x00000000, 0xFFFFFFFF, 0x12345678,
                                  0xDEADBEEF, 0x01020304, 0xAABBCCDD};
  for (std::uint32_t v : values) {
    auto cmd = std::make_unique<SetNodeColorCommand>(node_id, v);
    ASSERT_TRUE(cmd->execute(project).ok());
    EXPECT_EQ(node->color(), v);
  }
}

// =========================================================================
// Phase 8b — SetNodeNotesCommand
// =========================================================================

TEST(CommandTest, SetNodeNotesRoundTrip) {
  Project project = make_project();
  NodeId  node_id = project.add_node("Node");

  EXPECT_EQ(project.find_node(node_id)->notes(), "");

  auto cmd = std::make_unique<SetNodeNotesCommand>(node_id, "Some notes");

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(project.find_node(node_id)->notes(), "Some notes");

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_EQ(project.find_node(node_id)->notes(), "");

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_EQ(project.find_node(node_id)->notes(), "Some notes");
}

TEST(CommandTest, SetNodeNotesMissingIdFails) {
  Project project = make_project();

  auto cmd = std::make_unique<SetNodeNotesCommand>(NodeId::generate(), "X");
  EXPECT_FALSE(cmd->execute(project).ok());
}

TEST(CommandTest, SetNodeNotesMissingIdDoesNotChangeProject) {
  Project project = make_project();
  NodeId  node_id = project.add_node("Node");
  project.find_node(node_id)->set_notes("Original");

  auto cmd = std::make_unique<SetNodeNotesCommand>(NodeId::generate(), "X");
  EXPECT_FALSE(cmd->execute(project).ok());
  EXPECT_EQ(project.find_node(node_id)->notes(), "Original");
}

TEST(CommandTest, SetNodeNotesEmptyString) {
  Project project = make_project();
  NodeId  node_id = project.add_node("Node");
  project.find_node(node_id)->set_notes("Not empty");

  auto cmd = std::make_unique<SetNodeNotesCommand>(node_id, "");

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(project.find_node(node_id)->notes(), "");

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_EQ(project.find_node(node_id)->notes(), "Not empty");
}

TEST(CommandTest, SetNodeNotesLongString) {
  Project project = make_project();
  NodeId  node_id = project.add_node("Node");

  std::string long_notes(100'000, 'z');
  auto        cmd = std::make_unique<SetNodeNotesCommand>(node_id, long_notes);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(project.find_node(node_id)->notes(), long_notes);

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_EQ(project.find_node(node_id)->notes(), "");
}

TEST(CommandTest, SetNodeNotesUtf8Bytes) {
  Project project = make_project();
  NodeId  node_id = project.add_node("Node");

  auto cmd = std::make_unique<SetNodeNotesCommand>(
      node_id, "\xc2\xa1Hola! \xf0\x9f\x8e\xb5");

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(project.find_node(node_id)->notes(), "¡Hola! 🎵");

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_EQ(project.find_node(node_id)->notes(), "");
}

TEST(CommandTest, SetNodeNotesDoubleExecuteRejected) {
  Project project = make_project();
  NodeId  node_id = project.add_node("Node");

  auto cmd = std::make_unique<SetNodeNotesCommand>(node_id, "X");

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_FALSE(cmd->execute(project).ok());
  EXPECT_EQ(project.find_node(node_id)->notes(), "X");
}

TEST(CommandTest, SetNodeNotesUndoWithoutExecuteRejected) {
  Project project = make_project();
  NodeId  node_id = project.add_node("Node");

  auto cmd = std::make_unique<SetNodeNotesCommand>(node_id, "X");

  EXPECT_FALSE(cmd->undo(project).ok());
  EXPECT_EQ(project.find_node(node_id)->notes(), "");
}

TEST(CommandTest, SetNodeNotesRedoWithoutUndoRejected) {
  Project project = make_project();
  NodeId  node_id = project.add_node("Node");

  auto cmd = std::make_unique<SetNodeNotesCommand>(node_id, "X");
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_FALSE(cmd->redo(project).ok());
  EXPECT_EQ(project.find_node(node_id)->notes(), "X");
}

// =========================================================================
// Phase 8b — SetNodePositionCommand
// =========================================================================

TEST(CommandTest, SetNodePositionRoundTrip) {
  Project project = make_project();
  NodeId  node_id = project.add_node("Node");

  auto* node = project.find_node(node_id);
  EXPECT_DOUBLE_EQ(node->position().x, 0.0);
  EXPECT_DOUBLE_EQ(node->position().y, 0.0);

  auto cmd = std::make_unique<SetNodePositionCommand>(
      node_id, GraphPosition{42.5, -17.25});

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_DOUBLE_EQ(node->position().x, 42.5);
  EXPECT_DOUBLE_EQ(node->position().y, -17.25);

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_DOUBLE_EQ(node->position().x, 0.0);
  EXPECT_DOUBLE_EQ(node->position().y, 0.0);

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_DOUBLE_EQ(node->position().x, 42.5);
  EXPECT_DOUBLE_EQ(node->position().y, -17.25);
}

TEST(CommandTest, SetNodePositionMissingIdFails) {
  Project project = make_project();

  auto cmd = std::make_unique<SetNodePositionCommand>(NodeId::generate(),
                                                      GraphPosition{1.0, 2.0});
  EXPECT_FALSE(cmd->execute(project).ok());
}

TEST(CommandTest, SetNodePositionMissingIdDoesNotChangeProject) {
  Project project = make_project();
  NodeId  node_id = project.add_node("Node");
  project.find_node(node_id)->set_position(GraphPosition{3.0, 4.0});

  auto cmd = std::make_unique<SetNodePositionCommand>(
      NodeId::generate(), GraphPosition{99.0, 99.0});
  EXPECT_FALSE(cmd->execute(project).ok());
  EXPECT_DOUBLE_EQ(project.find_node(node_id)->position().x, 3.0);
  EXPECT_DOUBLE_EQ(project.find_node(node_id)->position().y, 4.0);
}

TEST(CommandTest, SetNodePositionBitwiseIeeeRoundTrip) {
  Project project = make_project();
  NodeId  node_id = project.add_node("Node");
  auto*   node    = project.find_node(node_id);

  constexpr std::uint64_t kDefaultZeroBits = 0x0000000000000000u;
  EXPECT_EQ(std::bit_cast<std::uint64_t>(node->position().x), kDefaultZeroBits);
  EXPECT_EQ(std::bit_cast<std::uint64_t>(node->position().y), kDefaultZeroBits);

  struct Case {
    std::uint64_t x_bits;
    std::uint64_t y_bits;
    const char*   label;
  };

  const Case cases[] = {
      {0x0000000000000000u, 0x8000000000000000u, "+0.0 / -0.0"},
      {0x8000000000000000u, 0x0000000000000000u, "-0.0 / +0.0"},
      {0x3FF0000000000000u, 0x4000000000000000u, "1.0 / 2.0"},
      {0xC066400000000000u, 0x4069000000000000u, "-178.25 / 200.0"},
      {0x7FEFFFFFFFFFFFFFu, 0xFFEFFFFFFFFFFFFFu, "+max finite / -max finite"},
      {0x0010000000000000u, 0x0000000000000001u, "min normal / min subnormal"},
      {0x7FF0000000000000u, 0xFFF0000000000000u, "+inf / -inf"},
      {0x7FF8000000000001u, 0xFFF8000000000000u, "qNaN payload 1 / -qNaN"},
      {0x7FF0000000000001u, 0xFFF8000000000001u, "sNaN / -qNaN payload 1"},
  };

  std::uint64_t expected_old_x = kDefaultZeroBits;
  std::uint64_t expected_old_y = kDefaultZeroBits;
  for (const auto& c : cases) {
    const double  vx = std::bit_cast<double>(c.x_bits);
    const double  vy = std::bit_cast<double>(c.y_bits);
    GraphPosition new_pos{vx, vy};
    auto cmd = std::make_unique<SetNodePositionCommand>(node_id, new_pos);

    ASSERT_TRUE(cmd->execute(project).ok()) << c.label;
    EXPECT_EQ(std::bit_cast<std::uint64_t>(node->position().x), c.x_bits)
        << "x " << c.label;
    EXPECT_EQ(std::bit_cast<std::uint64_t>(node->position().y), c.y_bits)
        << "y " << c.label;

    ASSERT_TRUE(cmd->undo(project).ok()) << "undo " << c.label;
    EXPECT_EQ(std::bit_cast<std::uint64_t>(node->position().x), expected_old_x)
        << "undo x " << c.label;
    EXPECT_EQ(std::bit_cast<std::uint64_t>(node->position().y), expected_old_y)
        << "undo y " << c.label;

    ASSERT_TRUE(cmd->redo(project).ok()) << "redo " << c.label;
    EXPECT_EQ(std::bit_cast<std::uint64_t>(node->position().x), c.x_bits)
        << "redo x " << c.label;
    EXPECT_EQ(std::bit_cast<std::uint64_t>(node->position().y), c.y_bits)
        << "redo y " << c.label;

    expected_old_x = c.x_bits;
    expected_old_y = c.y_bits;
  }
}

TEST(CommandTest, SetNodePositionUnrelatedFieldsUnchanged) {
  Project project = make_project();
  NodeId  node_id = project.add_node("OriginalName");
  auto*   node    = project.find_node(node_id);
  node->set_color(0x11223344);

  auto cmd = std::make_unique<SetNodePositionCommand>(
      node_id, GraphPosition{10.0, 20.0});

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(node->name(), "OriginalName");
  EXPECT_EQ(node->color(), 0x11223344u);
}
