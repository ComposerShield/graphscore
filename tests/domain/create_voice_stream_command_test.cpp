// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <variant>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>

using graphscore::Clef;
using graphscore::CreateVoiceStreamCommand;
using graphscore::decompose_measure_aligned_rests;
using graphscore::Duration;
using graphscore::KeySignature;
using graphscore::Letter;
using graphscore::make_note;
using graphscore::Measure;
using graphscore::MidiChannel;
using graphscore::Node;
using graphscore::NodeId;
using graphscore::NodeTimeline;
using graphscore::NoteValue;
using graphscore::Project;
using graphscore::ProjectId;
using graphscore::Rational;
using graphscore::Rest;
using graphscore::Result;
using graphscore::ResultCode;
using graphscore::SpelledPitch;
using graphscore::StaffLayout;
using graphscore::StaveDefinition;
using graphscore::StaveId;
using graphscore::TimeSignature;
using graphscore::TrackId;
using graphscore::Voice;
using graphscore::VoiceContent;

namespace {

Measure measure(std::uint8_t numerator = 4, std::uint16_t denominator = 4) {
  return Measure{*TimeSignature::create(numerator, denominator),
                 KeySignature{}};
}

Project make_project() {
  return Project(ProjectId::generate(), "Test");
}

// Builds a single-track, single-node, single-stave project with a
// 2-measure 4/4 timeline (node_end() == 2/1) and every voice left
// genuinely empty -- the real shape of a stave nothing has ever entered
// into, unlike most other domain command fixtures which pre-fill every
// voice with rests. That gap is exactly what CreateVoiceStreamCommand
// exists to close.
struct Fixture {
  Project project = make_project();
  TrackId track;
  StaveId stave;
  NodeId  node_id;

  explicit Fixture(std::vector<Measure> measures = {measure(), measure()}) {
    const auto tid = project.add_track("Track", StaffLayout::single_staff(),
                                       *MidiChannel::create(0));
    assert(tid.has_value());
    track                 = *tid;
    const auto* track_ptr = project.find_active_track(track);
    assert(track_ptr != nullptr);
    stave = track_ptr->layout().staves()[0].id;

    node_id      = project.add_node("A");
    Node* node_p = project.find_node(node_id);
    assert(node_p != nullptr);

    auto timeline = NodeTimeline::create(
        std::move(measures), {StaveDefinition{stave, Clef::kTreble}});
    assert(timeline.has_value());
    node_p->set_timeline(std::move(*timeline));
    node_p->lane(track)->ensure_stave(stave);
  }

  Node* node() { return project.find_node(node_id); }

  VoiceContent& voice(std::uint8_t voice_index = 1) {
    return node()->lane(track)->stave(stave)->voice(
        *Voice::create(voice_index));
  }

  Rational node_end() const {
    return project.find_node(node_id)->timeline()->node_end();
  }
};

std::unique_ptr<CreateVoiceStreamCommand> make_command(
    Fixture& fixture, std::uint8_t voice_index = 1) {
  return std::make_unique<CreateVoiceStreamCommand>(
      fixture.node_id, fixture.track, fixture.stave,
      *Voice::create(voice_index));
}

}  // namespace

// ---- Execute: fills an empty voice with the measure-aligned rest fill ----

TEST(CreateVoiceStreamCommandTest,
     ExecuteFillsEmptyVoiceMatchingTheSharedHelper) {
  Fixture fixture;
  ASSERT_TRUE(fixture.voice().events().empty());

  const auto expected =
      decompose_measure_aligned_rests(*fixture.node()->timeline());
  ASSERT_TRUE(expected.has_value());

  auto cmd = make_command(fixture);
  ASSERT_TRUE(cmd->execute(fixture.project).ok());

  ASSERT_EQ(fixture.voice().events().size(), expected->size());
  for (std::size_t i = 0; i < expected->size(); ++i) {
    ASSERT_TRUE(std::holds_alternative<Rest>(fixture.voice().events()[i]));
    EXPECT_EQ(std::get<Rest>(fixture.voice().events()[i]).duration,
              (*expected)[i].duration);
  }
  EXPECT_EQ(fixture.voice().total_length(), fixture.node_end());
}

// ---- Rejection: non-empty voice, unowned node/track/stave ----

TEST(CreateVoiceStreamCommandTest, RejectsNonEmptyVoiceLeavingItUnchanged) {
  Fixture fixture;
  ASSERT_TRUE(fixture.voice()
                  .append(make_note(*SpelledPitch::create(Letter::kC, 4),
                                    *Duration::create(NoteValue::kQuarter, 0)))
                  .ok());
  const VoiceContent pre = fixture.voice();

  auto cmd = make_command(fixture);
  EXPECT_FALSE(cmd->execute(fixture.project).ok());
  EXPECT_EQ(fixture.voice(), pre);
}

TEST(CreateVoiceStreamCommandTest, RejectsUnownedNode) {
  Fixture fixture;
  auto    cmd = std::make_unique<CreateVoiceStreamCommand>(
      NodeId::generate(), fixture.track, fixture.stave, *Voice::create(1));
  EXPECT_EQ(cmd->execute(fixture.project).code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(fixture.voice().events().empty());
}

TEST(CreateVoiceStreamCommandTest, RejectsUnownedTrack) {
  Fixture fixture;
  auto    cmd = std::make_unique<CreateVoiceStreamCommand>(
      fixture.node_id, TrackId::generate(), fixture.stave, *Voice::create(1));
  EXPECT_EQ(cmd->execute(fixture.project).code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(fixture.voice().events().empty());
}

TEST(CreateVoiceStreamCommandTest, RejectsUnownedStave) {
  Fixture fixture;
  auto    cmd = std::make_unique<CreateVoiceStreamCommand>(
      fixture.node_id, fixture.track, StaveId::generate(), *Voice::create(1));
  EXPECT_EQ(cmd->execute(fixture.project).code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(fixture.voice().events().empty());
}

// ---- Failure atomicity: an unrepresentable fill mutates nothing ----

TEST(CreateVoiceStreamCommandTest,
     UnrepresentableFillLeavesVoiceEmptyAndCommandRetryable) {
  Fixture fixture({measure()});
  ASSERT_TRUE(
      fixture.node()->timeline()->set_pickdown(*Rational::create(1, 3)).ok());

  auto cmd = make_command(fixture);
  EXPECT_FALSE(cmd->execute(fixture.project).ok());
  EXPECT_TRUE(fixture.voice().events().empty());

  // The command itself is untouched by the failed attempt: fixing the
  // timeline and retrying the same command instance succeeds.
  ASSERT_TRUE(fixture.node()->timeline()->clear_pickdown().ok());
  EXPECT_TRUE(cmd->execute(fixture.project).ok());
  EXPECT_FALSE(fixture.voice().events().empty());
}

TEST(CreateVoiceStreamCommandTest, DoubleExecuteIsRejected) {
  Fixture fixture;
  auto    cmd = make_command(fixture);
  ASSERT_TRUE(cmd->execute(fixture.project).ok());
  EXPECT_FALSE(cmd->execute(fixture.project).ok());
}

// ---- Undo: exact restoration to completely empty (zero events) ----

TEST(CreateVoiceStreamCommandTest, UndoRestoresCompletelyEmptyVoice) {
  Fixture fixture;
  auto    cmd = make_command(fixture);
  ASSERT_TRUE(cmd->execute(fixture.project).ok());
  ASSERT_FALSE(fixture.voice().events().empty());

  ASSERT_TRUE(cmd->undo(fixture.project).ok());
  EXPECT_TRUE(fixture.voice().events().empty());
  EXPECT_EQ(fixture.voice(), VoiceContent{});
}

// ---- Redo: id-for-id identical to the original execute ----

TEST(CreateVoiceStreamCommandTest, RedoReproducesIdForIdIdenticalFill) {
  Fixture fixture;
  auto    cmd = make_command(fixture);
  ASSERT_TRUE(cmd->execute(fixture.project).ok());
  const VoiceContent post_snapshot = fixture.voice();

  ASSERT_TRUE(cmd->undo(fixture.project).ok());
  ASSERT_TRUE(cmd->redo(fixture.project).ok());
  EXPECT_EQ(fixture.voice(), post_snapshot);

  // Second undo/redo cycle reproduces the exact same state again.
  ASSERT_TRUE(cmd->undo(fixture.project).ok());
  EXPECT_TRUE(fixture.voice().events().empty());
  ASSERT_TRUE(cmd->redo(fixture.project).ok());
  EXPECT_EQ(fixture.voice(), post_snapshot);
}

// ---- Isolation: only the armed voice's slot is ever written ----

TEST(CreateVoiceStreamCommandTest, OnlyTheTargetVoiceIsWritten) {
  Fixture fixture;
  // Pre-fill voice 1 with real content so it has something to disturb.
  ASSERT_TRUE(fixture.voice(1)
                  .append(make_note(*SpelledPitch::create(Letter::kC, 4),
                                    *Duration::create(NoteValue::kWhole, 0)))
                  .ok());
  ASSERT_TRUE(fixture.voice(1)
                  .append(make_note(*SpelledPitch::create(Letter::kD, 4),
                                    *Duration::create(NoteValue::kWhole, 0)))
                  .ok());
  const VoiceContent voice1_before = fixture.voice(1);
  const VoiceContent voice3_before = fixture.voice(3);
  const VoiceContent voice4_before = fixture.voice(4);

  auto cmd = make_command(fixture, 2);
  ASSERT_TRUE(cmd->execute(fixture.project).ok());

  EXPECT_EQ(fixture.voice(1), voice1_before);
  EXPECT_EQ(fixture.voice(3), voice3_before);
  EXPECT_EQ(fixture.voice(4), voice4_before);
  EXPECT_FALSE(fixture.voice(2).events().empty());

  ASSERT_TRUE(cmd->undo(fixture.project).ok());
  EXPECT_EQ(fixture.voice(1), voice1_before);
  EXPECT_EQ(fixture.voice(3), voice3_before);
  EXPECT_EQ(fixture.voice(4), voice4_before);
}
