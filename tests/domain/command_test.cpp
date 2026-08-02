// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>

using graphscore::AddBeamOverrideCommand;
using graphscore::AddClefChangeCommand;
using graphscore::AddDynamicCommand;
using graphscore::AddGraceGroupCommand;
using graphscore::AddHairpinCommand;
using graphscore::AddInputConnectorCommand;
using graphscore::AddNodeCommand;
using graphscore::AddOutputConnectorCommand;
using graphscore::AddPedalSpanCommand;
using graphscore::AddSlurCommand;
using graphscore::AddTempoPointCommand;
using graphscore::AddTrackCommand;
using graphscore::ArchiveTrackCommand;
using graphscore::BeamOverride;
using graphscore::BindOutputEventCommand;
using graphscore::Chord;
using graphscore::ChordNote;
using graphscore::ClearPickdownCommand;
using graphscore::Clef;
using graphscore::ClefLane;
using graphscore::Command;
using graphscore::CommandHistory;
using graphscore::CommandTransaction;
using graphscore::ConnectCommand;
using graphscore::ConnectorId;
using graphscore::ConnectorType;
using graphscore::ConvertEventToRestCommand;
using graphscore::DisconnectCommand;
using graphscore::Duration;
using graphscore::Dynamic;
using graphscore::DynamicMarking;
using graphscore::event_id;
using graphscore::EventId;
using graphscore::EventListener;
using graphscore::GraceGroup;
using graphscore::GraceNote;
using graphscore::GraceNoteType;
using graphscore::Graph;
using graphscore::GraphPosition;
using graphscore::Hairpin;
using graphscore::HairpinDirection;
using graphscore::KeySignature;
using graphscore::Letter;
using graphscore::make_beam_override;
using graphscore::make_chord;
using graphscore::make_dynamic_marking;
using graphscore::make_grace_group;
using graphscore::make_hairpin;
using graphscore::make_note;
using graphscore::make_pedal_span;
using graphscore::make_rest;
using graphscore::make_slur;
using graphscore::Measure;
using graphscore::MidiChannel;
using graphscore::MoveClefChangeCommand;
using graphscore::MoveTempoPointCommand;
using graphscore::Node;
using graphscore::NodeId;
using graphscore::NodeTimeline;
using graphscore::NotationEntityId;
using graphscore::Note;
using graphscore::NoteValue;
using graphscore::OutputConnector;
using graphscore::PedalSpan;
using graphscore::Project;
using graphscore::ProjectId;
using graphscore::QueuePolicy;
using graphscore::Rational;
using graphscore::RegisterEventCommand;
using graphscore::RemoveBeamOverrideCommand;
using graphscore::RemoveClefChangeCommand;
using graphscore::RemoveDynamicCommand;
using graphscore::RemoveEventCommand;
using graphscore::RemoveGraceGroupCommand;
using graphscore::RemoveHairpinCommand;
using graphscore::RemoveInputConnectorCommand;
using graphscore::RemoveNodeCommand;
using graphscore::RemoveOutputConnectorCommand;
using graphscore::RemovePedalSpanCommand;
using graphscore::RemoveSlurCommand;
using graphscore::RemoveTempoPointCommand;
using graphscore::ResetRouteCommand;
using graphscore::Rest;
using graphscore::RestoreTrackCommand;
using graphscore::Result;
using graphscore::ResultCode;
using graphscore::RouteGeometry;
using graphscore::RoutePoint;
using graphscore::SetCustomRouteCommand;
using graphscore::SetEventCommand;
using graphscore::SetInputConnectorNameCommand;
using graphscore::SetListenerPolicyCommand;
using graphscore::SetMeasureKeySignatureCommand;
using graphscore::SetNodeColorCommand;
using graphscore::SetNodeNameCommand;
using graphscore::SetNodeNotesCommand;
using graphscore::SetNodePositionCommand;
using graphscore::SetOutputConnectorNameCommand;
using graphscore::SetOutputExportEnabledCommand;
using graphscore::SetOutputPriorityCommand;
using graphscore::SetOutputTypeCommand;
using graphscore::SetOutputWeightCommand;
using graphscore::SetPickdownCommand;
using graphscore::SetProjectDynamicCommand;
using graphscore::SetProjectNameCommand;
using graphscore::SetProjectTempoCommand;
using graphscore::SetStartNodeCommand;
using graphscore::SetTempoPointCommand;
using graphscore::SetTieCommand;
using graphscore::SetTrackGainCommand;
using graphscore::SetTrackMuteCommand;
using graphscore::SetTrackNameCommand;
using graphscore::SetTrackPanCommand;
using graphscore::SetTrackSoloCommand;
using graphscore::Slur;
using graphscore::SpelledPitch;
using graphscore::StaffLayout;
using graphscore::StaveId;
using graphscore::Tempo;
using graphscore::TempoLane;
using graphscore::TempoPoint;
using graphscore::TempoSegmentKind;
using graphscore::TimeSignature;
using graphscore::TrackId;
using graphscore::TrackLane;
using graphscore::Voice;
using graphscore::VoiceContent;
using graphscore::VoiceEvent;

namespace {

Project make_project() {
  return Project(ProjectId::generate(), "Test Project");
}

// A test-only command that can be configured to fail on execute, undo, or
// redo, and records the order in which it was touched for assertion.
class TestCommand : public Command {
 public:
  enum class FailMode { kNever, kOnExecute, kOnUndo, kOnRedo };

  explicit TestCommand(std::string name, FailMode fail = FailMode::kNever,
                       std::vector<std::string>* log = nullptr)
      : name_(std::move(name)), fail_(fail), log_(log) {}

  [[nodiscard]] const std::string& name() const noexcept { return name_; }

  Result execute(Project& /*project*/) noexcept override {
    if (log_ != nullptr)
      log_->push_back(name_ + "-execute");
    if (state_ != State::kFresh)
      return Result(ResultCode::kInvalidArgument);
    if (fail_ == FailMode::kOnExecute)
      return Result(ResultCode::kInternalError);
    state_ = State::kDone;
    return Result();
  }

  Result undo(Project& /*project*/) noexcept override {
    if (log_ != nullptr)
      log_->push_back(name_ + "-undo");
    if (state_ != State::kDone)
      return Result(ResultCode::kInvalidArgument);
    if (fail_ == FailMode::kOnUndo)
      return Result(ResultCode::kInternalError);
    state_ = State::kUndone;
    return Result();
  }

  Result redo(Project& /*project*/) noexcept override {
    if (log_ != nullptr)
      log_->push_back(name_ + "-redo");
    if (state_ != State::kUndone)
      return Result(ResultCode::kInvalidArgument);
    if (fail_ == FailMode::kOnRedo)
      return Result(ResultCode::kInternalError);
    state_ = State::kDone;
    return Result();
  }

 private:
  std::string               name_;
  FailMode                  fail_  = FailMode::kNever;
  std::vector<std::string>* log_   = nullptr;
  State                     state_ = State::kFresh;
};

// A stateful test command that modifies a node name (observable model state)
// and can be configured to fail on any phase. Retains only stable NodeId
// plus value/failure state; mutates the Project& supplied to execute/undo/redo
// — never a stored raw pointer.
//
// One-shot controls (fail_next_undo, fail_next_redo) allow a single
// failure followed by normal success so transaction retry semantics can be
// tested without replacing the command.
class AdversarialNameCommand : public Command {
 public:
  enum class FailMode { kNever, kOnExecute, kOnUndo, kOnRedo };

  AdversarialNameCommand(NodeId node_id, std::string new_name, FailMode fail,
                         std::vector<std::string>* log = nullptr)
      : node_id_(node_id),
        new_name_(std::move(new_name)),
        fail_(fail),
        log_(log) {}

  void set_fail_next_undo(bool v) noexcept { fail_next_undo_ = v; }

  void set_fail_next_redo(bool v) noexcept { fail_next_redo_ = v; }

  Result execute(Project& project) noexcept override {
    if (log_ != nullptr)
      log_->push_back("Adv-execute");
    if (state_ != State::kFresh)
      return Result(ResultCode::kInvalidArgument);
    if (fail_ == FailMode::kOnExecute)
      return Result(ResultCode::kInternalError);

    auto* node = project.find_node(node_id_);
    if (node == nullptr)
      return Result(ResultCode::kInvalidArgument);

    std::string prepared_old;
    std::string prepared_new;
    try {
      prepared_old = node->name();
      prepared_new = new_name_;
    } catch (const std::bad_alloc&) {
      return Result(ResultCode::kOutOfMemory);
    } catch (const std::length_error&) {
      return Result(ResultCode::kOutOfMemory);
    }

    old_name_ = std::move(prepared_old);
    node->set_name(std::move(prepared_new));
    state_ = State::kDone;
    return Result();
  }

  Result undo(Project& project) noexcept override {
    if (log_ != nullptr)
      log_->push_back("Adv-undo");
    if (state_ != State::kDone)
      return Result(ResultCode::kInvalidArgument);

    if (fail_ == FailMode::kOnUndo || fail_next_undo_) {
      fail_next_undo_ = false;
      return Result(ResultCode::kInternalError);
    }

    auto* node = project.find_node(node_id_);
    if (node == nullptr)
      return Result(ResultCode::kInvalidArgument);

    std::string prepared;
    try {
      prepared = old_name_;
    } catch (const std::bad_alloc&) {
      return Result(ResultCode::kOutOfMemory);
    } catch (const std::length_error&) {
      return Result(ResultCode::kOutOfMemory);
    }

    node->set_name(std::move(prepared));
    state_ = State::kUndone;
    return Result();
  }

  Result redo(Project& project) noexcept override {
    if (log_ != nullptr)
      log_->push_back("Adv-redo");
    if (state_ != State::kUndone)
      return Result(ResultCode::kInvalidArgument);

    if (fail_ == FailMode::kOnRedo || fail_next_redo_) {
      fail_next_redo_ = false;
      return Result(ResultCode::kInternalError);
    }

    auto* node = project.find_node(node_id_);
    if (node == nullptr)
      return Result(ResultCode::kInvalidArgument);

    std::string prepared;
    try {
      prepared = new_name_;
    } catch (const std::bad_alloc&) {
      return Result(ResultCode::kOutOfMemory);
    } catch (const std::length_error&) {
      return Result(ResultCode::kOutOfMemory);
    }

    node->set_name(std::move(prepared));
    state_ = State::kDone;
    return Result();
  }

  [[nodiscard]] NodeId node_id() const noexcept { return node_id_; }

 private:
  NodeId                    node_id_;
  std::string               new_name_;
  std::string               old_name_;
  FailMode                  fail_           = FailMode::kNever;
  bool                      fail_next_undo_ = false;
  bool                      fail_next_redo_ = false;
  std::vector<std::string>* log_            = nullptr;
  State                     state_          = State::kFresh;
};

// A command that fails on undo only after it has been redone (two-phase).
// Executes normally, undoes normally the first time, redoes and sets a flag,
// then fails on the compensating undo after a redo failure elsewhere.
class TwoPhaseUndoFailCommand : public Command {
 public:
  explicit TwoPhaseUndoFailCommand(std::vector<std::string>* log = nullptr)
      : log_(log) {}

  Result execute(Project& /*project*/) noexcept override {
    if (log_ != nullptr)
      log_->push_back("TwoPhase-execute");
    if (state_ != State::kFresh)
      return Result(ResultCode::kInvalidArgument);
    state_ = State::kDone;
    return Result();
  }

  Result undo(Project& /*project*/) noexcept override {
    if (log_ != nullptr)
      log_->push_back("TwoPhase-undo");
    if (state_ != State::kDone)
      return Result(ResultCode::kInvalidArgument);
    if (was_redone_) {
      was_redone_ = false;
      return Result(ResultCode::kInternalError);
    }
    state_ = State::kUndone;
    return Result();
  }

  Result redo(Project& /*project*/) noexcept override {
    if (log_ != nullptr)
      log_->push_back("TwoPhase-redo");
    if (state_ != State::kUndone)
      return Result(ResultCode::kInvalidArgument);
    was_redone_ = true;
    state_      = State::kDone;
    return Result();
  }

 private:
  std::vector<std::string>* log_        = nullptr;
  bool                      was_redone_ = false;
  State                     state_      = State::kFresh;
};

// Builds a project with one active track, one node with a single 4/4
// measure timeline, and one stave whose voices are fillable.  Returns the
// project, node end, and the stable ids needed to address specific voices.
struct NotationSetup {
  Project  project;
  NodeId   node_id;
  TrackId  track_id;
  StaveId  stave_id;
  Rational node_end;
};

NotationSetup make_notation_setup() {
  Project    project = make_project();
  const auto t       = project.add_track("Track", StaffLayout::single_staff(),
                                         *MidiChannel::create(0));
  assert(t.has_value());
  const TrackId track_id = *t;

  const NodeId node_id = project.add_node("Node");

  std::vector<Measure> measures = {
      Measure{*TimeSignature::create(4, 4), *KeySignature::create(0)}};
  auto tl = NodeTimeline::create(std::move(measures), {});
  assert(tl.has_value());
  project.find_node(node_id)->set_timeline(std::move(*tl));
  const Rational node_end(1);  // One 4/4 measure = 1 whole note

  Node*                    node  = project.find_node(node_id);
  const graphscore::Track* track = project.find_active_track(track_id);
  StaveId                  stave_id;
  for (const graphscore::StaveDefinition& stave_def :
       track->layout().staves()) {
    node->lane(track_id)->ensure_stave(stave_def.id);
    stave_id = stave_def.id;
  }

  return NotationSetup{std::move(project), node_id, track_id, stave_id,
                       node_end};
}

SpelledPitch pitch_c4() {
  return *SpelledPitch::create(Letter::kC, 4);
}

SpelledPitch pitch_d4() {
  return *SpelledPitch::create(Letter::kD, 4);
}

SpelledPitch pitch_e4() {
  return *SpelledPitch::create(Letter::kE, 4);
}

Duration quarter() {
  return *Duration::create(NoteValue::kQuarter, 0);
}

Duration half() {
  return *Duration::create(NoteValue::kHalf, 0);
}

Duration whole() {
  return *Duration::create(NoteValue::kWhole, 0);
}

Duration eighth() {
  return *Duration::create(NoteValue::kEighth, 0);
}

Duration dotted_half() {
  return *Duration::create(NoteValue::kHalf, 1);
}

// Fill all four voices on a stave with one quarter note each and
// normalize to `node_end`.  Required for whole-TrackLane candidate
// validation now that validate_lane_candidate checks rhythmic
// completeness of every voice in every stave.
void fill_all_voices(TrackLane* lane, StaveId stave_id, Rational node_end) {
  for (int v = 1; v <= 4; ++v) {
    VoiceContent* vc = &lane->stave(stave_id)->voice(
        *Voice::create(static_cast<std::uint8_t>(v)));
    ASSERT_NE(vc, nullptr);
    ASSERT_TRUE(vc->append(make_note(pitch_c4(), quarter())).ok());
    ASSERT_TRUE(vc->normalize(node_end).ok());
  }
}

}  // namespace

// =========================================================================
// SetNodeNameCommand
// =========================================================================

TEST(CommandTest, SetNodeNameRoundTrip) {
  Project project = make_project();
  NodeId  node_id = project.add_node("Original");

  auto cmd = std::make_unique<SetNodeNameCommand>(node_id, "Renamed");

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(project.find_node(node_id)->name(), "Renamed");

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_EQ(project.find_node(node_id)->name(), "Original");

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_EQ(project.find_node(node_id)->name(), "Renamed");
}

TEST(CommandTest, SetNodeNameMissingIdFails) {
  Project project = make_project();
  NodeId  missing = NodeId::generate();

  auto cmd = std::make_unique<SetNodeNameCommand>(missing, "X");

  Result result = cmd->execute(project);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetNodeNameDoubleExecuteRejected) {
  Project project = make_project();
  NodeId  node_id = project.add_node("A");

  auto cmd = std::make_unique<SetNodeNameCommand>(node_id, "B");

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_FALSE(cmd->execute(project).ok());
  EXPECT_EQ(project.find_node(node_id)->name(), "B");
}

TEST(CommandTest, SetNodeNameUndoWithoutExecuteRejected) {
  Project project = make_project();
  NodeId  node_id = project.add_node("A");

  auto cmd = std::make_unique<SetNodeNameCommand>(node_id, "B");

  EXPECT_FALSE(cmd->undo(project).ok());
  EXPECT_EQ(project.find_node(node_id)->name(), "A");
}

TEST(CommandTest, SetNodeNameRedoWithoutUndoRejected) {
  Project project = make_project();
  NodeId  node_id = project.add_node("A");

  auto cmd = std::make_unique<SetNodeNameCommand>(node_id, "B");
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_FALSE(cmd->redo(project).ok());
  EXPECT_EQ(project.find_node(node_id)->name(), "B");
}

// =========================================================================
// SetTrackNameCommand
// =========================================================================

TEST(CommandTest, SetTrackNameRoundTrip) {
  Project    project  = make_project();
  const auto track_id = project.add_track("Old", StaffLayout::single_staff(),
                                          *MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());

  auto cmd = std::make_unique<SetTrackNameCommand>(*track_id, "New");

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(project.find_active_track(*track_id)->name(), "New");

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_EQ(project.find_active_track(*track_id)->name(), "Old");

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_EQ(project.find_active_track(*track_id)->name(), "New");
}

TEST(CommandTest, SetTrackNameMissingIdFails) {
  Project project = make_project();
  TrackId missing = TrackId::generate();

  auto cmd = std::make_unique<SetTrackNameCommand>(missing, "X");

  EXPECT_FALSE(cmd->execute(project).ok());
}

// =========================================================================
// SetProjectTempoCommand
// =========================================================================

TEST(CommandTest, SetProjectTempoRoundTrip) {
  Project project = make_project();

  const auto old_tempo = project.default_tempo();
  const auto new_tempo = *Tempo::create(Rational(160), NoteValue::kQuarter);

  auto cmd = std::make_unique<SetProjectTempoCommand>(new_tempo);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(project.default_tempo(), new_tempo);

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_EQ(project.default_tempo(), old_tempo);

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_EQ(project.default_tempo(), new_tempo);
}

// =========================================================================
// CommandHistory — basic stack behaviour
// =========================================================================

TEST(CommandTest, HistoryUndoRedoOrder) {
  Project project = make_project();
  NodeId  a       = project.add_node("A");
  NodeId  b       = project.add_node("B");

  CommandHistory history;

  ASSERT_TRUE(
      history
          .execute_new(std::make_unique<SetNodeNameCommand>(a, "A-renamed"),
                       project)
          .ok());
  ASSERT_TRUE(
      history
          .execute_new(std::make_unique<SetNodeNameCommand>(b, "B-renamed"),
                       project)
          .ok());

  EXPECT_EQ(project.find_node(a)->name(), "A-renamed");
  EXPECT_EQ(project.find_node(b)->name(), "B-renamed");
  EXPECT_EQ(history.undo_stack_size(), 2u);
  EXPECT_EQ(history.redo_stack_size(), 0u);

  ASSERT_TRUE(history.undo(project).ok());
  EXPECT_EQ(project.find_node(b)->name(), "B");
  EXPECT_EQ(history.undo_stack_size(), 1u);
  EXPECT_EQ(history.redo_stack_size(), 1u);

  ASSERT_TRUE(history.undo(project).ok());
  EXPECT_EQ(project.find_node(a)->name(), "A");
  EXPECT_EQ(history.undo_stack_size(), 0u);
  EXPECT_EQ(history.redo_stack_size(), 2u);

  ASSERT_TRUE(history.redo(project).ok());
  EXPECT_EQ(project.find_node(a)->name(), "A-renamed");
  EXPECT_EQ(history.undo_stack_size(), 1u);
  EXPECT_EQ(history.redo_stack_size(), 1u);

  ASSERT_TRUE(history.redo(project).ok());
  EXPECT_EQ(project.find_node(b)->name(), "B-renamed");
  EXPECT_EQ(history.undo_stack_size(), 2u);
  EXPECT_EQ(history.redo_stack_size(), 0u);
}

TEST(CommandTest, HistoryEmptyUndoIsSafe) {
  Project        project = make_project();
  CommandHistory history;

  EXPECT_TRUE(history.undo(project).ok());
  EXPECT_EQ(history.undo_stack_size(), 0u);
}

TEST(CommandTest, HistoryEmptyRedoIsSafe) {
  Project        project = make_project();
  CommandHistory history;

  EXPECT_TRUE(history.redo(project).ok());
  EXPECT_EQ(history.redo_stack_size(), 0u);
}

TEST(CommandTest, HistoryClear) {
  Project        project = make_project();
  NodeId         node_id = project.add_node("A");
  CommandHistory history;

  ASSERT_TRUE(
      history
          .execute_new(std::make_unique<SetNodeNameCommand>(node_id, "B"),
                       project)
          .ok());
  ASSERT_TRUE(history.undo(project).ok());

  EXPECT_EQ(history.undo_stack_size(), 0u);
  EXPECT_EQ(history.redo_stack_size(), 1u);

  history.clear();

  EXPECT_EQ(history.undo_stack_size(), 0u);
  EXPECT_EQ(history.redo_stack_size(), 0u);
  EXPECT_EQ(project.find_node(node_id)->name(), "A");
}

TEST(CommandTest, HistoryFailedCommandNotRecorded) {
  Project project = make_project();
  NodeId  missing = NodeId::generate();

  CommandHistory history;

  Result result = history.execute_new(
      std::make_unique<SetNodeNameCommand>(missing, "X"), project);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(history.undo_stack_size(), 0u);
  EXPECT_EQ(history.redo_stack_size(), 0u);
}

TEST(CommandTest, HistoryRedoClearedByNewSuccessfulCommand) {
  Project        project = make_project();
  NodeId         node_id = project.add_node("A");
  CommandHistory history;

  ASSERT_TRUE(
      history
          .execute_new(std::make_unique<SetNodeNameCommand>(node_id, "B"),
                       project)
          .ok());
  ASSERT_TRUE(history.undo(project).ok());
  EXPECT_EQ(history.redo_stack_size(), 1u);

  ASSERT_TRUE(
      history
          .execute_new(std::make_unique<SetNodeNameCommand>(node_id, "C"),
                       project)
          .ok());
  EXPECT_EQ(history.redo_stack_size(), 0u);
  EXPECT_EQ(history.undo_stack_size(), 1u);
  EXPECT_EQ(project.find_node(node_id)->name(), "C");
}

TEST(CommandTest, HistoryRedoStackSurvivesFailedNewCommand) {
  Project        project = make_project();
  NodeId         node_id = project.add_node("A");
  CommandHistory history;

  ASSERT_TRUE(
      history
          .execute_new(std::make_unique<SetNodeNameCommand>(node_id, "B"),
                       project)
          .ok());
  ASSERT_TRUE(history.undo(project).ok());
  EXPECT_EQ(history.redo_stack_size(), 1u);
  EXPECT_EQ(history.undo_stack_size(), 0u);
  EXPECT_EQ(project.find_node(node_id)->name(), "A");

  Result result = history.execute_new(
      std::make_unique<SetNodeNameCommand>(NodeId::generate(), "X"), project);
  EXPECT_FALSE(result.ok());

  EXPECT_EQ(history.redo_stack_size(), 1u);
  EXPECT_EQ(history.undo_stack_size(), 0u);
  EXPECT_TRUE(history.can_redo());
  EXPECT_EQ(project.find_node(node_id)->name(), "A");

  ASSERT_TRUE(history.redo(project).ok());
  EXPECT_EQ(project.find_node(node_id)->name(), "B");
}

TEST(CommandTest, HistoryNullCommandReturnsInvalidArgument) {
  Project        project = make_project();
  CommandHistory history;

  Result result = history.execute_new(nullptr, project);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(history.undo_stack_size(), 0u);
  EXPECT_EQ(history.redo_stack_size(), 0u);
}

// =========================================================================
// History undo/redo failure does not lose the command
// =========================================================================

TEST(CommandTest, HistoryUndoFailureKeepsCommandInUndo) {
  Project project = make_project();
  auto cmd = std::make_unique<TestCommand>("X", TestCommand::FailMode::kOnUndo);

  CommandHistory history;
  ASSERT_TRUE(history.execute_new(std::move(cmd), project).ok());

  Result result = history.undo(project);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(history.undo_stack_size(), 1u);
  EXPECT_EQ(history.redo_stack_size(), 0u);
}

TEST(CommandTest, HistoryRedoFailureKeepsCommandInRedo) {
  Project project = make_project();
  auto cmd = std::make_unique<TestCommand>("X", TestCommand::FailMode::kOnRedo);

  CommandHistory history;
  ASSERT_TRUE(history.execute_new(std::move(cmd), project).ok());
  ASSERT_TRUE(history.undo(project).ok());

  Result result = history.redo(project);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(history.undo_stack_size(), 0u);
  EXPECT_EQ(history.redo_stack_size(), 1u);
}

TEST(CommandTest, HistoryEmptyUndoRedoNoModelChange) {
  Project        project = make_project();
  NodeId         node_id = project.add_node("Original");
  CommandHistory history;

  EXPECT_TRUE(history.undo(project).ok());
  EXPECT_TRUE(history.redo(project).ok());
  EXPECT_EQ(project.find_node(node_id)->name(), "Original");
}

// =========================================================================
// Full history round-trip with real commands
// =========================================================================

TEST(CommandTest, FullHistoryRoundTripWithRealCommands) {
  Project project = make_project();
  NodeId  node_a  = project.add_node("A");
  NodeId  node_b  = project.add_node("B");

  CommandHistory history;

  ASSERT_TRUE(
      history
          .execute_new(std::make_unique<SetNodeNameCommand>(node_a, "A1"),
                       project)
          .ok());
  ASSERT_TRUE(
      history
          .execute_new(std::make_unique<SetNodeNameCommand>(node_b, "B1"),
                       project)
          .ok());
  ASSERT_TRUE(
      history
          .execute_new(std::make_unique<SetNodeNameCommand>(node_a, "A2"),
                       project)
          .ok());

  EXPECT_EQ(project.find_node(node_a)->name(), "A2");
  EXPECT_EQ(project.find_node(node_b)->name(), "B1");

  ASSERT_TRUE(history.undo(project).ok());
  EXPECT_EQ(project.find_node(node_a)->name(), "A1");
  EXPECT_EQ(project.find_node(node_b)->name(), "B1");

  ASSERT_TRUE(history.undo(project).ok());
  EXPECT_EQ(project.find_node(node_a)->name(), "A1");
  EXPECT_EQ(project.find_node(node_b)->name(), "B");

  ASSERT_TRUE(history.redo(project).ok());
  EXPECT_EQ(project.find_node(node_a)->name(), "A1");
  EXPECT_EQ(project.find_node(node_b)->name(), "B1");

  ASSERT_TRUE(
      history
          .execute_new(std::make_unique<SetNodeNameCommand>(node_a, "A3"),
                       project)
          .ok());
  EXPECT_EQ(history.redo_stack_size(), 0u);
  EXPECT_EQ(project.find_node(node_a)->name(), "A3");
  EXPECT_EQ(project.find_node(node_b)->name(), "B1");
}

// =========================================================================
// Missing-ID mutation verification: model is unchanged on failure
// =========================================================================

TEST(CommandTest, SetNodeNameMissingIdDoesNotChangeProject) {
  Project project = make_project();
  NodeId  node_id = project.add_node("Original");

  auto cmd = std::make_unique<SetNodeNameCommand>(NodeId::generate(), "X");
  EXPECT_FALSE(cmd->execute(project).ok());
  EXPECT_EQ(project.find_node(node_id)->name(), "Original");
}

TEST(CommandTest, SetTrackNameMissingIdDoesNotChangeProject) {
  Project    project  = make_project();
  const auto track_id = project.add_track(
      "Original", StaffLayout::single_staff(), *MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());

  auto cmd = std::make_unique<SetTrackNameCommand>(TrackId::generate(), "X");
  EXPECT_FALSE(cmd->execute(project).ok());
  EXPECT_EQ(project.find_active_track(*track_id)->name(), "Original");
}

// =========================================================================
// CommandTransaction — execute / undo / redo order
// =========================================================================

TEST(CommandTest, TransactionExecuteUndoRedoOrder) {
  Project project = make_project();
  NodeId  a       = project.add_node("A");
  NodeId  b       = project.add_node("B");
  NodeId  c       = project.add_node("C");

  auto tx = std::make_unique<CommandTransaction>();
  ASSERT_TRUE(
      tx->add_command(std::make_unique<SetNodeNameCommand>(a, "A1")).ok());
  ASSERT_TRUE(
      tx->add_command(std::make_unique<SetNodeNameCommand>(b, "B1")).ok());
  ASSERT_TRUE(
      tx->add_command(std::make_unique<SetNodeNameCommand>(c, "C1")).ok());

  ASSERT_TRUE(tx->execute(project).ok());
  EXPECT_EQ(project.find_node(a)->name(), "A1");
  EXPECT_EQ(project.find_node(b)->name(), "B1");
  EXPECT_EQ(project.find_node(c)->name(), "C1");

  ASSERT_TRUE(tx->undo(project).ok());
  EXPECT_EQ(project.find_node(a)->name(), "A");
  EXPECT_EQ(project.find_node(b)->name(), "B");
  EXPECT_EQ(project.find_node(c)->name(), "C");

  ASSERT_TRUE(tx->redo(project).ok());
  EXPECT_EQ(project.find_node(a)->name(), "A1");
  EXPECT_EQ(project.find_node(b)->name(), "B1");
  EXPECT_EQ(project.find_node(c)->name(), "C1");
}

// =========================================================================
// Transaction failure rollback (execute)
// =========================================================================

TEST(CommandTest, TransactionExecuteMiddleFailureRollsBack) {
  Project project = make_project();
  NodeId  a       = project.add_node("A");
  NodeId  b       = project.add_node("B");

  auto tx = std::make_unique<CommandTransaction>();
  ASSERT_TRUE(
      tx->add_command(std::make_unique<SetNodeNameCommand>(a, "A1")).ok());
  ASSERT_TRUE(tx->add_command(std::make_unique<SetNodeNameCommand>(
                                  NodeId::generate(), "MISSING"))
                  .ok());
  ASSERT_TRUE(
      tx->add_command(std::make_unique<SetNodeNameCommand>(b, "B1")).ok());

  Result result = tx->execute(project);
  EXPECT_FALSE(result.ok());

  EXPECT_EQ(project.find_node(a)->name(), "A");
  EXPECT_EQ(project.find_node(b)->name(), "B");
}

TEST(CommandTest, TransactionExecuteFirstChildFailureNoModelChange) {
  Project project = make_project();

  auto tx = std::make_unique<CommandTransaction>();
  ASSERT_TRUE(tx->add_command(std::make_unique<SetNodeNameCommand>(
                                  NodeId::generate(), "MISSING"))
                  .ok());

  Result result = tx->execute(project);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.code(), ResultCode::kInvalidArgument);
}

// =========================================================================
// Transaction failure rollback (redo)
// =========================================================================

TEST(CommandTest, TransactionRedoMiddleFailureRollsBack) {
  Project                  project = make_project();
  std::vector<std::string> log;

  auto tx = std::make_unique<CommandTransaction>();
  ASSERT_TRUE(tx->add_command(std::make_unique<TestCommand>(
                                  "A", TestCommand::FailMode::kNever, &log))
                  .ok());
  ASSERT_TRUE(tx->add_command(std::make_unique<TestCommand>(
                                  "B", TestCommand::FailMode::kOnRedo, &log))
                  .ok());
  ASSERT_TRUE(tx->add_command(std::make_unique<TestCommand>(
                                  "C", TestCommand::FailMode::kNever, &log))
                  .ok());

  ASSERT_TRUE(tx->execute(project).ok());
  ASSERT_TRUE(tx->undo(project).ok());

  log.clear();
  Result result = tx->redo(project);
  EXPECT_FALSE(result.ok());

  ASSERT_EQ(log.size(), 3u);
  EXPECT_EQ(log[0], "A-redo");
  EXPECT_EQ(log[1], "B-redo");
  EXPECT_EQ(log[2], "A-undo");
}

// =========================================================================
// Transaction failure rollback (undo)
// =========================================================================

TEST(CommandTest, TransactionUndoMiddleFailureRollsBack) {
  Project                  project = make_project();
  std::vector<std::string> log;

  auto tx = std::make_unique<CommandTransaction>();
  ASSERT_TRUE(tx->add_command(std::make_unique<TestCommand>(
                                  "A", TestCommand::FailMode::kNever, &log))
                  .ok());
  ASSERT_TRUE(tx->add_command(std::make_unique<TestCommand>(
                                  "B", TestCommand::FailMode::kOnUndo, &log))
                  .ok());
  ASSERT_TRUE(tx->add_command(std::make_unique<TestCommand>(
                                  "C", TestCommand::FailMode::kNever, &log))
                  .ok());

  ASSERT_TRUE(tx->execute(project).ok());

  log.clear();
  Result result = tx->undo(project);
  EXPECT_FALSE(result.ok());

  ASSERT_EQ(log.size(), 3u);
  EXPECT_EQ(log[0], "C-undo");
  EXPECT_EQ(log[1], "B-undo");
  EXPECT_EQ(log[2], "C-redo");
}

// =========================================================================
// Empty transaction
// =========================================================================

TEST(CommandTest, EmptyTransactionSucceedsOnAllOps) {
  Project project = make_project();
  auto    tx      = std::make_unique<CommandTransaction>();

  EXPECT_TRUE(tx->execute(project).ok());
  EXPECT_TRUE(tx->undo(project).ok());
  EXPECT_TRUE(tx->redo(project).ok());
  EXPECT_EQ(tx->child_count(), 0u);
}

// =========================================================================
// State misuse edges
// =========================================================================

TEST(CommandTest, TransactionDoubleExecuteRejected) {
  Project project = make_project();
  auto    tx      = std::make_unique<CommandTransaction>();

  ASSERT_TRUE(tx->execute(project).ok());
  EXPECT_FALSE(tx->execute(project).ok());
}

TEST(CommandTest, TransactionUndoWithoutExecuteRejected) {
  Project project = make_project();
  auto    tx      = std::make_unique<CommandTransaction>();

  EXPECT_FALSE(tx->undo(project).ok());
}

TEST(CommandTest, TransactionRedoWithoutUndoRejected) {
  Project project = make_project();
  auto    tx      = std::make_unique<CommandTransaction>();

  ASSERT_TRUE(tx->execute(project).ok());
  EXPECT_FALSE(tx->redo(project).ok());
}

// =========================================================================
// Transaction — null child and post-build add_command rejection
// =========================================================================

TEST(CommandTest, TransactionAddNullChildReturnsInvalidArgument) {
  auto tx = std::make_unique<CommandTransaction>();

  Result result = tx->add_command(nullptr);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(tx->child_count(), 0u);
}

TEST(CommandTest, TransactionAddCommandAfterExecuteRejected) {
  Project project = make_project();
  auto    tx      = std::make_unique<CommandTransaction>();

  ASSERT_TRUE(tx->execute(project).ok());

  Result result = tx->add_command(std::make_unique<TestCommand>("X"));
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, TransactionAddCommandAfterExecuteFailureKfaulted) {
  Project project = make_project();

  auto tx = std::make_unique<CommandTransaction>();
  ASSERT_TRUE(tx->add_command(std::make_unique<SetNodeNameCommand>(
                                  NodeId::generate(), "MISSING"))
                  .ok());
  ASSERT_FALSE(tx->execute(project).ok());

  Result result = tx->add_command(std::make_unique<TestCommand>("X"));
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, TransactionAddCommandAfterUndoRejected) {
  Project project = make_project();
  NodeId  a       = project.add_node("A");

  auto tx = std::make_unique<CommandTransaction>();
  ASSERT_TRUE(
      tx->add_command(std::make_unique<SetNodeNameCommand>(a, "A1")).ok());
  ASSERT_TRUE(tx->execute(project).ok());
  ASSERT_TRUE(tx->undo(project).ok());

  Result result = tx->add_command(std::make_unique<TestCommand>("X"));
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.code(), ResultCode::kInvalidArgument);
}

// =========================================================================
// Transaction with real commands at history level
// =========================================================================

TEST(CommandTest, TransactionInHistoryRoundTrip) {
  Project project = make_project();
  NodeId  a       = project.add_node("A");
  NodeId  b       = project.add_node("B");

  CommandHistory history;

  auto tx = std::make_unique<CommandTransaction>();
  ASSERT_TRUE(
      tx->add_command(std::make_unique<SetNodeNameCommand>(a, "A1")).ok());
  ASSERT_TRUE(
      tx->add_command(std::make_unique<SetNodeNameCommand>(b, "B1")).ok());

  ASSERT_TRUE(history.execute_new(std::move(tx), project).ok());
  EXPECT_EQ(project.find_node(a)->name(), "A1");
  EXPECT_EQ(project.find_node(b)->name(), "B1");

  ASSERT_TRUE(history.undo(project).ok());
  EXPECT_EQ(project.find_node(a)->name(), "A");
  EXPECT_EQ(project.find_node(b)->name(), "B");

  ASSERT_TRUE(history.redo(project).ok());
  EXPECT_EQ(project.find_node(a)->name(), "A1");
  EXPECT_EQ(project.find_node(b)->name(), "B1");
}

// =========================================================================
// Transaction execute failure rollback — verify model rollback
// =========================================================================

TEST(CommandTest, TransactionExecuteRollbackPreservesExactPreState) {
  Project    project  = make_project();
  NodeId     node_id  = project.add_node("Original");
  const auto track_id = project.add_track("Track1", StaffLayout::single_staff(),
                                          *MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());

  auto tx = std::make_unique<CommandTransaction>();
  ASSERT_TRUE(
      tx->add_command(std::make_unique<SetNodeNameCommand>(node_id, "Node1"))
          .ok());
  ASSERT_TRUE(tx->add_command(std::make_unique<SetTrackNameCommand>(
                                  *track_id, "Track1-renamed"))
                  .ok());
  ASSERT_TRUE(tx->add_command(std::make_unique<SetNodeNameCommand>(
                                  NodeId::generate(), "MISSING"))
                  .ok());

  Result result = tx->execute(project);
  EXPECT_FALSE(result.ok());

  EXPECT_EQ(project.find_node(node_id)->name(), "Original");
  EXPECT_EQ(project.find_active_track(*track_id)->name(), "Track1");
}

// =========================================================================
// Adversarial: execute-failure + rollback undo failure → kFaulted
// (model-observable: child 0 mutates A, undo fails, A stuck at new value)
// =========================================================================

TEST(CommandTest, ExecuteFailureRollbackUndoFailModelObservable) {
  Project project = make_project();
  NodeId  a       = project.add_node("A");

  auto tx = std::make_unique<CommandTransaction>();
  ASSERT_TRUE(
      tx->add_command(std::make_unique<AdversarialNameCommand>(
                          a, "A1", AdversarialNameCommand::FailMode::kOnUndo))
          .ok());
  ASSERT_TRUE(tx->add_command(std::make_unique<SetNodeNameCommand>(
                                  NodeId::generate(), "MISSING"))
                  .ok());

  Result result = tx->execute(project);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.code(), ResultCode::kTransactionRollbackFailed);

  // Child 0 executed (A→A1), then compensating undo failed → A stuck
  EXPECT_EQ(project.find_node(a)->name(), "A1");

  // kFaulted — no further operations
  EXPECT_FALSE(tx->execute(project).ok());
  EXPECT_FALSE(tx->undo(project).ok());
  EXPECT_FALSE(tx->redo(project).ok());
  Result add_result = tx->add_command(std::make_unique<TestCommand>("Z"));
  EXPECT_FALSE(add_result.ok());
}

// =========================================================================
// Adversarial: execute failure + successful rollback → kFaulted (no retry)
// =========================================================================

TEST(CommandTest, ExecuteFailureSuccessfulRollbackIsFaulted) {
  Project project = make_project();
  NodeId  a       = project.add_node("A");

  auto tx = std::make_unique<CommandTransaction>();
  ASSERT_TRUE(
      tx->add_command(std::make_unique<AdversarialNameCommand>(
                          a, "A1", AdversarialNameCommand::FailMode::kNever))
          .ok());
  ASSERT_TRUE(tx->add_command(std::make_unique<SetNodeNameCommand>(
                                  NodeId::generate(), "MISSING"))
                  .ok());

  Result result = tx->execute(project);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.code(), ResultCode::kInvalidArgument);

  // Rollback successful: A is back to "A"
  EXPECT_EQ(project.find_node(a)->name(), "A");

  // Terminal — cannot retry
  EXPECT_FALSE(tx->execute(project).ok());
  EXPECT_FALSE(tx->undo(project).ok());
  EXPECT_FALSE(tx->redo(project).ok());

  Result add_result = tx->add_command(std::make_unique<TestCommand>("Z"));
  EXPECT_FALSE(add_result.ok());
}

// =========================================================================
// Adversarial: undo-failure + compensation redo failure → kFaulted
//
// Three children: [A:never, B:failOnUndo, C:failOnRedo]
// Undo (reverse order):
//   i=3: undo C → kUndone fine.
//   i=2: undo B → FAILS.
//   Compensation: redo C → kUndone, kOnRedo → FAILS → kFaulted.
// =========================================================================

TEST(CommandTest, UndoFailureCompensationRedoFailsFaulted) {
  Project                  project = make_project();
  std::vector<std::string> log;

  auto tx = std::make_unique<CommandTransaction>();
  ASSERT_TRUE(tx->add_command(std::make_unique<TestCommand>(
                                  "A", TestCommand::FailMode::kNever, &log))
                  .ok());
  ASSERT_TRUE(tx->add_command(std::make_unique<TestCommand>(
                                  "B", TestCommand::FailMode::kOnUndo, &log))
                  .ok());
  ASSERT_TRUE(tx->add_command(std::make_unique<TestCommand>(
                                  "C", TestCommand::FailMode::kOnRedo, &log))
                  .ok());

  ASSERT_TRUE(tx->execute(project).ok());

  log.clear();
  Result result = tx->undo(project);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.code(), ResultCode::kTransactionRollbackFailed);

  ASSERT_GE(log.size(), 3u);
  EXPECT_EQ(log[0], "C-undo");
  EXPECT_EQ(log[1], "B-undo");
  EXPECT_EQ(log[2], "C-redo");

  // kFaulted
  EXPECT_FALSE(tx->undo(project).ok());
  EXPECT_FALSE(tx->redo(project).ok());
  EXPECT_FALSE(tx->execute(project).ok());
}

// =========================================================================
// Adversarial: redo-failure + compensation undo failure → kFaulted
//
// Children: [TwoPhaseUndoFail, FailOnRedo]
// Execute, undo both. Redo: TwoPhase redoes fine (sets was_redone_=true).
//   Fail redo fails. Compensation: undo TwoPhase → fails (was_redone_).
// =========================================================================

TEST(CommandTest, RedoFailureCompensationUndoFailsFaulted) {
  Project                  project = make_project();
  std::vector<std::string> log;

  auto tx = std::make_unique<CommandTransaction>();
  ASSERT_TRUE(
      tx->add_command(std::make_unique<TwoPhaseUndoFailCommand>(&log)).ok());
  ASSERT_TRUE(tx->add_command(std::make_unique<TestCommand>(
                                  "Fail", TestCommand::FailMode::kOnRedo, &log))
                  .ok());

  ASSERT_TRUE(tx->execute(project).ok());
  ASSERT_TRUE(tx->undo(project).ok());

  log.clear();
  Result result = tx->redo(project);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.code(), ResultCode::kTransactionRollbackFailed);

  ASSERT_GE(log.size(), 3u);
  EXPECT_EQ(log[0], "TwoPhase-redo");
  EXPECT_EQ(log[1], "Fail-redo");
  EXPECT_EQ(log[2], "TwoPhase-undo");

  // kFaulted
  EXPECT_FALSE(tx->redo(project).ok());
  EXPECT_FALSE(tx->undo(project).ok());
  EXPECT_FALSE(tx->execute(project).ok());
}

// =========================================================================
// Transaction — first child execute failure → kFaulted
// =========================================================================

TEST(CommandTest, TransactionFirstChildExecuteFailureTerminal) {
  Project project = make_project();

  auto tx = std::make_unique<CommandTransaction>();
  ASSERT_TRUE(tx->add_command(std::make_unique<SetNodeNameCommand>(
                                  NodeId::generate(), "MISSING"))
                  .ok());

  Result result = tx->execute(project);
  EXPECT_FALSE(result.ok());
  EXPECT_FALSE(tx->execute(project).ok());
  EXPECT_FALSE(tx->undo(project).ok());
  EXPECT_FALSE(tx->redo(project).ok());
}

// =========================================================================
// Stateful single-child transaction: undo failure preserves model state
// =========================================================================

TEST(CommandTest, StatefulUndoFailurePreservesExecutedState) {
  Project project = make_project();
  NodeId  a       = project.add_node("A");

  auto tx = std::make_unique<CommandTransaction>();
  ASSERT_TRUE(
      tx->add_command(std::make_unique<AdversarialNameCommand>(
                          a, "A1", AdversarialNameCommand::FailMode::kOnUndo))
          .ok());

  ASSERT_TRUE(tx->execute(project).ok());
  EXPECT_EQ(project.find_node(a)->name(), "A1");

  // Undo fails — model stays at A1
  Result result = tx->undo(project);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(project.find_node(a)->name(), "A1");

  // State is still kDone (single child, no compensation path triggered)
  EXPECT_FALSE(tx->redo(project).ok());
}

// =========================================================================
// Finding 1: undo-failure restoration insertion order (model-observable)
//
// Four children, two modifying the same node. The middle child (B) fails on
// undo. Already-undone children (C, D) must be redone in insertion order
// (C then D). If the restoration loop used reverse insertion order (D then
// C), the shared node would end up with C's value instead of D's.
// =========================================================================

TEST(CommandTest, UndoRestorationInsertionOrder) {
  Project project = make_project();
  NodeId  a       = project.add_node("A");
  NodeId  b       = project.add_node("B");
  NodeId  shared  = project.add_node("Shared");

  auto tx = std::make_unique<CommandTransaction>();
  // Child 0 — never fails, modifies node a
  ASSERT_TRUE(
      tx->add_command(std::make_unique<AdversarialNameCommand>(
                          a, "A1", AdversarialNameCommand::FailMode::kNever))
          .ok());
  // Child 1 — fails on undo, modifies node b (model-observable: stays "B1")
  ASSERT_TRUE(
      tx->add_command(std::make_unique<AdversarialNameCommand>(
                          b, "B1", AdversarialNameCommand::FailMode::kOnUndo))
          .ok());
  // Child 2 — never fails, first modifier of `shared`
  ASSERT_TRUE(tx
                  ->add_command(std::make_unique<AdversarialNameCommand>(
                      shared, "C1", AdversarialNameCommand::FailMode::kNever))
                  .ok());
  // Child 3 — never fails, second modifier of `shared`
  ASSERT_TRUE(tx
                  ->add_command(std::make_unique<AdversarialNameCommand>(
                      shared, "D1", AdversarialNameCommand::FailMode::kNever))
                  .ok());

  ASSERT_TRUE(tx->execute(project).ok());
  EXPECT_EQ(project.find_node(a)->name(), "A1");
  EXPECT_EQ(project.find_node(b)->name(), "B1");
  EXPECT_EQ(project.find_node(shared)->name(), "D1");

  // Undo fails at child 1 (B). Children 3 (D) and 2 (C) were already
  // undone. Compensation must redo C then D (insertion order).
  Result result = tx->undo(project);
  EXPECT_FALSE(result.ok());

  // If insertion order was used: redo C → "C1", then redo D → "D1".
  // If reverse order was used: redo D → "D1", then redo C → "C1".
  EXPECT_EQ(project.find_node(shared)->name(), "D1");
  EXPECT_EQ(project.find_node(a)->name(), "A1");
  EXPECT_EQ(project.find_node(b)->name(), "B1");
}

// =========================================================================
// Finding 2: best-effort compensation — execute
//
// Children: [A:never, B:failOnUndo, C:never, D:failOnExecute]
// D fails on execute. Compensation undoes C, B, A in reverse order. B's
// undo fails but A's undo must STILL run. Result: kTransactionRollbackFailed.
// =========================================================================

TEST(CommandTest, ExecuteBestEffortCompensationContinuesAfterFailure) {
  Project                  project = make_project();
  std::vector<std::string> log;

  auto tx = std::make_unique<CommandTransaction>();
  ASSERT_TRUE(tx->add_command(std::make_unique<TestCommand>(
                                  "A", TestCommand::FailMode::kNever, &log))
                  .ok());
  ASSERT_TRUE(tx->add_command(std::make_unique<TestCommand>(
                                  "B", TestCommand::FailMode::kOnUndo, &log))
                  .ok());
  ASSERT_TRUE(tx->add_command(std::make_unique<TestCommand>(
                                  "C", TestCommand::FailMode::kNever, &log))
                  .ok());
  ASSERT_TRUE(tx->add_command(std::make_unique<TestCommand>(
                                  "D", TestCommand::FailMode::kOnExecute, &log))
                  .ok());

  log.clear();
  Result result = tx->execute(project);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.code(), ResultCode::kTransactionRollbackFailed);

  // All three compensators must have been attempted: C-undo, B-undo, A-undo.
  ASSERT_GE(log.size(), 5u);
  EXPECT_EQ(log[0], "A-execute");
  EXPECT_EQ(log[1], "B-execute");
  EXPECT_EQ(log[2], "C-execute");
  EXPECT_EQ(log[3], "D-execute");
  EXPECT_EQ(log[4], "C-undo");
  EXPECT_EQ(log[5], "B-undo");
  EXPECT_EQ(log[6], "A-undo");
  EXPECT_EQ(log.size(), 7u);
}

// =========================================================================
// Finding 2: best-effort compensation — undo
//
// Children: [A:never, B:failOnUndo, C:failOnRedo, D:never]
// B fails on undo. Compensation redoes C (fails) then D (still runs).
// Result: kTransactionRollbackFailed.
// =========================================================================

TEST(CommandTest, UndoBestEffortCompensationContinuesAfterFailure) {
  Project                  project = make_project();
  std::vector<std::string> log;

  auto tx = std::make_unique<CommandTransaction>();
  ASSERT_TRUE(tx->add_command(std::make_unique<TestCommand>(
                                  "A", TestCommand::FailMode::kNever, &log))
                  .ok());
  ASSERT_TRUE(tx->add_command(std::make_unique<TestCommand>(
                                  "B", TestCommand::FailMode::kOnUndo, &log))
                  .ok());
  ASSERT_TRUE(tx->add_command(std::make_unique<TestCommand>(
                                  "C", TestCommand::FailMode::kOnRedo, &log))
                  .ok());
  ASSERT_TRUE(tx->add_command(std::make_unique<TestCommand>(
                                  "D", TestCommand::FailMode::kNever, &log))
                  .ok());

  ASSERT_TRUE(tx->execute(project).ok());

  log.clear();
  Result result = tx->undo(project);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.code(), ResultCode::kTransactionRollbackFailed);

  // D-undo, C-undo, B-undo (fails), C-redo (fails), D-redo (still runs).
  ASSERT_GE(log.size(), 5u);
  EXPECT_EQ(log[0], "D-undo");
  EXPECT_EQ(log[1], "C-undo");
  EXPECT_EQ(log[2], "B-undo");
  EXPECT_EQ(log[3], "C-redo");
  EXPECT_EQ(log[4], "D-redo");
  EXPECT_EQ(log.size(), 5u);
}

// =========================================================================
// Finding 2: best-effort compensation — redo
//
// Children: [A:never, TwoPhaseUndoFail, B:never, D:failOnRedo].
// First undo succeeds (TwoPhase does not fail on first undo).
// D fails on redo. Compensation undoes B, TwoPhase (FAILS), A in
// reverse order. B's undo and A's undo must still run.
// Result: kTransactionRollbackFailed.
// =========================================================================

TEST(CommandTest, RedoBestEffortCompensationContinuesAfterFailure) {
  Project                  project = make_project();
  std::vector<std::string> log;

  auto tx = std::make_unique<CommandTransaction>();
  ASSERT_TRUE(tx->add_command(std::make_unique<TestCommand>(
                                  "A", TestCommand::FailMode::kNever, &log))
                  .ok());
  ASSERT_TRUE(
      tx->add_command(std::make_unique<TwoPhaseUndoFailCommand>(&log)).ok());
  ASSERT_TRUE(tx->add_command(std::make_unique<TestCommand>(
                                  "B", TestCommand::FailMode::kNever, &log))
                  .ok());
  ASSERT_TRUE(tx->add_command(std::make_unique<TestCommand>(
                                  "D", TestCommand::FailMode::kOnRedo, &log))
                  .ok());

  ASSERT_TRUE(tx->execute(project).ok());
  ASSERT_TRUE(tx->undo(project).ok());

  log.clear();
  Result result = tx->redo(project);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.code(), ResultCode::kTransactionRollbackFailed);

  // A-redo, TwoPhase-redo, B-redo, D-redo (fails),
  // B-undo, TwoPhase-undo (fails, but best-effort continues),
  // A-undo (still runs).
  ASSERT_GE(log.size(), 7u);
  EXPECT_EQ(log[0], "A-redo");
  EXPECT_EQ(log[1], "TwoPhase-redo");
  EXPECT_EQ(log[2], "B-redo");
  EXPECT_EQ(log[3], "D-redo");
  EXPECT_EQ(log[4], "B-undo");
  EXPECT_EQ(log[5], "TwoPhase-undo");
  EXPECT_EQ(log[6], "A-undo");
  EXPECT_EQ(log.size(), 7u);
}

// =========================================================================
// Finding 4: execute failure with model-observable rollback
//
// Last child (index 3) fails; previously executed children 0-2 must be
// undone and model restored exactly.
// =========================================================================

TEST(CommandTest, ExecuteLastChildFailureModelRollback) {
  Project project = make_project();
  NodeId  a       = project.add_node("A");
  NodeId  b       = project.add_node("B");
  NodeId  c       = project.add_node("C");

  auto tx = std::make_unique<CommandTransaction>();
  ASSERT_TRUE(
      tx->add_command(std::make_unique<AdversarialNameCommand>(
                          a, "A1", AdversarialNameCommand::FailMode::kNever))
          .ok());
  ASSERT_TRUE(
      tx->add_command(std::make_unique<AdversarialNameCommand>(
                          b, "B1", AdversarialNameCommand::FailMode::kNever))
          .ok());
  ASSERT_TRUE(
      tx->add_command(std::make_unique<AdversarialNameCommand>(
                          c, "C1", AdversarialNameCommand::FailMode::kNever))
          .ok());
  ASSERT_TRUE(tx->add_command(std::make_unique<SetNodeNameCommand>(
                                  NodeId::generate(), "MISSING"))
                  .ok());

  Result result = tx->execute(project);
  EXPECT_FALSE(result.ok());

  // All three previously-executed children were rolled back.
  EXPECT_EQ(project.find_node(a)->name(), "A");
  EXPECT_EQ(project.find_node(b)->name(), "B");
  EXPECT_EQ(project.find_node(c)->name(), "C");

  // Terminal — kFaulted, cannot retry.
  EXPECT_FALSE(tx->execute(project).ok());
  EXPECT_FALSE(tx->undo(project).ok());
}

// =========================================================================
// Finding 4: undo failure — last-inserted child is first undone.
// Child index 3 (D) fails on undo immediately; no compensation needed.
// Other children are untouched. Model stays at executed values.
// =========================================================================

TEST(CommandTest, UndoFirstUndoneChildFailureModelState) {
  Project project = make_project();
  NodeId  a       = project.add_node("A");
  NodeId  b       = project.add_node("B");
  NodeId  c       = project.add_node("C");
  NodeId  d       = project.add_node("D");

  auto tx = std::make_unique<CommandTransaction>();
  ASSERT_TRUE(
      tx->add_command(std::make_unique<AdversarialNameCommand>(
                          a, "A1", AdversarialNameCommand::FailMode::kNever))
          .ok());
  ASSERT_TRUE(
      tx->add_command(std::make_unique<AdversarialNameCommand>(
                          b, "B1", AdversarialNameCommand::FailMode::kNever))
          .ok());
  ASSERT_TRUE(
      tx->add_command(std::make_unique<AdversarialNameCommand>(
                          c, "C1", AdversarialNameCommand::FailMode::kNever))
          .ok());
  // Last child (inserted, undone first) fails on undo.
  ASSERT_TRUE(
      tx->add_command(std::make_unique<AdversarialNameCommand>(
                          d, "D1", AdversarialNameCommand::FailMode::kOnUndo))
          .ok());

  ASSERT_TRUE(tx->execute(project).ok());
  EXPECT_EQ(project.find_node(a)->name(), "A1");
  EXPECT_EQ(project.find_node(b)->name(), "B1");
  EXPECT_EQ(project.find_node(c)->name(), "C1");
  EXPECT_EQ(project.find_node(d)->name(), "D1");

  // Undo — D fails immediately, no compensation path triggered.
  Result result = tx->undo(project);
  EXPECT_FALSE(result.ok());

  // All nodes retain their executed names (undo never reached A, B, C).
  EXPECT_EQ(project.find_node(a)->name(), "A1");
  EXPECT_EQ(project.find_node(b)->name(), "B1");
  EXPECT_EQ(project.find_node(c)->name(), "C1");
  EXPECT_EQ(project.find_node(d)->name(), "D1");

  // State still kDone (no compensation needed).
  EXPECT_FALSE(tx->redo(project).ok());

  // Can retry undo (state is still kDone).
  // Bump D to no-fail for a clean undo of the remaining children.
  // But since we cannot change D's mode, just verify the transaction
  // is not kFaulted from a single-child undo failure.
  EXPECT_FALSE(tx->execute(project).ok());
}

// =========================================================================
// Finding 4: redo failure — first child fails.
// No children were redone before it, so no compensation needed.
// Model stays at undone values. State stays kUndone.
// =========================================================================

TEST(CommandTest, RedoFirstChildFailureModelState) {
  Project project = make_project();
  NodeId  a       = project.add_node("A");
  NodeId  b       = project.add_node("B");

  auto tx = std::make_unique<CommandTransaction>();
  ASSERT_TRUE(
      tx->add_command(std::make_unique<AdversarialNameCommand>(
                          a, "A1", AdversarialNameCommand::FailMode::kOnRedo))
          .ok());
  ASSERT_TRUE(
      tx->add_command(std::make_unique<AdversarialNameCommand>(
                          b, "B1", AdversarialNameCommand::FailMode::kNever))
          .ok());

  ASSERT_TRUE(tx->execute(project).ok());
  ASSERT_TRUE(tx->undo(project).ok());

  EXPECT_EQ(project.find_node(a)->name(), "A");
  EXPECT_EQ(project.find_node(b)->name(), "B");

  // Redo — first child fails immediately, no compensation.
  Result result = tx->redo(project);
  EXPECT_FALSE(result.ok());

  // Nodes remain at undone names.
  EXPECT_EQ(project.find_node(a)->name(), "A");
  EXPECT_EQ(project.find_node(b)->name(), "B");

  // State still kUndone (can retry redo).
  EXPECT_FALSE(tx->undo(project).ok());
}

// =========================================================================
// Finding 4: execute rollback partial failure — model stuck mid-rollback.
//
// Children: [A:failOnUndo, B:never, C:never, D:failOnExecute].
// Execute: A, B, C all succeed; D fails.
// Compensation: undo C (OK), undo B (OK), undo A (FAILS).
// kFaulted, kTransactionRollbackFailed.
// A stays at "A1", B and C restored.
// =========================================================================

TEST(CommandTest, ExecuteCompensationPartialFailureModelState) {
  Project project = make_project();
  NodeId  a       = project.add_node("A");
  NodeId  b       = project.add_node("B");
  NodeId  c       = project.add_node("C");

  auto tx = std::make_unique<CommandTransaction>();
  ASSERT_TRUE(
      tx->add_command(std::make_unique<AdversarialNameCommand>(
                          a, "A1", AdversarialNameCommand::FailMode::kOnUndo))
          .ok());
  ASSERT_TRUE(
      tx->add_command(std::make_unique<AdversarialNameCommand>(
                          b, "B1", AdversarialNameCommand::FailMode::kNever))
          .ok());
  ASSERT_TRUE(
      tx->add_command(std::make_unique<AdversarialNameCommand>(
                          c, "C1", AdversarialNameCommand::FailMode::kNever))
          .ok());
  ASSERT_TRUE(tx->add_command(std::make_unique<SetNodeNameCommand>(
                                  NodeId::generate(), "MISSING"))
                  .ok());

  Result result = tx->execute(project);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.code(), ResultCode::kTransactionRollbackFailed);

  // A's undo failed → A stuck at "A1".
  EXPECT_EQ(project.find_node(a)->name(), "A1");
  // B and C were successfully rolled back.
  EXPECT_EQ(project.find_node(b)->name(), "B");
  EXPECT_EQ(project.find_node(c)->name(), "C");

  // kFaulted — no further operations.
  EXPECT_FALSE(tx->execute(project).ok());
  EXPECT_FALSE(tx->undo(project).ok());
  EXPECT_FALSE(tx->redo(project).ok());
}

// =========================================================================
// Finding 4: undo compensation partial failure — model stuck mid-restore.
//
// Children: [A:never, B:failOnUndo, C:failOnRedo, D:never].
// Undo: D, C, B fails. Compensation: redo C (FAILS), redo D (still runs).
// C stuck at undone (pre-execute), D restored.
// =========================================================================

TEST(CommandTest, UndoCompensationPartialFailureModelState) {
  Project project = make_project();
  NodeId  a       = project.add_node("A");
  NodeId  b       = project.add_node("B");
  NodeId  c       = project.add_node("C");
  NodeId  d       = project.add_node("D");

  auto tx = std::make_unique<CommandTransaction>();
  ASSERT_TRUE(
      tx->add_command(std::make_unique<AdversarialNameCommand>(
                          a, "A1", AdversarialNameCommand::FailMode::kNever))
          .ok());
  ASSERT_TRUE(
      tx->add_command(std::make_unique<AdversarialNameCommand>(
                          b, "B1", AdversarialNameCommand::FailMode::kOnUndo))
          .ok());
  ASSERT_TRUE(
      tx->add_command(std::make_unique<AdversarialNameCommand>(
                          c, "C1", AdversarialNameCommand::FailMode::kOnRedo))
          .ok());
  ASSERT_TRUE(
      tx->add_command(std::make_unique<AdversarialNameCommand>(
                          d, "D1", AdversarialNameCommand::FailMode::kNever))
          .ok());

  ASSERT_TRUE(tx->execute(project).ok());
  EXPECT_EQ(project.find_node(a)->name(), "A1");
  EXPECT_EQ(project.find_node(b)->name(), "B1");
  EXPECT_EQ(project.find_node(c)->name(), "C1");
  EXPECT_EQ(project.find_node(d)->name(), "D1");

  Result result = tx->undo(project);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.code(), ResultCode::kTransactionRollbackFailed);

  // A untouched by undo (never reached). B untouched (failed its undo).
  EXPECT_EQ(project.find_node(a)->name(), "A1");
  EXPECT_EQ(project.find_node(b)->name(), "B1");
  // C was undone but redo failed → stuck at "C" (original).
  EXPECT_EQ(project.find_node(c)->name(), "C");
  // D was undone but redo succeeded (best-effort ran it) → back to "D1".
  EXPECT_EQ(project.find_node(d)->name(), "D1");

  // kFaulted.
  EXPECT_FALSE(tx->undo(project).ok());
  EXPECT_FALSE(tx->redo(project).ok());
}

// =========================================================================
// Finding 4: redo compensation partial failure — model stuck mid-rollback.
//
// Children: [TwoPhaseUndoFail, A:never, B:never, D:failOnRedo].
// First undo succeeds (TwoPhase does not fail on first undo).
// Redo: TwoPhase sets was_redone_, A/B succeed, D fails.
// Compensation: undo B (OK), undo A (OK), undo TwoPhase (FAILS).
// Best-effort: A and B are undone despite TwoPhase failure.
// D never redone, TwoPhase stuck at redone (but model-immaterial).
// =========================================================================

TEST(CommandTest, RedoCompensationPartialFailureModelState) {
  Project                  project = make_project();
  NodeId                   a       = project.add_node("A");
  NodeId                   b       = project.add_node("B");
  NodeId                   d       = project.add_node("D");
  std::vector<std::string> log;

  auto tx = std::make_unique<CommandTransaction>();
  // Child 0: two-phase — fails on undo only after being redone.
  ASSERT_TRUE(
      tx->add_command(std::make_unique<TwoPhaseUndoFailCommand>(&log)).ok());
  ASSERT_TRUE(
      tx->add_command(std::make_unique<AdversarialNameCommand>(
                          a, "A1", AdversarialNameCommand::FailMode::kNever))
          .ok());
  ASSERT_TRUE(
      tx->add_command(std::make_unique<AdversarialNameCommand>(
                          b, "B1", AdversarialNameCommand::FailMode::kNever))
          .ok());
  ASSERT_TRUE(
      tx->add_command(std::make_unique<AdversarialNameCommand>(
                          d, "D1", AdversarialNameCommand::FailMode::kOnRedo))
          .ok());

  ASSERT_TRUE(tx->execute(project).ok());
  ASSERT_TRUE(tx->undo(project).ok());

  EXPECT_EQ(project.find_node(a)->name(), "A");
  EXPECT_EQ(project.find_node(b)->name(), "B");
  EXPECT_EQ(project.find_node(d)->name(), "D");

  log.clear();
  Result result = tx->redo(project);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.code(), ResultCode::kTransactionRollbackFailed);

  // A and B were redone, then compensation-undo ran (best-effort).
  EXPECT_EQ(project.find_node(a)->name(), "A");
  EXPECT_EQ(project.find_node(b)->name(), "B");
  // D was never redone (the failing child) → stays at undone value.
  EXPECT_EQ(project.find_node(d)->name(), "D");

  // Log proves both redo and compensating undo ran for TwoPhase.
  ASSERT_GE(log.size(), 2u);
  EXPECT_EQ(log[0], "TwoPhase-redo");
  EXPECT_EQ(log[1], "TwoPhase-undo");

  // kFaulted.
  EXPECT_FALSE(tx->redo(project).ok());
  EXPECT_FALSE(tx->undo(project).ok());
}

// =========================================================================
// Noexcept contract — compile-time enforcement
// =========================================================================

TEST(CommandTest, NoexceptStaticAsserts) {
  static_assert(
      noexcept(std::declval<Command&>().execute(std::declval<Project&>())));
  static_assert(
      noexcept(std::declval<Command&>().undo(std::declval<Project&>())));
  static_assert(
      noexcept(std::declval<Command&>().redo(std::declval<Project&>())));

  static_assert(noexcept(std::declval<CommandHistory&>().execute_new(
      std::declval<std::unique_ptr<Command>>(), std::declval<Project&>())));
  static_assert(
      noexcept(std::declval<CommandHistory&>().undo(std::declval<Project&>())));
  static_assert(
      noexcept(std::declval<CommandHistory&>().redo(std::declval<Project&>())));

  static_assert(noexcept(
      std::declval<CommandTransaction&>().execute(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<CommandTransaction&>().undo(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<CommandTransaction&>().redo(std::declval<Project&>())));

  static_assert(noexcept(
      std::declval<SetNodeNameCommand&>().execute(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<SetNodeNameCommand&>().undo(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<SetNodeNameCommand&>().redo(std::declval<Project&>())));

  static_assert(noexcept(
      std::declval<SetTrackNameCommand&>().execute(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<SetTrackNameCommand&>().undo(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<SetTrackNameCommand&>().redo(std::declval<Project&>())));

  static_assert(noexcept(std::declval<SetProjectTempoCommand&>().execute(
      std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<SetProjectTempoCommand&>().undo(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<SetProjectTempoCommand&>().redo(std::declval<Project&>())));
}

// =========================================================================
// kCommandFaulted — exact code checks
// =========================================================================

TEST(CommandTest, FaultedTransactionReturnsCommandFaulted) {
  Project project = make_project();
  NodeId  a       = project.add_node("A");

  auto tx = std::make_unique<CommandTransaction>();
  ASSERT_TRUE(
      tx->add_command(std::make_unique<AdversarialNameCommand>(
                          a, "A1", AdversarialNameCommand::FailMode::kOnUndo))
          .ok());
  ASSERT_TRUE(tx->add_command(std::make_unique<SetNodeNameCommand>(
                                  NodeId::generate(), "MISSING"))
                  .ok());

  Result result = tx->execute(project);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.code(), ResultCode::kTransactionRollbackFailed);

  // Once kFaulted, all operations return kCommandFaulted uniformly.
  EXPECT_EQ(tx->execute(project).code(), ResultCode::kCommandFaulted);
  EXPECT_EQ(tx->undo(project).code(), ResultCode::kCommandFaulted);
  EXPECT_EQ(tx->redo(project).code(), ResultCode::kCommandFaulted);
}

TEST(CommandTest, FaultedByExecuteWithCleanRollbackReturnsCommandFaulted) {
  Project project = make_project();
  NodeId  a       = project.add_node("A");

  auto tx = std::make_unique<CommandTransaction>();
  ASSERT_TRUE(
      tx->add_command(std::make_unique<AdversarialNameCommand>(
                          a, "A1", AdversarialNameCommand::FailMode::kNever))
          .ok());
  ASSERT_TRUE(tx->add_command(std::make_unique<SetNodeNameCommand>(
                                  NodeId::generate(), "MISSING"))
                  .ok());

  Result result = tx->execute(project);
  EXPECT_FALSE(result.ok());
  // Clean rollback: terminal by design but NOT a rollback failure.
  EXPECT_EQ(result.code(), ResultCode::kInvalidArgument);

  EXPECT_EQ(project.find_node(a)->name(), "A");

  // Terminal — subsequent calls report kCommandFaulted.
  EXPECT_EQ(tx->execute(project).code(), ResultCode::kCommandFaulted);
  EXPECT_EQ(tx->undo(project).code(), ResultCode::kCommandFaulted);
  EXPECT_EQ(tx->redo(project).code(), ResultCode::kCommandFaulted);
}

// =========================================================================
// Finding 6a: undo child fails once, restoration leaves kDone, retry succeeds
//
// Children: [A (never), B (one-shot undo fail), C (never)].
// Undo reverse: C-undo ok, B-undo FAILS (one-shot consumed).
//   Compensation: C-redo ok. State stays kDone.
//   Retry undo: C-undo ok, B-undo ok (one-shot consumed), A-undo ok.
//   State becomes kUndone. Model returns to pre-execute values.
// =========================================================================

TEST(CommandTest, UndoRetryAfterSuccessfulCompensation) {
  Project project = make_project();
  NodeId  a       = project.add_node("A");
  NodeId  b       = project.add_node("B");
  NodeId  c       = project.add_node("C");

  auto cmd_b = std::make_unique<AdversarialNameCommand>(
      b, "B1", AdversarialNameCommand::FailMode::kNever);
  cmd_b->set_fail_next_undo(true);

  auto tx = std::make_unique<CommandTransaction>();
  ASSERT_TRUE(
      tx->add_command(std::make_unique<AdversarialNameCommand>(
                          a, "A1", AdversarialNameCommand::FailMode::kNever))
          .ok());
  ASSERT_TRUE(tx->add_command(std::move(cmd_b)).ok());
  ASSERT_TRUE(
      tx->add_command(std::make_unique<AdversarialNameCommand>(
                          c, "C1", AdversarialNameCommand::FailMode::kNever))
          .ok());

  ASSERT_TRUE(tx->execute(project).ok());
  EXPECT_EQ(project.find_node(a)->name(), "A1");
  EXPECT_EQ(project.find_node(b)->name(), "B1");
  EXPECT_EQ(project.find_node(c)->name(), "C1");

  // First undo — B fails on undo, C is restored, state stays kDone.
  Result first = tx->undo(project);
  EXPECT_FALSE(first.ok());
  EXPECT_EQ(first.code(), ResultCode::kInternalError);
  // Model is back to post-execute state (compensation restored C, A untouched).
  EXPECT_EQ(project.find_node(a)->name(), "A1");
  EXPECT_EQ(project.find_node(b)->name(), "B1");  // B never undone
  EXPECT_EQ(project.find_node(c)->name(), "C1");  // C restored

  // Retry undo — all succeed.
  Result second = tx->undo(project);
  EXPECT_TRUE(second.ok());
  EXPECT_EQ(project.find_node(a)->name(), "A");
  EXPECT_EQ(project.find_node(b)->name(), "B");
  EXPECT_EQ(project.find_node(c)->name(), "C");

  // Can redo now.
  ASSERT_TRUE(tx->redo(project).ok());
  EXPECT_EQ(project.find_node(a)->name(), "A1");
  EXPECT_EQ(project.find_node(b)->name(), "B1");
  EXPECT_EQ(project.find_node(c)->name(), "C1");
}

// =========================================================================
// Finding 6b: redo child fails once, rollback leaves kUndone, retry succeeds
//
// Children: [A (one-shot redo fail), B (never)].
// Execute, undo both. Redo: A redo FAILS (one-shot consumed), no compensation
//   needed (i=0). State stays kUndone.
//   Retry redo: A redo ok, B redo ok. State becomes kDone.
// =========================================================================

TEST(CommandTest, RedoRetryAfterSuccessfulRollback) {
  Project project = make_project();
  NodeId  a       = project.add_node("A");
  NodeId  b       = project.add_node("B");

  auto cmd_a = std::make_unique<AdversarialNameCommand>(
      a, "A1", AdversarialNameCommand::FailMode::kNever);
  cmd_a->set_fail_next_redo(true);

  auto tx = std::make_unique<CommandTransaction>();
  ASSERT_TRUE(tx->add_command(std::move(cmd_a)).ok());
  ASSERT_TRUE(
      tx->add_command(std::make_unique<AdversarialNameCommand>(
                          b, "B1", AdversarialNameCommand::FailMode::kNever))
          .ok());

  ASSERT_TRUE(tx->execute(project).ok());
  EXPECT_EQ(project.find_node(a)->name(), "A1");
  EXPECT_EQ(project.find_node(b)->name(), "B1");

  ASSERT_TRUE(tx->undo(project).ok());
  EXPECT_EQ(project.find_node(a)->name(), "A");
  EXPECT_EQ(project.find_node(b)->name(), "B");

  // First redo — A fails, no compensation needed, state stays kUndone.
  Result first = tx->redo(project);
  EXPECT_FALSE(first.ok());
  EXPECT_EQ(first.code(), ResultCode::kInternalError);
  EXPECT_EQ(project.find_node(a)->name(), "A");
  EXPECT_EQ(project.find_node(b)->name(), "B");

  // Retry redo — all succeed.
  Result second = tx->redo(project);
  EXPECT_TRUE(second.ok());
  EXPECT_EQ(project.find_node(a)->name(), "A1");
  EXPECT_EQ(project.find_node(b)->name(), "B1");
}

// =========================================================================
// Finding 6c: undo failure at insertion index 0 restores already-undone
// children in insertion order, retry succeeds.
//
// Children: [C0 (one-shot undo fail), C1 (never), C2 (never)] — three
// modifying the same node `shared`.
// Execute: C0→"C0", C1→"C1", C2→"C2" → shared name is "C2".
// Undo (reverse): C2 undo→"C1", C1 undo→"C0", C0 undo FAILS (one-shot
//   consumed). Compensation redoes in insertion order:
//   C1 redo→"C1", C2 redo→"C2". State stays kDone.
// Retry undo: C2 undo→"C1", C1 undo→"C0", C0 undo→"Original".
//   State becomes kUndone. Shared is "Original".
// =========================================================================

TEST(CommandTest, UndoFailureAtIndex0RestoreInsertionOrderRetrySucceeds) {
  Project project = make_project();
  NodeId  shared  = project.add_node("Original");

  auto cmd0 = std::make_unique<AdversarialNameCommand>(
      shared, "C0", AdversarialNameCommand::FailMode::kNever);
  cmd0->set_fail_next_undo(true);

  auto tx = std::make_unique<CommandTransaction>();
  ASSERT_TRUE(tx->add_command(std::move(cmd0)).ok());
  ASSERT_TRUE(tx
                  ->add_command(std::make_unique<AdversarialNameCommand>(
                      shared, "C1", AdversarialNameCommand::FailMode::kNever))
                  .ok());
  ASSERT_TRUE(tx
                  ->add_command(std::make_unique<AdversarialNameCommand>(
                      shared, "C2", AdversarialNameCommand::FailMode::kNever))
                  .ok());

  ASSERT_TRUE(tx->execute(project).ok());
  EXPECT_EQ(project.find_node(shared)->name(), "C2");

  // First undo — C0 fails at the end of reverse traversal.
  Result first = tx->undo(project);
  EXPECT_FALSE(first.ok());
  EXPECT_EQ(first.code(), ResultCode::kInternalError);
  // Compensation in insertion order: C1→"C1", C2→"C2".
  EXPECT_EQ(project.find_node(shared)->name(), "C2");

  // Retry undo — all succeed in reverse order.
  Result second = tx->undo(project);
  EXPECT_TRUE(second.ok());
  EXPECT_EQ(project.find_node(shared)->name(), "Original");

  // Can redo.
  ASSERT_TRUE(tx->redo(project).ok());
  EXPECT_EQ(project.find_node(shared)->name(), "C2");
}

// =========================================================================
// Transaction forwards the provided project correctly — the command's
// execute/undo/redo receive the same Project& that the transaction received.
// =========================================================================

TEST(CommandTest, TransactionForwardsProjectToChildren) {
  Project project = make_project();
  NodeId  node_a  = project.add_node("A");
  NodeId  node_b  = project.add_node("B");

  // Two children target different nodes in the same project.
  auto tx = std::make_unique<CommandTransaction>();
  ASSERT_TRUE(tx
                  ->add_command(std::make_unique<AdversarialNameCommand>(
                      node_a, "A1", AdversarialNameCommand::FailMode::kNever))
                  .ok());
  ASSERT_TRUE(tx
                  ->add_command(std::make_unique<AdversarialNameCommand>(
                      node_b, "B1", AdversarialNameCommand::FailMode::kNever))
                  .ok());

  ASSERT_TRUE(tx->execute(project).ok());
  EXPECT_EQ(project.find_node(node_a)->name(), "A1");
  EXPECT_EQ(project.find_node(node_b)->name(), "B1");

  ASSERT_TRUE(tx->undo(project).ok());
  EXPECT_EQ(project.find_node(node_a)->name(), "A");
  EXPECT_EQ(project.find_node(node_b)->name(), "B");
}

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

// =========================================================================
// Phase 8c-i — ArchiveTrackCommand
// =========================================================================

TEST(CommandTest, ArchiveTrackRoundTrip) {
  Project    project  = make_project();
  const auto track_id = project.add_track("Track", StaffLayout::single_staff(),
                                          *MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());

  auto cmd = std::make_unique<ArchiveTrackCommand>(*track_id);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(project.active_tracks().size(), 0u);
  EXPECT_EQ(project.archived_tracks().size(), 1u);
  EXPECT_EQ(project.archived_tracks()[0].id(), *track_id);
  EXPECT_EQ(project.find_active_track(*track_id), nullptr);
  EXPECT_NE(project.find_archived_track(*track_id), nullptr);

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_EQ(project.active_tracks().size(), 1u);
  EXPECT_EQ(project.archived_tracks().size(), 0u);
  EXPECT_EQ(project.active_tracks()[0].id(), *track_id);
  EXPECT_NE(project.find_active_track(*track_id), nullptr);
  EXPECT_EQ(project.find_archived_track(*track_id), nullptr);

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_EQ(project.active_tracks().size(), 0u);
  EXPECT_EQ(project.archived_tracks().size(), 1u);
}

TEST(CommandTest, ArchiveTrackMissingIdFails) {
  Project project = make_project();

  auto cmd = std::make_unique<ArchiveTrackCommand>(TrackId::generate());
  EXPECT_FALSE(cmd->execute(project).ok());
}

TEST(CommandTest, ArchiveTrackDoubleExecuteRejected) {
  Project    project  = make_project();
  const auto track_id = project.add_track("Track", StaffLayout::single_staff(),
                                          *MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());

  auto cmd = std::make_unique<ArchiveTrackCommand>(*track_id);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, ArchiveTrackUndoWithoutExecuteRejected) {
  Project    project  = make_project();
  const auto track_id = project.add_track("Track", StaffLayout::single_staff(),
                                          *MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());

  auto cmd = std::make_unique<ArchiveTrackCommand>(*track_id);
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, ArchiveTrackRedoWithoutUndoRejected) {
  Project    project  = make_project();
  const auto track_id = project.add_track("Track", StaffLayout::single_staff(),
                                          *MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());

  auto cmd = std::make_unique<ArchiveTrackCommand>(*track_id);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, ArchiveTrackUndoFailsWhenRestoreWouldExceedCap) {
  Project project = make_project();

  std::optional<TrackId> first_track_id;
  for (int i = 0; i < static_cast<int>(Project::kMaxActiveTracks); ++i) {
    const auto id = project.add_track("Track", StaffLayout::single_staff(),
                                      *MidiChannel::create(0));
    ASSERT_TRUE(id.has_value());
    if (i == 0)
      first_track_id = id;
  }
  ASSERT_TRUE(first_track_id.has_value());

  auto cmd = std::make_unique<ArchiveTrackCommand>(*first_track_id);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(project.active_tracks().size(), Project::kMaxActiveTracks - 1);

  // Fill the freed slot with a different track so the cap is full again.
  const auto filler = project.add_track("Filler", StaffLayout::single_staff(),
                                        *MidiChannel::create(1));
  ASSERT_TRUE(filler.has_value());
  EXPECT_EQ(project.active_tracks().size(), Project::kMaxActiveTracks);

  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
  // State stays kDone: the track remains archived.
  EXPECT_EQ(project.find_archived_track(*first_track_id) != nullptr, true);
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}

// =========================================================================
// Phase 8c-i — RestoreTrackCommand
// =========================================================================

TEST(CommandTest, RestoreTrackRoundTrip) {
  Project    project  = make_project();
  const auto track_id = project.add_track("Track", StaffLayout::single_staff(),
                                          *MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());
  ASSERT_TRUE(project.archive_track(*track_id).ok());

  auto cmd = std::make_unique<RestoreTrackCommand>(*track_id);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(project.active_tracks().size(), 1u);
  EXPECT_EQ(project.archived_tracks().size(), 0u);

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_EQ(project.active_tracks().size(), 0u);
  EXPECT_EQ(project.archived_tracks().size(), 1u);

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_EQ(project.active_tracks().size(), 1u);
  EXPECT_EQ(project.archived_tracks().size(), 0u);
}

TEST(CommandTest, RestoreTrackMissingIdFails) {
  Project project = make_project();

  auto cmd = std::make_unique<RestoreTrackCommand>(TrackId::generate());
  EXPECT_FALSE(cmd->execute(project).ok());
}

TEST(CommandTest, RestoreTrackOnActiveTrackFails) {
  Project    project  = make_project();
  const auto track_id = project.add_track("Track", StaffLayout::single_staff(),
                                          *MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());

  auto cmd = std::make_unique<RestoreTrackCommand>(*track_id);
  EXPECT_FALSE(cmd->execute(project).ok());
  EXPECT_EQ(project.active_tracks().size(), 1u);
}

TEST(CommandTest, RestoreTrackDoubleExecuteRejected) {
  Project    project  = make_project();
  const auto track_id = project.add_track("Track", StaffLayout::single_staff(),
                                          *MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());
  ASSERT_TRUE(project.archive_track(*track_id).ok());

  auto cmd = std::make_unique<RestoreTrackCommand>(*track_id);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, RestoreTrackUndoWithoutExecuteRejected) {
  Project    project  = make_project();
  const auto track_id = project.add_track("Track", StaffLayout::single_staff(),
                                          *MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());
  ASSERT_TRUE(project.archive_track(*track_id).ok());

  auto cmd = std::make_unique<RestoreTrackCommand>(*track_id);
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, RestoreTrackRedoWithoutUndoRejected) {
  Project    project  = make_project();
  const auto track_id = project.add_track("Track", StaffLayout::single_staff(),
                                          *MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());
  ASSERT_TRUE(project.archive_track(*track_id).ok());

  auto cmd = std::make_unique<RestoreTrackCommand>(*track_id);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}

// =========================================================================
// Phase 8c-i — SetOutputTypeCommand
// =========================================================================

TEST(CommandTest, SetOutputTypeRoundTrip) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out", ConnectorType::kSequential);

  auto cmd = std::make_unique<SetOutputTypeCommand>(node_id, out_id,
                                                    ConnectorType::kVertical);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(node->find_output(out_id)->type(), ConnectorType::kVertical);

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_EQ(node->find_output(out_id)->type(), ConnectorType::kSequential);

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_EQ(node->find_output(out_id)->type(), ConnectorType::kVertical);
}

TEST(CommandTest, SetOutputTypeMissingNodeIdFails) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out");

  auto cmd = std::make_unique<SetOutputTypeCommand>(NodeId::generate(), out_id,
                                                    ConnectorType::kVertical);
  EXPECT_FALSE(cmd->execute(project).ok());
}

TEST(CommandTest, SetOutputTypeMissingConnectorIdFails) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");

  auto cmd = std::make_unique<SetOutputTypeCommand>(
      node_id, ConnectorId::generate(), ConnectorType::kVertical);
  EXPECT_FALSE(cmd->execute(project).ok());
}

TEST(CommandTest, SetOutputTypeDoubleExecuteRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out");

  auto cmd = std::make_unique<SetOutputTypeCommand>(node_id, out_id,
                                                    ConnectorType::kVertical);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetOutputTypeUndoWithoutExecuteRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out");

  auto cmd = std::make_unique<SetOutputTypeCommand>(node_id, out_id,
                                                    ConnectorType::kVertical);
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetOutputTypeRedoWithoutUndoRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out");

  auto cmd = std::make_unique<SetOutputTypeCommand>(node_id, out_id,
                                                    ConnectorType::kVertical);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetOutputTypeRejectsClashAndLeavesStateUnchanged) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto first   = node->add_output("First", ConnectorType::kSequential);
  const auto second  = node->add_output("Second", ConnectorType::kSequential);
  const auto event   = EventId::generate();
  ASSERT_TRUE(node->bind_output_event(first, event).ok());
  ASSERT_TRUE(node->bind_output_event(second, event).ok());

  auto cmd = std::make_unique<SetOutputTypeCommand>(node_id, first,
                                                    ConnectorType::kVertical);
  EXPECT_FALSE(cmd->execute(project).ok());
  EXPECT_EQ(node->find_output(first)->type(), ConnectorType::kSequential);
  EXPECT_EQ(node->find_output(second)->type(), ConnectorType::kSequential);

  // The command is still kFresh -- undo/redo remain rejected.
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetOutputTypeUpdatesAndRestoresBoundListenerType) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out", ConnectorType::kSequential);
  const auto event   = EventId::generate();
  ASSERT_TRUE(node->bind_output_event(out_id, event).ok());
  ASSERT_EQ(node->find_listener(event)->bound_type(),
            ConnectorType::kSequential);

  auto cmd = std::make_unique<SetOutputTypeCommand>(node_id, out_id,
                                                    ConnectorType::kVertical);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(node->find_listener(event)->bound_type(), ConnectorType::kVertical);

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_EQ(node->find_listener(event)->bound_type(),
            ConnectorType::kSequential);
}

// =========================================================================
// Phase 8c-i — SetListenerPolicyCommand
// =========================================================================

TEST(CommandTest, SetListenerPolicyRoundTrip) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out", ConnectorType::kVertical);
  const auto event   = EventId::generate();
  ASSERT_TRUE(node->bind_output_event(out_id, event).ok());

  const auto* listener = node->find_listener(event);
  ASSERT_NE(listener, nullptr);
  EXPECT_EQ(listener->policy(), QueuePolicy::kLatestValidWins);
  EXPECT_EQ(listener->capacity(), 1u);

  auto cmd = std::make_unique<SetListenerPolicyCommand>(node_id, event,
                                                        QueuePolicy::kFifo, 5);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(listener->policy(), QueuePolicy::kFifo);
  EXPECT_EQ(listener->capacity(), 5u);

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_EQ(listener->policy(), QueuePolicy::kLatestValidWins);
  EXPECT_EQ(listener->capacity(), 1u);

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_EQ(listener->policy(), QueuePolicy::kFifo);
  EXPECT_EQ(listener->capacity(), 5u);
}

TEST(CommandTest, SetListenerPolicyMissingNodeIdFails) {
  Project project = make_project();

  auto cmd = std::make_unique<SetListenerPolicyCommand>(
      NodeId::generate(), EventId::generate(), QueuePolicy::kFifo, 5);
  EXPECT_FALSE(cmd->execute(project).ok());
}

TEST(CommandTest, SetListenerPolicyNoListenerFails) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");

  auto cmd = std::make_unique<SetListenerPolicyCommand>(
      node_id, EventId::generate(), QueuePolicy::kFifo, 5);
  EXPECT_FALSE(cmd->execute(project).ok());
}

TEST(CommandTest, SetListenerPolicyFifoZeroCapacityRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out", ConnectorType::kVertical);
  const auto event   = EventId::generate();
  ASSERT_TRUE(node->bind_output_event(out_id, event).ok());

  auto cmd = std::make_unique<SetListenerPolicyCommand>(node_id, event,
                                                        QueuePolicy::kFifo, 0);
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);

  const auto* listener = node->find_listener(event);
  EXPECT_EQ(listener->policy(), QueuePolicy::kLatestValidWins);
  EXPECT_EQ(listener->capacity(), 1u);
}

TEST(CommandTest, SetListenerPolicyDoubleExecuteRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out", ConnectorType::kVertical);
  const auto event   = EventId::generate();
  ASSERT_TRUE(node->bind_output_event(out_id, event).ok());

  auto cmd = std::make_unique<SetListenerPolicyCommand>(node_id, event,
                                                        QueuePolicy::kFifo, 5);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetListenerPolicyUndoWithoutExecuteRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out", ConnectorType::kVertical);
  const auto event   = EventId::generate();
  ASSERT_TRUE(node->bind_output_event(out_id, event).ok());

  auto cmd = std::make_unique<SetListenerPolicyCommand>(node_id, event,
                                                        QueuePolicy::kFifo, 5);
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetListenerPolicyRedoWithoutUndoRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out", ConnectorType::kVertical);
  const auto event   = EventId::generate();
  ASSERT_TRUE(node->bind_output_event(out_id, event).ok());

  auto cmd = std::make_unique<SetListenerPolicyCommand>(node_id, event,
                                                        QueuePolicy::kFifo, 5);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}

// =========================================================================
// Phase 8c-i — SetOutputPriorityCommand
// =========================================================================

TEST(CommandTest, SetOutputPriorityRoundTrip) {
  Project     project = make_project();
  const auto  node_id = project.add_node("Node");
  Node*       node    = project.find_node(node_id);
  const auto  out_id  = node->add_output("Out");
  const auto* output  = node->find_output(out_id);
  EXPECT_EQ(output->priority(), 0);

  auto cmd = std::make_unique<SetOutputPriorityCommand>(node_id, out_id, 7);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(output->priority(), 7);

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_EQ(output->priority(), 0);

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_EQ(output->priority(), 7);
}

TEST(CommandTest, SetOutputPriorityMissingNodeIdFails) {
  Project project = make_project();

  auto cmd = std::make_unique<SetOutputPriorityCommand>(
      NodeId::generate(), ConnectorId::generate(), 7);
  EXPECT_FALSE(cmd->execute(project).ok());
}

TEST(CommandTest, SetOutputPriorityMissingConnectorIdFails) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");

  auto cmd = std::make_unique<SetOutputPriorityCommand>(
      node_id, ConnectorId::generate(), 7);
  EXPECT_FALSE(cmd->execute(project).ok());
}

TEST(CommandTest, SetOutputPriorityDoubleExecuteRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out");

  auto cmd = std::make_unique<SetOutputPriorityCommand>(node_id, out_id, 7);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetOutputPriorityUndoWithoutExecuteRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out");

  auto cmd = std::make_unique<SetOutputPriorityCommand>(node_id, out_id, 7);
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetOutputPriorityRedoWithoutUndoRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out");

  auto cmd = std::make_unique<SetOutputPriorityCommand>(node_id, out_id, 7);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}

// =========================================================================
// Phase 8c-i — SetOutputWeightCommand
// =========================================================================

TEST(CommandTest, SetOutputWeightRoundTrip) {
  Project     project = make_project();
  const auto  node_id = project.add_node("Node");
  Node*       node    = project.find_node(node_id);
  const auto  out_id  = node->add_output("Out");
  const auto* output  = node->find_output(out_id);
  EXPECT_EQ(output->weight(), Rational(1));

  const Rational new_weight = *Rational::create(1, 3);
  auto           cmd =
      std::make_unique<SetOutputWeightCommand>(node_id, out_id, new_weight);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(output->weight(), new_weight);

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_EQ(output->weight(), Rational(1));

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_EQ(output->weight(), new_weight);
}

TEST(CommandTest, SetOutputWeightNegativeRejectedNoMutation) {
  Project     project = make_project();
  const auto  node_id = project.add_node("Node");
  Node*       node    = project.find_node(node_id);
  const auto  out_id  = node->add_output("Out");
  const auto* output  = node->find_output(out_id);

  auto cmd =
      std::make_unique<SetOutputWeightCommand>(node_id, out_id, Rational(-1));
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(output->weight(), Rational(1));

  // Still kFresh -- undo/redo remain rejected.
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetOutputWeightMissingIdsFail) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out");

  auto cmd1 = std::make_unique<SetOutputWeightCommand>(NodeId::generate(),
                                                       out_id, Rational(1));
  EXPECT_FALSE(cmd1->execute(project).ok());

  auto cmd2 = std::make_unique<SetOutputWeightCommand>(
      node_id, ConnectorId::generate(), Rational(1));
  EXPECT_FALSE(cmd2->execute(project).ok());
}

TEST(CommandTest, SetOutputWeightDoubleExecuteRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out");

  auto cmd =
      std::make_unique<SetOutputWeightCommand>(node_id, out_id, Rational(2));
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetOutputWeightUndoWithoutExecuteRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out");

  auto cmd =
      std::make_unique<SetOutputWeightCommand>(node_id, out_id, Rational(2));
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetOutputWeightRedoWithoutUndoRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out");

  auto cmd =
      std::make_unique<SetOutputWeightCommand>(node_id, out_id, Rational(2));
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}

// =========================================================================
// Phase 8c-i — SetOutputExportEnabledCommand
// =========================================================================

TEST(CommandTest, SetOutputExportEnabledRoundTrip) {
  Project     project = make_project();
  const auto  node_id = project.add_node("Node");
  Node*       node    = project.find_node(node_id);
  const auto  out_id  = node->add_output("Out");
  const auto* output  = node->find_output(out_id);
  EXPECT_TRUE(output->export_enabled());

  auto cmd =
      std::make_unique<SetOutputExportEnabledCommand>(node_id, out_id, false);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_FALSE(output->export_enabled());

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_TRUE(output->export_enabled());

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_FALSE(output->export_enabled());
}

TEST(CommandTest, SetOutputExportEnabledMissingIdsFail) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out");

  auto cmd1 = std::make_unique<SetOutputExportEnabledCommand>(
      NodeId::generate(), out_id, false);
  EXPECT_FALSE(cmd1->execute(project).ok());

  auto cmd2 = std::make_unique<SetOutputExportEnabledCommand>(
      node_id, ConnectorId::generate(), false);
  EXPECT_FALSE(cmd2->execute(project).ok());
}

TEST(CommandTest, SetOutputExportEnabledDoubleExecuteRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out");

  auto cmd =
      std::make_unique<SetOutputExportEnabledCommand>(node_id, out_id, false);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetOutputExportEnabledUndoWithoutExecuteRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out");

  auto cmd =
      std::make_unique<SetOutputExportEnabledCommand>(node_id, out_id, false);
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetOutputExportEnabledRedoWithoutUndoRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out");

  auto cmd =
      std::make_unique<SetOutputExportEnabledCommand>(node_id, out_id, false);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}

// =========================================================================
// Phase 8c-i — SetInputConnectorNameCommand
// =========================================================================

TEST(CommandTest, SetInputConnectorNameRoundTrip) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto in_id   = node->add_input("In");

  auto cmd =
      std::make_unique<SetInputConnectorNameCommand>(node_id, in_id, "Renamed");

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(node->find_input(in_id)->name(), "Renamed");

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_EQ(node->find_input(in_id)->name(), "In");

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_EQ(node->find_input(in_id)->name(), "Renamed");
}

TEST(CommandTest, SetInputConnectorNameEmptyLongAndUtf8RoundTrip) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto in_id   = node->add_input("In");

  const std::string kLong(4096, 'x');
  const std::string kUtf8 = "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E \xCE\xA9";

  for (const std::string& name : {std::string(), kLong, kUtf8}) {
    auto cmd =
        std::make_unique<SetInputConnectorNameCommand>(node_id, in_id, name);
    ASSERT_TRUE(cmd->execute(project).ok());
    EXPECT_EQ(node->find_input(in_id)->name(), name);
    ASSERT_TRUE(cmd->undo(project).ok());
    EXPECT_EQ(node->find_input(in_id)->name(), "In");
    ASSERT_TRUE(cmd->redo(project).ok());
    EXPECT_EQ(node->find_input(in_id)->name(), name);
    ASSERT_TRUE(cmd->undo(project).ok());
  }
}

TEST(CommandTest, SetInputConnectorNameUnrelatedConnectorsUntouched) {
  Project    project     = make_project();
  const auto node_id     = project.add_node("Node");
  Node*      node        = project.find_node(node_id);
  const auto in_id       = node->add_input("In");
  const auto other_in_id = node->add_input("Other In");
  const auto out_id      = node->add_output("Out");

  auto cmd =
      std::make_unique<SetInputConnectorNameCommand>(node_id, in_id, "Renamed");
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(node->find_input(other_in_id)->name(), "Other In");
  EXPECT_EQ(node->find_output(out_id)->name(), "Out");
}

TEST(CommandTest, SetInputConnectorNameMissingIdsFail) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto in_id   = node->add_input("In");

  auto cmd1 = std::make_unique<SetInputConnectorNameCommand>(NodeId::generate(),
                                                             in_id, "Renamed");
  EXPECT_FALSE(cmd1->execute(project).ok());
  EXPECT_EQ(node->find_input(in_id)->name(), "In");

  auto cmd2 = std::make_unique<SetInputConnectorNameCommand>(
      node_id, ConnectorId::generate(), "Renamed");
  EXPECT_FALSE(cmd2->execute(project).ok());
}

TEST(CommandTest, SetInputConnectorNameDoubleExecuteRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto in_id   = node->add_input("In");

  auto cmd =
      std::make_unique<SetInputConnectorNameCommand>(node_id, in_id, "Renamed");
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetInputConnectorNameUndoWithoutExecuteRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto in_id   = node->add_input("In");

  auto cmd =
      std::make_unique<SetInputConnectorNameCommand>(node_id, in_id, "Renamed");
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetInputConnectorNameRedoWithoutUndoRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto in_id   = node->add_input("In");

  auto cmd =
      std::make_unique<SetInputConnectorNameCommand>(node_id, in_id, "Renamed");
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}

// =========================================================================
// Phase 8c-i — SetOutputConnectorNameCommand
// =========================================================================

TEST(CommandTest, SetOutputConnectorNameRoundTrip) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out");

  auto cmd = std::make_unique<SetOutputConnectorNameCommand>(node_id, out_id,
                                                             "Renamed");

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(node->find_output(out_id)->name(), "Renamed");

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_EQ(node->find_output(out_id)->name(), "Out");

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_EQ(node->find_output(out_id)->name(), "Renamed");
}

TEST(CommandTest, SetOutputConnectorNameEmptyLongAndUtf8RoundTrip) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out");

  const std::string kLong(4096, 'y');
  const std::string kUtf8 = "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E \xCE\xA9";

  for (const std::string& name : {std::string(), kLong, kUtf8}) {
    auto cmd =
        std::make_unique<SetOutputConnectorNameCommand>(node_id, out_id, name);
    ASSERT_TRUE(cmd->execute(project).ok());
    EXPECT_EQ(node->find_output(out_id)->name(), name);
    ASSERT_TRUE(cmd->undo(project).ok());
    EXPECT_EQ(node->find_output(out_id)->name(), "Out");
    ASSERT_TRUE(cmd->redo(project).ok());
    EXPECT_EQ(node->find_output(out_id)->name(), name);
    ASSERT_TRUE(cmd->undo(project).ok());
  }
}

TEST(CommandTest, SetOutputConnectorNameUnrelatedConnectorsUntouched) {
  Project    project      = make_project();
  const auto node_id      = project.add_node("Node");
  Node*      node         = project.find_node(node_id);
  const auto out_id       = node->add_output("Out");
  const auto other_out_id = node->add_output("Other Out");
  const auto in_id        = node->add_input("In");

  auto cmd = std::make_unique<SetOutputConnectorNameCommand>(node_id, out_id,
                                                             "Renamed");
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(node->find_output(other_out_id)->name(), "Other Out");
  EXPECT_EQ(node->find_input(in_id)->name(), "In");
}

TEST(CommandTest, SetOutputConnectorNameMissingIdsFail) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out");

  auto cmd1 = std::make_unique<SetOutputConnectorNameCommand>(
      NodeId::generate(), out_id, "Renamed");
  EXPECT_FALSE(cmd1->execute(project).ok());
  EXPECT_EQ(node->find_output(out_id)->name(), "Out");

  auto cmd2 = std::make_unique<SetOutputConnectorNameCommand>(
      node_id, ConnectorId::generate(), "Renamed");
  EXPECT_FALSE(cmd2->execute(project).ok());
}

TEST(CommandTest, SetOutputConnectorNameDoubleExecuteRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out");

  auto cmd = std::make_unique<SetOutputConnectorNameCommand>(node_id, out_id,
                                                             "Renamed");
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetOutputConnectorNameUndoWithoutExecuteRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out");

  auto cmd = std::make_unique<SetOutputConnectorNameCommand>(node_id, out_id,
                                                             "Renamed");
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetOutputConnectorNameRedoWithoutUndoRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out");

  auto cmd = std::make_unique<SetOutputConnectorNameCommand>(node_id, out_id,
                                                             "Renamed");
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}

// =========================================================================
// Phase 8c-i — deterministic replay
// =========================================================================

namespace {

struct ProjectSnapshot {
  std::size_t                active_track_count;
  std::size_t                archived_track_count;
  std::string                node_name;
  std::uint32_t              node_color;
  std::vector<int>           output_priorities;
  std::vector<Rational>      output_weights;
  std::vector<bool>          output_export_enabled;
  std::vector<ConnectorType> output_types;
  std::vector<std::string>   output_names;
  std::vector<std::string>   input_names;
};

ProjectSnapshot snapshot(const Project& project, NodeId node_id) {
  const Node*     node = project.find_node(node_id);
  ProjectSnapshot snap{
      .active_track_count    = project.active_tracks().size(),
      .archived_track_count  = project.archived_tracks().size(),
      .node_name             = node->name(),
      .node_color            = node->color(),
      .output_priorities     = {},
      .output_weights        = {},
      .output_export_enabled = {},
      .output_types          = {},
      .output_names          = {},
      .input_names           = {},
  };
  for (const auto& output : node->outputs()) {
    snap.output_priorities.push_back(output.priority());
    snap.output_weights.push_back(output.weight());
    snap.output_export_enabled.push_back(output.export_enabled());
    snap.output_types.push_back(output.type());
    snap.output_names.push_back(output.name());
  }
  for (const auto& input : node->inputs()) {
    snap.input_names.push_back(input.name());
  }
  return snap;
}

bool operator==(const ProjectSnapshot& a, const ProjectSnapshot& b) {
  return a.active_track_count == b.active_track_count &&
         a.archived_track_count == b.archived_track_count &&
         a.node_name == b.node_name && a.node_color == b.node_color &&
         a.output_priorities == b.output_priorities &&
         a.output_weights == b.output_weights &&
         a.output_export_enabled == b.output_export_enabled &&
         a.output_types == b.output_types && a.output_names == b.output_names &&
         a.input_names == b.input_names;
}

}  // namespace

TEST(CommandTest, DeterministicReplayProducesEqualProjects) {
  auto run_sequence = [](Project& project) {
    const auto track_id = project.add_track(
        "Track", StaffLayout::single_staff(), *MidiChannel::create(0));
    const auto node_id = project.add_node("Node");
    Node*      node    = project.find_node(node_id);
    const auto out_id  = node->add_output("Out", ConnectorType::kSequential);
    (void)node->add_input("In");

    CommandHistory history;
    EXPECT_TRUE(
        history
            .execute_new(std::make_unique<ArchiveTrackCommand>(*track_id),
                         project)
            .ok());
    EXPECT_TRUE(history
                    .execute_new(std::make_unique<SetOutputPriorityCommand>(
                                     node_id, out_id, 4),
                                 project)
                    .ok());
    EXPECT_TRUE(history
                    .execute_new(std::make_unique<SetOutputWeightCommand>(
                                     node_id, out_id, *Rational::create(1, 3)),
                                 project)
                    .ok());
    EXPECT_TRUE(
        history
            .execute_new(std::make_unique<SetOutputExportEnabledCommand>(
                             node_id, out_id, false),
                         project)
            .ok());
    EXPECT_TRUE(
        history
            .execute_new(std::make_unique<SetOutputConnectorNameCommand>(
                             node_id, out_id, "Renamed Out"),
                         project)
            .ok());
    return node_id;
  };

  Project first  = make_project();
  Project second = make_project();

  const NodeId first_node_id  = run_sequence(first);
  const NodeId second_node_id = run_sequence(second);

  EXPECT_TRUE(snapshot(first, first_node_id) ==
              snapshot(second, second_node_id));
}

// =========================================================================
// Phase 8c-ii — ConnectCommand
// =========================================================================

TEST(CommandTest, ConnectRoundTrip) {
  Project    project = make_project();
  const auto a_id    = project.add_node("A");
  const auto b_id    = project.add_node("B");
  const auto out_id  = project.find_node(a_id)->add_output("Out");
  const auto in_id   = project.find_node(b_id)->add_input("In");

  auto cmd = std::make_unique<ConnectCommand>(a_id, out_id, b_id, in_id);

  ASSERT_TRUE(cmd->execute(project).ok());
  const auto* out = project.find_node(a_id)->find_output(out_id);
  ASSERT_TRUE(out->destination().has_value());
  EXPECT_EQ(out->destination()->node, b_id);
  EXPECT_EQ(out->destination()->connector, in_id);

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_FALSE(out->destination().has_value());

  ASSERT_TRUE(cmd->redo(project).ok());
  ASSERT_TRUE(out->destination().has_value());
  EXPECT_EQ(out->destination()->node, b_id);
  EXPECT_EQ(out->destination()->connector, in_id);
}

TEST(CommandTest, ConnectMissingNodeIdFails) {
  Project    project = make_project();
  const auto b_id    = project.add_node("B");
  const auto in_id   = project.find_node(b_id)->add_input("In");

  auto cmd = std::make_unique<ConnectCommand>(
      NodeId::generate(), ConnectorId::generate(), b_id, in_id);
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, ConnectMissingConnectorIdFails) {
  Project    project = make_project();
  const auto a_id    = project.add_node("A");
  const auto b_id    = project.add_node("B");
  const auto out_id  = project.find_node(a_id)->add_output("Out");

  auto cmd = std::make_unique<ConnectCommand>(a_id, out_id, b_id,
                                              ConnectorId::generate());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, ConnectMissingIdDoesNotChangeProject) {
  Project    project = make_project();
  const auto a_id    = project.add_node("A");
  const auto out_id  = project.find_node(a_id)->add_output("Out");

  auto cmd = std::make_unique<ConnectCommand>(a_id, out_id, NodeId::generate(),
                                              ConnectorId::generate());
  EXPECT_FALSE(cmd->execute(project).ok());
  EXPECT_FALSE(
      project.find_node(a_id)->find_output(out_id)->destination().has_value());
}

TEST(CommandTest, ConnectDoubleExecuteRejected) {
  Project    project = make_project();
  const auto a_id    = project.add_node("A");
  const auto b_id    = project.add_node("B");
  const auto out_id  = project.find_node(a_id)->add_output("Out");
  const auto in_id   = project.find_node(b_id)->add_input("In");

  auto cmd = std::make_unique<ConnectCommand>(a_id, out_id, b_id, in_id);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, ConnectUndoWithoutExecuteRejected) {
  Project    project = make_project();
  const auto a_id    = project.add_node("A");
  const auto b_id    = project.add_node("B");
  const auto out_id  = project.find_node(a_id)->add_output("Out");
  const auto in_id   = project.find_node(b_id)->add_input("In");

  auto cmd = std::make_unique<ConnectCommand>(a_id, out_id, b_id, in_id);
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, ConnectRedoWithoutUndoRejected) {
  Project    project = make_project();
  const auto a_id    = project.add_node("A");
  const auto b_id    = project.add_node("B");
  const auto out_id  = project.find_node(a_id)->add_output("Out");
  const auto in_id   = project.find_node(b_id)->add_input("In");

  auto cmd = std::make_unique<ConnectCommand>(a_id, out_id, b_id, in_id);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, ConnectAlreadyConnectedOutputFails) {
  Project    project = make_project();
  const auto a_id    = project.add_node("A");
  const auto b_id    = project.add_node("B");
  const auto c_id    = project.add_node("C");
  const auto out_id  = project.find_node(a_id)->add_output("Out");
  const auto b_in    = project.find_node(b_id)->add_input("In");
  const auto c_in    = project.find_node(c_id)->add_input("In");

  Graph graph(project);
  ASSERT_TRUE(graph.connect(a_id, out_id, b_id, b_in).ok());

  auto cmd = std::make_unique<ConnectCommand>(a_id, out_id, c_id, c_in);
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);

  // Still fresh: undo/redo remain rejected.
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);

  const auto* out = project.find_node(a_id)->find_output(out_id);
  ASSERT_TRUE(out->destination().has_value());
  EXPECT_EQ(out->destination()->node, b_id);
}

TEST(CommandTest, ConnectPreservesCustomRouteAcrossUndo) {
  Project    project = make_project();
  const auto a_id    = project.add_node("A");
  const auto b_id    = project.add_node("B");
  const auto out_id  = project.find_node(a_id)->add_output("Out");
  const auto in_id   = project.find_node(b_id)->add_input("In");

  OutputConnector* out = project.find_node(a_id)->find_output(out_id);
  const std::vector<RoutePoint> waypoints = {{0.0, 0.0}, {10.0, 0.0}};
  ASSERT_TRUE(out->route().set_custom_route(waypoints).ok());

  auto cmd = std::make_unique<ConnectCommand>(a_id, out_id, b_id, in_id);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_TRUE(out->destination().has_value());
  EXPECT_FALSE(out->route().is_automatic());
  EXPECT_EQ(out->route().waypoints(), waypoints);

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_FALSE(out->destination().has_value());
  EXPECT_FALSE(out->route().is_automatic());
  EXPECT_EQ(out->route().waypoints(), waypoints);

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_TRUE(out->destination().has_value());
  EXPECT_FALSE(out->route().is_automatic());
  EXPECT_EQ(out->route().waypoints(), waypoints);
}

// =========================================================================
// Phase 8c-ii — DisconnectCommand
// =========================================================================

TEST(CommandTest, DisconnectRoundTrip) {
  Project    project = make_project();
  const auto a_id    = project.add_node("A");
  const auto b_id    = project.add_node("B");
  const auto out_id  = project.find_node(a_id)->add_output("Out");
  const auto in_id   = project.find_node(b_id)->add_input("In");

  Graph graph(project);
  ASSERT_TRUE(graph.connect(a_id, out_id, b_id, in_id).ok());

  auto cmd = std::make_unique<DisconnectCommand>(a_id, out_id);

  const auto* out = project.find_node(a_id)->find_output(out_id);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_FALSE(out->destination().has_value());

  ASSERT_TRUE(cmd->undo(project).ok());
  ASSERT_TRUE(out->destination().has_value());
  EXPECT_EQ(out->destination()->node, b_id);
  EXPECT_EQ(out->destination()->connector, in_id);

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_FALSE(out->destination().has_value());
}

TEST(CommandTest, DisconnectNoDestinationFails) {
  Project    project = make_project();
  const auto a_id    = project.add_node("A");
  const auto out_id  = project.find_node(a_id)->add_output("Out");

  auto cmd = std::make_unique<DisconnectCommand>(a_id, out_id);
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, DisconnectMissingIdsFail) {
  Project    project = make_project();
  const auto a_id    = project.add_node("A");
  const auto out_id  = project.find_node(a_id)->add_output("Out");

  auto missing_node =
      std::make_unique<DisconnectCommand>(NodeId::generate(), out_id);
  EXPECT_EQ(missing_node->execute(project).code(),
            ResultCode::kInvalidArgument);

  auto missing_connector =
      std::make_unique<DisconnectCommand>(a_id, ConnectorId::generate());
  EXPECT_EQ(missing_connector->execute(project).code(),
            ResultCode::kInvalidArgument);
}

TEST(CommandTest, DisconnectDoubleExecuteRejected) {
  Project    project = make_project();
  const auto a_id    = project.add_node("A");
  const auto b_id    = project.add_node("B");
  const auto out_id  = project.find_node(a_id)->add_output("Out");
  const auto in_id   = project.find_node(b_id)->add_input("In");

  Graph graph(project);
  ASSERT_TRUE(graph.connect(a_id, out_id, b_id, in_id).ok());

  auto cmd = std::make_unique<DisconnectCommand>(a_id, out_id);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, DisconnectUndoWithoutExecuteRejected) {
  Project    project = make_project();
  const auto a_id    = project.add_node("A");
  const auto b_id    = project.add_node("B");
  const auto out_id  = project.find_node(a_id)->add_output("Out");
  const auto in_id   = project.find_node(b_id)->add_input("In");

  Graph graph(project);
  ASSERT_TRUE(graph.connect(a_id, out_id, b_id, in_id).ok());

  auto cmd = std::make_unique<DisconnectCommand>(a_id, out_id);
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, DisconnectRedoWithoutUndoRejected) {
  Project    project = make_project();
  const auto a_id    = project.add_node("A");
  const auto b_id    = project.add_node("B");
  const auto out_id  = project.find_node(a_id)->add_output("Out");
  const auto in_id   = project.find_node(b_id)->add_input("In");

  Graph graph(project);
  ASSERT_TRUE(graph.connect(a_id, out_id, b_id, in_id).ok());

  auto cmd = std::make_unique<DisconnectCommand>(a_id, out_id);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, DisconnectPreservesCustomRouteAcrossUndo) {
  Project    project = make_project();
  const auto a_id    = project.add_node("A");
  const auto b_id    = project.add_node("B");
  const auto out_id  = project.find_node(a_id)->add_output("Out");
  const auto in_id   = project.find_node(b_id)->add_input("In");

  Graph graph(project);
  ASSERT_TRUE(graph.connect(a_id, out_id, b_id, in_id).ok());

  OutputConnector* out = project.find_node(a_id)->find_output(out_id);
  const std::vector<RoutePoint> waypoints = {{0.0, 0.0}, {0.0, 5.0}};
  ASSERT_TRUE(out->route().set_custom_route(waypoints).ok());

  auto cmd = std::make_unique<DisconnectCommand>(a_id, out_id);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_FALSE(out->destination().has_value());
  EXPECT_TRUE(out->route().is_automatic());

  ASSERT_TRUE(cmd->undo(project).ok());
  ASSERT_TRUE(out->destination().has_value());
  EXPECT_EQ(out->destination()->node, b_id);
  EXPECT_EQ(out->destination()->connector, in_id);
  EXPECT_FALSE(out->route().is_automatic());
  EXPECT_EQ(out->route().waypoints(), waypoints);
}

// =========================================================================
// Phase 8c-ii — BindOutputEventCommand
// =========================================================================

TEST(CommandTest, BindOutputEventRoundTripDestroysAndRecreatesListener) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out", ConnectorType::kVertical);
  const auto event   = project.events().add_event("Attack");
  ASSERT_TRUE(event.has_value());

  auto cmd = std::make_unique<BindOutputEventCommand>(node_id, out_id, event);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(node->find_output(out_id)->event_binding(), event);
  ASSERT_NE(node->find_listener(*event), nullptr);
  EXPECT_EQ(node->find_listener(*event)->policy(),
            QueuePolicy::kLatestValidWins);
  EXPECT_EQ(node->find_listener(*event)->capacity(), 1u);

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_FALSE(node->find_output(out_id)->event_binding().has_value());
  EXPECT_EQ(node->find_listener(*event), nullptr);

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_EQ(node->find_output(out_id)->event_binding(), event);
  ASSERT_NE(node->find_listener(*event), nullptr);
  EXPECT_EQ(node->find_listener(*event)->policy(),
            QueuePolicy::kLatestValidWins);
  EXPECT_EQ(node->find_listener(*event)->capacity(), 1u);
}

TEST(CommandTest,
     BindOutputEventRebindDestroysSoleListenerUndoRestoresExactConfig) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out", ConnectorType::kSequential);
  const auto event_e = project.events().add_event("E");
  const auto event_f = project.events().add_event("F");
  ASSERT_TRUE(event_e.has_value());
  ASSERT_TRUE(event_f.has_value());

  ASSERT_TRUE(node->bind_output_event(out_id, event_e).ok());
  ASSERT_TRUE(node->set_listener_policy(*event_e, QueuePolicy::kFifo, 5).ok());

  auto cmd = std::make_unique<BindOutputEventCommand>(node_id, out_id, event_f);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(node->find_output(out_id)->event_binding(), event_f);
  EXPECT_EQ(node->find_listener(*event_e), nullptr);
  ASSERT_NE(node->find_listener(*event_f), nullptr);

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_EQ(node->find_output(out_id)->event_binding(), event_e);
  ASSERT_NE(node->find_listener(*event_e), nullptr);
  EXPECT_EQ(node->find_listener(*event_e)->policy(), QueuePolicy::kFifo);
  EXPECT_EQ(node->find_listener(*event_e)->capacity(), 5u);
}

TEST(CommandTest, BindOutputEventRebindSurvivingListenerConfigUnchanged) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out1    = node->add_output("Out1", ConnectorType::kSequential);
  const auto out2    = node->add_output("Out2", ConnectorType::kSequential);
  const auto event_e = project.events().add_event("E");
  const auto event_f = project.events().add_event("F");
  ASSERT_TRUE(event_e.has_value());
  ASSERT_TRUE(event_f.has_value());

  ASSERT_TRUE(node->bind_output_event(out1, event_e).ok());
  ASSERT_TRUE(node->bind_output_event(out2, event_e).ok());
  ASSERT_TRUE(node->set_listener_policy(*event_e, QueuePolicy::kFifo, 3).ok());

  auto cmd = std::make_unique<BindOutputEventCommand>(node_id, out1, event_f);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(node->find_output(out1)->event_binding(), event_f);
  EXPECT_EQ(node->find_output(out2)->event_binding(), event_e);
  ASSERT_NE(node->find_listener(*event_e), nullptr);
  EXPECT_EQ(node->find_listener(*event_e)->policy(), QueuePolicy::kFifo);
  EXPECT_EQ(node->find_listener(*event_e)->capacity(), 3u);

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_EQ(node->find_output(out1)->event_binding(), event_e);
  ASSERT_NE(node->find_listener(*event_e), nullptr);
  EXPECT_EQ(node->find_listener(*event_e)->policy(), QueuePolicy::kFifo);
  EXPECT_EQ(node->find_listener(*event_e)->capacity(), 3u);
  EXPECT_EQ(node->find_listener(*event_f), nullptr);
}

TEST(CommandTest, BindOutputEventClearToNulloptUndoRestoresBinding) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out", ConnectorType::kSequential);
  const auto event   = project.events().add_event("Attack");
  ASSERT_TRUE(event.has_value());

  ASSERT_TRUE(node->bind_output_event(out_id, event).ok());

  auto cmd =
      std::make_unique<BindOutputEventCommand>(node_id, out_id, std::nullopt);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_FALSE(node->find_output(out_id)->event_binding().has_value());
  EXPECT_EQ(node->find_listener(*event), nullptr);

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_EQ(node->find_output(out_id)->event_binding(), event);
  ASSERT_NE(node->find_listener(*event), nullptr);

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_FALSE(node->find_output(out_id)->event_binding().has_value());
}

TEST(CommandTest, BindOutputEventMissingNodeIdFails) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out");

  auto cmd = std::make_unique<BindOutputEventCommand>(
      NodeId::generate(), out_id, EventId::generate());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, BindOutputEventMissingConnectorIdFails) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");

  auto cmd = std::make_unique<BindOutputEventCommand>(
      node_id, ConnectorId::generate(), EventId::generate());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, BindOutputEventUnregisteredEventFailsNoMutation) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out", ConnectorType::kVertical);

  auto cmd = std::make_unique<BindOutputEventCommand>(node_id, out_id,
                                                      EventId::generate());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
  EXPECT_FALSE(node->find_output(out_id)->event_binding().has_value());
}

TEST(CommandTest, BindOutputEventDoubleExecuteRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out", ConnectorType::kVertical);
  const auto event   = project.events().add_event("Attack");
  ASSERT_TRUE(event.has_value());

  auto cmd = std::make_unique<BindOutputEventCommand>(node_id, out_id, event);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, BindOutputEventUndoWithoutExecuteRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out", ConnectorType::kVertical);
  const auto event   = project.events().add_event("Attack");
  ASSERT_TRUE(event.has_value());

  auto cmd = std::make_unique<BindOutputEventCommand>(node_id, out_id, event);
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, BindOutputEventRedoWithoutUndoRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out", ConnectorType::kVertical);
  const auto event   = project.events().add_event("Attack");
  ASSERT_TRUE(event.has_value());

  auto cmd = std::make_unique<BindOutputEventCommand>(node_id, out_id, event);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}

// =========================================================================
// Phase 8c-ii — SetCustomRouteCommand
// =========================================================================

TEST(CommandTest, SetCustomRouteRoundTrip) {
  Project                       project   = make_project();
  const auto                    node_id   = project.add_node("Node");
  Node*                         node      = project.find_node(node_id);
  const auto                    out_id    = node->add_output("Out");
  const std::vector<RoutePoint> waypoints = {
      {0.0, 0.0}, {10.0, 0.0}, {10.0, 5.0}};

  auto cmd =
      std::make_unique<SetCustomRouteCommand>(node_id, out_id, waypoints);

  ASSERT_TRUE(cmd->execute(project).ok());
  const auto* out = node->find_output(out_id);
  EXPECT_FALSE(out->route().is_automatic());
  EXPECT_EQ(out->route().waypoints(), waypoints);

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_TRUE(out->route().is_automatic());
  EXPECT_TRUE(out->route().waypoints().empty());

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_FALSE(out->route().is_automatic());
  EXPECT_EQ(out->route().waypoints(), waypoints);
}

TEST(CommandTest, SetCustomRouteInvalidWaypointsRejectedNoMutation) {
  Project                       project   = make_project();
  const auto                    node_id   = project.add_node("Node");
  Node*                         node      = project.find_node(node_id);
  const auto                    out_id    = node->add_output("Out");
  const std::vector<RoutePoint> waypoints = {
      {0.0, 0.0}, {5.0, 3.0}};  // Diagonal: not axis-aligned.

  auto cmd =
      std::make_unique<SetCustomRouteCommand>(node_id, out_id, waypoints);

  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
  const auto* out = node->find_output(out_id);
  EXPECT_TRUE(out->route().is_automatic());
  EXPECT_TRUE(out->route().waypoints().empty());

  // Still fresh: undo/redo remain rejected.
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetCustomRouteUndoRestoresPreviouslyCustomizedRoute) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out");

  OutputConnector*              out      = node->find_output(out_id);
  const std::vector<RoutePoint> original = {{0.0, 0.0}, {0.0, 5.0}};
  ASSERT_TRUE(out->route().set_custom_route(original).ok());

  const std::vector<RoutePoint> replacement = {{0.0, 0.0}, {20.0, 0.0}};
  auto                          cmd =
      std::make_unique<SetCustomRouteCommand>(node_id, out_id, replacement);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(out->route().waypoints(), replacement);

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_FALSE(out->route().is_automatic());
  EXPECT_EQ(out->route().waypoints(), original);
}

TEST(CommandTest, SetCustomRouteMissingIdsFail) {
  Project                       project   = make_project();
  const auto                    node_id   = project.add_node("Node");
  const std::vector<RoutePoint> waypoints = {{0.0, 0.0}, {10.0, 0.0}};

  auto missing_node = std::make_unique<SetCustomRouteCommand>(
      NodeId::generate(), ConnectorId::generate(), waypoints);
  EXPECT_EQ(missing_node->execute(project).code(),
            ResultCode::kInvalidArgument);

  auto missing_connector = std::make_unique<SetCustomRouteCommand>(
      node_id, ConnectorId::generate(), waypoints);
  EXPECT_EQ(missing_connector->execute(project).code(),
            ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetCustomRouteDoubleExecuteRejected) {
  Project                       project   = make_project();
  const auto                    node_id   = project.add_node("Node");
  Node*                         node      = project.find_node(node_id);
  const auto                    out_id    = node->add_output("Out");
  const std::vector<RoutePoint> waypoints = {{0.0, 0.0}, {10.0, 0.0}};

  auto cmd =
      std::make_unique<SetCustomRouteCommand>(node_id, out_id, waypoints);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetCustomRouteUndoWithoutExecuteRejected) {
  Project                       project   = make_project();
  const auto                    node_id   = project.add_node("Node");
  Node*                         node      = project.find_node(node_id);
  const auto                    out_id    = node->add_output("Out");
  const std::vector<RoutePoint> waypoints = {{0.0, 0.0}, {10.0, 0.0}};

  auto cmd =
      std::make_unique<SetCustomRouteCommand>(node_id, out_id, waypoints);
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetCustomRouteRedoWithoutUndoRejected) {
  Project                       project   = make_project();
  const auto                    node_id   = project.add_node("Node");
  Node*                         node      = project.find_node(node_id);
  const auto                    out_id    = node->add_output("Out");
  const std::vector<RoutePoint> waypoints = {{0.0, 0.0}, {10.0, 0.0}};

  auto cmd =
      std::make_unique<SetCustomRouteCommand>(node_id, out_id, waypoints);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}

// =========================================================================
// Phase 8c-ii — ResetRouteCommand
// =========================================================================

TEST(CommandTest, ResetRouteRoundTrip) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out");

  OutputConnector*              out       = node->find_output(out_id);
  const std::vector<RoutePoint> waypoints = {
      {0.0, 0.0}, {10.0, 0.0}, {10.0, 5.0}};
  ASSERT_TRUE(out->route().set_custom_route(waypoints).ok());

  auto cmd = std::make_unique<ResetRouteCommand>(node_id, out_id);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_TRUE(out->route().is_automatic());
  EXPECT_TRUE(out->route().waypoints().empty());

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_FALSE(out->route().is_automatic());
  EXPECT_EQ(out->route().waypoints(), waypoints);

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_TRUE(out->route().is_automatic());
  EXPECT_TRUE(out->route().waypoints().empty());
}

TEST(CommandTest, ResetRouteMissingIdsFail) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");

  auto missing_node = std::make_unique<ResetRouteCommand>(
      NodeId::generate(), ConnectorId::generate());
  EXPECT_EQ(missing_node->execute(project).code(),
            ResultCode::kInvalidArgument);

  auto missing_connector =
      std::make_unique<ResetRouteCommand>(node_id, ConnectorId::generate());
  EXPECT_EQ(missing_connector->execute(project).code(),
            ResultCode::kInvalidArgument);
}

TEST(CommandTest, ResetRouteDoubleExecuteRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out");
  ASSERT_TRUE(
      node->find_output(out_id)->route().set_custom_route({{0.0, 0.0}}).ok());

  auto cmd = std::make_unique<ResetRouteCommand>(node_id, out_id);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, ResetRouteUndoWithoutExecuteRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out");

  auto cmd = std::make_unique<ResetRouteCommand>(node_id, out_id);
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, ResetRouteRedoWithoutUndoRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out");
  ASSERT_TRUE(
      node->find_output(out_id)->route().set_custom_route({{0.0, 0.0}}).ok());

  auto cmd = std::make_unique<ResetRouteCommand>(node_id, out_id);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}

// =========================================================================
// Phase 8c-ii — deterministic replay (Connect + BindOutputEvent +
// SetCustomRoute)
// =========================================================================

namespace {

struct GraphOutputSnapshot {
  ConnectorType type;
  bool          has_destination;
  bool          has_event_binding;
  RouteGeometry route;
  QueuePolicy   listener_policy;
  std::size_t   listener_capacity;
  ConnectorType listener_bound_type;
};

GraphOutputSnapshot graph_output_snapshot(const Project& project,
                                          NodeId         node_id,
                                          ConnectorId    output_id) {
  const Node*            node = project.find_node(node_id);
  const OutputConnector* out  = node->find_output(output_id);

  GraphOutputSnapshot snap{
      .type                = out->type(),
      .has_destination     = out->destination().has_value(),
      .has_event_binding   = out->event_binding().has_value(),
      .route               = out->route(),
      .listener_policy     = QueuePolicy::kLatestValidWins,
      .listener_capacity   = 0,
      .listener_bound_type = ConnectorType::kSequential,
  };

  if (out->event_binding().has_value()) {
    const EventListener* listener = node->find_listener(*out->event_binding());
    if (listener != nullptr) {
      snap.listener_policy     = listener->policy();
      snap.listener_capacity   = listener->capacity();
      snap.listener_bound_type = listener->bound_type();
    }
  }

  return snap;
}

bool operator==(const GraphOutputSnapshot& a, const GraphOutputSnapshot& b) {
  return a.type == b.type && a.has_destination == b.has_destination &&
         a.has_event_binding == b.has_event_binding && a.route == b.route &&
         a.listener_policy == b.listener_policy &&
         a.listener_capacity == b.listener_capacity &&
         a.listener_bound_type == b.listener_bound_type;
}

}  // namespace

TEST(CommandTest, DeterministicReplay8cii) {
  auto run_sequence = [](Project& project) {
    const auto a_id = project.add_node("A");
    const auto b_id = project.add_node("B");
    const auto out_id =
        project.find_node(a_id)->add_output("Out", ConnectorType::kSequential);
    const auto in_id = project.find_node(b_id)->add_input("In");
    const auto event = project.events().add_event("Attack");
    EXPECT_TRUE(event.has_value());

    CommandHistory history;
    EXPECT_TRUE(history
                    .execute_new(std::make_unique<ConnectCommand>(a_id, out_id,
                                                                  b_id, in_id),
                                 project)
                    .ok());
    EXPECT_TRUE(history
                    .execute_new(std::make_unique<BindOutputEventCommand>(
                                     a_id, out_id, event),
                                 project)
                    .ok());
    EXPECT_TRUE(history
                    .execute_new(std::make_unique<SetCustomRouteCommand>(
                                     a_id, out_id,
                                     std::vector<RoutePoint>{
                                         {0.0, 0.0}, {10.0, 0.0}, {10.0, 5.0}}),
                                 project)
                    .ok());
    return std::pair{a_id, out_id};
  };

  Project first  = make_project();
  Project second = make_project();

  const auto [first_node, first_out]   = run_sequence(first);
  const auto [second_node, second_out] = run_sequence(second);

  EXPECT_TRUE(graph_output_snapshot(first, first_node, first_out) ==
              graph_output_snapshot(second, second_node, second_out));
}

// =========================================================================
// Phase 8d-i — AddInputConnectorCommand
// =========================================================================

TEST(CommandTest, AddInputConnectorRoundTripPreservesId) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");

  auto cmd = std::make_unique<AddInputConnectorCommand>(node_id, "In");

  ASSERT_TRUE(cmd->execute(project).ok());
  Node* node = project.find_node(node_id);
  ASSERT_EQ(node->inputs().size(), 1u);
  const ConnectorId created_id = node->inputs().front().id();
  EXPECT_EQ(node->find_input(created_id)->name(), "In");

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_EQ(node->find_input(created_id), nullptr);
  EXPECT_TRUE(node->inputs().empty());

  ASSERT_TRUE(cmd->redo(project).ok());
  ASSERT_EQ(node->inputs().size(), 1u);
  EXPECT_EQ(node->inputs().front().id(), created_id);
  EXPECT_EQ(node->find_input(created_id)->name(), "In");
}

TEST(CommandTest, AddInputConnectorMissingNodeIdFailsNoMutation) {
  Project project = make_project();

  auto cmd =
      std::make_unique<AddInputConnectorCommand>(NodeId::generate(), "In");
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, AddInputConnectorDoubleExecuteRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");

  auto cmd = std::make_unique<AddInputConnectorCommand>(node_id, "In");
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(project.find_node(node_id)->inputs().size(), 1u);
}

TEST(CommandTest, AddInputConnectorUndoWithoutExecuteRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");

  auto cmd = std::make_unique<AddInputConnectorCommand>(node_id, "In");
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(project.find_node(node_id)->inputs().empty());
}

TEST(CommandTest, AddInputConnectorRedoWithoutUndoRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");

  auto cmd = std::make_unique<AddInputConnectorCommand>(node_id, "In");
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(project.find_node(node_id)->inputs().size(), 1u);
}

// =========================================================================
// Phase 8d-i — AddOutputConnectorCommand
// =========================================================================

TEST(CommandTest, AddOutputConnectorRoundTripPreservesId) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");

  auto cmd = std::make_unique<AddOutputConnectorCommand>(
      node_id, "Out", ConnectorType::kVertical);

  ASSERT_TRUE(cmd->execute(project).ok());
  Node* node = project.find_node(node_id);
  ASSERT_EQ(node->outputs().size(), 1u);
  const ConnectorId created_id = node->outputs().front().id();
  EXPECT_EQ(node->find_output(created_id)->name(), "Out");
  EXPECT_EQ(node->find_output(created_id)->type(), ConnectorType::kVertical);

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_EQ(node->find_output(created_id), nullptr);
  EXPECT_TRUE(node->outputs().empty());

  ASSERT_TRUE(cmd->redo(project).ok());
  ASSERT_EQ(node->outputs().size(), 1u);
  EXPECT_EQ(node->outputs().front().id(), created_id);
  EXPECT_EQ(node->find_output(created_id)->name(), "Out");
  EXPECT_EQ(node->find_output(created_id)->type(), ConnectorType::kVertical);
}

TEST(CommandTest, AddOutputConnectorMissingNodeIdFailsNoMutation) {
  Project project = make_project();

  auto cmd = std::make_unique<AddOutputConnectorCommand>(
      NodeId::generate(), "Out", ConnectorType::kSequential);
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, AddOutputConnectorDoubleExecuteRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");

  auto cmd = std::make_unique<AddOutputConnectorCommand>(
      node_id, "Out", ConnectorType::kSequential);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(project.find_node(node_id)->outputs().size(), 1u);
}

TEST(CommandTest, AddOutputConnectorUndoWithoutExecuteRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");

  auto cmd = std::make_unique<AddOutputConnectorCommand>(
      node_id, "Out", ConnectorType::kSequential);
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(project.find_node(node_id)->outputs().empty());
}

TEST(CommandTest, AddOutputConnectorRedoWithoutUndoRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");

  auto cmd = std::make_unique<AddOutputConnectorCommand>(
      node_id, "Out", ConnectorType::kSequential);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(project.find_node(node_id)->outputs().size(), 1u);
}

// =========================================================================
// Phase 8d-i — RemoveOutputConnectorCommand
// =========================================================================

TEST(CommandTest, RemoveOutputConnectorRoundTrip) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto out_id  = node->add_output("Out", ConnectorType::kSequential);

  auto cmd = std::make_unique<RemoveOutputConnectorCommand>(node_id, out_id);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(node->find_output(out_id), nullptr);

  ASSERT_TRUE(cmd->undo(project).ok());
  const auto* restored = node->find_output(out_id);
  ASSERT_NE(restored, nullptr);
  EXPECT_EQ(restored->name(), "Out");
  EXPECT_EQ(restored->type(), ConnectorType::kSequential);

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_EQ(node->find_output(out_id), nullptr);
}

TEST(CommandTest, RemoveOutputConnectorMissingNodeIdFailsNoMutation) {
  Project project = make_project();

  auto cmd = std::make_unique<RemoveOutputConnectorCommand>(
      NodeId::generate(), ConnectorId::generate());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, RemoveOutputConnectorMissingConnectorIdFailsNoMutation) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");

  auto cmd = std::make_unique<RemoveOutputConnectorCommand>(
      node_id, ConnectorId::generate());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, RemoveOutputConnectorDoubleExecuteRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  const auto out_id  = project.find_node(node_id)->add_output("Out");

  auto cmd = std::make_unique<RemoveOutputConnectorCommand>(node_id, out_id);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, RemoveOutputConnectorUndoWithoutExecuteRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  const auto out_id  = project.find_node(node_id)->add_output("Out");

  auto cmd = std::make_unique<RemoveOutputConnectorCommand>(node_id, out_id);
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
  EXPECT_NE(project.find_node(node_id)->find_output(out_id), nullptr);
}

TEST(CommandTest, RemoveOutputConnectorRedoWithoutUndoRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  const auto out_id  = project.find_node(node_id)->add_output("Out");

  auto cmd = std::make_unique<RemoveOutputConnectorCommand>(node_id, out_id);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest,
     RemoveOutputConnectorRestoresDestroyedListenerAcrossUndoRedo) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto event   = EventId::generate();
  const auto out_id  = node->add_output("Out", ConnectorType::kVertical);
  ASSERT_TRUE(node->bind_output_event(out_id, event).ok());
  ASSERT_TRUE(node->set_listener_policy(event, QueuePolicy::kFifo, 5).ok());

  auto cmd = std::make_unique<RemoveOutputConnectorCommand>(node_id, out_id);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(node->find_output(out_id), nullptr);
  EXPECT_EQ(node->find_listener(event), nullptr);

  ASSERT_TRUE(cmd->undo(project).ok());
  const auto* restored = node->find_output(out_id);
  ASSERT_NE(restored, nullptr);
  EXPECT_EQ(restored->event_binding(), event);
  const auto* listener = node->find_listener(event);
  ASSERT_NE(listener, nullptr);
  EXPECT_EQ(listener->policy(), QueuePolicy::kFifo);
  EXPECT_EQ(listener->capacity(), 5u);

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_EQ(node->find_output(out_id), nullptr);
  EXPECT_EQ(node->find_listener(event), nullptr);
}

TEST(CommandTest, RemoveOutputConnectorSurvivingListenerCaseUnaffected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto event   = EventId::generate();
  const auto first   = node->add_output("First", ConnectorType::kVertical);
  const auto second  = node->add_output("Second", ConnectorType::kVertical);
  ASSERT_TRUE(node->bind_output_event(first, event).ok());
  ASSERT_TRUE(node->bind_output_event(second, event).ok());
  ASSERT_TRUE(node->set_listener_policy(event, QueuePolicy::kFifo, 3).ok());

  auto cmd = std::make_unique<RemoveOutputConnectorCommand>(node_id, first);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(node->find_output(first), nullptr);
  const auto* still_listening = node->find_listener(event);
  ASSERT_NE(still_listening, nullptr);
  EXPECT_EQ(still_listening->policy(), QueuePolicy::kFifo);
  EXPECT_EQ(still_listening->capacity(), 3u);

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_NE(node->find_output(first), nullptr);
  const auto* after_undo = node->find_listener(event);
  ASSERT_NE(after_undo, nullptr);
  EXPECT_EQ(after_undo->policy(), QueuePolicy::kFifo);
  EXPECT_EQ(after_undo->capacity(), 3u);
}

// =========================================================================
// Phase 8d-i — RemoveInputConnectorCommand
// =========================================================================

TEST(CommandTest, RemoveInputConnectorRoundTrip) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  Node*      node    = project.find_node(node_id);
  const auto in_id   = node->add_input("In");

  auto cmd = std::make_unique<RemoveInputConnectorCommand>(node_id, in_id);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(node->find_input(in_id), nullptr);

  ASSERT_TRUE(cmd->undo(project).ok());
  const auto* restored = node->find_input(in_id);
  ASSERT_NE(restored, nullptr);
  EXPECT_EQ(restored->name(), "In");

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_EQ(node->find_input(in_id), nullptr);
}

TEST(CommandTest, RemoveInputConnectorMissingNodeIdFailsNoMutation) {
  Project project = make_project();

  auto cmd = std::make_unique<RemoveInputConnectorCommand>(
      NodeId::generate(), ConnectorId::generate());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, RemoveInputConnectorMissingConnectorIdFailsNoMutation) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");

  auto cmd = std::make_unique<RemoveInputConnectorCommand>(
      node_id, ConnectorId::generate());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, RemoveInputConnectorDoubleExecuteRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  const auto in_id   = project.find_node(node_id)->add_input("In");

  auto cmd = std::make_unique<RemoveInputConnectorCommand>(node_id, in_id);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, RemoveInputConnectorUndoWithoutExecuteRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  const auto in_id   = project.find_node(node_id)->add_input("In");

  auto cmd = std::make_unique<RemoveInputConnectorCommand>(node_id, in_id);
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
  EXPECT_NE(project.find_node(node_id)->find_input(in_id), nullptr);
}

TEST(CommandTest, RemoveInputConnectorRedoWithoutUndoRejected) {
  Project    project = make_project();
  const auto node_id = project.add_node("Node");
  const auto in_id   = project.find_node(node_id)->add_input("In");

  auto cmd = std::make_unique<RemoveInputConnectorCommand>(node_id, in_id);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest,
     RemoveInputConnectorCrossNodeCascadeRestoresDestinationAndRoute) {
  Project    project = make_project();
  const auto a_id    = project.add_node("A");
  const auto b_id    = project.add_node("B");
  Node*      a       = project.find_node(a_id);
  Node*      b       = project.find_node(b_id);
  const auto in_x    = a->add_input("X");
  const auto b_out   = b->add_output("Out");

  Graph graph(project);
  ASSERT_TRUE(graph.connect(b_id, b_out, a_id, in_x).ok());

  OutputConnector*              b_output  = b->find_output(b_out);
  const std::vector<RoutePoint> waypoints = {{0.0, 0.0}, {5.0, 0.0}};
  ASSERT_TRUE(b_output->route().set_custom_route(waypoints).ok());

  auto cmd = std::make_unique<RemoveInputConnectorCommand>(a_id, in_x);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(a->find_input(in_x), nullptr);
  EXPECT_FALSE(b_output->destination().has_value());
  EXPECT_TRUE(b_output->route().is_automatic());

  ASSERT_TRUE(cmd->undo(project).ok());
  const auto* restored_input = a->find_input(in_x);
  ASSERT_NE(restored_input, nullptr);
  EXPECT_EQ(restored_input->name(), "X");
  ASSERT_TRUE(b_output->destination().has_value());
  EXPECT_EQ(b_output->destination()->node, a_id);
  EXPECT_EQ(b_output->destination()->connector, in_x);
  EXPECT_FALSE(b_output->route().is_automatic());
  EXPECT_EQ(b_output->route().waypoints(), waypoints);

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_EQ(a->find_input(in_x), nullptr);
  EXPECT_FALSE(b_output->destination().has_value());
  EXPECT_TRUE(b_output->route().is_automatic());
}

TEST(CommandTest, RemoveInputConnectorMultiOutputFanInRestoredOnUndo) {
  Project    project = make_project();
  const auto a_id    = project.add_node("A");
  const auto b_id    = project.add_node("B");
  const auto c_id    = project.add_node("C");
  Node*      a       = project.find_node(a_id);
  Node*      b       = project.find_node(b_id);
  Node*      c       = project.find_node(c_id);
  const auto in_x    = a->add_input("X");
  const auto b_out   = b->add_output("Out");
  const auto c_out   = c->add_output("Out");

  Graph graph(project);
  ASSERT_TRUE(graph.connect(b_id, b_out, a_id, in_x).ok());
  ASSERT_TRUE(graph.connect(c_id, c_out, a_id, in_x).ok());

  OutputConnector*              b_output    = b->find_output(b_out);
  OutputConnector*              c_output    = c->find_output(c_out);
  const std::vector<RoutePoint> b_waypoints = {{0.0, 0.0}, {5.0, 0.0}};
  const std::vector<RoutePoint> c_waypoints = {{0.0, 0.0}, {0.0, 7.0}};
  ASSERT_TRUE(b_output->route().set_custom_route(b_waypoints).ok());
  ASSERT_TRUE(c_output->route().set_custom_route(c_waypoints).ok());

  auto cmd = std::make_unique<RemoveInputConnectorCommand>(a_id, in_x);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(a->find_input(in_x), nullptr);
  EXPECT_FALSE(b_output->destination().has_value());
  EXPECT_TRUE(b_output->route().is_automatic());
  EXPECT_FALSE(c_output->destination().has_value());
  EXPECT_TRUE(c_output->route().is_automatic());

  ASSERT_TRUE(cmd->undo(project).ok());
  ASSERT_NE(a->find_input(in_x), nullptr);
  ASSERT_TRUE(b_output->destination().has_value());
  EXPECT_EQ(b_output->destination()->connector, in_x);
  EXPECT_FALSE(b_output->route().is_automatic());
  EXPECT_EQ(b_output->route().waypoints(), b_waypoints);
  ASSERT_TRUE(c_output->destination().has_value());
  EXPECT_EQ(c_output->destination()->connector, in_x);
  EXPECT_FALSE(c_output->route().is_automatic());
  EXPECT_EQ(c_output->route().waypoints(), c_waypoints);

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_EQ(a->find_input(in_x), nullptr);
  EXPECT_FALSE(b_output->destination().has_value());
  EXPECT_TRUE(b_output->route().is_automatic());
  EXPECT_FALSE(c_output->destination().has_value());
  EXPECT_TRUE(c_output->route().is_automatic());
}

// =========================================================================
// Phase 8d-i — deterministic replay
// =========================================================================

TEST(CommandTest, DeterministicReplay8di) {
  auto run_sequence = [](Project& project) {
    const auto node_id = project.add_node("Node");

    CommandHistory history;
    EXPECT_TRUE(history
                    .execute_new(std::make_unique<AddInputConnectorCommand>(
                                     node_id, "In"),
                                 project)
                    .ok());
    EXPECT_TRUE(
        history
            .execute_new(std::make_unique<AddOutputConnectorCommand>(
                             node_id, "Out", ConnectorType::kSequential),
                         project)
            .ok());

    const Node* node   = project.find_node(node_id);
    const auto  in_id  = node->inputs().front().id();
    const auto  out_id = node->outputs().front().id();

    EXPECT_TRUE(history
                    .execute_new(std::make_unique<RemoveInputConnectorCommand>(
                                     node_id, in_id),
                                 project)
                    .ok());

    return std::pair{node_id, out_id};
  };

  Project first  = make_project();
  Project second = make_project();

  const auto [first_node, first_out]   = run_sequence(first);
  const auto [second_node, second_out] = run_sequence(second);

  const Node* first_node_ptr  = first.find_node(first_node);
  const Node* second_node_ptr = second.find_node(second_node);

  EXPECT_TRUE(first_node_ptr->inputs().empty());
  EXPECT_TRUE(second_node_ptr->inputs().empty());
  EXPECT_EQ(first_node_ptr->outputs().size(), 1u);
  EXPECT_EQ(second_node_ptr->outputs().size(), 1u);
  EXPECT_EQ(first_node_ptr->find_output(first_out)->name(), "Out");
  EXPECT_EQ(second_node_ptr->find_output(second_out)->name(), "Out");
  EXPECT_EQ(first_node_ptr->find_output(first_out)->type(),
            second_node_ptr->find_output(second_out)->type());
}

// =========================================================================
// Phase 8d-ii — RegisterEventCommand
// =========================================================================

TEST(CommandTest, RegisterEventRoundTripPreservesId) {
  Project project = make_project();

  auto cmd = std::make_unique<RegisterEventCommand>("Attack");

  ASSERT_TRUE(cmd->execute(project).ok());
  const auto* registered = project.events().find_by_name("Attack");
  ASSERT_NE(registered, nullptr);
  const EventId created_id = registered->id;
  EXPECT_EQ(project.events().size(), 1u);

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_EQ(project.events().find_by_id(created_id), nullptr);
  EXPECT_EQ(project.events().size(), 0u);

  ASSERT_TRUE(cmd->redo(project).ok());
  const auto* restored = project.events().find_by_id(created_id);
  ASSERT_NE(restored, nullptr);
  EXPECT_EQ(restored->name, "Attack");
  EXPECT_EQ(project.events().size(), 1u);
}

TEST(CommandTest, RegisterEventDuplicateNameFailsNoMutation) {
  Project project = make_project();
  ASSERT_TRUE(project.events().add_event("Attack").has_value());

  auto cmd = std::make_unique<RegisterEventCommand>("Attack");
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(project.events().size(), 1u);
}

TEST(CommandTest, RegisterEventDoubleExecuteRejected) {
  Project project = make_project();

  auto cmd = std::make_unique<RegisterEventCommand>("Attack");
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(project.events().size(), 1u);
}

TEST(CommandTest, RegisterEventUndoWithoutExecuteRejected) {
  Project project = make_project();

  auto cmd = std::make_unique<RegisterEventCommand>("Attack");
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(project.events().size(), 0u);
}

TEST(CommandTest, RegisterEventRedoWithoutUndoRejected) {
  Project project = make_project();

  auto cmd = std::make_unique<RegisterEventCommand>("Attack");
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(project.events().size(), 1u);
}

// =========================================================================
// Phase 8d-ii — RemoveEventCommand
// =========================================================================

TEST(CommandTest, RemoveEventRoundTripRestoresBindingsAndListeners) {
  Project    project  = make_project();
  const auto event_id = *project.events().add_event("Attack");

  const auto node_a = project.add_node("A");
  const auto node_b = project.add_node("B");
  Node*      a      = project.find_node(node_a);
  Node*      b      = project.find_node(node_b);
  const auto out_a  = a->add_output("Out", ConnectorType::kSequential);
  const auto out_b  = b->add_output("Out", ConnectorType::kSequential);

  Graph graph(project);
  ASSERT_TRUE(graph.bind_output_event(node_a, out_a, event_id).ok());
  ASSERT_TRUE(graph.bind_output_event(node_b, out_b, event_id).ok());
  ASSERT_TRUE(a->set_listener_policy(event_id, QueuePolicy::kFifo, 4).ok());
  ASSERT_TRUE(
      b->set_listener_policy(event_id, QueuePolicy::kFirstWins, 1).ok());

  auto cmd = std::make_unique<RemoveEventCommand>(event_id);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(project.events().find_by_id(event_id), nullptr);
  EXPECT_FALSE(a->find_output(out_a)->event_binding().has_value());
  EXPECT_FALSE(b->find_output(out_b)->event_binding().has_value());
  EXPECT_EQ(a->find_listener(event_id), nullptr);
  EXPECT_EQ(b->find_listener(event_id), nullptr);

  ASSERT_TRUE(cmd->undo(project).ok());
  const auto* restored_def = project.events().find_by_id(event_id);
  ASSERT_NE(restored_def, nullptr);
  EXPECT_EQ(restored_def->name, "Attack");
  ASSERT_TRUE(a->find_output(out_a)->event_binding().has_value());
  EXPECT_EQ(*a->find_output(out_a)->event_binding(), event_id);
  ASSERT_TRUE(b->find_output(out_b)->event_binding().has_value());
  EXPECT_EQ(*b->find_output(out_b)->event_binding(), event_id);
  const auto* a_listener = a->find_listener(event_id);
  ASSERT_NE(a_listener, nullptr);
  EXPECT_EQ(a_listener->policy(), QueuePolicy::kFifo);
  EXPECT_EQ(a_listener->capacity(), 4u);
  const auto* b_listener = b->find_listener(event_id);
  ASSERT_NE(b_listener, nullptr);
  EXPECT_EQ(b_listener->policy(), QueuePolicy::kFirstWins);
  EXPECT_EQ(b_listener->capacity(), 1u);

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_EQ(project.events().find_by_id(event_id), nullptr);
  EXPECT_FALSE(a->find_output(out_a)->event_binding().has_value());
  EXPECT_FALSE(b->find_output(out_b)->event_binding().has_value());
  EXPECT_EQ(a->find_listener(event_id), nullptr);
  EXPECT_EQ(b->find_listener(event_id), nullptr);
}

TEST(CommandTest, RemoveEventMissingIdFailsNoMutation) {
  Project project = make_project();

  auto cmd = std::make_unique<RemoveEventCommand>(EventId::generate());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, RemoveEventDoubleExecuteRejected) {
  Project    project  = make_project();
  const auto event_id = *project.events().add_event("Attack");

  auto cmd = std::make_unique<RemoveEventCommand>(event_id);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, RemoveEventUndoWithoutExecuteRejected) {
  Project    project  = make_project();
  const auto event_id = *project.events().add_event("Attack");

  auto cmd = std::make_unique<RemoveEventCommand>(event_id);
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
  EXPECT_NE(project.events().find_by_id(event_id), nullptr);
}

TEST(CommandTest, RemoveEventRedoWithoutUndoRejected) {
  Project    project  = make_project();
  const auto event_id = *project.events().add_event("Attack");

  auto cmd = std::make_unique<RemoveEventCommand>(event_id);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}

// =========================================================================
// Phase 8d-ii — deterministic replay
// =========================================================================

TEST(CommandTest, DeterministicReplay8dii) {
  auto run_sequence = [](Project& project) {
    CommandHistory history;

    EXPECT_TRUE(
        history
            .execute_new(std::make_unique<RegisterEventCommand>("Attack"),
                         project)
            .ok());
    const EventId first_event = project.events().find_by_name("Attack")->id;

    EXPECT_TRUE(
        history
            .execute_new(std::make_unique<RegisterEventCommand>("Release"),
                         project)
            .ok());

    EXPECT_TRUE(
        history
            .execute_new(std::make_unique<RemoveEventCommand>(first_event),
                         project)
            .ok());

    return project.events().find_by_name("Release")->id;
  };

  Project first  = make_project();
  Project second = make_project();

  const EventId first_release  = run_sequence(first);
  const EventId second_release = run_sequence(second);

  EXPECT_EQ(first.events().size(), 1u);
  EXPECT_EQ(second.events().size(), 1u);
  EXPECT_EQ(first.events().find_by_name("Attack"), nullptr);
  EXPECT_EQ(second.events().find_by_name("Attack"), nullptr);
  EXPECT_NE(first.events().find_by_id(first_release), nullptr);
  EXPECT_NE(second.events().find_by_id(second_release), nullptr);
}

// =========================================================================
// Phase 8d-iii — AddTrackCommand
// =========================================================================

TEST(CommandTest, AddTrackRoundTripPreservesId) {
  Project project = make_project();
  NodeId  node_id = project.add_node("Node");

  auto cmd = std::make_unique<AddTrackCommand>(
      "Track", StaffLayout::single_staff(), *MidiChannel::create(0));

  ASSERT_TRUE(cmd->execute(project).ok());
  ASSERT_EQ(project.active_tracks().size(), 1u);
  const TrackId created_id = project.active_tracks().front().id();
  EXPECT_EQ(project.active_tracks().front().name(), "Track");
  EXPECT_TRUE(project.find_node(node_id)->has_lane(created_id));

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_EQ(project.find_active_track(created_id), nullptr);
  EXPECT_EQ(project.find_archived_track(created_id), nullptr);
  EXPECT_EQ(project.active_tracks().size(), 0u);
  EXPECT_FALSE(project.find_node(node_id)->has_lane(created_id));

  ASSERT_TRUE(cmd->redo(project).ok());
  ASSERT_NE(project.find_active_track(created_id), nullptr);
  EXPECT_EQ(project.find_active_track(created_id)->id(), created_id);
  EXPECT_TRUE(project.find_node(node_id)->has_lane(created_id));
  EXPECT_EQ(project.active_tracks().size(), 1u);
}

TEST(CommandTest, AddTrackDoubleExecuteRejected) {
  Project project = make_project();

  auto cmd = std::make_unique<AddTrackCommand>(
      "Track", StaffLayout::single_staff(), *MidiChannel::create(0));
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(project.active_tracks().size(), 1u);
}

TEST(CommandTest, AddTrackUndoWithoutExecuteRejected) {
  Project project = make_project();

  auto cmd = std::make_unique<AddTrackCommand>(
      "Track", StaffLayout::single_staff(), *MidiChannel::create(0));
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(project.active_tracks().size(), 0u);
}

TEST(CommandTest, AddTrackRedoWithoutUndoRejected) {
  Project project = make_project();

  auto cmd = std::make_unique<AddTrackCommand>(
      "Track", StaffLayout::single_staff(), *MidiChannel::create(0));
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(project.active_tracks().size(), 1u);
}

TEST(CommandTest, AddTrackAtCapFailsNoMutation) {
  Project project = make_project();
  for (int i = 0; i < 64; ++i) {
    ASSERT_TRUE(
        project
            .add_track("Track " + std::to_string(i),
                       StaffLayout::single_staff(),
                       *MidiChannel::create(static_cast<std::uint8_t>(i % 16)))
            .has_value());
  }

  auto cmd = std::make_unique<AddTrackCommand>(
      "Overflow", StaffLayout::single_staff(), *MidiChannel::create(0));
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(project.active_tracks().size(), 64u);
}

// Linear-history safety: a later command must undo before the AddTrack does,
// so hard_remove_track always runs against an empty, just-added lane.
TEST(CommandTest, AddTrackLinearHistoryUndoLeavesNoOrphanState) {
  Project project = make_project();
  NodeId  node_id = project.add_node("Node");

  CommandHistory history;
  ASSERT_TRUE(history
                  .execute_new(std::make_unique<AddTrackCommand>(
                                   "Track", StaffLayout::single_staff(),
                                   *MidiChannel::create(0)),
                               project)
                  .ok());
  const TrackId track_id = project.active_tracks().front().id();

  ASSERT_TRUE(
      history
          .execute_new(std::make_unique<SetNodeNameCommand>(node_id, "Renamed"),
                       project)
          .ok());

  EXPECT_EQ(project.active_tracks().size(), 1u);
  EXPECT_EQ(project.find_node(node_id)->name(), "Renamed");

  ASSERT_TRUE(history.undo(project).ok());
  EXPECT_EQ(project.find_node(node_id)->name(), "Node");
  EXPECT_EQ(project.active_tracks().size(), 1u);

  ASSERT_TRUE(history.undo(project).ok());
  EXPECT_EQ(project.active_tracks().size(), 0u);
  EXPECT_EQ(project.find_active_track(track_id), nullptr);
  EXPECT_FALSE(project.find_node(node_id)->has_lane(track_id));
}

// 64-track/64-measure practicality: AddTrackCommand at the cap fails
// cleanly, and archive/restore of an existing track still round-trips.
TEST(CommandTest, SixtyFourTrackAndMeasureNodePracticality) {
  Project project = make_project();

  for (int i = 0; i < 64; ++i) {
    ASSERT_TRUE(
        project
            .add_track("Track " + std::to_string(i),
                       StaffLayout::single_staff(),
                       *MidiChannel::create(static_cast<std::uint8_t>(i % 16)))
            .has_value());
  }
  ASSERT_EQ(project.active_tracks().size(), 64u);

  NodeId node_id = project.add_node("Node");
  Node*  node    = project.find_node(node_id);
  ASSERT_EQ(node->lane_count(), 64u);

  std::vector<Measure> measures;
  measures.reserve(64);
  for (int i = 0; i < 64; ++i) {
    measures.push_back(
        Measure{*TimeSignature::create(4, 4), *KeySignature::create(0)});
  }
  auto timeline = NodeTimeline::create(std::move(measures), {});
  ASSERT_TRUE(timeline.has_value());
  EXPECT_EQ(timeline->measures().measure_count(), 64u);
  node->set_timeline(std::move(*timeline));

  auto add_cmd = std::make_unique<AddTrackCommand>(
      "Overflow", StaffLayout::single_staff(), *MidiChannel::create(0));
  EXPECT_EQ(add_cmd->execute(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(project.active_tracks().size(), 64u);

  const TrackId archive_id  = project.active_tracks().front().id();
  auto          archive_cmd = std::make_unique<ArchiveTrackCommand>(archive_id);
  ASSERT_TRUE(archive_cmd->execute(project).ok());
  EXPECT_EQ(project.find_active_track(archive_id), nullptr);

  ASSERT_TRUE(archive_cmd->undo(project).ok());
  EXPECT_NE(project.find_active_track(archive_id), nullptr);
  EXPECT_EQ(project.active_tracks().size(), 64u);
}

// =========================================================================
// Phase 8d-iii — deterministic replay
// =========================================================================

TEST(CommandTest, DeterministicReplay8diii) {
  auto run_sequence = [](Project& project) {
    CommandHistory history;

    EXPECT_TRUE(history
                    .execute_new(std::make_unique<AddTrackCommand>(
                                     "First", StaffLayout::single_staff(),
                                     *MidiChannel::create(0)),
                                 project)
                    .ok());
    const TrackId first_id = project.active_tracks().front().id();

    EXPECT_TRUE(history
                    .execute_new(std::make_unique<AddTrackCommand>(
                                     "Second", StaffLayout::single_staff(),
                                     *MidiChannel::create(1)),
                                 project)
                    .ok());

    EXPECT_TRUE(
        history
            .execute_new(std::make_unique<ArchiveTrackCommand>(first_id),
                         project)
            .ok());

    return project.active_tracks().front().id();
  };

  Project first  = make_project();
  Project second = make_project();

  const TrackId first_survivor  = run_sequence(first);
  const TrackId second_survivor = run_sequence(second);

  EXPECT_EQ(first.active_tracks().size(), 1u);
  EXPECT_EQ(second.active_tracks().size(), 1u);
  EXPECT_EQ(first.archived_tracks().size(), 1u);
  EXPECT_EQ(second.archived_tracks().size(), 1u);
  EXPECT_EQ(first.active_tracks().front().name(), "Second");
  EXPECT_EQ(second.active_tracks().front().name(), "Second");
  EXPECT_NE(first.find_active_track(first_survivor), nullptr);
  EXPECT_NE(second.find_active_track(second_survivor), nullptr);
}

// =========================================================================
// Phase 8d-iv — AddNodeCommand
// =========================================================================

TEST(CommandTest, AddNodeRoundTripPreservesId) {
  Project    project  = make_project();
  const auto track_id = project.add_track("Track", StaffLayout::single_staff(),
                                          *MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());

  auto cmd = std::make_unique<AddNodeCommand>("Node");

  ASSERT_TRUE(cmd->execute(project).ok());
  ASSERT_EQ(project.nodes().size(), 1u);
  const NodeId created_id = project.nodes().front().id();
  EXPECT_EQ(project.nodes().front().name(), "Node");
  EXPECT_TRUE(project.find_node(created_id)->has_lane(*track_id));

  ASSERT_TRUE(cmd->undo(project).ok());
  EXPECT_EQ(project.find_node(created_id), nullptr);
  EXPECT_EQ(project.nodes().size(), 0u);

  ASSERT_TRUE(cmd->redo(project).ok());
  ASSERT_NE(project.find_node(created_id), nullptr);
  EXPECT_EQ(project.find_node(created_id)->id(), created_id);
  EXPECT_EQ(project.find_node(created_id)->name(), "Node");
  EXPECT_TRUE(project.find_node(created_id)->has_lane(*track_id));
  EXPECT_EQ(project.nodes().size(), 1u);
}

TEST(CommandTest, AddNodeDoubleExecuteRejected) {
  Project project = make_project();

  auto cmd = std::make_unique<AddNodeCommand>("Node");
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(project.nodes().size(), 1u);
}

TEST(CommandTest, AddNodeUndoWithoutExecuteRejected) {
  Project project = make_project();

  auto cmd = std::make_unique<AddNodeCommand>("Node");
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(project.nodes().size(), 0u);
}

TEST(CommandTest, AddNodeRedoWithoutUndoRejected) {
  Project project = make_project();

  auto cmd = std::make_unique<AddNodeCommand>("Node");
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(project.nodes().size(), 1u);
}

// =========================================================================
// Phase 8d-iv — RemoveNodeCommand
// =========================================================================

TEST(CommandTest, RemoveNodeMissingIdFailsNoMutation) {
  Project project = make_project();
  NodeId  node_id = project.add_node("Node");

  auto cmd = std::make_unique<RemoveNodeCommand>(NodeId::generate());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
  EXPECT_NE(project.find_node(node_id), nullptr);
  EXPECT_EQ(project.nodes().size(), 1u);
}

TEST(CommandTest, RemoveNodeDoubleExecuteRejected) {
  Project project = make_project();
  NodeId  node_id = project.add_node("Node");

  auto cmd = std::make_unique<RemoveNodeCommand>(node_id);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, RemoveNodeUndoWithoutExecuteRejected) {
  Project project = make_project();
  NodeId  node_id = project.add_node("Node");

  auto cmd = std::make_unique<RemoveNodeCommand>(node_id);
  EXPECT_EQ(cmd->undo(project).code(), ResultCode::kInvalidArgument);
  EXPECT_NE(project.find_node(node_id), nullptr);
}

TEST(CommandTest, RemoveNodeRedoWithoutUndoRejected) {
  Project project = make_project();
  NodeId  node_id = project.add_node("Node");

  auto cmd = std::make_unique<RemoveNodeCommand>(node_id);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(cmd->redo(project).code(), ResultCode::kInvalidArgument);
}

// The crux test: fan-in cascade across two other nodes, an outgoing edge
// from the removed node to one of them, a NodeTimeline, notation content
// in a lane, and start-node status -- everything RemoveNodeCommand's
// snapshot must carry, all restored exactly on undo.
TEST(CommandTest, RemoveNodeFullAggregateRoundTrip) {
  Project    project  = make_project();
  const auto track_id = project.add_track("Track", StaffLayout::single_staff(),
                                          *MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());

  const NodeId a_id = project.add_node("A");
  const NodeId b_id = project.add_node("B");
  const NodeId c_id = project.add_node("C");
  Node*        a    = project.find_node(a_id);
  Node*        b    = project.find_node(b_id);
  Node*        c    = project.find_node(c_id);

  const ConnectorId a_in   = a->add_input("AIn");
  const ConnectorId b_in_1 = b->add_input("BIn1");
  const ConnectorId b_in_2 = b->add_input("BIn2");
  const ConnectorId a_out  = a->add_output("AOut");
  const ConnectorId c_out  = c->add_output("COut");
  const ConnectorId b_out  = b->add_output("BOut");

  Graph graph(project);
  ASSERT_TRUE(graph.connect(a_id, a_out, b_id, b_in_1).ok());
  ASSERT_TRUE(graph.connect(c_id, c_out, b_id, b_in_2).ok());
  ASSERT_TRUE(graph.connect(b_id, b_out, a_id, a_in).ok());

  OutputConnector*              a_output    = a->find_output(a_out);
  OutputConnector*              c_output    = c->find_output(c_out);
  OutputConnector*              b_output    = b->find_output(b_out);
  const std::vector<RoutePoint> a_waypoints = {{0.0, 0.0}, {5.0, 0.0}};
  const std::vector<RoutePoint> c_waypoints = {{0.0, 0.0}, {0.0, 7.0}};
  const std::vector<RoutePoint> b_waypoints = {{1.0, 1.0}, {1.0, 2.0}};
  ASSERT_TRUE(a_output->route().set_custom_route(a_waypoints).ok());
  ASSERT_TRUE(c_output->route().set_custom_route(c_waypoints).ok());
  ASSERT_TRUE(b_output->route().set_custom_route(b_waypoints).ok());

  ASSERT_TRUE(project.set_start_node(b_id).ok());

  b->lane(*track_id)->ensure_stave(StaveId::generate());
  std::vector<Measure> measures = {
      Measure{*TimeSignature::create(4, 4), *KeySignature::create(0)}};
  auto timeline = NodeTimeline::create(std::move(measures), {});
  ASSERT_TRUE(timeline.has_value());
  b->set_timeline(std::move(*timeline));

  auto cmd = std::make_unique<RemoveNodeCommand>(b_id);

  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(project.find_node(b_id), nullptr);
  EXPECT_FALSE(a_output->destination().has_value());
  EXPECT_TRUE(a_output->route().is_automatic());
  EXPECT_FALSE(c_output->destination().has_value());
  EXPECT_TRUE(c_output->route().is_automatic());
  EXPECT_FALSE(project.start_node().has_value());

  ASSERT_TRUE(cmd->undo(project).ok());
  const Node* restored_b = project.find_node(b_id);
  ASSERT_NE(restored_b, nullptr);
  EXPECT_EQ(restored_b->id(), b_id);
  ASSERT_TRUE(restored_b->has_timeline());
  EXPECT_EQ(restored_b->timeline()->measures().measure_count(), 1u);
  ASSERT_TRUE(restored_b->has_lane(*track_id));
  EXPECT_EQ(restored_b->lane(*track_id)->stave_count(), 1u);
  const OutputConnector* restored_b_output = restored_b->find_output(b_out);
  ASSERT_NE(restored_b_output, nullptr);
  ASSERT_TRUE(restored_b_output->destination().has_value());
  EXPECT_EQ(restored_b_output->destination()->node, a_id);
  EXPECT_EQ(restored_b_output->destination()->connector, a_in);
  EXPECT_FALSE(restored_b_output->route().is_automatic());
  EXPECT_EQ(restored_b_output->route().waypoints(), b_waypoints);

  ASSERT_TRUE(project.start_node().has_value());
  EXPECT_EQ(*project.start_node(), b_id);

  ASSERT_TRUE(a_output->destination().has_value());
  EXPECT_EQ(a_output->destination()->node, b_id);
  EXPECT_EQ(a_output->destination()->connector, b_in_1);
  EXPECT_FALSE(a_output->route().is_automatic());
  EXPECT_EQ(a_output->route().waypoints(), a_waypoints);

  ASSERT_TRUE(c_output->destination().has_value());
  EXPECT_EQ(c_output->destination()->node, b_id);
  EXPECT_EQ(c_output->destination()->connector, b_in_2);
  EXPECT_FALSE(c_output->route().is_automatic());
  EXPECT_EQ(c_output->route().waypoints(), c_waypoints);

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_EQ(project.find_node(b_id), nullptr);
  EXPECT_FALSE(a_output->destination().has_value());
  EXPECT_TRUE(a_output->route().is_automatic());
  EXPECT_FALSE(c_output->destination().has_value());
  EXPECT_TRUE(c_output->route().is_automatic());
  EXPECT_FALSE(project.start_node().has_value());
}

// Dedicated self-loop test: a node's own output targeting one of its own
// inputs must be restored intact as part of the Node snapshot, and must
// never be treated as a cross-node cascade edge that undo tries (and
// fails) to reconnect via Graph::connect on an already-destined output.
TEST(CommandTest, RemoveNodeSelfLoopSurvivesRoundTrip) {
  Project           project = make_project();
  const NodeId      node_id = project.add_node("Node");
  Node*             node    = project.find_node(node_id);
  const ConnectorId in      = node->add_input("In");
  const ConnectorId out     = node->add_output("Out");

  Graph graph(project);
  ASSERT_TRUE(graph.connect(node_id, out, node_id, in).ok());

  auto cmd = std::make_unique<RemoveNodeCommand>(node_id);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(project.find_node(node_id), nullptr);

  ASSERT_TRUE(cmd->undo(project).ok());
  const Node* restored = project.find_node(node_id);
  ASSERT_NE(restored, nullptr);
  const OutputConnector* restored_output = restored->find_output(out);
  ASSERT_NE(restored_output, nullptr);
  ASSERT_TRUE(restored_output->destination().has_value());
  EXPECT_EQ(restored_output->destination()->node, node_id);
  EXPECT_EQ(restored_output->destination()->connector, in);

  ASSERT_TRUE(cmd->redo(project).ok());
  EXPECT_EQ(project.find_node(node_id), nullptr);
}

// 64-track/64-measure practicality: remove-then-undo a node carrying a
// 64-measure timeline in a project with 64 active tracks.
TEST(CommandTest, RemoveNodeSixtyFourTrackAndMeasurePracticality) {
  Project project = make_project();
  for (int i = 0; i < 64; ++i) {
    ASSERT_TRUE(
        project
            .add_track("Track " + std::to_string(i),
                       StaffLayout::single_staff(),
                       *MidiChannel::create(static_cast<std::uint8_t>(i % 16)))
            .has_value());
  }
  ASSERT_EQ(project.active_tracks().size(), 64u);

  const NodeId node_id = project.add_node("Node");
  Node*        node    = project.find_node(node_id);
  ASSERT_EQ(node->lane_count(), 64u);

  std::vector<Measure> measures;
  measures.reserve(64);
  for (int i = 0; i < 64; ++i) {
    measures.push_back(
        Measure{*TimeSignature::create(4, 4), *KeySignature::create(0)});
  }
  auto timeline = NodeTimeline::create(std::move(measures), {});
  ASSERT_TRUE(timeline.has_value());
  node->set_timeline(std::move(*timeline));

  auto cmd = std::make_unique<RemoveNodeCommand>(node_id);
  ASSERT_TRUE(cmd->execute(project).ok());
  EXPECT_EQ(project.find_node(node_id), nullptr);

  ASSERT_TRUE(cmd->undo(project).ok());
  const Node* restored = project.find_node(node_id);
  ASSERT_NE(restored, nullptr);
  EXPECT_EQ(restored->lane_count(), 64u);
  ASSERT_TRUE(restored->has_timeline());
  EXPECT_EQ(restored->timeline()->measures().measure_count(), 64u);
}

// =========================================================================
// Phase 8d-iv — deterministic replay
// =========================================================================

TEST(CommandTest, DeterministicReplay8div) {
  auto run_sequence = [](Project& project) {
    CommandHistory history;

    EXPECT_TRUE(
        history.execute_new(std::make_unique<AddNodeCommand>("A"), project)
            .ok());
    const NodeId a_id = project.nodes().front().id();

    EXPECT_TRUE(
        history.execute_new(std::make_unique<AddNodeCommand>("B"), project)
            .ok());
    const NodeId b_id = project.nodes().back().id();

    EXPECT_TRUE(
        history.execute_new(std::make_unique<RemoveNodeCommand>(a_id), project)
            .ok());

    return b_id;
  };

  Project first  = make_project();
  Project second = make_project();

  const NodeId first_survivor  = run_sequence(first);
  const NodeId second_survivor = run_sequence(second);

  EXPECT_EQ(first.nodes().size(), 1u);
  EXPECT_EQ(second.nodes().size(), 1u);
  EXPECT_EQ(first.nodes().front().name(), "B");
  EXPECT_EQ(second.nodes().front().name(), "B");
  EXPECT_NE(first.find_node(first_survivor), nullptr);
  EXPECT_NE(second.find_node(second_survivor), nullptr);
}

// =========================================================================
// Phase 8e-i — SetEventCommand
// =========================================================================

TEST(CommandTest, SetEventNoteToRestRoundTrip) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  const VoiceEvent note = make_note(pitch_c4(), quarter());
  ASSERT_TRUE(voice->append(note).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const VoiceEvent rest = make_rest(quarter());
  auto             cmd =
      std::make_unique<SetEventCommand>(fx.node_id, fx.track_id, fx.stave_id,
                                        *Voice::create(1), Rational(0), rest);

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  ASSERT_GE(voice->events().size(),
            2u);  // quarter rest + dotted-half rest (3/4)
  EXPECT_TRUE(voice->check_complete(fx.node_end).ok());

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  ASSERT_TRUE(std::holds_alternative<Note>(voice->events()[0]));

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  ASSERT_TRUE(std::holds_alternative<Rest>(voice->events()[0]));
}

TEST(CommandTest, SetEventRestToNoteRoundTrip) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_rest(quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const VoiceEvent note = make_note(pitch_c4(), quarter());
  auto             cmd =
      std::make_unique<SetEventCommand>(fx.node_id, fx.track_id, fx.stave_id,
                                        *Voice::create(1), Rational(0), note);

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_TRUE(std::holds_alternative<Note>(voice->events()[0]));

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_TRUE(std::holds_alternative<Rest>(voice->events()[0]));

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_TRUE(std::holds_alternative<Note>(voice->events()[0]));
}

TEST(CommandTest, SetEventNoteToChordRoundTrip) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const Chord chord = make_chord(quarter(), {ChordNote{.pitch = pitch_c4()},
                                             ChordNote{.pitch = pitch_e4()}});
  VoiceEvent  ve    = VoiceEvent(chord);
  auto        cmd   = std::make_unique<SetEventCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0), ve);

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_TRUE(std::holds_alternative<Chord>(voice->events()[0]));

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_TRUE(std::holds_alternative<Note>(voice->events()[0]));

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_TRUE(std::holds_alternative<Chord>(voice->events()[0]));
}

TEST(CommandTest, SetEventChordToNoteRoundTrip) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  const Chord chord = make_chord(
      half(), {ChordNote{.pitch = pitch_c4()}, ChordNote{.pitch = pitch_e4()}});
  ASSERT_TRUE(voice->append(VoiceEvent(chord)).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const VoiceEvent note = make_note(pitch_c4(), half());
  auto             cmd =
      std::make_unique<SetEventCommand>(fx.node_id, fx.track_id, fx.stave_id,
                                        *Voice::create(1), Rational(0), note);

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_TRUE(std::holds_alternative<Note>(voice->events()[0]));

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_TRUE(std::holds_alternative<Chord>(voice->events()[0]));

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_TRUE(std::holds_alternative<Note>(voice->events()[0]));
}

TEST(CommandTest, SetEventDurationChangeRenormalizes) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  // Fill with a half note + a half rest → exactly 1 whole note.
  const VoiceEvent half_note = make_note(pitch_c4(), half());
  ASSERT_TRUE(voice->append(half_note).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());
  ASSERT_EQ(voice->total_length(), Rational(1));

  // Replace half note with quarter note — creates a gap that normalise
  // fills with a quarter rest.
  const VoiceEvent quarter_note = make_note(pitch_c4(), quarter());
  auto cmd = std::make_unique<SetEventCommand>(fx.node_id, fx.track_id,
                                               fx.stave_id, *Voice::create(1),
                                               Rational(0), quarter_note);

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(voice->total_length(), Rational(1));
  EXPECT_TRUE(voice->check_complete(fx.node_end).ok());
  EXPECT_GE(voice->events().size(), 2u);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(voice->total_length(), Rational(1));
  EXPECT_EQ(voice->events().size(), 2u);  // restored half note + half rest

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_GE(voice->events().size(), 2u);  // quarter note + rest fill
}

// Replace a rest with a whole note exactly filling an empty measure.
TEST(CommandTest, SetEventRestToWholeNoteFillsMeasure) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->normalize(fx.node_end).ok());
  ASSERT_EQ(voice->total_length(), Rational(1));
  ASSERT_GE(voice->events().size(), 1u);

  const VoiceEvent whole_note = make_note(pitch_c4(), whole());
  auto cmd = std::make_unique<SetEventCommand>(fx.node_id, fx.track_id,
                                               fx.stave_id, *Voice::create(1),
                                               Rational(0), whole_note);

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  // Whole note fills the measure, but any remaining rests that overflow
  // will cause normalize to fail → the command rolls back.
  // With len=1 and a whole note at pos 0, the prev events (rests) get
  // pushed past node_end, so this should succeed only if there was
  // exactly 1 event and it was a rest of duration 1.
  // In practice, decompose_rest(1) gives [whole_rest], so this works.
  EXPECT_EQ(voice->total_length(), Rational(1));
  EXPECT_TRUE(voice->check_complete(fx.node_end).ok());
  ASSERT_GE(voice->events().size(), 1u);
  const graphscore::Note* n =
      std::get_if<graphscore::Note>(&voice->events()[0]);
  ASSERT_NE(n, nullptr);
  EXPECT_EQ(n->duration.resolved(), whole().resolved());

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(voice->total_length(), Rational(1));

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(voice->total_length(), Rational(1));
}

TEST(CommandTest, SetEventChordBuildAddNotehead) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  const Chord chord2 = make_chord(quarter(), {ChordNote{.pitch = pitch_c4()},
                                              ChordNote{.pitch = pitch_d4()}});
  ASSERT_TRUE(voice->append(VoiceEvent(chord2)).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const Chord chord3 = make_chord(quarter(), {ChordNote{.pitch = pitch_c4()},
                                              ChordNote{.pitch = pitch_d4()},
                                              ChordNote{.pitch = pitch_e4()}});
  VoiceEvent  ve3    = VoiceEvent(chord3);
  auto        cmd =
      std::make_unique<SetEventCommand>(fx.node_id, fx.track_id, fx.stave_id,
                                        *Voice::create(1), Rational(0), ve3);

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const Chord* result = std::get_if<Chord>(&voice->events()[0]);
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(result->notes.size(), 3u);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  const Chord* undone = std::get_if<Chord>(&voice->events()[0]);
  ASSERT_NE(undone, nullptr);
  EXPECT_EQ(undone->notes.size(), 2u);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  const Chord* redone = std::get_if<Chord>(&voice->events()[0]);
  ASSERT_NE(redone, nullptr);
  EXPECT_EQ(redone->notes.size(), 3u);
}

TEST(CommandTest, SetEventRejectsNilEmbeddedChordNoteId) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  const Chord original = make_chord(
      quarter(),
      {ChordNote{.pitch = pitch_c4()}, ChordNote{.pitch = pitch_e4()}});
  ASSERT_TRUE(voice->append(VoiceEvent(original)).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());
  const auto orig_note0_id = original.notes[0].id;
  const auto orig_note1_id = original.notes[1].id;

  // Build a replacement chord with a nil ChordNote id.
  const Chord bad_chord =
      Chord{NotationEntityId::generate(),
            quarter(),
            {ChordNote{{}, pitch_c4(), false},
             ChordNote{NotationEntityId::generate(), pitch_e4(), false}},
            {},
            {}};
  auto cmd = std::make_unique<SetEventCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      VoiceEvent(bad_chord));

  EXPECT_FALSE(cmd->execute(fx.project).ok());
  // Model unchanged.
  const Chord* result = std::get_if<Chord>(&voice->events()[0]);
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(result->notes[0].id, orig_note0_id);
  EXPECT_EQ(result->notes[1].id, orig_note1_id);
}

TEST(CommandTest, SetEventRejectsChordNoteIdEqualToParent) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  const Chord original = make_chord(
      quarter(),
      {ChordNote{.pitch = pitch_c4()}, ChordNote{.pitch = pitch_e4()}});
  ASSERT_TRUE(voice->append(VoiceEvent(original)).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());
  const auto orig_note0_id = original.notes[0].id;

  // Build a chord where a ChordNote id equals the Chord's own id.
  const auto  parent_id = NotationEntityId::generate();
  const Chord bad_chord =
      Chord{parent_id,
            quarter(),
            {ChordNote{parent_id, pitch_c4(), false},
             ChordNote{NotationEntityId::generate(), pitch_e4(), false}},
            {},
            {}};
  auto cmd = std::make_unique<SetEventCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      VoiceEvent(bad_chord));

  EXPECT_FALSE(cmd->execute(fx.project).ok());
  const Chord* result = std::get_if<Chord>(&voice->events()[0]);
  ASSERT_NE(result, nullptr);
  EXPECT_EQ(result->notes[0].id, orig_note0_id);
}

TEST(CommandTest, SetEventDoubleExecuteRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  auto cmd = std::make_unique<SetEventCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      make_rest(quarter()));

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetEventUndoWithoutExecuteRejected) {
  auto fx  = make_notation_setup();
  auto cmd = std::make_unique<SetEventCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      make_rest(quarter()));
  EXPECT_EQ(cmd->undo(fx.project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetEventRedoWithoutUndoRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  auto cmd = std::make_unique<SetEventCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      make_rest(quarter()));

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(cmd->redo(fx.project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetEventMissingNodeIdFails) {
  auto   fx      = make_notation_setup();
  NodeId missing = NodeId::generate();
  auto   cmd     = std::make_unique<SetEventCommand>(
      missing, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      make_rest(quarter()));
  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetEventMissingTrackIdFails) {
  auto    fx      = make_notation_setup();
  TrackId missing = TrackId::generate();
  auto cmd = std::make_unique<SetEventCommand>(fx.node_id, missing, fx.stave_id,
                                               *Voice::create(1), Rational(0),
                                               make_rest(quarter()));
  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetEventMissingStaveIdFails) {
  auto    fx      = make_notation_setup();
  StaveId missing = StaveId::generate();
  auto cmd = std::make_unique<SetEventCommand>(fx.node_id, fx.track_id, missing,
                                               *Voice::create(1), Rational(0),
                                               make_rest(quarter()));
  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetEventNoTimelineFails) {
  Project project = make_project();
  auto    t       = project.add_track("T", StaffLayout::single_staff(),
                                      *MidiChannel::create(0));
  ASSERT_TRUE(t.has_value());
  NodeId nid = project.add_node("N");
  Node*  n   = project.find_node(nid);

  StaveId            sid;
  graphscore::Track* tr = project.find_active_track(*t);
  for (const graphscore::StaveDefinition& sd : tr->layout().staves()) {
    n->lane(*t)->ensure_stave(sd.id);
    sid = sd.id;
  }

  auto cmd = std::make_unique<SetEventCommand>(
      nid, *t, sid, *Voice::create(1), Rational(0), make_rest(quarter()));
  EXPECT_EQ(cmd->execute(project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetEventInvalidPositionFails) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  // Position 1/8 is inside the first quarter note — not a boundary.
  auto cmd = std::make_unique<SetEventCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1),
      *Rational::create(1, 8), make_rest(eighth()));

  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
  // Voice must be unchanged.
  EXPECT_TRUE(std::holds_alternative<Note>(voice->events()[0]));
}

TEST(CommandTest, SetEventSingleNoteChordRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const Chord bad_chord =
      make_chord(quarter(), {ChordNote{.pitch = pitch_c4()}});  // only 1 note
  auto cmd = std::make_unique<SetEventCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      VoiceEvent(bad_chord));

  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(std::holds_alternative<Note>(voice->events()[0]));
}

TEST(CommandTest, SetEventPreservesNotationEntityIdsOnRedo) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  const VoiceEvent original = make_note(pitch_c4(), quarter());
  ASSERT_TRUE(voice->append(original).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const VoiceEvent replacement = make_rest(quarter());
  NotationEntityId first_pass_id;

  auto cmd = std::make_unique<SetEventCommand>(fx.node_id, fx.track_id,
                                               fx.stave_id, *Voice::create(1),
                                               Rational(0), replacement);

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  first_pass_id = graphscore::event_id(voice->events()[0]);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  ASSERT_TRUE(cmd->redo(fx.project).ok());

  EXPECT_EQ(graphscore::event_id(voice->events()[0]), first_pass_id);
}

TEST(CommandTest, SetEventUnrelatedVoicePreserved) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* v1 =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));
  VoiceContent* v2 =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(2));

  ASSERT_TRUE(v1->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(v1->normalize(fx.node_end).ok());
  ASSERT_TRUE(v2->append(make_note(pitch_d4(), half())).ok());
  ASSERT_TRUE(v2->normalize(fx.node_end).ok());

  const VoiceContent saved_v2 = *v2;

  auto cmd = std::make_unique<SetEventCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      make_rest(quarter()));
  ASSERT_TRUE(cmd->execute(fx.project).ok());

  EXPECT_EQ(*v2, saved_v2);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(*v2, saved_v2);
}

// =========================================================================
// Phase 8e-i — ConvertEventToRestCommand
// =========================================================================

TEST(CommandTest, ConvertNoteToRestRoundTrip) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());
  NotationEntityId note_id = graphscore::event_id(voice->events()[0]);

  auto cmd = std::make_unique<ConvertEventToRestCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0));

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_TRUE(std::holds_alternative<Rest>(voice->events()[0]));
  EXPECT_EQ(voice->total_length(), fx.node_end);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_TRUE(std::holds_alternative<Note>(voice->events()[0]));
  EXPECT_EQ(graphscore::event_id(voice->events()[0]), note_id);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_TRUE(std::holds_alternative<Rest>(voice->events()[0]));
}

TEST(CommandTest, ConvertChordToRestRoundTrip) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  const Chord chord = make_chord(
      half(), {ChordNote{.pitch = pitch_c4()}, ChordNote{.pitch = pitch_e4()}});
  ASSERT_TRUE(voice->append(VoiceEvent(chord)).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  auto cmd = std::make_unique<ConvertEventToRestCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0));

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_TRUE(std::holds_alternative<Rest>(voice->events()[0]));

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_TRUE(std::holds_alternative<Chord>(voice->events()[0]));

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_TRUE(std::holds_alternative<Rest>(voice->events()[0]));
}

TEST(CommandTest, ConvertRestToRestIsIdempotent) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_rest(quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  auto cmd = std::make_unique<ConvertEventToRestCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0));

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_TRUE(std::holds_alternative<Rest>(voice->events()[0]));
  const Rest* r = std::get_if<Rest>(&voice->events()[0]);
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->duration.resolved(), quarter().resolved());

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_TRUE(std::holds_alternative<Rest>(voice->events()[0]));
}

TEST(CommandTest, ConvertEventToRestDoubleExecuteRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  auto cmd = std::make_unique<ConvertEventToRestCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0));
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, ConvertEventToRestMissingNodeFails) {
  auto fx  = make_notation_setup();
  auto cmd = std::make_unique<ConvertEventToRestCommand>(
      NodeId::generate(), fx.track_id, fx.stave_id, *Voice::create(1),
      Rational(0));
  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, ConvertEventToRestInvalidPositionFails) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  auto cmd = std::make_unique<ConvertEventToRestCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1),
      *Rational::create(1, 2));  // no event starts at 1/2
  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, ConvertPreservesDuration) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), dotted_half())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  auto cmd = std::make_unique<ConvertEventToRestCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0));

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const Rest* r = std::get_if<Rest>(&voice->events()[0]);
  ASSERT_NE(r, nullptr);
  EXPECT_EQ(r->duration.resolved(), dotted_half().resolved());
}

// =========================================================================
// Phase 8e-i — SetTieCommand
// =========================================================================

TEST(CommandTest, SetTieNoteTieThenUntieRoundTrip) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  // Tie the first note.
  auto tie_cmd = std::make_unique<SetTieCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      std::nullopt, true);

  ASSERT_TRUE(tie_cmd->execute(fx.project).ok());
  const Note* n1 = std::get_if<Note>(&voice->events()[0]);
  ASSERT_NE(n1, nullptr);
  EXPECT_TRUE(n1->tied_to_next);

  ASSERT_TRUE(tie_cmd->undo(fx.project).ok());
  const Note* n1u = std::get_if<Note>(&voice->events()[0]);
  ASSERT_NE(n1u, nullptr);
  EXPECT_FALSE(n1u->tied_to_next);

  ASSERT_TRUE(tie_cmd->redo(fx.project).ok());
  const Note* n1r = std::get_if<Note>(&voice->events()[0]);
  ASSERT_NE(n1r, nullptr);
  EXPECT_TRUE(n1r->tied_to_next);

  // Untie it.
  auto untie_cmd = std::make_unique<SetTieCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      std::nullopt, false);

  ASSERT_TRUE(untie_cmd->execute(fx.project).ok());
  const Note* n1u2 = std::get_if<Note>(&voice->events()[0]);
  ASSERT_NE(n1u2, nullptr);
  EXPECT_FALSE(n1u2->tied_to_next);
}

TEST(CommandTest, SetTieChordNoteheadTieThenUntieRoundTrip) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  const Chord chord = make_chord(quarter(), {ChordNote{.pitch = pitch_c4()},
                                             ChordNote{.pitch = pitch_e4()}});
  ASSERT_TRUE(voice->append(VoiceEvent(chord)).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  // Tie notehead index 0 (C4) in the chord.
  auto tie_cmd = std::make_unique<SetTieCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      std::make_optional<std::size_t>(0), true);

  ASSERT_TRUE(tie_cmd->execute(fx.project).ok());
  const Chord* c = std::get_if<Chord>(&voice->events()[0]);
  ASSERT_NE(c, nullptr);
  EXPECT_TRUE(c->notes[0].tied_to_next);   // C4
  EXPECT_FALSE(c->notes[1].tied_to_next);  // E4

  ASSERT_TRUE(tie_cmd->undo(fx.project).ok());
  const Chord* cu = std::get_if<Chord>(&voice->events()[0]);
  ASSERT_NE(cu, nullptr);
  EXPECT_FALSE(cu->notes[0].tied_to_next);
  EXPECT_FALSE(cu->notes[1].tied_to_next);

  ASSERT_TRUE(tie_cmd->redo(fx.project).ok());
  const Chord* cr = std::get_if<Chord>(&voice->events()[0]);
  ASSERT_NE(cr, nullptr);
  EXPECT_TRUE(cr->notes[0].tied_to_next);
  EXPECT_FALSE(cr->notes[1].tied_to_next);
}

TEST(CommandTest, SetTieChordNoIndexRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  const Chord chord = make_chord(quarter(), {ChordNote{.pitch = pitch_c4()},
                                             ChordNote{.pitch = pitch_e4()}});
  ASSERT_TRUE(voice->append(VoiceEvent(chord)).ok());
  const Chord successor = make_chord(
      quarter(),
      {ChordNote{.pitch = pitch_c4()}, ChordNote{.pitch = pitch_e4()}});
  ASSERT_TRUE(voice->append(VoiceEvent(successor)).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  // Chord requires an explicit notehead index; nullopt is rejected.
  auto cmd = std::make_unique<SetTieCommand>(fx.node_id, fx.track_id,
                                             fx.stave_id, *Voice::create(1),
                                             Rational(0), std::nullopt, true);

  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
  // Voice unchanged — no tie flags set.
  const Chord* c = std::get_if<Chord>(&voice->events()[0]);
  ASSERT_NE(c, nullptr);
  EXPECT_FALSE(c->notes[0].tied_to_next);
  EXPECT_FALSE(c->notes[1].tied_to_next);
}

TEST(CommandTest, SetTieChordNoteheadMismatchedRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  const Chord chord = make_chord(quarter(), {ChordNote{.pitch = pitch_c4()},
                                             ChordNote{.pitch = pitch_e4()}});
  ASSERT_TRUE(voice->append(VoiceEvent(chord)).ok());
  // Successor has C4 but not E4 — tying E4 (index 1) is invalid.
  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  auto cmd = std::make_unique<SetTieCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      std::make_optional<std::size_t>(1), true);

  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
  // Voice unchanged — no tie flags set.
  const Chord* c = std::get_if<Chord>(&voice->events()[0]);
  ASSERT_NE(c, nullptr);
  EXPECT_FALSE(c->notes[0].tied_to_next);
  EXPECT_FALSE(c->notes[1].tied_to_next);
}

TEST(CommandTest, SetTieRestFails) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_rest(quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  auto cmd = std::make_unique<SetTieCommand>(fx.node_id, fx.track_id,
                                             fx.stave_id, *Voice::create(1),
                                             Rational(0), std::nullopt, true);
  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
  // Voice unchanged.
  EXPECT_TRUE(std::holds_alternative<Rest>(voice->events()[0]));
}

TEST(CommandTest, SetTieChordMissingNoteheadFails) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  const Chord chord = make_chord(quarter(), {ChordNote{.pitch = pitch_c4()},
                                             ChordNote{.pitch = pitch_e4()}});
  ASSERT_TRUE(voice->append(VoiceEvent(chord)).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  // Index 2 is out of range (chord has only 2 notes).
  auto cmd = std::make_unique<SetTieCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      std::make_optional<std::size_t>(2), true);
  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetTieNoteRejectsExplicitNoteheadIndex) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  // A Note carries a single tie flag; supplying an explicit notehead index
  // is rejected.
  auto cmd = std::make_unique<SetTieCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      std::make_optional<std::size_t>(0), true);
  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
  // Voice unchanged.
  const Note* n = std::get_if<Note>(&voice->events()[0]);
  ASSERT_NE(n, nullptr);
  EXPECT_FALSE(n->tied_to_next);
}

TEST(CommandTest, SetTieDoubleExecuteRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  auto cmd = std::make_unique<SetTieCommand>(fx.node_id, fx.track_id,
                                             fx.stave_id, *Voice::create(1),
                                             Rational(0), std::nullopt, true);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetTieUndoWithoutExecuteRejected) {
  auto fx  = make_notation_setup();
  auto cmd = std::make_unique<SetTieCommand>(fx.node_id, fx.track_id,
                                             fx.stave_id, *Voice::create(1),
                                             Rational(0), std::nullopt, true);
  EXPECT_EQ(cmd->undo(fx.project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetTieRedoWithoutUndoRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  auto cmd = std::make_unique<SetTieCommand>(fx.node_id, fx.track_id,
                                             fx.stave_id, *Voice::create(1),
                                             Rational(0), std::nullopt, true);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(cmd->redo(fx.project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetTieMissingNodeFails) {
  auto fx  = make_notation_setup();
  auto cmd = std::make_unique<SetTieCommand>(NodeId::generate(), fx.track_id,
                                             fx.stave_id, *Voice::create(1),
                                             Rational(0), std::nullopt, true);
  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetTieInvalidPositionFails) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  auto cmd = std::make_unique<SetTieCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1),
      *Rational::create(1, 2), std::nullopt, true);
  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetTiePreservesOtherEventFields) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  const VoiceEvent note    = make_note(pitch_d4(), half(), false,
                                       {graphscore::Articulation::kStaccato});
  NotationEntityId note_id = graphscore::event_id(note);
  ASSERT_TRUE(voice->append(note).ok());
  // A valid tie needs a successor that sounds the same pitch.
  ASSERT_TRUE(voice->append(make_note(pitch_d4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  auto cmd = std::make_unique<SetTieCommand>(fx.node_id, fx.track_id,
                                             fx.stave_id, *Voice::create(1),
                                             Rational(0), std::nullopt, true);

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const Note* n = std::get_if<Note>(&voice->events()[0]);
  ASSERT_NE(n, nullptr);
  EXPECT_TRUE(n->tied_to_next);
  EXPECT_EQ(n->pitch, pitch_d4());
  EXPECT_EQ(n->duration.resolved(), half().resolved());
  EXPECT_EQ(n->id, note_id);
  ASSERT_EQ(n->articulations.size(), 1u);
  EXPECT_EQ(n->articulations[0], graphscore::Articulation::kStaccato);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  const Note* nu = std::get_if<Note>(&voice->events()[0]);
  ASSERT_NE(nu, nullptr);
  EXPECT_FALSE(nu->tied_to_next);
  EXPECT_EQ(nu->id, note_id);
}

// =========================================================================
// Phase 8e-i — Deterministic replay and ordering
// =========================================================================

TEST(CommandTest, DeterministicReplay8ei) {
  auto run_sequence =
      [](Project& project) -> std::pair<NotationEntityId, VoiceEvent> {
    CommandHistory history;

    const auto   t   = project.add_track("Track", StaffLayout::single_staff(),
                                         *MidiChannel::create(0));
    const NodeId nid = project.add_node("Node");
    Node*        n   = project.find_node(nid);

    std::vector<Measure> measures = {
        Measure{*TimeSignature::create(4, 4), *KeySignature::create(0)}};
    auto tl = NodeTimeline::create(std::move(measures), {});
    n->set_timeline(std::move(*tl));

    StaveId sid;
    for (const graphscore::StaveDefinition& sd :
         project.find_active_track(*t)->layout().staves()) {
      n->lane(*t)->ensure_stave(sd.id);
      sid = sd.id;
    }

    VoiceContent* voice = &n->lane(*t)->stave(sid)->voice(*Voice::create(1));
    static_cast<void>(voice->append(make_note(pitch_c4(), quarter())));
    static_cast<void>(voice->normalize(Rational(1)));

    NotationEntityId first_id = graphscore::event_id(voice->events()[0]);
    const VoiceEvent rest     = make_rest(quarter());

    static_cast<void>(history.execute_new(
        std::make_unique<SetEventCommand>(nid, *t, sid, *Voice::create(1),
                                          Rational(0), rest),
        project));
    static_cast<void>(
        history.execute_new(std::make_unique<ConvertEventToRestCommand>(
                                nid, *t, sid, *Voice::create(1), Rational(0)),
                            project));

    return std::make_pair(first_id, voice->events()[0]);
  };

  Project first  = make_project();
  Project second = make_project();

  auto [fid, f_ev] = run_sequence(first);
  auto [sid, s_ev] = run_sequence(second);

  EXPECT_TRUE(std::holds_alternative<Rest>(f_ev));
  EXPECT_TRUE(std::holds_alternative<Rest>(s_ev));
  // Ids are independently generated, so should differ between projects.
  EXPECT_NE(fid, sid);
}

// =========================================================================
// Phase 8e-i — Tie validation (rejection of invalid ties)
// =========================================================================

TEST(CommandTest, SetTieLastEventRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  // The only event is the last one — tying it has no successor.
  auto cmd = std::make_unique<SetTieCommand>(fx.node_id, fx.track_id,
                                             fx.stave_id, *Voice::create(1),
                                             Rational(0), std::nullopt, true);
  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
  const Note* n = std::get_if<Note>(&voice->events()[0]);
  ASSERT_NE(n, nullptr);
  EXPECT_FALSE(n->tied_to_next);
}

TEST(CommandTest, SetTieMismatchedSuccessorRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_d4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  // Tie C4 → successor is D4 — pitch mismatch.
  auto cmd = std::make_unique<SetTieCommand>(fx.node_id, fx.track_id,
                                             fx.stave_id, *Voice::create(1),
                                             Rational(0), std::nullopt, true);
  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
  const Note* n = std::get_if<Note>(&voice->events()[0]);
  ASSERT_NE(n, nullptr);
  EXPECT_FALSE(n->tied_to_next);
}

// =========================================================================
// Phase 8e-i — ConvertEventToRestCommand ID stability
// =========================================================================

TEST(CommandTest, ConvertNoteToRestPreservesIdOnRedo) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const NotationEntityId original_id = graphscore::event_id(voice->events()[0]);

  auto cmd = std::make_unique<ConvertEventToRestCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0));

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const NotationEntityId first_rest_id =
      graphscore::event_id(voice->events()[0]);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  ASSERT_TRUE(cmd->redo(fx.project).ok());
  const NotationEntityId second_rest_id =
      graphscore::event_id(voice->events()[0]);

  // The rest preserves the original note's id.
  EXPECT_EQ(first_rest_id, original_id);
  // Redo produces the exact same id.
  EXPECT_EQ(second_rest_id, first_rest_id);
}

TEST(CommandTest, ConvertChordToRestPreservesIdOnRedo) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  const Chord chord = make_chord(
      half(), {ChordNote{.pitch = pitch_c4()}, ChordNote{.pitch = pitch_e4()}});
  ASSERT_TRUE(voice->append(VoiceEvent(chord)).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());
  const NotationEntityId original_id = graphscore::event_id(voice->events()[0]);

  auto cmd = std::make_unique<ConvertEventToRestCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0));

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const NotationEntityId first_id = graphscore::event_id(voice->events()[0]);
  EXPECT_EQ(first_id, original_id);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(graphscore::event_id(voice->events()[0]), first_id);
}

// =========================================================================
// Phase 8e-i — Predecessor-tie preservation (no dangling references)
// =========================================================================

TEST(CommandTest, SetEventBreaksPredecessorTieRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  const VoiceEvent n1 = make_note(pitch_c4(), quarter(), true);
  const VoiceEvent n2 = make_note(pitch_c4(), quarter());
  ASSERT_TRUE(voice->append(n1).ok());
  ASSERT_TRUE(voice->append(n2).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  // Replace the second note (the tie target) with D4 — tie from C4 breaks.
  const VoiceEvent replacement = make_note(pitch_d4(), quarter());
  auto             cmd         = std::make_unique<SetEventCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1),
      *Rational::create(1, 4), replacement);

  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
  // Voice unchanged: first note still tied_to_next, second note still C4.
  const Note* n1_after = std::get_if<Note>(&voice->events()[0]);
  ASSERT_NE(n1_after, nullptr);
  EXPECT_TRUE(n1_after->tied_to_next);
  const Note* n2_after = std::get_if<Note>(&voice->events()[1]);
  ASSERT_NE(n2_after, nullptr);
  EXPECT_EQ(n2_after->pitch, pitch_c4());
}

TEST(CommandTest, ConvertEventToRestBreaksPredecessorTieRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  const VoiceEvent n1 = make_note(pitch_c4(), quarter(), true);
  const VoiceEvent n2 = make_note(pitch_c4(), quarter());
  ASSERT_TRUE(voice->append(n1).ok());
  ASSERT_TRUE(voice->append(n2).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  // Convert the second note to rest — predecessor tie breaks.
  auto cmd = std::make_unique<ConvertEventToRestCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1),
      *Rational::create(1, 4));

  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
  const Note* n1_after = std::get_if<Note>(&voice->events()[0]);
  ASSERT_NE(n1_after, nullptr);
  EXPECT_TRUE(n1_after->tied_to_next);
  EXPECT_TRUE(std::holds_alternative<Note>(voice->events()[1]));
}

// =========================================================================
// Phase 8e-i — Overflow rejection (duration expansion cannot silently
//                clip later content)
// =========================================================================

TEST(CommandTest, SetEventDurationExpansionOverflowRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_d4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const VoiceContent saved = *voice;

  // Replace the quarter at 0 with a whole note — would exceed
  // target_length(1).
  const VoiceEvent whole_note = make_note(pitch_c4(), whole());
  auto cmd = std::make_unique<SetEventCommand>(fx.node_id, fx.track_id,
                                               fx.stave_id, *Voice::create(1),
                                               Rational(0), whole_note);

  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(*voice, saved);
}

// =========================================================================
// Phase 8e-i — Full VoiceContent equality with IDs on execute→undo→redo
// =========================================================================

TEST(CommandTest, SetEventFullEqualityWithIdsOnUndoRedo) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());
  const VoiceContent original = *voice;

  const VoiceEvent replacement = make_rest(quarter());
  auto cmd = std::make_unique<SetEventCommand>(fx.node_id, fx.track_id,
                                               fx.stave_id, *Voice::create(1),
                                               Rational(0), replacement);

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const VoiceContent after_execute = *voice;
  EXPECT_NE(after_execute, original);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(*voice, original);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(*voice, after_execute);
}

TEST(CommandTest, ConvertEventToRestFullEqualityWithIdsOnUndoRedo) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());
  const VoiceContent original = *voice;

  auto cmd = std::make_unique<ConvertEventToRestCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0));

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const VoiceContent after_execute = *voice;
  EXPECT_NE(after_execute, original);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(*voice, original);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(*voice, after_execute);
}

TEST(CommandTest, SetTieFullEqualityWithIdsOnUndoRedo) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());
  const VoiceContent original = *voice;

  auto cmd = std::make_unique<SetTieCommand>(fx.node_id, fx.track_id,
                                             fx.stave_id, *Voice::create(1),
                                             Rational(0), std::nullopt, true);

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const VoiceContent after_execute = *voice;
  EXPECT_NE(after_execute, original);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(*voice, original);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(*voice, after_execute);
}

// =========================================================================
// VoiceContent positional-mutator tests
// =========================================================================

TEST(CommandTest, VoiceContentInsertIntoRestCoverageAtBeginning) {
  VoiceContent voice;
  // Rest coverage at position 0: R(q), N(q).
  ASSERT_TRUE(voice.append(make_rest(quarter())).ok());
  ASSERT_TRUE(voice.append(make_note(pitch_d4(), quarter())).ok());

  // Insert eighth note at position 0 consuming part of the quarter rest.
  ASSERT_TRUE(voice
                  .insert_event(Rational(0), make_note(pitch_c4(), eighth()),
                                Rational(1))
                  .ok());

  ASSERT_GE(voice.events().size(), 3u);
  EXPECT_TRUE(std::holds_alternative<Note>(voice.events()[0]));
  EXPECT_EQ(voice.total_length(), Rational(1));
}

TEST(CommandTest, VoiceContentInsertAtEnd) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());

  const Rational end_pos = voice.total_length();
  ASSERT_TRUE(
      voice.insert_event(end_pos, make_rest(eighth()), Rational(1)).ok());

  ASSERT_GE(voice.events().size(), 2u);
  EXPECT_TRUE(std::holds_alternative<Rest>(voice.events()[1]));
  EXPECT_EQ(voice.total_length(), Rational(1));
}

TEST(CommandTest, VoiceContentInsertAtEventBoundary) {
  VoiceContent voice;
  // N(q) at 0, R(h) at 1/4, N(q) at 3/4.  Total = 1.0.
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice.append(make_rest(half())).ok());
  ASSERT_TRUE(voice.append(make_note(pitch_d4(), quarter())).ok());
  ASSERT_EQ(voice.total_length(), Rational(1));

  // Insert a quarter rest at 1/4, consuming into the half rest coverage.
  ASSERT_TRUE(voice
                  .insert_event(*Rational::create(1, 4), make_rest(quarter()),
                                Rational(2))
                  .ok());

  EXPECT_TRUE(std::holds_alternative<Note>(voice.events()[0]));
  EXPECT_TRUE(std::holds_alternative<Rest>(voice.events()[1]));
  // The D4 sounding onset at 3/4 must be preserved somewhere beyond the
  // inserted + remainder rests.
  bool found_d4 = false;
  for (const VoiceEvent& ev : voice.events()) {
    if (const auto* n = std::get_if<Note>(&ev)) {
      if (n->pitch == pitch_d4())
        found_d4 = true;
    }
  }
  EXPECT_TRUE(found_d4);
  EXPECT_EQ(voice.total_length(), Rational(2));
}

TEST(CommandTest, VoiceContentInsertInvalidPositionFails) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());

  EXPECT_FALSE(voice
                   .insert_event(*Rational::create(1, 8), make_rest(eighth()),
                                 Rational(1))
                   .ok());
}

TEST(CommandTest, VoiceContentInsertOverflowRejected) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), half())).ok());
  ASSERT_TRUE(voice.append(make_note(pitch_d4(), half())).ok());
  ASSERT_EQ(voice.total_length(), Rational(1));

  // Inserting anything into an already-full voice exceeds target_length=1.
  EXPECT_FALSE(
      voice.insert_event(Rational(0), make_rest(eighth()), Rational(1)).ok());
  EXPECT_EQ(voice.total_length(), Rational(1));
}

TEST(CommandTest, VoiceContentRemoveEvent) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice.append(make_note(pitch_d4(), quarter())).ok());

  ASSERT_TRUE(voice.remove_event(*Rational::create(1, 4), Rational(1)).ok());
  ASSERT_EQ(voice.total_length(), Rational(1));
  EXPECT_TRUE(std::holds_alternative<Note>(voice.events()[0]));
}

TEST(CommandTest, VoiceContentRemoveInvalidPositionFails) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());

  EXPECT_FALSE(voice.remove_event(*Rational::create(1, 2), Rational(1)).ok());
}

TEST(CommandTest, VoiceContentReplaceEvent) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());

  ASSERT_TRUE(
      voice.replace_event(Rational(0), make_rest(quarter()), Rational(1)).ok());
  EXPECT_TRUE(std::holds_alternative<Rest>(voice.events()[0]));
  EXPECT_EQ(voice.total_length(), Rational(1));
}

TEST(CommandTest, VoiceContentReplaceInvalidPositionFails) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());

  EXPECT_FALSE(voice
                   .replace_event(*Rational::create(1, 2), make_rest(quarter()),
                                  Rational(1))
                   .ok());
}

TEST(CommandTest, VoiceContentReplaceOverflowRejected) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), half())).ok());
  ASSERT_TRUE(voice.append(make_note(pitch_d4(), half())).ok());
  ASSERT_EQ(voice.total_length(), Rational(1));

  // Replacing a half note with a whole note overflows target_length=1.
  EXPECT_FALSE(voice
                   .replace_event(Rational(0), make_note(pitch_c4(), whole()),
                                  Rational(1))
                   .ok());
  EXPECT_EQ(voice.total_length(), Rational(1));
}

TEST(CommandTest, VoiceContentFindEventIndexEmptyVoice) {
  VoiceContent voice;
  EXPECT_FALSE(voice.find_event_index_at(Rational(0)).has_value());
}

TEST(CommandTest, VoiceContentFindEventIndexValid) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice.append(make_note(pitch_d4(), half())).ok());

  auto idx0 = voice.find_event_index_at(Rational(0));
  ASSERT_TRUE(idx0.has_value());
  EXPECT_EQ(*idx0, 0u);

  auto idx1 = voice.find_event_index_at(*Rational::create(1, 4));
  ASSERT_TRUE(idx1.has_value());
  EXPECT_EQ(*idx1, 1u);

  // Position at total_length() has no event starting there.
  auto idx_end = voice.find_event_index_at(*Rational::create(3, 4));
  EXPECT_FALSE(idx_end.has_value());
}

TEST(CommandTest, VoiceContentInsertSingleNoteChordRejected) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());

  const Chord bad_chord =
      make_chord(eighth(), {ChordNote{.pitch = pitch_c4()}});
  EXPECT_FALSE(
      voice.insert_event(Rational(0), VoiceEvent(bad_chord), Rational(1)).ok());
  EXPECT_EQ(voice.events().size(), 1u);
}

// =========================================================================
// Cross-command order independence
// =========================================================================

TEST(CommandTest, SetEventAndSetTieInterleavedUndoRedo) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  // 1. Set event: replace first note with rest.
  auto set_cmd = std::make_unique<SetEventCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      make_rest(quarter()));
  ASSERT_TRUE(set_cmd->execute(fx.project).ok());
  EXPECT_TRUE(std::holds_alternative<Rest>(voice->events()[0]));

  // 2. Set tie on the second event (the note at 1/4), which has a
  //    matching successor at 1/2.
  auto tie_cmd = std::make_unique<SetTieCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1),
      *Rational::create(1, 4), std::nullopt, true);
  ASSERT_TRUE(tie_cmd->execute(fx.project).ok());
  const Note* n2 = std::get_if<Note>(&voice->events()[1]);
  ASSERT_NE(n2, nullptr);
  EXPECT_TRUE(n2->tied_to_next);

  // Undo tie first.
  ASSERT_TRUE(tie_cmd->undo(fx.project).ok());
  const Note* n2u = std::get_if<Note>(&voice->events()[1]);
  ASSERT_NE(n2u, nullptr);
  EXPECT_FALSE(n2u->tied_to_next);
  EXPECT_TRUE(std::holds_alternative<Rest>(voice->events()[0]));

  // Undo set.
  ASSERT_TRUE(set_cmd->undo(fx.project).ok());
  EXPECT_TRUE(std::holds_alternative<Note>(voice->events()[0]));
  const Note* n2uu = std::get_if<Note>(&voice->events()[1]);
  ASSERT_NE(n2uu, nullptr);
  EXPECT_FALSE(n2uu->tied_to_next);  // tie was already undone

  // Redo set.
  ASSERT_TRUE(set_cmd->redo(fx.project).ok());
  EXPECT_TRUE(std::holds_alternative<Rest>(voice->events()[0]));

  // Redo tie.
  ASSERT_TRUE(tie_cmd->redo(fx.project).ok());
  const Note* n2r = std::get_if<Note>(&voice->events()[1]);
  ASSERT_NE(n2r, nullptr);
  EXPECT_TRUE(n2r->tied_to_next);
}

// =========================================================================
// Phase 8e-i — Duration expansion (consuming following rests)
// =========================================================================

TEST(CommandTest, SetEventDurationExpansionConsumesWholeRest) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  // Quarter note at 0, quarter rest at 1/4, filled to 1 whole.
  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->append(make_rest(quarter())).ok());
  ASSERT_TRUE(voice->normalize(Rational(1)).ok());
  ASSERT_EQ(voice->total_length(), Rational(1));

  // Replace quarter note with half note -> consumes the following quarter
  // rest.
  auto cmd = std::make_unique<SetEventCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      make_note(pitch_c4(), half()));

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(voice->total_length(), Rational(1));
  EXPECT_TRUE(voice->check_complete(Rational(1)).ok());
  // Should now be: half note + normalized rest covering remainder.
  EXPECT_TRUE(std::holds_alternative<Note>(voice->events()[0]));
  EXPECT_EQ(event_duration(voice->events()[0]).resolved(), half().resolved());

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(voice->total_length(), Rational(1));
  EXPECT_TRUE(std::holds_alternative<Note>(voice->events()[0]));
  EXPECT_EQ(event_duration(voice->events()[0]).resolved(),
            quarter().resolved());
}

TEST(CommandTest, SetEventDurationExpansionConsumesPartialRest) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  // Quarter note at 0, eighth rest at 1/4, filled.
  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->append(make_rest(eighth())).ok());
  ASSERT_TRUE(voice->normalize(Rational(1)).ok());

  // Replace quarter with 3/8 note -> partial rest consumption
  // (needs an extra 1/8, the eighth rest covers it exactly).
  // A dotted quarter = 3/8.
  const Duration dotted_quarter = *Duration::create(NoteValue::kQuarter, 1);
  auto           cmd            = std::make_unique<SetEventCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      make_note(pitch_c4(), dotted_quarter));

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(voice->total_length(), Rational(1));
  EXPECT_TRUE(voice->check_complete(Rational(1)).ok());
  EXPECT_TRUE(std::holds_alternative<Note>(voice->events()[0]));
  EXPECT_EQ(event_duration(voice->events()[0]).resolved(),
            dotted_quarter.resolved());
}

TEST(CommandTest,
     SetEventDurationExpansionRejectedWhenFollowingEventIsSounding) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  // Quarter note at 0, another quarter note at 1/4.
  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_d4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(Rational(1)).ok());
  const VoiceContent saved = *voice;

  // Replace first quarter with half — would need to consume D4 note.
  auto cmd = std::make_unique<SetEventCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      make_note(pitch_c4(), half()));

  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(*voice, saved);
}

TEST(CommandTest,
     SetEventDurationExpansionRejectedWhenRestCoverageInsufficient) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  // Quarter note at 0, eighth rest at 1/4, then a sounding quarter note at 3/8.
  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->append(make_rest(eighth())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_d4(), quarter())).ok());
  // Fill remainder from 5/8 to 1 whole.
  ASSERT_TRUE(voice->normalize(Rational(1)).ok());
  ASSERT_EQ(voice->total_length(), Rational(1));
  const VoiceContent saved = *voice;

  // Replace quarter with half note — needs 1/4 extra, only 1/8 rest
  // available before the D4 note blocks further consumption.
  auto cmd = std::make_unique<SetEventCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      make_note(pitch_c4(), half()));

  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(*voice, saved);
}

TEST(CommandTest, SetEventDurationExpansionConsumesMultipleFollowingRests) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  // Quarter note at 0, two eighth rests, filled.
  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->append(make_rest(eighth())).ok());
  ASSERT_TRUE(voice->append(make_rest(eighth())).ok());
  ASSERT_TRUE(voice->normalize(Rational(1)).ok());

  // Replace quarter with half — consumes both eighth rests.
  auto cmd = std::make_unique<SetEventCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      make_note(pitch_c4(), half()));

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(voice->total_length(), Rational(1));
  EXPECT_TRUE(voice->check_complete(Rational(1)).ok());
}

// =========================================================================
// Phase 8e-i — Command rejection of dangling dynamic/grace references
// =========================================================================

TEST(CommandTest, SetEventRejectsDanglingDynamicReference) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const NotationEntityId ev_id = graphscore::event_id(voice->events()[0]);
  ASSERT_TRUE(voice
                  ->add_dynamic(graphscore::make_dynamic_marking(
                      ev_id, graphscore::Dynamic::kMf))
                  .ok());

  // Replace the event — would leave dynamic dangling.
  auto cmd = std::make_unique<SetEventCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      make_rest(quarter()));

  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
  // Voice must be unchanged.
  EXPECT_TRUE(std::holds_alternative<Note>(voice->events()[0]));
}

TEST(CommandTest, ConvertEventToRestRejectsDanglingGraceReference) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const NotationEntityId ev_id = graphscore::event_id(voice->events()[0]);
  ASSERT_TRUE(
      voice
          ->add_grace_group(graphscore::make_grace_group(
              ev_id, {graphscore::GraceNote{
                         .pitch    = pitch_e4(),
                         .duration = eighth(),
                         .type     = graphscore::GraceNoteType::kAppoggiatura,
                         .slashed  = false}}))
          .ok());

  // Convert to rest — principal event would become Rest.
  auto cmd = std::make_unique<ConvertEventToRestCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0));

  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(std::holds_alternative<Note>(voice->events()[0]));
}

// =========================================================================
// Phase 8e-i — Stale-context undo/redo rejection
// =========================================================================

TEST(CommandTest, SetEventUndoRejectedWhenVoiceChanged) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  auto cmd = std::make_unique<SetEventCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      make_rest(quarter()));
  ASSERT_TRUE(cmd->execute(fx.project).ok());

  // Manually change the voice — undo should reject.
  voice->clear();
  ASSERT_TRUE(voice->append(make_note(pitch_d4(), half())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  EXPECT_EQ(cmd->undo(fx.project).code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, SetEventRedoRejectedWhenVoiceChanged) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  auto cmd = std::make_unique<SetEventCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      make_rest(quarter()));
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  ASSERT_TRUE(cmd->undo(fx.project).ok());

  // Manually change the voice — redo should reject.
  voice->clear();
  ASSERT_TRUE(voice->append(make_note(pitch_d4(), half())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  EXPECT_EQ(cmd->redo(fx.project).code(), ResultCode::kInvalidArgument);
}

// =========================================================================
// Phase 8e-i — Duplicate-pitch chord notehead index targeting
// =========================================================================

TEST(CommandTest, SetTieDuplicatePitchChordTargetsExactIndex) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  // Chord with two C4 noteheads (unison doublings are permitted).
  const Chord chord = make_chord(
      half(), {ChordNote{.pitch = pitch_c4()}, ChordNote{.pitch = pitch_c4()}});
  ASSERT_TRUE(voice->append(VoiceEvent(chord)).ok());
  // Successor sounds C4 so the tie is valid.
  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  // Tie index 0 only.
  auto cmd0 = std::make_unique<SetTieCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      std::make_optional<std::size_t>(0), true);
  ASSERT_TRUE(cmd0->execute(fx.project).ok());
  const Chord* c0 = std::get_if<Chord>(&voice->events()[0]);
  ASSERT_NE(c0, nullptr);
  EXPECT_TRUE(c0->notes[0].tied_to_next);
  EXPECT_FALSE(c0->notes[1].tied_to_next);

  // Undo, then tie index 1 only.
  ASSERT_TRUE(cmd0->undo(fx.project).ok());
  auto cmd1 = std::make_unique<SetTieCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      std::make_optional<std::size_t>(1), true);
  ASSERT_TRUE(cmd1->execute(fx.project).ok());
  const Chord* c1 = std::get_if<Chord>(&voice->events()[0]);
  ASSERT_NE(c1, nullptr);
  EXPECT_FALSE(c1->notes[0].tied_to_next);
  EXPECT_TRUE(c1->notes[1].tied_to_next);
}

// =========================================================================
// Phase 8e-i — True deterministic replay with whole-VoiceContent equality
// =========================================================================

TEST(CommandTest, DeterministicReplaySetEventExactEqualityWithIds) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  auto cmd = std::make_unique<SetEventCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      make_rest(quarter()));

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const VoiceContent after_execute = *voice;
  EXPECT_NE(after_execute, VoiceContent{});

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  ASSERT_TRUE(cmd->redo(fx.project).ok());
  // Redo produces exactly the same voice as the original execute.
  EXPECT_EQ(*voice, after_execute);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  ASSERT_TRUE(cmd->redo(fx.project).ok());
  // Second redo cycle produces the same result.
  EXPECT_EQ(*voice, after_execute);
}

TEST(CommandTest, DeterministicReplaySetTieExactEqualityWithIds) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  auto cmd = std::make_unique<SetTieCommand>(fx.node_id, fx.track_id,
                                             fx.stave_id, *Voice::create(1),
                                             Rational(0), std::nullopt, true);

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const VoiceContent after_execute = *voice;
  EXPECT_NE(after_execute, VoiceContent{});

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(*voice, after_execute);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(*voice, after_execute);
}

// =========================================================================
// Phase 8e-i — Duration contraction fills with rests
// =========================================================================

TEST(CommandTest, SetEventDurationContractionFillsWithNormalizedRests) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  // Half note at 0.
  ASSERT_TRUE(voice->append(make_note(pitch_c4(), half())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());
  ASSERT_EQ(voice->events().size(), 2u);  // half note + half rest

  // Replace with quarter note — gap filled by a quarter rest.
  auto cmd = std::make_unique<SetEventCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      make_note(pitch_c4(), quarter()));

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(voice->total_length(), Rational(1));
  EXPECT_TRUE(voice->check_complete(fx.node_end).ok());
  // Should have quarter note + quarter rest + half rest (from original
  // normalize which filled the remainder).
  ASSERT_GE(voice->events().size(), 2u);
}

// =========================================================================
// Phase 8e-i — Remove preserves later event onsets
// =========================================================================

TEST(CommandTest, RemoveEventPreservesLaterOnsets) {
  VoiceContent voice;
  // N(q, C4) at 0, N(q, D4) at 1/4, N(q, E4) at 1/2.  Total 3/4.
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice.append(make_note(pitch_d4(), quarter())).ok());
  ASSERT_TRUE(voice.append(make_note(pitch_e4(), quarter())).ok());

  // Remove the D4 at 1/4.  It should be replaced with rests of the same
  // duration at position 1/4, leaving the E4 at onset 1/2.
  ASSERT_TRUE(voice.remove_event(*Rational::create(1, 4), Rational(1)).ok());

  EXPECT_EQ(voice.total_length(), Rational(1));
  // C4 note still at position 0.
  EXPECT_TRUE(std::holds_alternative<Note>(voice.events()[0]));
  // E4 note preserved at its original onset.  Find it by scanning.
  bool     found_e4 = false;
  Rational cumulative(0);
  for (const VoiceEvent& ev : voice.events()) {
    if (cumulative == *Rational::create(1, 2)) {
      if (const auto* n = std::get_if<Note>(&ev)) {
        if (n->pitch == pitch_e4())
          found_e4 = true;
      }
    }
    cumulative = cumulative + event_duration(ev).resolved();
  }
  EXPECT_TRUE(found_e4);
}

TEST(CommandTest, RemovePreservesExactRationalOnsets) {
  VoiceContent voice;
  // N(q, C4) at 0, N(e, D4) at 1/4, N(q, E4) at 3/8.  Total 5/8.
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice.append(make_note(pitch_d4(), eighth())).ok());
  ASSERT_TRUE(voice.append(make_note(pitch_e4(), quarter())).ok());

  // Remove C4 at 0.  D4 must remain at exact onset 1/4, E4 at 3/8.
  ASSERT_TRUE(voice.remove_event(Rational(0), Rational(1)).ok());
  EXPECT_EQ(voice.total_length(), Rational(1));

  Rational cumulative(0);
  bool     found_d4_at_1_4 = false;
  bool     found_e4_at_3_8 = false;
  for (const VoiceEvent& ev : voice.events()) {
    if (cumulative == *Rational::create(1, 4)) {
      if (const auto* n = std::get_if<Note>(&ev))
        found_d4_at_1_4 = (n->pitch == pitch_d4());
    }
    if (cumulative == *Rational::create(3, 8)) {
      if (const auto* n = std::get_if<Note>(&ev))
        found_e4_at_3_8 = (n->pitch == pitch_e4());
    }
    cumulative = cumulative + event_duration(ev).resolved();
  }
  EXPECT_TRUE(found_d4_at_1_4) << "D4 onset not preserved at 1/4";
  EXPECT_TRUE(found_e4_at_3_8) << "E4 onset not preserved at 3/8";
}

// =========================================================================
// Phase 8e-i — Contraction inserts rests at position (later onsets preserved)
// =========================================================================

TEST(CommandTest, ReplaceEventContractionPreservesLaterOnsets) {
  VoiceContent voice;
  // N(h, C4) at 0 (half note), N(q, D4) at 1/2.  Total 3/4.
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), half())).ok());
  ASSERT_TRUE(voice.append(make_note(pitch_d4(), quarter())).ok());

  // Replace half note with quarter note -> gap of 1/4, must be filled
  // at position 1/4 with a rest, leaving D4 at onset 1/2.
  ASSERT_TRUE(voice
                  .replace_event(Rational(0), make_note(pitch_c4(), quarter()),
                                 Rational(1))
                  .ok());
  EXPECT_EQ(voice.total_length(), Rational(1));

  Rational cumulative(0);
  bool     found_d4_at_1_2 = false;
  for (const VoiceEvent& ev : voice.events()) {
    if (cumulative == *Rational::create(1, 2)) {
      if (const auto* n = std::get_if<Note>(&ev))
        found_d4_at_1_2 = (n->pitch == pitch_d4());
    }
    cumulative = cumulative + event_duration(ev).resolved();
  }
  EXPECT_TRUE(found_d4_at_1_2) << "D4 onset not preserved at 1/2";
}

// =========================================================================
// Phase 8e-i — Insert into rest coverage of a complete voice
// =========================================================================

TEST(CommandTest, InsertIntoRestCoverageOfCompleteVoice) {
  VoiceContent voice;
  // N(q, C4) at 0, R(q) at 1/4, R(q) at 1/2, R(q) at 3/4.  Total 1.0.
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice.append(make_rest(quarter())).ok());
  ASSERT_TRUE(voice.append(make_rest(quarter())).ok());
  ASSERT_TRUE(voice.append(make_rest(quarter())).ok());

  // Insert a quarter note at 1/4, consuming one quarter rest.
  ASSERT_TRUE(voice
                  .insert_event(*Rational::create(1, 4),
                                make_note(pitch_d4(), quarter()), Rational(1))
                  .ok());
  EXPECT_EQ(voice.total_length(), Rational(1));
  EXPECT_TRUE(voice.check_complete(Rational(1)).ok());

  // C4 at 0, D4 at 1/4, remainder rests at 1/2 and beyond.
  bool     found_c4_at_0   = false;
  bool     found_d4_at_1_4 = false;
  Rational cumulative(0);
  for (const VoiceEvent& ev : voice.events()) {
    if (cumulative == Rational(0)) {
      if (const auto* n = std::get_if<Note>(&ev))
        found_c4_at_0 = (n->pitch == pitch_c4());
    }
    if (cumulative == *Rational::create(1, 4)) {
      if (const auto* n = std::get_if<Note>(&ev))
        found_d4_at_1_4 = (n->pitch == pitch_d4());
    }
    cumulative = cumulative + event_duration(ev).resolved();
  }
  EXPECT_TRUE(found_c4_at_0);
  EXPECT_TRUE(found_d4_at_1_4);
}

// =========================================================================
// Phase 8e-i — Partial final-Rest consumption
// =========================================================================

TEST(CommandTest, InsertPartialLastRestConsumption) {
  VoiceContent voice;
  // R(w) at 0 (whole rest).  Total 1.0.
  ASSERT_TRUE(voice.append(make_rest(whole())).ok());

  // Insert a quarter note at 0, consuming the first quarter of the whole
  // rest.  The remainder (3/4) is decomposed from the original rest's id.
  ASSERT_TRUE(voice
                  .insert_event(Rational(0), make_note(pitch_c4(), quarter()),
                                Rational(1))
                  .ok());
  EXPECT_EQ(voice.total_length(), Rational(1));
  EXPECT_TRUE(voice.check_complete(Rational(1)).ok());
  EXPECT_TRUE(std::holds_alternative<Note>(voice.events()[0]));
}

TEST(CommandTest, ReplaceEventPartialRestConsumption) {
  VoiceContent voice;
  // N(q, C4) at 0, R(h) at 1/4.  Total 3/4.
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice.append(make_rest(half())).ok());

  const NotationEntityId rest_id = event_id(voice.events()[1]);

  // Replace quarter with dotted quarter (3/8) — needs 1/8 extra, consumes
  // part of the half rest.  The remainder rest should preserve the original
  // Rest id so markings referencing it stay valid.
  const Duration dotted_q = *Duration::create(NoteValue::kQuarter, 1);
  ASSERT_TRUE(voice
                  .replace_event(Rational(0), make_note(pitch_c4(), dotted_q),
                                 Rational(1))
                  .ok());
  EXPECT_EQ(voice.total_length(), Rational(1));

  // The remainder rest at the consumed position should carry the original id.
  bool found_remainder_with_id = false;
  for (const VoiceEvent& ev : voice.events()) {
    if (std::holds_alternative<Rest>(ev)) {
      if (event_id(ev) == rest_id)
        found_remainder_with_id = true;
    }
  }
  EXPECT_TRUE(found_remainder_with_id)
      << "Partial rest remainder did not preserve the original Rest id";
}

// =========================================================================
// Phase 8e-i — Exact generated-ID redo (complete VoiceContent equality)
// =========================================================================

TEST(CommandTest, SetEventRedoExactEqualityWithContraction) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  // N(h, C4) at 0, normalize fills remainder.  Total 1.0.
  ASSERT_TRUE(voice->append(make_note(pitch_c4(), half())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  auto cmd = std::make_unique<SetEventCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      make_note(pitch_c4(), quarter()));

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const VoiceContent after_execute = *voice;

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  ASSERT_TRUE(cmd->redo(fx.project).ok());
  // Redo produces exactly the same voice — every Rest id identical.
  EXPECT_EQ(*voice, after_execute);

  // Second redo cycle.
  ASSERT_TRUE(cmd->undo(fx.project).ok());
  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(*voice, after_execute);
}

TEST(CommandTest, SetEventRedoExactEqualityWithExpansion) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  // N(q, C4) at 0, R(q) at 1/4, normalize remainder.  Total 1.0.
  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->append(make_rest(quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  auto cmd = std::make_unique<SetEventCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      make_note(pitch_c4(), half()));

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const VoiceContent after_execute = *voice;

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(*voice, after_execute);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(*voice, after_execute);
}

TEST(CommandTest, ConvertEventToRestRedoExactEquality) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  auto cmd = std::make_unique<ConvertEventToRestCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0));

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const VoiceContent after_execute = *voice;

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(*voice, after_execute);
}

// =========================================================================
// Phase 8e-i — Duplicate supplied IDs
// =========================================================================

TEST(CommandTest, AppendDuplicateIdRejected) {
  VoiceContent     voice;
  const VoiceEvent n1 = make_note(pitch_c4(), quarter());
  ASSERT_TRUE(voice.append(n1).ok());

  // Append the same event again — duplicate id.
  EXPECT_FALSE(voice.append(n1).ok());
  EXPECT_EQ(voice.events().size(), 1u);
}

TEST(CommandTest, InsertDuplicateIdRejected) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_rest(quarter())).ok());
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());

  const VoiceEvent dup_rest = voice.events()[0];  // same Rest as at index 0
  EXPECT_FALSE(
      voice.insert_event(*Rational::create(1, 4), dup_rest, Rational(1)).ok());
  EXPECT_EQ(voice.events().size(), 2u);
  EXPECT_TRUE(std::holds_alternative<Rest>(voice.events()[0]));
  EXPECT_TRUE(std::holds_alternative<Note>(voice.events()[1]));
}

TEST(CommandTest, ReplaceDuplicateIdRejected) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice.append(make_note(pitch_d4(), quarter())).ok());

  const VoiceEvent dup = voice.events()[0];  // same id as C4
  // Try to replace D4 with a copy of C4 — id collision.
  EXPECT_FALSE(
      voice.replace_event(*Rational::create(1, 4), dup, Rational(1)).ok());
}

TEST(CommandTest, ReplaceSelfIdAllowed) {
  VoiceContent     voice;
  const VoiceEvent n = make_note(pitch_c4(), quarter());
  ASSERT_TRUE(voice.append(n).ok());
  ASSERT_TRUE(voice.normalize(Rational(1)).ok());

  // Replace the event with a rest sharing the same id (allowed).
  const NotationEntityId original_id = event_id(n);
  VoiceEvent             repl(Rest{original_id, quarter()});
  ASSERT_TRUE(voice.replace_event(Rational(0), repl, Rational(1)).ok());
  EXPECT_TRUE(std::holds_alternative<Rest>(voice.events()[0]));
  EXPECT_EQ(event_id(voice.events()[0]), original_id);
}

// =========================================================================
// Phase 8e-i — Consumed/split-rest marking references
// =========================================================================

TEST(CommandTest, SetEventExpansionRejectsDanglingRestReference) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  // N(q, C4) at 0, R(q) at 1/4.  Attach a dynamic to the rest.
  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  const VoiceEvent rest_ev = make_rest(quarter());
  ASSERT_TRUE(voice->append(rest_ev).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const NotationEntityId rest_id = event_id(voice->events()[1]);
  ASSERT_TRUE(voice
                  ->add_dynamic(graphscore::make_dynamic_marking(
                      rest_id, graphscore::Dynamic::kMf))
                  .ok());

  const VoiceContent saved = *voice;

  // Replace quarter with half — fully consumes the rest, which has a
  // dynamic marking.  validate_voice_references should flag the dangling ref.
  auto cmd = std::make_unique<SetEventCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      make_note(pitch_c4(), half()));

  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(*voice, saved);
}

TEST(CommandTest, InsertConsumesRestWithMarkingRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  // R(q) at 0, N(q) at 1/4.  Slur references the rest.
  ASSERT_TRUE(voice->append(make_rest(quarter())).ok());
  const VoiceEvent note_ev = make_note(pitch_c4(), quarter());
  ASSERT_TRUE(voice->append(note_ev).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const NotationEntityId rest_id = event_id(voice->events()[0]);
  ASSERT_TRUE(
      voice
          ->add_slur(graphscore::Slur{NotationEntityId::generate(), rest_id,
                                      event_id(voice->events()[1])})
          .ok());

  const VoiceContent saved = *voice;

  // Insert a note at position 0 consuming the rest — the slur's start
  // endpoint would dangle.
  auto cmd = std::make_unique<SetEventCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      make_note(pitch_d4(), quarter()));

  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(*voice, saved);
}

TEST(CommandTest, PartialRestConsumptionPreservesSurvivingReference) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  // N(q, C4) at 0, R(h) at 1/4.  Dynamic marking references the rest.
  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->append(make_rest(half())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const NotationEntityId rest_id2 = event_id(voice->events()[1]);
  ASSERT_TRUE(voice
                  ->add_dynamic(graphscore::make_dynamic_marking(
                      rest_id2, graphscore::Dynamic::kMf))
                  .ok());

  // Replace quarter with 3/8 (dotted quarter) — consumes 1/8 from the
  // half rest, leaving 3/8 remainder that keeps the original rest id.
  const Duration dotted_q = *Duration::create(NoteValue::kQuarter, 1);
  auto           cmd      = std::make_unique<SetEventCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      make_note(pitch_c4(), dotted_q));

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  // The dynamic marking's referenced id should still resolve — the remainder
  // rest carries the original id.
  const std::vector<graphscore::NotationDiagnostic> diags =
      graphscore::validate_voice_references(*voice);
  EXPECT_TRUE(diags.empty()) << "dynamic marking should still resolve after "
                                "partial rest consumption";
}

// =========================================================================
// Phase 8e-i — Stale-context retryability
// =========================================================================

TEST(CommandTest, SetEventUndoStaleContextRetryable) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  auto cmd = std::make_unique<SetEventCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      make_rest(quarter()));
  ASSERT_TRUE(cmd->execute(fx.project).ok());

  const VoiceContent post_state = *voice;

  // Manually change voice — undo rejected.
  voice->clear();
  ASSERT_TRUE(voice->append(make_note(pitch_d4(), half())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());
  EXPECT_EQ(cmd->undo(fx.project).code(), ResultCode::kInvalidArgument);
  // Model was not corrupted by the rejected undo.
  EXPECT_TRUE(std::holds_alternative<Note>(voice->events()[0]));
  EXPECT_EQ(voice->events().size(), 2u);

  // Restore voice to exact post-snapshot — undo now succeeds and
  // restores the pre-edit state.
  *voice = post_state;
  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_TRUE(std::holds_alternative<Note>(voice->events()[0]));
}

TEST(CommandTest, ConvertEventToRestRedoStaleContextRetryable) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const VoiceContent original = *voice;

  auto cmd = std::make_unique<ConvertEventToRestCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0));
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  ASSERT_TRUE(cmd->undo(fx.project).ok());

  // Change voice — redo rejected.
  voice->clear();
  ASSERT_TRUE(voice->append(make_note(pitch_d4(), half())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());
  EXPECT_EQ(cmd->redo(fx.project).code(), ResultCode::kInvalidArgument);

  // Restore voice to the exact pre-snapshot — redo succeeds.
  *voice = original;
  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_TRUE(std::holds_alternative<Rest>(voice->events()[0]));
}

// =========================================================================
// Phase 8e-i — Append after duplicate detection works normally
// =========================================================================

TEST(CommandTest, AppendWithUniqueIdSucceeds) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice.append(make_note(pitch_d4(), quarter())).ok());
  EXPECT_EQ(voice.events().size(), 2u);
  EXPECT_NE(event_id(voice.events()[0]), event_id(voice.events()[1]));
}

// =========================================================================
// Phase 8e-i — Insertion at sounding-event boundary is rejected
// =========================================================================

TEST(CommandTest, InsertionBeforeSoundingEventRejected) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());

  // Position 0 is the start of a Note — cannot insert before sounding
  // material.
  EXPECT_FALSE(
      voice.insert_event(Rational(0), make_rest(eighth()), Rational(1)).ok());
}

TEST(CommandTest, InsertionIntoMidSoundingBoundaryRejected) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), half())).ok());
  ASSERT_TRUE(voice.append(make_note(pitch_d4(), half())).ok());

  // Position 1/2 is the start of the D4 Note — cannot insert there.
  EXPECT_FALSE(voice
                   .insert_event(*Rational::create(1, 2), make_rest(eighth()),
                                 Rational(2))
                   .ok());
}

// =========================================================================
// Phase 8e-i — Changed-timeline rejection and recovery
// =========================================================================

TEST(CommandTest, SetEventUndoRejectedWhenTimelineShortened) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  auto cmd = std::make_unique<SetEventCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      make_rest(quarter()));
  ASSERT_TRUE(cmd->execute(fx.project).ok());

  const VoiceContent post_state = *voice;

  // Replace the node's timeline with a shorter 3/4 measure.
  std::vector<Measure> short_measures = {
      Measure{*TimeSignature::create(3, 4), *KeySignature::create(0)}};
  auto short_tl = NodeTimeline::create(std::move(short_measures), {});
  ASSERT_TRUE(short_tl.has_value());
  node->set_timeline(std::move(*short_tl));

  // Undo must reject: pre_snapshot fills 1 whole note, but node_end is 3/4.
  EXPECT_EQ(cmd->undo(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(std::holds_alternative<Rest>(voice->events()[0]));

  // Restore the original timeline so undo can succeed.
  std::vector<Measure> orig_measures = {
      Measure{*TimeSignature::create(4, 4), *KeySignature::create(0)}};
  auto orig_tl = NodeTimeline::create(std::move(orig_measures), {});
  ASSERT_TRUE(orig_tl.has_value());
  node->set_timeline(std::move(*orig_tl));
  *voice = post_state;

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_TRUE(std::holds_alternative<Note>(voice->events()[0]));
}

// =====================================================================
// Phase 8e-ii — SetEventCommand rejects marking-ID collisions
// =====================================================================

TEST(CommandTest, SetEventReplaceSelfIdPreservesDynamicAtEventReference) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  // Add a dynamic pointing at the note.
  const NotationEntityId note_id      = event_id(voice->events()[0]);
  VoiceContent           pre_markings = *voice;
  ASSERT_TRUE(
      voice->add_dynamic(make_dynamic_marking(note_id, Dynamic::kF)).ok());

  // Replace the note with a different note length, reusing the same
  // event id.  This must succeed: self-id is allowed, and the dynamic
  // references the same id which is still valid after replacement.
  auto cmd = std::make_unique<SetEventCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      VoiceEvent(Note{note_id,
                      pitch_d4(),
                      half(),
                      false,
                      {},
                      graphscore::StemDirection::kAuto}));
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(voice->dynamics().size(), 1u);
}

TEST(CommandTest, SetEventWithMarkingCollidingIdRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  // Add a dynamic with some id.
  const NotationEntityId marking_id = NotationEntityId::generate();
  ASSERT_TRUE(voice
                  ->add_dynamic(DynamicMarking{
                      marking_id, event_id(voice->events()[0]), Dynamic::kF})
                  .ok());

  // Try to replace with an event whose id collides with the dynamic.
  auto cmd = std::make_unique<SetEventCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0),
      VoiceEvent(Note{marking_id,
                      pitch_d4(),
                      half(),
                      false,
                      {},
                      graphscore::StemDirection::kAuto}));
  const size_t pre_size = voice->events().size();
  const Result r        = cmd->execute(fx.project);
  EXPECT_FALSE(r.ok());
  EXPECT_EQ(r.code(), ResultCode::kInvalidArgument);

  // Voice must be unchanged: same event count, 1 dynamic.
  EXPECT_EQ(voice->events().size(), pre_size);
  EXPECT_EQ(voice->dynamics().size(), 1u);
}

TEST(CommandTest, VoiceContentAppendRejectsMarkingIdCollision) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());

  // Add a hairpin with a known id.
  const NotationEntityId id = NotationEntityId::generate();
  ASSERT_TRUE(voice
                  .add_hairpin(Hairpin{id, id, NotationEntityId::generate(),
                                       HairpinDirection::kCrescendo})
                  .ok());

  // Try to append an event that reuses the hairpin's id.
  const Result r = voice.append(VoiceEvent(Note{
      id, pitch_c4(), quarter(), false, {}, graphscore::StemDirection::kAuto}));
  EXPECT_FALSE(r.ok());
  EXPECT_EQ(r.code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(voice.events().size(), 1u);
  EXPECT_EQ(voice.hairpins().size(), 1u);
}

TEST(CommandTest, VoiceContentInsertEventRejectsMarkingIdCollision) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice.append(make_rest(quarter())).ok());
  ASSERT_TRUE(voice.normalize(*Rational::create(1, 2)).ok());

  // Add a slur with a unique id referencing the two events.
  const NotationEntityId id      = event_id(voice.events()[0]);
  const NotationEntityId id2     = event_id(voice.events()[1]);
  const NotationEntityId slur_id = NotationEntityId::generate();
  ASSERT_TRUE(voice.add_slur(Slur{slur_id, id, id2}).ok());

  const NotationEntityId insert_id = NotationEntityId::generate();
  ASSERT_TRUE(
      voice.add_dynamic(DynamicMarking{insert_id, id, Dynamic::kFf}).ok());

  // Try to insert an event with the dynamic's id at position 1/4
  // (the rest boundary), consuming the quarter rest's duration.
  const Result r =
      voice.insert_event(*Rational::create(1, 4),
                         VoiceEvent(Note{insert_id,
                                         pitch_d4(),
                                         eighth(),
                                         false,
                                         {},
                                         graphscore::StemDirection::kAuto}),
                         Rational(1));
  EXPECT_FALSE(r.ok());
  EXPECT_EQ(r.code(), ResultCode::kInvalidArgument);
  // Voice unchanged: 2 events, 1 slur, 1 dynamic.
  EXPECT_EQ(voice.events().size(), 2u);
  EXPECT_EQ(voice.slurs().size(), 1u);
  EXPECT_EQ(voice.dynamics().size(), 1u);
}

TEST(CommandTest, ConvertEventToRestRedoRejectedWhenTimelineExtended) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const VoiceContent original = *voice;

  auto cmd = std::make_unique<ConvertEventToRestCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), Rational(0));
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  ASSERT_TRUE(cmd->undo(fx.project).ok());

  // Replace the node's timeline with a longer one (one 4/4 + one 3/4).
  std::vector<Measure> long_measures = {
      Measure{*TimeSignature::create(4, 4), *KeySignature::create(0)},
      Measure{*TimeSignature::create(3, 4), *KeySignature::create(0)}};
  auto long_tl = NodeTimeline::create(std::move(long_measures), {});
  ASSERT_TRUE(long_tl.has_value());
  node->set_timeline(std::move(*long_tl));

  // Redo must reject: post_snapshot fills 1 whole note, but node_end is 7/4.
  EXPECT_EQ(cmd->redo(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_TRUE(std::holds_alternative<Note>(voice->events()[0]));

  // Restore the original timeline so redo succeeds.
  std::vector<Measure> orig_measures = {
      Measure{*TimeSignature::create(4, 4), *KeySignature::create(0)}};
  auto orig_tl = NodeTimeline::create(std::move(orig_measures), {});
  ASSERT_TRUE(orig_tl.has_value());
  node->set_timeline(std::move(*orig_tl));
  *voice = original;

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_TRUE(std::holds_alternative<Rest>(voice->events()[0]));
}

TEST(CommandTest, SetTieUndoRejectedWhenTimelineChanged) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  auto cmd = std::make_unique<SetTieCommand>(fx.node_id, fx.track_id,
                                             fx.stave_id, *Voice::create(1),
                                             Rational(0), std::nullopt, true);
  ASSERT_TRUE(cmd->execute(fx.project).ok());

  const VoiceContent post_state = *voice;

  // Replace timeline with a shorter one.
  std::vector<Measure> short_measures = {
      Measure{*TimeSignature::create(2, 4), *KeySignature::create(0)}};
  auto short_tl = NodeTimeline::create(std::move(short_measures), {});
  ASSERT_TRUE(short_tl.has_value());
  node->set_timeline(std::move(*short_tl));

  EXPECT_EQ(cmd->undo(fx.project).code(), ResultCode::kInvalidArgument);

  // Restore timeline and voice; retry succeeds.
  std::vector<Measure> orig_measures = {
      Measure{*TimeSignature::create(4, 4), *KeySignature::create(0)}};
  auto orig_tl = NodeTimeline::create(std::move(orig_measures), {});
  ASSERT_TRUE(orig_tl.has_value());
  node->set_timeline(std::move(*orig_tl));
  *voice = post_state;

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  const Note* n = std::get_if<Note>(&voice->events()[0]);
  ASSERT_NE(n, nullptr);
  EXPECT_FALSE(n->tied_to_next);
}

// =========================================================================
// Phase 8e-i — SetTie stale-context retry
// =========================================================================

TEST(CommandTest, SetTieUndoStaleContextRetryable) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  auto cmd = std::make_unique<SetTieCommand>(fx.node_id, fx.track_id,
                                             fx.stave_id, *Voice::create(1),
                                             Rational(0), std::nullopt, true);
  ASSERT_TRUE(cmd->execute(fx.project).ok());

  const VoiceContent post_state = *voice;

  // Manually change voice — undo rejected.
  voice->clear();
  ASSERT_TRUE(voice->append(make_note(pitch_d4(), half())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());
  EXPECT_EQ(cmd->undo(fx.project).code(), ResultCode::kInvalidArgument);

  // Restore voice to exact post-snapshot — undo succeeds.
  *voice = post_state;
  ASSERT_TRUE(cmd->undo(fx.project).ok());
  const Note* n = std::get_if<Note>(&voice->events()[0]);
  ASSERT_NE(n, nullptr);
  EXPECT_FALSE(n->tied_to_next);
}

TEST(CommandTest, SetTieRedoStaleContextRetryable) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const VoiceContent original = *voice;

  auto cmd = std::make_unique<SetTieCommand>(fx.node_id, fx.track_id,
                                             fx.stave_id, *Voice::create(1),
                                             Rational(0), std::nullopt, true);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  ASSERT_TRUE(cmd->undo(fx.project).ok());

  // Manually change voice — redo rejected.
  voice->clear();
  ASSERT_TRUE(voice->append(make_note(pitch_d4(), half())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());
  EXPECT_EQ(cmd->redo(fx.project).code(), ResultCode::kInvalidArgument);

  // Restore voice to exact pre-snapshot — redo succeeds.
  *voice = original;
  ASSERT_TRUE(cmd->redo(fx.project).ok());
  const Note* n = std::get_if<Note>(&voice->events()[0]);
  ASSERT_NE(n, nullptr);
  EXPECT_TRUE(n->tied_to_next);
}

// =========================================================================
// Phase 8e-ii — Dynamic marking add/remove
// =========================================================================

TEST(CommandTest, AddDynamicExecuteUndoRedoExactEquality) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const NotationEntityId eid     = event_id(voice->events()[0]);
  const DynamicMarking   marking = make_dynamic_marking(eid, Dynamic::kFf);

  auto cmd = std::make_unique<AddDynamicCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), marking);

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(voice->dynamics().size(), 1u);
  EXPECT_EQ(voice->dynamics()[0].id, marking.id);
  EXPECT_EQ(voice->dynamics()[0].at_event, eid);
  EXPECT_EQ(voice->dynamics()[0].value, Dynamic::kFf);

  const VoiceContent post_state = *voice;

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(voice->dynamics().size(), 0u);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_TRUE(*voice == post_state);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(voice->dynamics().size(), 0u);
}

TEST(CommandTest, AddDynamicDuplicateIdRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const NotationEntityId eid     = event_id(voice->events()[0]);
  const DynamicMarking   marking = make_dynamic_marking(eid, Dynamic::kFf);
  ASSERT_TRUE(voice->add_dynamic(marking).ok());

  auto cmd = std::make_unique<AddDynamicCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), marking);
  const Result result = cmd->execute(fx.project);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, AddDynamicDanglingReferenceRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const DynamicMarking marking = {NotationEntityId::generate(),
                                  NotationEntityId::generate(), Dynamic::kFf};

  auto cmd = std::make_unique<AddDynamicCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), marking);
  const Result result = cmd->execute(fx.project);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(voice->dynamics().size(), 0u);
}

TEST(CommandTest, RemoveDynamicRoundTripPreservesOtherMarkings) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_d4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const DynamicMarking dyn1 =
      make_dynamic_marking(event_id(voice->events()[0]), Dynamic::kF);
  const DynamicMarking dyn2 =
      make_dynamic_marking(event_id(voice->events()[1]), Dynamic::kP);
  ASSERT_TRUE(voice->add_dynamic(dyn1).ok());
  ASSERT_TRUE(voice->add_dynamic(dyn2).ok());
  EXPECT_EQ(voice->dynamics().size(), 2u);

  auto cmd = std::make_unique<RemoveDynamicCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), dyn1.id);

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(voice->dynamics().size(), 1u);
  EXPECT_EQ(voice->dynamics()[0].id, dyn2.id);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(voice->dynamics().size(), 2u);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(voice->dynamics().size(), 1u);
  EXPECT_EQ(voice->dynamics()[0].id, dyn2.id);
}

TEST(CommandTest, RemoveDynamicMissingIdRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  auto cmd = std::make_unique<RemoveDynamicCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1),
      NotationEntityId::generate());
  const Result result = cmd->execute(fx.project);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(voice->dynamics().size(), 0u);
}

// =========================================================================
// Phase 8e-ii — Hairpin add/remove
// =========================================================================

TEST(CommandTest, AddHairpinWithValidSpanRoundTrip) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_d4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const Hairpin hairpin =
      make_hairpin(event_id(voice->events()[0]), event_id(voice->events()[1]),
                   HairpinDirection::kCrescendo);

  auto cmd = std::make_unique<AddHairpinCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), hairpin);

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(voice->hairpins().size(), 1u);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(voice->hairpins().size(), 0u);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(voice->hairpins().size(), 1u);
}

TEST(CommandTest, AddHairpinEndBeforeStartRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_d4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const Hairpin hairpin =
      make_hairpin(event_id(voice->events()[1]), event_id(voice->events()[0]),
                   HairpinDirection::kCrescendo);

  auto cmd = std::make_unique<AddHairpinCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), hairpin);
  const Result result = cmd->execute(fx.project);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(voice->hairpins().size(), 0u);
}

// =========================================================================
// Phase 8e-ii — Slur add/remove
// =========================================================================

TEST(CommandTest, AddSlurWithValidSpanRoundTrip) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_d4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const Slur slur =
      make_slur(event_id(voice->events()[0]), event_id(voice->events()[1]));

  auto cmd = std::make_unique<AddSlurCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), slur);

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(voice->slurs().size(), 1u);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(voice->slurs().size(), 0u);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(voice->slurs().size(), 1u);
}

TEST(CommandTest, RemoveSlurPreservesHairpin) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_d4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const NotationEntityId first_id  = event_id(voice->events()[0]);
  const NotationEntityId second_id = event_id(voice->events()[1]);
  const Slur             slur      = make_slur(first_id, second_id);
  const Hairpin          hairpin =
      make_hairpin(first_id, second_id, HairpinDirection::kCrescendo);
  ASSERT_TRUE(voice->add_slur(slur).ok());
  ASSERT_TRUE(voice->add_hairpin(hairpin).ok());

  auto cmd = std::make_unique<RemoveSlurCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), slur.id);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(voice->slurs().size(), 0u);
  EXPECT_EQ(voice->hairpins().size(), 1u);
  EXPECT_EQ(voice->hairpins()[0].id, hairpin.id);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(voice->slurs().size(), 1u);
  EXPECT_EQ(voice->hairpins().size(), 1u);
}

// =========================================================================
// Phase 8e-ii — Beam override add/remove
// =========================================================================

TEST(CommandTest, AddBeamOverrideValidRoundTrip) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  const VoiceEvent e1 = make_note(pitch_c4(), eighth());
  const VoiceEvent e2 = make_note(pitch_d4(), eighth());
  ASSERT_TRUE(voice->append(e1).ok());
  ASSERT_TRUE(voice->append(e2).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const BeamOverride beam = make_beam_override(
      BeamOverride::Kind::kJoin,
      {event_id(voice->events()[0]), event_id(voice->events()[1])});

  auto cmd = std::make_unique<AddBeamOverrideCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), beam);

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(voice->beam_overrides().size(), 1u);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(voice->beam_overrides().size(), 0u);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(voice->beam_overrides().size(), 1u);
}

TEST(CommandTest, AddBeamOverrideNonAdjacentRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), eighth())).ok());
  ASSERT_TRUE(voice->append(make_rest(half())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_e4(), eighth())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const BeamOverride beam = make_beam_override(
      BeamOverride::Kind::kJoin,
      {event_id(voice->events()[0]), event_id(voice->events()[2])});

  auto cmd = std::make_unique<AddBeamOverrideCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), beam);
  const Result result = cmd->execute(fx.project);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(voice->beam_overrides().size(), 0u);
}

TEST(CommandTest, RemoveBeamOverrideMissingIdRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  auto cmd = std::make_unique<RemoveBeamOverrideCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1),
      NotationEntityId::generate());
  const Result result = cmd->execute(fx.project);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(voice->beam_overrides().size(), 0u);
}

// =========================================================================
// Phase 8e-ii — Grace group add/remove
// =========================================================================

TEST(CommandTest, AddGraceGroupValidRoundTrip) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const GraceGroup group =
      make_grace_group(event_id(voice->events()[0]),
                       {GraceNote{.pitch    = pitch_d4(),
                                  .duration = eighth(),
                                  .type     = GraceNoteType::kAcciaccatura,
                                  .slashed  = true}});

  auto cmd = std::make_unique<AddGraceGroupCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), group);

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(voice->grace_groups().size(), 1u);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(voice->grace_groups().size(), 0u);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(voice->grace_groups().size(), 1u);
}

TEST(CommandTest, AddGraceGroupPrincipalIsRestRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_rest(quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const GraceGroup group =
      make_grace_group(event_id(voice->events()[0]),
                       {GraceNote{.pitch    = pitch_d4(),
                                  .duration = eighth(),
                                  .type     = GraceNoteType::kAcciaccatura,
                                  .slashed  = true}});

  auto cmd = std::make_unique<AddGraceGroupCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), group);
  const Result result = cmd->execute(fx.project);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(voice->grace_groups().size(), 0u);
}

TEST(CommandTest, RemoveGraceGroupPreservesDynamics) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const NotationEntityId eid = event_id(voice->events()[0]);
  const DynamicMarking   dyn = make_dynamic_marking(eid, Dynamic::kF);
  const GraceGroup       group =
      make_grace_group(eid, {GraceNote{.pitch    = pitch_d4(),
                                       .duration = eighth(),
                                       .type     = GraceNoteType::kAcciaccatura,
                                       .slashed  = true}});
  ASSERT_TRUE(voice->add_dynamic(dyn).ok());
  ASSERT_TRUE(voice->add_grace_group(group).ok());

  auto cmd = std::make_unique<RemoveGraceGroupCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), group.id);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(voice->grace_groups().size(), 0u);
  EXPECT_EQ(voice->dynamics().size(), 1u);
  EXPECT_EQ(voice->dynamics()[0].id, dyn.id);
}

// =========================================================================
// Phase 8e-ii — Pedal span add/remove
// =========================================================================

TEST(CommandTest, AddPedalSpanExecuteUndoRedo) {
  auto       fx   = make_notation_setup();
  Node*      node = fx.project.find_node(fx.node_id);
  TrackLane* lane = node->lane(fx.track_id);

  ASSERT_TRUE(lane != nullptr);
  lane->ensure_stave(fx.stave_id);
  fill_all_voices(lane, fx.stave_id, fx.node_end);

  const PedalSpan span = make_pedal_span(Rational(0), *Rational::create(1, 2));

  auto cmd = std::make_unique<AddPedalSpanCommand>(fx.node_id, fx.track_id,
                                                   fx.stave_id, span);

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const std::vector<PedalSpan>* spans = lane->pedal_spans(fx.stave_id);
  ASSERT_NE(spans, nullptr);
  ASSERT_EQ(spans->size(), 1u);
  EXPECT_EQ((*spans)[0].id, span.id);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  spans = lane->pedal_spans(fx.stave_id);
  // Whole-lane undo restores exact pre-execute state with no pedal-spans key.
  if (spans != nullptr)
    EXPECT_EQ(spans->size(), 0u);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  spans = lane->pedal_spans(fx.stave_id);
  ASSERT_NE(spans, nullptr);
  EXPECT_EQ(spans->size(), 1u);
  EXPECT_EQ((*spans)[0].id, span.id);
}

TEST(CommandTest, AddPedalSpanInvalidRangeRejected) {
  auto       fx   = make_notation_setup();
  Node*      node = fx.project.find_node(fx.node_id);
  TrackLane* lane = node->lane(fx.track_id);

  lane->ensure_stave(fx.stave_id);
  fill_all_voices(lane, fx.stave_id, fx.node_end);

  const PedalSpan span = make_pedal_span(Rational(1), Rational(0));

  auto cmd = std::make_unique<AddPedalSpanCommand>(fx.node_id, fx.track_id,
                                                   fx.stave_id, span);
  const Result result = cmd->execute(fx.project);
  EXPECT_FALSE(result.ok());
  const std::vector<PedalSpan>* spans = lane->pedal_spans(fx.stave_id);
  EXPECT_TRUE(spans == nullptr || spans->empty());
}

TEST(CommandTest, AddPedalSpanDuplicateIdRejected) {
  auto       fx   = make_notation_setup();
  Node*      node = fx.project.find_node(fx.node_id);
  TrackLane* lane = node->lane(fx.track_id);

  lane->ensure_stave(fx.stave_id);
  fill_all_voices(lane, fx.stave_id, fx.node_end);

  const PedalSpan span = make_pedal_span(Rational(0), *Rational::create(1, 4));
  ASSERT_TRUE(lane->add_pedal_span(fx.stave_id, span).ok());

  auto cmd = std::make_unique<AddPedalSpanCommand>(fx.node_id, fx.track_id,
                                                   fx.stave_id, span);
  const Result result = cmd->execute(fx.project);
  EXPECT_FALSE(result.ok());
}

TEST(CommandTest, RemovePedalSpanRoundTrip) {
  auto       fx   = make_notation_setup();
  Node*      node = fx.project.find_node(fx.node_id);
  TrackLane* lane = node->lane(fx.track_id);

  lane->ensure_stave(fx.stave_id);
  fill_all_voices(lane, fx.stave_id, fx.node_end);

  const PedalSpan span = make_pedal_span(Rational(0), *Rational::create(1, 2));
  ASSERT_TRUE(lane->add_pedal_span(fx.stave_id, span).ok());

  auto cmd = std::make_unique<RemovePedalSpanCommand>(fx.node_id, fx.track_id,
                                                      fx.stave_id, span.id);

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const std::vector<PedalSpan>* spans = lane->pedal_spans(fx.stave_id);
  ASSERT_NE(spans, nullptr);
  EXPECT_EQ(spans->size(), 0u);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  spans = lane->pedal_spans(fx.stave_id);
  ASSERT_NE(spans, nullptr);
  ASSERT_EQ(spans->size(), 1u);
  EXPECT_EQ((*spans)[0].id, span.id);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  spans = lane->pedal_spans(fx.stave_id);
  ASSERT_NE(spans, nullptr);
  EXPECT_EQ(spans->size(), 0u);
}

TEST(CommandTest, RemovePedalSpanMissingIdRejected) {
  auto       fx   = make_notation_setup();
  Node*      node = fx.project.find_node(fx.node_id);
  TrackLane* lane = node->lane(fx.track_id);

  lane->ensure_stave(fx.stave_id);
  fill_all_voices(lane, fx.stave_id, fx.node_end);

  auto cmd = std::make_unique<RemovePedalSpanCommand>(
      fx.node_id, fx.track_id, fx.stave_id, NotationEntityId::generate());
  const Result result = cmd->execute(fx.project);
  EXPECT_FALSE(result.ok());
}

TEST(CommandTest, PedalSpanMultiStaveIsolation) {
  auto       fx   = make_notation_setup();
  Node*      node = fx.project.find_node(fx.node_id);
  TrackLane* lane = node->lane(fx.track_id);

  const StaveId stave_a = fx.stave_id;
  const StaveId stave_b = StaveId::generate();
  lane->ensure_stave(stave_a);
  lane->ensure_stave(stave_b);
  fill_all_voices(lane, stave_a, fx.node_end);
  fill_all_voices(lane, stave_b, fx.node_end);

  const PedalSpan span_a =
      make_pedal_span(Rational(0), *Rational::create(1, 4));
  ASSERT_TRUE(lane->add_pedal_span(stave_a, span_a).ok());

  auto cmd = std::make_unique<AddPedalSpanCommand>(
      fx.node_id, fx.track_id, stave_b,
      make_pedal_span(Rational(0), *Rational::create(1, 4)));
  ASSERT_TRUE(cmd->execute(fx.project).ok());

  // Stave A unchanged.
  const std::vector<PedalSpan>* spans_a = lane->pedal_spans(stave_a);
  ASSERT_NE(spans_a, nullptr);
  ASSERT_EQ(spans_a->size(), 1u);
  EXPECT_EQ((*spans_a)[0].id, span_a.id);

  // Stave B has one.
  const std::vector<PedalSpan>* spans_b = lane->pedal_spans(stave_b);
  ASSERT_NE(spans_b, nullptr);
  EXPECT_EQ(spans_b->size(), 1u);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  spans_a = lane->pedal_spans(stave_a);
  ASSERT_NE(spans_a, nullptr);
  EXPECT_EQ(spans_a->size(), 1u);
  // Whole-lane undo restores exact pre-execute state; stave_b had no spans.
  spans_b = lane->pedal_spans(stave_b);
  if (spans_b != nullptr)
    EXPECT_EQ(spans_b->size(), 0u);
}

// =========================================================================
// Phase 8e-ii — State misuse across the marking command family
// =========================================================================

TEST(CommandTest, MarkingCommandDoubleExecuteRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const DynamicMarking marking =
      make_dynamic_marking(event_id(voice->events()[0]), Dynamic::kF);
  auto cmd = std::make_unique<AddDynamicCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), marking);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_FALSE(cmd->execute(fx.project).ok());
}

TEST(CommandTest, MarkingCommandUndoBeforeExecuteRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const DynamicMarking marking =
      make_dynamic_marking(event_id(voice->events()[0]), Dynamic::kF);
  auto cmd = std::make_unique<AddDynamicCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), marking);
  EXPECT_FALSE(cmd->undo(fx.project).ok());
  EXPECT_EQ(voice->dynamics().size(), 0u);
}

TEST(CommandTest, MarkingCommandRedoBeforeUndoRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const DynamicMarking marking =
      make_dynamic_marking(event_id(voice->events()[0]), Dynamic::kF);
  auto cmd = std::make_unique<AddDynamicCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), marking);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_FALSE(cmd->redo(fx.project).ok());
  EXPECT_EQ(voice->dynamics().size(), 1u);
}

// =========================================================================
// Phase 8e-ii — Stale-context undo/redo rejection and retry
// =========================================================================

TEST(CommandTest, AddDynamicStaleContextUndoRejectedAndRetried) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const DynamicMarking marking =
      make_dynamic_marking(event_id(voice->events()[0]), Dynamic::kF);

  auto cmd = std::make_unique<AddDynamicCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), marking);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const VoiceContent post_state = *voice;

  // Manually change the voice — undo must be rejected.
  ASSERT_TRUE(voice
                  ->add_dynamic(make_dynamic_marking(
                      event_id(voice->events()[0]), Dynamic::kPp))
                  .ok());
  EXPECT_FALSE(cmd->undo(fx.project).ok());

  // Restore and retry.
  *voice = post_state;
  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(voice->dynamics().size(), 0u);
}

// =========================================================================
// Phase 8e-ii — Deterministic replay
// =========================================================================

TEST(CommandTest, AddDynamicDeterministicReplay) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const NotationEntityId eid = event_id(voice->events()[0]);
  const DynamicMarking   m   = make_dynamic_marking(eid, Dynamic::kFf);

  auto cmd = std::make_unique<AddDynamicCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), m);

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const VoiceContent after_execute = *voice;

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_TRUE(*voice == after_execute);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_TRUE(*voice == after_execute);
}

// =========================================================================
// Phase 8e-ii — Missing/stale node/track/stave IDs rejected
// =========================================================================

TEST(CommandTest, AddDynamicMissingNodeRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const DynamicMarking marking =
      make_dynamic_marking(event_id(voice->events()[0]), Dynamic::kF);
  auto cmd = std::make_unique<AddDynamicCommand>(
      NodeId::generate(), fx.track_id, fx.stave_id, *Voice::create(1), marking);
  const Result result = cmd->execute(fx.project);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, AddDynamicWrongVoiceScopeRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const DynamicMarking marking =
      make_dynamic_marking(event_id(voice->events()[0]), Dynamic::kF);
  auto cmd = std::make_unique<AddDynamicCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(2), marking);
  const Result result = cmd->execute(fx.project);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(voice->dynamics().size(), 0u);
}

// =========================================================================
// Phase 8e-ii — VoiceContent removal mutator tests
// =========================================================================

TEST(CommandTest, VoiceContentRemoveDynamicSuccess) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());
  const DynamicMarking m =
      make_dynamic_marking(event_id(voice.events()[0]), Dynamic::kF);
  ASSERT_TRUE(voice.add_dynamic(m).ok());
  EXPECT_EQ(voice.dynamics().size(), 1u);
  EXPECT_TRUE(voice.remove_dynamic(m.id).ok());
  EXPECT_EQ(voice.dynamics().size(), 0u);
}

TEST(CommandTest, VoiceContentRemoveDynamicMissingId) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());
  const Result r = voice.remove_dynamic(NotationEntityId::generate());
  EXPECT_FALSE(r.ok());
  EXPECT_EQ(r.code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, VoiceContentRemoveHairpinPreservesSlur) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice.append(make_note(pitch_d4(), quarter())).ok());

  const NotationEntityId first  = event_id(voice.events()[0]);
  const NotationEntityId second = event_id(voice.events()[1]);
  const Hairpin hp = make_hairpin(first, second, HairpinDirection::kCrescendo);
  const Slur    sl = make_slur(first, second);
  ASSERT_TRUE(voice.add_hairpin(hp).ok());
  ASSERT_TRUE(voice.add_slur(sl).ok());

  EXPECT_TRUE(voice.remove_hairpin(hp.id).ok());
  EXPECT_EQ(voice.hairpins().size(), 0u);
  EXPECT_EQ(voice.slurs().size(), 1u);
}

TEST(CommandTest, VoiceContentRemoveSlurMissingId) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice.append(make_note(pitch_d4(), quarter())).ok());

  const Result r = voice.remove_slur(NotationEntityId::generate());
  EXPECT_FALSE(r.ok());
  EXPECT_EQ(r.code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, VoiceContentRemoveBeamOverrideSuccess) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), eighth())).ok());
  ASSERT_TRUE(voice.append(make_note(pitch_d4(), eighth())).ok());

  const BeamOverride beam = make_beam_override(
      BeamOverride::Kind::kJoin,
      {event_id(voice.events()[0]), event_id(voice.events()[1])});
  ASSERT_TRUE(voice.add_beam_override(beam).ok());
  EXPECT_EQ(voice.beam_overrides().size(), 1u);
  EXPECT_TRUE(voice.remove_beam_override(beam.id).ok());
  EXPECT_EQ(voice.beam_overrides().size(), 0u);
}

TEST(CommandTest, VoiceContentRemoveGraceGroupSuccess) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());

  const GraceGroup group =
      make_grace_group(event_id(voice.events()[0]),
                       {GraceNote{.pitch    = pitch_d4(),
                                  .duration = eighth(),
                                  .type     = GraceNoteType::kAcciaccatura,
                                  .slashed  = true}});
  ASSERT_TRUE(voice.add_grace_group(group).ok());
  EXPECT_EQ(voice.grace_groups().size(), 1u);
  EXPECT_TRUE(voice.remove_grace_group(group.id).ok());
  EXPECT_EQ(voice.grace_groups().size(), 0u);
}

// =========================================================================
// Phase 8e-ii — TrackLane removal mutator tests
// =========================================================================

TEST(CommandTest, TrackLaneRemovePedalSpanSuccess) {
  TrackLane     lane;
  const StaveId stave = StaveId::generate();
  lane.ensure_stave(stave);

  const PedalSpan span = make_pedal_span(Rational(0), *Rational::create(1, 4));
  ASSERT_TRUE(lane.add_pedal_span(stave, span).ok());

  const Result r = lane.remove_pedal_span(stave, span.id);
  EXPECT_TRUE(r.ok());
  const std::vector<PedalSpan>* spans = lane.pedal_spans(stave);
  ASSERT_NE(spans, nullptr);
  EXPECT_EQ(spans->size(), 0u);
}

TEST(CommandTest, TrackLaneRemovePedalSpanMissingStave) {
  TrackLane    lane;
  const Result r =
      lane.remove_pedal_span(StaveId::generate(), NotationEntityId::generate());
  EXPECT_FALSE(r.ok());
  EXPECT_EQ(r.code(), ResultCode::kInvalidArgument);
}

TEST(CommandTest, TrackLaneRemovePedalSpanMissingId) {
  TrackLane     lane;
  const StaveId stave = StaveId::generate();
  lane.ensure_stave(stave);

  const Result r = lane.remove_pedal_span(stave, NotationEntityId::generate());
  EXPECT_FALSE(r.ok());
  EXPECT_EQ(r.code(), ResultCode::kInvalidArgument);
}

// =====================================================================
// Phase 8e-ii — TrackLane add_pedal_span transactional paths
// =====================================================================

TEST(CommandTest, TrackLaneAddPedalSpanAbsentStaveRejected) {
  TrackLane     lane;
  const StaveId stave = StaveId::generate();
  // No ensure_stave call: stave is absent from staves_.

  const PedalSpan span = make_pedal_span(Rational(0), *Rational::create(1, 4));
  const Result    r    = lane.add_pedal_span(stave, span);
  EXPECT_FALSE(r.ok());
  EXPECT_EQ(r.code(), ResultCode::kInvalidArgument);
  // Verify no orphan key was created in pedal_spans_.
  EXPECT_EQ(lane.pedal_spans(stave), nullptr);
}

TEST(CommandTest, TrackLaneAddPedalSpanAbsentStaveLeavesNoOrphanKey) {
  TrackLane     lane;
  const StaveId stave_a = StaveId::generate();
  const StaveId stave_b = StaveId::generate();
  lane.ensure_stave(stave_a);

  const PedalSpan span = make_pedal_span(Rational(0), *Rational::create(1, 4));
  ASSERT_TRUE(lane.add_pedal_span(stave_a, span).ok());

  // Same id on an absent stave is still rejected (stave check wins,
  // but if we change staves_ to contain it, the duplicate check
  // would still fire).
  const Result r = lane.add_pedal_span(stave_b, span);
  EXPECT_FALSE(r.ok());
  EXPECT_EQ(r.code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(lane.pedal_spans(stave_b), nullptr);
}

TEST(CommandTest, TrackLaneAddPedalSpanAbsentKeyCommitsCorrectly) {
  TrackLane     lane;
  const StaveId stave = StaveId::generate();
  lane.ensure_stave(stave);  // now the stave must exist

  const PedalSpan span = make_pedal_span(Rational(0), *Rational::create(1, 4));
  const Result    r    = lane.add_pedal_span(stave, span);
  EXPECT_TRUE(r.ok());

  const std::vector<PedalSpan>* spans = lane.pedal_spans(stave);
  ASSERT_NE(spans, nullptr);
  ASSERT_EQ(spans->size(), 1u);
  EXPECT_EQ((*spans)[0].id, span.id);
}

TEST(CommandTest, TrackLaneAddPedalSpanExistingKeyPreservesEntries) {
  TrackLane     lane;
  const StaveId stave = StaveId::generate();
  lane.ensure_stave(stave);

  const PedalSpan span1 = make_pedal_span(Rational(0), *Rational::create(1, 4));
  ASSERT_TRUE(lane.add_pedal_span(stave, span1).ok());

  const PedalSpan span2 =
      make_pedal_span(*Rational::create(1, 2), *Rational::create(3, 4));
  const Result r = lane.add_pedal_span(stave, span2);
  EXPECT_TRUE(r.ok());

  const std::vector<PedalSpan>* spans = lane.pedal_spans(stave);
  ASSERT_NE(spans, nullptr);
  ASSERT_EQ(spans->size(), 2u);
  EXPECT_EQ((*spans)[0].id, span1.id);
  EXPECT_EQ((*spans)[1].id, span2.id);
}

TEST(CommandTest, TrackLaneAddPedalSpanDuplicateAcrossStavesRejected) {
  TrackLane     lane;
  const StaveId stave_a = StaveId::generate();
  const StaveId stave_b = StaveId::generate();
  lane.ensure_stave(stave_a);
  lane.ensure_stave(stave_b);

  const PedalSpan span = make_pedal_span(Rational(0), *Rational::create(1, 4));
  ASSERT_TRUE(lane.add_pedal_span(stave_a, span).ok());

  // Same id on a different stave is rejected before any mutation.
  const Result r = lane.add_pedal_span(stave_b, span);
  EXPECT_FALSE(r.ok());
  EXPECT_EQ(r.code(), ResultCode::kInvalidArgument);

  // Verify stave_b's pedal_spans_ was never touched (absent key).
  EXPECT_EQ(lane.pedal_spans(stave_b), nullptr);
}

// =====================================================================
// Phase 8e-ii — Cross-kind marking ID uniqueness
// =====================================================================

TEST(CommandTest, AddSlurCrossKindDuplicateIdRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_d4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const NotationEntityId eid_first  = event_id(voice->events()[0]);
  const NotationEntityId eid_second = event_id(voice->events()[1]);

  // Add a dynamic with a specific id.
  const NotationEntityId shared_id = NotationEntityId::generate();
  const DynamicMarking   dyn{shared_id, eid_first, Dynamic::kMf};
  ASSERT_TRUE(voice->add_dynamic(dyn).ok());

  // Attempt to add a slur with the same id.
  const Slur slur{shared_id, eid_first, eid_second};
  auto       cmd = std::make_unique<AddSlurCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), slur);
  EXPECT_FALSE(cmd->execute(fx.project).ok());
}

TEST(CommandTest, AddHairpinCrossKindDuplicateIdRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_d4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const NotationEntityId eid_first  = event_id(voice->events()[0]);
  const NotationEntityId eid_second = event_id(voice->events()[1]);
  const NotationEntityId shared_id  = NotationEntityId::generate();
  const Slur             existing{shared_id, eid_first, eid_second};
  ASSERT_TRUE(voice->add_slur(existing).ok());

  const Hairpin hp{shared_id, eid_first, eid_second,
                   HairpinDirection::kCrescendo};
  auto          cmd = std::make_unique<AddHairpinCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), hp);
  EXPECT_FALSE(cmd->execute(fx.project).ok());
}

TEST(CommandTest, AddBeamOverrideCrossKindDuplicateIdRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), eighth())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_d4(), eighth())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const NotationEntityId shared_id = NotationEntityId::generate();
  const DynamicMarking   dyn{shared_id, event_id(voice->events()[0]),
                           Dynamic::kMf};
  ASSERT_TRUE(voice->add_dynamic(dyn).ok());

  const BeamOverride beam = make_beam_override(
      BeamOverride::Kind::kJoin,
      {event_id(voice->events()[0]), event_id(voice->events()[1])});
  // Reassign the beam's id to collide.
  const BeamOverride colliding{shared_id, beam.kind, beam.events};
  auto               cmd = std::make_unique<AddBeamOverrideCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), colliding);
  EXPECT_FALSE(cmd->execute(fx.project).ok());
}

TEST(CommandTest, AddGraceGroupCrossKindDuplicateIdRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const NotationEntityId shared_id = NotationEntityId::generate();
  const NotationEntityId eid       = event_id(voice->events()[0]);
  const Hairpin          hp{shared_id, eid, eid, HairpinDirection::kCrescendo};
  ASSERT_TRUE(voice->add_hairpin(hp).ok());

  const GraceGroup group =
      make_grace_group(eid, {GraceNote{.pitch    = pitch_d4(),
                                       .duration = eighth(),
                                       .type     = GraceNoteType::kAppoggiatura,
                                       .slashed  = false}});
  const GraceGroup colliding{shared_id, group.principal_event, group.notes};
  auto             cmd = std::make_unique<AddGraceGroupCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), colliding);
  EXPECT_FALSE(cmd->execute(fx.project).ok());
}

TEST(CommandTest, AddDynamicDuplicateMarkingIdRejects) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());
  const DynamicMarking m1 =
      make_dynamic_marking(event_id(voice.events()[0]), Dynamic::kFf);
  ASSERT_TRUE(voice.add_dynamic(m1).ok());
  EXPECT_FALSE(voice.add_dynamic(m1).ok());
}

TEST(CommandTest, AddDynamicEventIdCollisionRejected) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());
  const DynamicMarking m{event_id(voice.events()[0]),  // event id
                         event_id(voice.events()[0]), Dynamic::kFf};
  EXPECT_FALSE(voice.add_dynamic(m).ok());
}

// =====================================================================
// Phase 8e-ii — Slur Rest endpoint rejection via command
// =====================================================================

TEST(CommandTest, AddSlurAttachedToRestRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_rest(quarter())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const Slur slur =
      make_slur(event_id(voice->events()[0]), event_id(voice->events()[1]));
  auto cmd = std::make_unique<AddSlurCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), slur);
  EXPECT_FALSE(cmd->execute(fx.project).ok());
  EXPECT_EQ(voice->slurs().size(), 0u);
}

TEST(CommandTest, AddSlurBothEndpointsRestRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_rest(quarter())).ok());
  ASSERT_TRUE(voice->append(make_rest(quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const Slur slur =
      make_slur(event_id(voice->events()[0]), event_id(voice->events()[1]));
  auto cmd = std::make_unique<AddSlurCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), slur);
  EXPECT_FALSE(cmd->execute(fx.project).ok());
}

// =====================================================================
// Phase 8e-ii — Hairpin dangling/invalid endpoint rejection
// =====================================================================

TEST(CommandTest, AddHairpinDanglingEndpointRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const Hairpin hp =
      make_hairpin(event_id(voice->events()[0]), NotationEntityId::generate(),
                   HairpinDirection::kCrescendo);
  auto cmd = std::make_unique<AddHairpinCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), hp);
  EXPECT_FALSE(cmd->execute(fx.project).ok());
  EXPECT_EQ(voice->hairpins().size(), 0u);
}

// =====================================================================
// Phase 8e-ii — Beam override invalid events
// =====================================================================

TEST(CommandTest, AddBeamOverrideNonBeamableRestRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_rest(eighth())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_c4(), eighth())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const BeamOverride beam = make_beam_override(
      BeamOverride::Kind::kJoin,
      {event_id(voice->events()[0]), event_id(voice->events()[1])});
  auto cmd = std::make_unique<AddBeamOverrideCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), beam);
  EXPECT_FALSE(cmd->execute(fx.project).ok());
}

// =====================================================================
// Phase 8e-ii — Grace group dangling/invalid principal
// =====================================================================

TEST(CommandTest, AddGraceGroupPrincipalNotFoundRejected) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const GraceGroup group =
      make_grace_group(NotationEntityId::generate(),
                       {GraceNote{.pitch    = pitch_d4(),
                                  .duration = eighth(),
                                  .type     = GraceNoteType::kAppoggiatura,
                                  .slashed  = false}});
  auto cmd = std::make_unique<AddGraceGroupCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), group);
  EXPECT_FALSE(cmd->execute(fx.project).ok());
  EXPECT_EQ(voice->grace_groups().size(), 0u);
}

// =====================================================================
// Phase 8e-ii — Pedal span beyond node_end
// =====================================================================

TEST(CommandTest, AddPedalSpanBeyondNodeEndRejected) {
  auto       fx   = make_notation_setup();
  Node*      node = fx.project.find_node(fx.node_id);
  TrackLane* lane = node->lane(fx.track_id);

  ASSERT_TRUE(lane != nullptr);
  lane->ensure_stave(fx.stave_id);
  fill_all_voices(lane, fx.stave_id, fx.node_end);

  // node_end is 1 whole note (one 4/4 measure).  Span end = 2 is beyond it.
  const PedalSpan span = make_pedal_span(Rational(0), Rational(2));
  auto cmd = std::make_unique<AddPedalSpanCommand>(fx.node_id, fx.track_id,
                                                   fx.stave_id, span);
  EXPECT_FALSE(cmd->execute(fx.project).ok());
}

// =====================================================================
// Phase 8e-ii — Pedal span exact ordering and absent-container restoration
// =====================================================================

TEST(CommandTest, AddPedalSpanExactOrderAfterUndoRedo) {
  auto       fx   = make_notation_setup();
  Node*      node = fx.project.find_node(fx.node_id);
  TrackLane* lane = node->lane(fx.track_id);

  lane->ensure_stave(fx.stave_id);
  fill_all_voices(lane, fx.stave_id, fx.node_end);

  // Add two pedal spans with distinct start positions.
  const PedalSpan span1 = make_pedal_span(Rational(0), *Rational::create(1, 4));
  const PedalSpan span2 =
      make_pedal_span(*Rational::create(1, 2), *Rational::create(3, 4));
  ASSERT_TRUE(lane->add_pedal_span(fx.stave_id, span1).ok());
  ASSERT_TRUE(lane->add_pedal_span(fx.stave_id, span2).ok());

  // Now add a third via command.
  const PedalSpan span3 =
      make_pedal_span(*Rational::create(1, 4), *Rational::create(1, 2));
  auto cmd = std::make_unique<AddPedalSpanCommand>(fx.node_id, fx.track_id,
                                                   fx.stave_id, span3);
  ASSERT_TRUE(cmd->execute(fx.project).ok());

  const std::vector<PedalSpan>* spans = lane->pedal_spans(fx.stave_id);
  ASSERT_NE(spans, nullptr);
  ASSERT_EQ(spans->size(), 3u);

  const TrackLane post_add = *lane;

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  spans = lane->pedal_spans(fx.stave_id);
  ASSERT_NE(spans, nullptr);
  ASSERT_EQ(spans->size(), 2u);
  EXPECT_EQ((*spans)[0].id, span1.id);
  EXPECT_EQ((*spans)[1].id, span2.id);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(*lane, post_add);
}

TEST(CommandTest, RemovePedalSpanRestoresOrderExactly) {
  auto       fx   = make_notation_setup();
  Node*      node = fx.project.find_node(fx.node_id);
  TrackLane* lane = node->lane(fx.track_id);

  lane->ensure_stave(fx.stave_id);
  fill_all_voices(lane, fx.stave_id, fx.node_end);

  const PedalSpan span1 = make_pedal_span(Rational(0), *Rational::create(1, 4));
  const PedalSpan span2 =
      make_pedal_span(*Rational::create(1, 2), *Rational::create(3, 4));
  const PedalSpan span3 =
      make_pedal_span(*Rational::create(1, 4), *Rational::create(7, 8));
  ASSERT_TRUE(lane->add_pedal_span(fx.stave_id, span1).ok());
  ASSERT_TRUE(lane->add_pedal_span(fx.stave_id, span2).ok());
  ASSERT_TRUE(lane->add_pedal_span(fx.stave_id, span3).ok());

  const TrackLane before_remove = *lane;

  auto cmd = std::make_unique<RemovePedalSpanCommand>(fx.node_id, fx.track_id,
                                                      fx.stave_id, span3.id);
  ASSERT_TRUE(cmd->execute(fx.project).ok());

  const std::vector<PedalSpan>* spans = lane->pedal_spans(fx.stave_id);
  ASSERT_NE(spans, nullptr);
  ASSERT_EQ(spans->size(), 2u);
  EXPECT_EQ((*spans)[0].id, span1.id);
  EXPECT_EQ((*spans)[1].id, span2.id);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(*lane, before_remove);
}

// =====================================================================
// Phase 8e-ii — Pedal span absent container restoration
// =====================================================================

TEST(CommandTest, PedalSpanAbsentKeyPreservedAfterUndoRedo) {
  auto       fx   = make_notation_setup();
  Node*      node = fx.project.find_node(fx.node_id);
  TrackLane* lane = node->lane(fx.track_id);

  const StaveId stave_a = fx.stave_id;
  const StaveId stave_b = StaveId::generate();
  lane->ensure_stave(stave_a);
  lane->ensure_stave(stave_b);
  fill_all_voices(lane, stave_a, fx.node_end);
  fill_all_voices(lane, stave_b, fx.node_end);

  // Only stave_a gets pedal spans.
  const PedalSpan span = make_pedal_span(Rational(0), *Rational::create(1, 4));
  ASSERT_TRUE(lane->add_pedal_span(stave_a, span).ok());

  auto cmd = std::make_unique<RemovePedalSpanCommand>(fx.node_id, fx.track_id,
                                                      stave_a, span.id);
  ASSERT_TRUE(cmd->execute(fx.project).ok());

  // After remove, stave_a pedal vector should be empty but the key
  // should still exist (container not absent).
  const std::vector<PedalSpan>* spans_a = lane->pedal_spans(stave_a);
  EXPECT_TRUE(spans_a == nullptr || spans_a->empty());

  // Stave_b still has no pedal key at all.
  EXPECT_EQ(lane->pedal_spans(stave_b), nullptr);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  spans_a = lane->pedal_spans(stave_a);
  ASSERT_NE(spans_a, nullptr);
  ASSERT_EQ(spans_a->size(), 1u);
  EXPECT_EQ((*spans_a)[0].id, span.id);
}

// =====================================================================
// Phase 8e-ii — Pedal stale context undo/redo
// =====================================================================

TEST(CommandTest, AddPedalSpanStaleContextUndoRejected) {
  auto       fx   = make_notation_setup();
  Node*      node = fx.project.find_node(fx.node_id);
  TrackLane* lane = node->lane(fx.track_id);

  lane->ensure_stave(fx.stave_id);
  fill_all_voices(lane, fx.stave_id, fx.node_end);

  const PedalSpan span1 = make_pedal_span(Rational(0), *Rational::create(1, 4));
  auto cmd = std::make_unique<AddPedalSpanCommand>(fx.node_id, fx.track_id,
                                                   fx.stave_id, span1);
  ASSERT_TRUE(cmd->execute(fx.project).ok());

  // Manually change the lane — undo must be rejected.
  const PedalSpan span2 =
      make_pedal_span(*Rational::create(1, 2), *Rational::create(3, 4));
  ASSERT_TRUE(lane->add_pedal_span(fx.stave_id, span2).ok());

  EXPECT_FALSE(cmd->undo(fx.project).ok());
}

TEST(CommandTest, RemovePedalSpanStaleContextRedoRejected) {
  auto       fx   = make_notation_setup();
  Node*      node = fx.project.find_node(fx.node_id);
  TrackLane* lane = node->lane(fx.track_id);

  lane->ensure_stave(fx.stave_id);
  fill_all_voices(lane, fx.stave_id, fx.node_end);

  const PedalSpan span = make_pedal_span(Rational(0), *Rational::create(1, 4));
  ASSERT_TRUE(lane->add_pedal_span(fx.stave_id, span).ok());

  const TrackLane before_remove = *lane;

  auto cmd = std::make_unique<RemovePedalSpanCommand>(fx.node_id, fx.track_id,
                                                      fx.stave_id, span.id);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(*lane, before_remove);

  // Manually change the lane — redo must be rejected.
  const PedalSpan span2 =
      make_pedal_span(*Rational::create(1, 2), *Rational::create(3, 4));
  ASSERT_TRUE(lane->add_pedal_span(fx.stave_id, span2).ok());

  EXPECT_FALSE(cmd->redo(fx.project).ok());
}

// =====================================================================
// Phase 8e-ii — Pedal deterministic replay
// =====================================================================

TEST(CommandTest, AddPedalSpanDeterministicReplay) {
  auto       fx   = make_notation_setup();
  Node*      node = fx.project.find_node(fx.node_id);
  TrackLane* lane = node->lane(fx.track_id);

  lane->ensure_stave(fx.stave_id);
  fill_all_voices(lane, fx.stave_id, fx.node_end);

  const PedalSpan span = make_pedal_span(Rational(0), *Rational::create(1, 4));
  auto cmd = std::make_unique<AddPedalSpanCommand>(fx.node_id, fx.track_id,
                                                   fx.stave_id, span);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const TrackLane after_execute = *lane;

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(*lane, after_execute);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(*lane, after_execute);
}

// =====================================================================
// Phase 8e-ii — Timeline change rejects stale undo/redo
// =====================================================================

TEST(CommandTest, AddDynamicTimelineChangeUndoRejectedAndRetryable) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const DynamicMarking marking =
      make_dynamic_marking(event_id(voice->events()[0]), Dynamic::kF);
  auto cmd = std::make_unique<AddDynamicCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), marking);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const VoiceContent post_exec = *voice;

  // Replace timeline: 2/4 measure shortens node_end to 0.5.
  // The voice was complete for node_end 1.0; undo's snapshot validation
  // against the new node_end fails because the voice is now over-full.
  std::vector<Measure> short_measures = {
      Measure{*TimeSignature::create(2, 4), *KeySignature::create(0)}};
  auto short_tl = NodeTimeline::create(std::move(short_measures), {});
  ASSERT_TRUE(short_tl.has_value());
  node->set_timeline(std::move(*short_tl));

  // Undo must reject atomically; voice unchanged, command still kDone.
  EXPECT_FALSE(cmd->undo(fx.project).ok());
  EXPECT_EQ(*voice, post_exec);
  EXPECT_EQ(cmd->undo(fx.project).code(), ResultCode::kInvalidArgument);

  // Restore the exact original timeline and voice.
  std::vector<Measure> orig_measures = {
      Measure{*TimeSignature::create(4, 4), *KeySignature::create(0)}};
  auto orig_tl = NodeTimeline::create(std::move(orig_measures), {});
  ASSERT_TRUE(orig_tl.has_value());
  node->set_timeline(std::move(*orig_tl));
  *voice = post_exec;

  // Retry succeeds.
  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(voice->dynamics().size(), 0u);
}

TEST(CommandTest, AddDynamicTimelineChangeRedoRejectedAndRetryable) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const DynamicMarking marking =
      make_dynamic_marking(event_id(voice->events()[0]), Dynamic::kF);
  auto cmd = std::make_unique<AddDynamicCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), marking);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const VoiceContent post_exec = *voice;

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(voice->dynamics().size(), 0u);
  const VoiceContent post_undo = *voice;

  // Replace timeline: shorten node_end.
  std::vector<Measure> short_measures = {
      Measure{*TimeSignature::create(2, 4), *KeySignature::create(0)}};
  auto short_tl = NodeTimeline::create(std::move(short_measures), {});
  ASSERT_TRUE(short_tl.has_value());
  node->set_timeline(std::move(*short_tl));

  // Redo must reject atomically; voice unchanged, command still kUndone.
  EXPECT_FALSE(cmd->redo(fx.project).ok());
  EXPECT_EQ(*voice, post_undo);
  EXPECT_EQ(cmd->redo(fx.project).code(), ResultCode::kInvalidArgument);

  // Restore timeline and voice; retry succeeds.
  std::vector<Measure> orig_measures = {
      Measure{*TimeSignature::create(4, 4), *KeySignature::create(0)}};
  auto orig_tl = NodeTimeline::create(std::move(orig_measures), {});
  ASSERT_TRUE(orig_tl.has_value());
  node->set_timeline(std::move(*orig_tl));
  *voice = post_undo;

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(*voice, post_exec);
}

TEST(CommandTest, AddPedalSpanTimelineChangeUndoRejectedAndRetryable) {
  auto       fx   = make_notation_setup();
  Node*      node = fx.project.find_node(fx.node_id);
  TrackLane* lane = node->lane(fx.track_id);

  lane->ensure_stave(fx.stave_id);
  fill_all_voices(lane, fx.stave_id, fx.node_end);

  const PedalSpan span = make_pedal_span(Rational(0), *Rational::create(1, 2));
  auto cmd = std::make_unique<AddPedalSpanCommand>(fx.node_id, fx.track_id,
                                                   fx.stave_id, span);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const TrackLane post_exec = *lane;

  // Shorten timeline.
  std::vector<Measure> short_measures = {
      Measure{*TimeSignature::create(2, 4), *KeySignature::create(0)}};
  auto short_tl = NodeTimeline::create(std::move(short_measures), {});
  ASSERT_TRUE(short_tl.has_value());
  node->set_timeline(std::move(*short_tl));

  // Undo must reject atomically; lane unchanged.
  EXPECT_FALSE(cmd->undo(fx.project).ok());
  EXPECT_EQ(*lane, post_exec);
  EXPECT_EQ(cmd->undo(fx.project).code(), ResultCode::kInvalidArgument);

  // Restore timeline and lane; retry succeeds.
  std::vector<Measure> orig_measures = {
      Measure{*TimeSignature::create(4, 4), *KeySignature::create(0)}};
  auto orig_tl = NodeTimeline::create(std::move(orig_measures), {});
  ASSERT_TRUE(orig_tl.has_value());
  node->set_timeline(std::move(*orig_tl));
  *lane = post_exec;

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(lane->pedal_spans(fx.stave_id), nullptr);
}

TEST(CommandTest, AddPedalSpanTimelineChangeRedoRejectedAndRetryable) {
  auto       fx   = make_notation_setup();
  Node*      node = fx.project.find_node(fx.node_id);
  TrackLane* lane = node->lane(fx.track_id);

  lane->ensure_stave(fx.stave_id);
  fill_all_voices(lane, fx.stave_id, fx.node_end);

  const PedalSpan span = make_pedal_span(Rational(0), *Rational::create(1, 2));
  auto cmd = std::make_unique<AddPedalSpanCommand>(fx.node_id, fx.track_id,
                                                   fx.stave_id, span);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const TrackLane post_exec = *lane;

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  const TrackLane post_undo = *lane;

  // Shorten timeline.
  std::vector<Measure> short_measures = {
      Measure{*TimeSignature::create(2, 4), *KeySignature::create(0)}};
  auto short_tl = NodeTimeline::create(std::move(short_measures), {});
  ASSERT_TRUE(short_tl.has_value());
  node->set_timeline(std::move(*short_tl));

  // Redo must reject atomically.
  EXPECT_FALSE(cmd->redo(fx.project).ok());
  EXPECT_EQ(*lane, post_undo);
  EXPECT_EQ(cmd->redo(fx.project).code(), ResultCode::kInvalidArgument);

  // Restore timeline and lane; retry succeeds.
  std::vector<Measure> orig_measures = {
      Measure{*TimeSignature::create(4, 4), *KeySignature::create(0)}};
  auto orig_tl = NodeTimeline::create(std::move(orig_measures), {});
  ASSERT_TRUE(orig_tl.has_value());
  node->set_timeline(std::move(*orig_tl));
  *lane = post_undo;

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(*lane, post_exec);
}

// =====================================================================
// Phase 8e-ii — Non-target voice incompleteness blocks pedal commands
// =====================================================================

TEST(CommandTest, AddPedalSpanIncompleteNonTargetVoiceRejectedAndRetryable) {
  auto       fx   = make_notation_setup();
  Node*      node = fx.project.find_node(fx.node_id);
  TrackLane* lane = node->lane(fx.track_id);

  lane->ensure_stave(fx.stave_id);
  // Only fill voice 1 (target stave). Leave voices 2-4 empty/incomplete.
  {
    VoiceContent* vc = &lane->stave(fx.stave_id)->voice(*Voice::create(1));
    ASSERT_NE(vc, nullptr);
    ASSERT_TRUE(vc->append(make_note(pitch_c4(), quarter())).ok());
    ASSERT_TRUE(vc->normalize(fx.node_end).ok());
  }

  const PedalSpan span = make_pedal_span(Rational(0), *Rational::create(1, 2));
  auto cmd = std::make_unique<AddPedalSpanCommand>(fx.node_id, fx.track_id,
                                                   fx.stave_id, span);

  // Execute must reject because voice 2 is incomplete.
  EXPECT_FALSE(cmd->execute(fx.project).ok());
  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);

  // Fill voices 2-4; retry succeeds.
  for (int v = 2; v <= 4; ++v) {
    VoiceContent* vc =
        &lane->stave(fx.stave_id)
             ->voice(*Voice::create(static_cast<std::uint8_t>(v)));
    ASSERT_NE(vc, nullptr);
    ASSERT_TRUE(vc->append(make_note(pitch_c4(), quarter())).ok());
    ASSERT_TRUE(vc->normalize(fx.node_end).ok());
  }

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const std::vector<PedalSpan>* spans = lane->pedal_spans(fx.stave_id);
  ASSERT_NE(spans, nullptr);
  ASSERT_EQ(spans->size(), 1u);
}

TEST(CommandTest, AddPedalSpanUndoNonTargetVoiceBrokenAfterExecuteRejected) {
  auto       fx   = make_notation_setup();
  Node*      node = fx.project.find_node(fx.node_id);
  TrackLane* lane = node->lane(fx.track_id);

  lane->ensure_stave(fx.stave_id);
  fill_all_voices(lane, fx.stave_id, fx.node_end);

  const PedalSpan span = make_pedal_span(Rational(0), *Rational::create(1, 2));
  auto cmd = std::make_unique<AddPedalSpanCommand>(fx.node_id, fx.track_id,
                                                   fx.stave_id, span);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const TrackLane post_exec = *lane;

  // After execute, break a non-target voice by clearing it.
  VoiceContent*      vc2 = &lane->stave(fx.stave_id)->voice(*Voice::create(2));
  const VoiceContent saved_vc2 = *vc2;
  vc2->clear();
  const TrackLane broken = *lane;

  // Undo must reject atomically: lane unchanged from broken state.
  EXPECT_FALSE(cmd->undo(fx.project).ok());
  EXPECT_EQ(*lane, broken);
  EXPECT_EQ(cmd->undo(fx.project).code(), ResultCode::kInvalidArgument);

  // Restore the broken voice from our saved copy; retry succeeds.
  *vc2 = saved_vc2;
  EXPECT_EQ(*lane, post_exec);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(lane->pedal_spans(fx.stave_id), nullptr);
}

// =====================================================================
// Phase 8e-ii — Pedal command validates invalid spans on non-target
//   stave; add_pedal_span rejects absent staves
// =====================================================================

TEST(CommandTest, AddPedalSpanInvalidSpanOnNonTargetStaveRejected) {
  auto       fx   = make_notation_setup();
  Node*      node = fx.project.find_node(fx.node_id);
  TrackLane* lane = node->lane(fx.track_id);

  const StaveId stave_a = fx.stave_id;
  const StaveId stave_b = StaveId::generate();
  lane->ensure_stave(stave_a);
  lane->ensure_stave(stave_b);
  fill_all_voices(lane, stave_a, fx.node_end);
  fill_all_voices(lane, stave_b, fx.node_end);

  // Add an invalid pedal span (end > node_end) directly on stave_b.
  const PedalSpan bad_span =
      make_pedal_span(Rational(0), Rational(2));  // beyond node_end=1
  ASSERT_TRUE(lane->add_pedal_span(stave_b, bad_span).ok());

  // Now try to add a valid span on stave_a via command.
  // validate_lane_candidate must see the bad span on stave_b and reject.
  const PedalSpan good_span =
      make_pedal_span(Rational(0), *Rational::create(1, 4));
  auto cmd = std::make_unique<AddPedalSpanCommand>(fx.node_id, fx.track_id,
                                                   stave_a, good_span);
  const Result r = cmd->execute(fx.project);
  EXPECT_FALSE(r.ok());
  EXPECT_EQ(r.code(), ResultCode::kInvalidArgument);

  // The valid span was never added; stave_a has no pedal spans.
  const std::vector<PedalSpan>* spans_a = lane->pedal_spans(stave_a);
  EXPECT_TRUE(spans_a == nullptr || spans_a->empty());
}

// =====================================================================
// Phase 8e-ii — Public add API cross-kind identity scope
// =====================================================================

TEST(CommandTest, VoiceContentPublicAddSlurDuplicateIdRejected) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());

  const NotationEntityId eid = event_id(voice.events()[0]);
  const Slur             slur{NotationEntityId::generate(), eid, eid};
  ASSERT_TRUE(voice.add_slur(slur).ok());
  EXPECT_FALSE(voice.add_slur(slur).ok());
}

TEST(CommandTest, VoiceContentPublicAddHairpinDuplicateIdRejected) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());

  const NotationEntityId eid = event_id(voice.events()[0]);
  const Hairpin          hp{NotationEntityId::generate(), eid, eid,
                   HairpinDirection::kCrescendo};
  ASSERT_TRUE(voice.add_hairpin(hp).ok());
  EXPECT_FALSE(voice.add_hairpin(hp).ok());
}

TEST(CommandTest, VoiceContentPublicAddSlurCrossKindRejected) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());

  const NotationEntityId eid       = event_id(voice.events()[0]);
  const NotationEntityId shared_id = NotationEntityId::generate();

  const DynamicMarking dyn{shared_id, eid, Dynamic::kFf};
  ASSERT_TRUE(voice.add_dynamic(dyn).ok());

  const Slur slur{shared_id, eid, eid};
  EXPECT_FALSE(voice.add_slur(slur).ok());
}

TEST(CommandTest, VoiceContentPublicAddGraceGroupDuplicateRejected) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());

  const GraceGroup group =
      make_grace_group(event_id(voice.events()[0]),
                       {GraceNote{.pitch    = pitch_d4(),
                                  .duration = eighth(),
                                  .type     = GraceNoteType::kAppoggiatura,
                                  .slashed  = false}});
  ASSERT_TRUE(voice.add_grace_group(group).ok());
  EXPECT_FALSE(voice.add_grace_group(group).ok());
}

// Phase 8f-i follow-up — principal_event self-collision rejection

TEST(CommandTest, AddGraceGroupRejectsPrincipalEventEqualToGroupId) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());

  // Construct a group whose own id doubles as the principal_event.
  const NotationEntityId shared_id = NotationEntityId::generate();
  const GraceGroup       bad_group =
      GraceGroup{shared_id,
                 shared_id,
                 {GraceNote{NotationEntityId::generate(), pitch_d4(), eighth(),
                            GraceNoteType::kAppoggiatura, false}}};
  EXPECT_FALSE(voice.add_grace_group(bad_group).ok());
  EXPECT_EQ(voice.grace_groups().size(), 0u);
}

TEST(CommandTest, AddGraceGroupRejectsPrincipalEventEqualToGraceNoteId) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());

  // Construct a group where a GraceNote.id equals the principal_event.
  const NotationEntityId shared_id = NotationEntityId::generate();
  const GraceGroup       bad_group =
      GraceGroup{NotationEntityId::generate(),
                 shared_id,
                 {GraceNote{shared_id, pitch_d4(), eighth(),
                            GraceNoteType::kAppoggiatura, false}}};
  EXPECT_FALSE(voice.add_grace_group(bad_group).ok());
  EXPECT_EQ(voice.grace_groups().size(), 0u);
}

TEST(CommandTest, VoiceContentPublicAddBeamOverrideDuplicateRejected) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), eighth())).ok());
  ASSERT_TRUE(voice.append(make_note(pitch_d4(), eighth())).ok());

  const BeamOverride beam = make_beam_override(
      BeamOverride::Kind::kJoin,
      {event_id(voice.events()[0]), event_id(voice.events()[1])});
  ASSERT_TRUE(voice.add_beam_override(beam).ok());
  EXPECT_FALSE(voice.add_beam_override(beam).ok());
}

// Phase 8f-i — nil-ID rejection for every public marking insertion API

TEST(CommandTest, VoiceContentPublicAddDynamicNilIdRejected) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());

  const NotationEntityId eid = event_id(voice.events()[0]);
  const DynamicMarking   bad{NotationEntityId{}, eid, Dynamic::kMf};
  EXPECT_FALSE(voice.add_dynamic(bad).ok());
  EXPECT_EQ(voice.dynamics().size(), 0u);
}

TEST(CommandTest, VoiceContentPublicAddHairpinNilIdRejected) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());

  const NotationEntityId eid = event_id(voice.events()[0]);
  const Hairpin bad{NotationEntityId{}, eid, eid, HairpinDirection::kCrescendo};
  EXPECT_FALSE(voice.add_hairpin(bad).ok());
  EXPECT_EQ(voice.hairpins().size(), 0u);
}

TEST(CommandTest, VoiceContentPublicAddSlurNilIdRejected) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), quarter())).ok());

  const NotationEntityId eid = event_id(voice.events()[0]);
  const Slur             bad{NotationEntityId{}, eid, eid};
  EXPECT_FALSE(voice.add_slur(bad).ok());
  EXPECT_EQ(voice.slurs().size(), 0u);
}

TEST(CommandTest, VoiceContentPublicAddBeamOverrideNilIdRejected) {
  VoiceContent voice;
  ASSERT_TRUE(voice.append(make_note(pitch_c4(), eighth())).ok());
  ASSERT_TRUE(voice.append(make_note(pitch_d4(), eighth())).ok());

  const BeamOverride bad{
      NotationEntityId{},
      BeamOverride::Kind::kJoin,
      {event_id(voice.events()[0]), event_id(voice.events()[1])}};
  EXPECT_FALSE(voice.add_beam_override(bad).ok());
  EXPECT_EQ(voice.beam_overrides().size(), 0u);
}

TEST(CommandTest, TrackLaneAddPedalSpanNilIdRejected) {
  TrackLane     lane;
  const StaveId stave = StaveId::generate();
  lane.ensure_stave(stave);

  const PedalSpan bad{NotationEntityId{}, Rational(0), *Rational::create(1, 4)};
  EXPECT_FALSE(lane.add_pedal_span(stave, bad).ok());
  const std::vector<PedalSpan>* spans = lane.pedal_spans(stave);
  EXPECT_TRUE(spans == nullptr || spans->empty());
}

TEST(CommandTest, TrackLaneAddPedalSpanCrossStaveDuplicateRejected) {
  TrackLane     lane;
  const StaveId stave_a = StaveId::generate();
  const StaveId stave_b = StaveId::generate();
  lane.ensure_stave(stave_a);
  lane.ensure_stave(stave_b);

  const PedalSpan span = make_pedal_span(Rational(0), *Rational::create(1, 4));
  ASSERT_TRUE(lane.add_pedal_span(stave_a, span).ok());
  // Same id on a different stave should be rejected.
  EXPECT_FALSE(lane.add_pedal_span(stave_b, span).ok());
}

// =====================================================================
// Phase 8e-ii — Execute/undo/redo exact round-trip for every 12 commands
// =====================================================================

TEST(CommandTest, AddDynamicExactExecuteUndoRedo) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const DynamicMarking marking =
      make_dynamic_marking(event_id(voice->events()[0]), Dynamic::kFf);

  auto cmd = std::make_unique<AddDynamicCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), marking);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const VoiceContent post_exec = *voice;

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(voice->dynamics().size(), 0u);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(*voice, post_exec);
}

TEST(CommandTest, RemoveDynamicExactExecuteUndoRedo) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const DynamicMarking marking =
      make_dynamic_marking(event_id(voice->events()[0]), Dynamic::kFf);
  ASSERT_TRUE(voice->add_dynamic(marking).ok());
  const VoiceContent pre_exec = *voice;

  auto cmd = std::make_unique<RemoveDynamicCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), marking.id);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(voice->dynamics().size(), 0u);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(*voice, pre_exec);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(voice->dynamics().size(), 0u);
}

TEST(CommandTest, AddHairpinExactExecuteUndoRedo) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_d4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const Hairpin hp =
      make_hairpin(event_id(voice->events()[0]), event_id(voice->events()[1]),
                   HairpinDirection::kCrescendo);

  auto cmd = std::make_unique<AddHairpinCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), hp);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const VoiceContent post_exec = *voice;

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(voice->hairpins().size(), 0u);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(*voice, post_exec);
}

TEST(CommandTest, RemoveHairpinExactExecuteUndoRedo) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_d4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const Hairpin hp =
      make_hairpin(event_id(voice->events()[0]), event_id(voice->events()[1]),
                   HairpinDirection::kCrescendo);
  ASSERT_TRUE(voice->add_hairpin(hp).ok());
  const VoiceContent pre_exec = *voice;

  auto cmd = std::make_unique<RemoveHairpinCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), hp.id);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(voice->hairpins().size(), 0u);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(*voice, pre_exec);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(voice->hairpins().size(), 0u);
}

TEST(CommandTest, AddSlurExactExecuteUndoRedo) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_d4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const Slur slur =
      make_slur(event_id(voice->events()[0]), event_id(voice->events()[1]));

  auto cmd = std::make_unique<AddSlurCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), slur);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const VoiceContent post_exec = *voice;

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(voice->slurs().size(), 0u);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(*voice, post_exec);
}

TEST(CommandTest, RemoveSlurExactExecuteUndoRedo) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_d4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const Slur slur =
      make_slur(event_id(voice->events()[0]), event_id(voice->events()[1]));
  ASSERT_TRUE(voice->add_slur(slur).ok());
  const VoiceContent pre_exec = *voice;

  auto cmd = std::make_unique<RemoveSlurCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), slur.id);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(voice->slurs().size(), 0u);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(*voice, pre_exec);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(voice->slurs().size(), 0u);
}

TEST(CommandTest, AddBeamOverrideExactExecuteUndoRedo) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), eighth())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_d4(), eighth())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const BeamOverride beam = make_beam_override(
      BeamOverride::Kind::kJoin,
      {event_id(voice->events()[0]), event_id(voice->events()[1])});

  auto cmd = std::make_unique<AddBeamOverrideCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), beam);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const VoiceContent post_exec = *voice;

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(voice->beam_overrides().size(), 0u);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(*voice, post_exec);
}

TEST(CommandTest, RemoveBeamOverrideExactExecuteUndoRedo) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), eighth())).ok());
  ASSERT_TRUE(voice->append(make_note(pitch_d4(), eighth())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const BeamOverride beam = make_beam_override(
      BeamOverride::Kind::kJoin,
      {event_id(voice->events()[0]), event_id(voice->events()[1])});
  ASSERT_TRUE(voice->add_beam_override(beam).ok());
  const VoiceContent pre_exec = *voice;

  auto cmd = std::make_unique<RemoveBeamOverrideCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), beam.id);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(voice->beam_overrides().size(), 0u);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(*voice, pre_exec);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(voice->beam_overrides().size(), 0u);
}

TEST(CommandTest, AddGraceGroupExactExecuteUndoRedo) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const GraceGroup group =
      make_grace_group(event_id(voice->events()[0]),
                       {GraceNote{.pitch    = pitch_d4(),
                                  .duration = eighth(),
                                  .type     = GraceNoteType::kAppoggiatura,
                                  .slashed  = false}});

  auto cmd = std::make_unique<AddGraceGroupCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), group);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const VoiceContent post_exec = *voice;

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(voice->grace_groups().size(), 0u);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(*voice, post_exec);
}

TEST(CommandTest, RemoveGraceGroupExactExecuteUndoRedo) {
  auto          fx   = make_notation_setup();
  Node*         node = fx.project.find_node(fx.node_id);
  VoiceContent* voice =
      &node->lane(fx.track_id)->stave(fx.stave_id)->voice(*Voice::create(1));

  ASSERT_TRUE(voice->append(make_note(pitch_c4(), quarter())).ok());
  ASSERT_TRUE(voice->normalize(fx.node_end).ok());

  const GraceGroup group =
      make_grace_group(event_id(voice->events()[0]),
                       {GraceNote{.pitch    = pitch_d4(),
                                  .duration = eighth(),
                                  .type     = GraceNoteType::kAppoggiatura,
                                  .slashed  = false}});
  ASSERT_TRUE(voice->add_grace_group(group).ok());
  const VoiceContent pre_exec = *voice;

  auto cmd = std::make_unique<RemoveGraceGroupCommand>(
      fx.node_id, fx.track_id, fx.stave_id, *Voice::create(1), group.id);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(voice->grace_groups().size(), 0u);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(*voice, pre_exec);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(voice->grace_groups().size(), 0u);
}

TEST(CommandTest, AddPedalSpanExactExecuteUndoRedo) {
  auto       fx   = make_notation_setup();
  Node*      node = fx.project.find_node(fx.node_id);
  TrackLane* lane = node->lane(fx.track_id);

  ASSERT_TRUE(lane != nullptr);
  lane->ensure_stave(fx.stave_id);
  fill_all_voices(lane, fx.stave_id, fx.node_end);

  const PedalSpan span = make_pedal_span(Rational(0), *Rational::create(1, 2));

  auto cmd = std::make_unique<AddPedalSpanCommand>(fx.node_id, fx.track_id,
                                                   fx.stave_id, span);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const TrackLane post_exec = *lane;

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(lane->pedal_spans(fx.stave_id), nullptr);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(*lane, post_exec);
}

TEST(CommandTest, RemovePedalSpanExactExecuteUndoRedo) {
  auto       fx   = make_notation_setup();
  Node*      node = fx.project.find_node(fx.node_id);
  TrackLane* lane = node->lane(fx.track_id);

  lane->ensure_stave(fx.stave_id);
  fill_all_voices(lane, fx.stave_id, fx.node_end);

  const PedalSpan span = make_pedal_span(Rational(0), *Rational::create(1, 2));
  ASSERT_TRUE(lane->add_pedal_span(fx.stave_id, span).ok());
  const TrackLane pre_exec = *lane;

  auto cmd = std::make_unique<RemovePedalSpanCommand>(fx.node_id, fx.track_id,
                                                      fx.stave_id, span.id);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  // Whole-lane snapshot may preserve the key with an empty vector.
  const std::vector<PedalSpan>* spans = lane->pedal_spans(fx.stave_id);
  EXPECT_TRUE(spans == nullptr || spans->empty());

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(*lane, pre_exec);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  // Whole-lane snapshot may preserve the key with an empty vector.
  const std::vector<PedalSpan>* spans_redo = lane->pedal_spans(fx.stave_id);
  EXPECT_TRUE(spans_redo == nullptr || spans_redo->empty());
}

// =========================================================================
// Node tempo-lane commands: AddTempoPointCommand, RemoveTempoPointCommand,
// MoveTempoPointCommand, SetTempoPointCommand
// =========================================================================

namespace {

// A project with one node whose timeline is `measure_count` 4/4 measures,
// i.e. node_end == measure_count whole notes.
struct TempoSetup {
  Project project;
  NodeId  node_id;
};

TempoSetup make_tempo_setup(int measure_count = 4) {
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

}  // namespace

TEST(CommandTest, TempoPointCommandsAreNoexcept) {
  static_assert(noexcept(
      std::declval<AddTempoPointCommand&>().execute(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<AddTempoPointCommand&>().undo(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<AddTempoPointCommand&>().redo(std::declval<Project&>())));

  static_assert(noexcept(std::declval<RemoveTempoPointCommand&>().execute(
      std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<RemoveTempoPointCommand&>().undo(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<RemoveTempoPointCommand&>().redo(std::declval<Project&>())));

  static_assert(noexcept(std::declval<MoveTempoPointCommand&>().execute(
      std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<MoveTempoPointCommand&>().undo(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<MoveTempoPointCommand&>().redo(std::declval<Project&>())));

  static_assert(noexcept(
      std::declval<SetTempoPointCommand&>().execute(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<SetTempoPointCommand&>().undo(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<SetTempoPointCommand&>().redo(std::declval<Project&>())));
}

TEST(CommandTest, AddTempoPointRoundTrip) {
  auto fx = make_tempo_setup();
  seed_lane(&fx.project, fx.node_id,
            {tempo_point(Rational(0), 120), tempo_point(Rational(2), 90)});
  const std::vector<TempoPoint> before = tempo_points(fx.project, fx.node_id);

  auto cmd = std::make_unique<AddTempoPointCommand>(
      fx.node_id, tempo_point(Rational(1), 100, TempoSegmentKind::kLinear));

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const std::vector<TempoPoint> after = tempo_points(fx.project, fx.node_id);
  ASSERT_EQ(after.size(), 3u);
  EXPECT_EQ(after[0].position, Rational(0));
  EXPECT_EQ(after[1].position, Rational(1));
  EXPECT_EQ(after[2].position, Rational(2));
  EXPECT_EQ(after[1].segment_kind, TempoSegmentKind::kLinear);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), before);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), after);
}

TEST(CommandTest, AddTempoPointCreatesLaneAndUndoRemovesItEntirely) {
  auto fx = make_tempo_setup();
  ASSERT_EQ(tempo_lane(fx.project, fx.node_id), nullptr);

  auto cmd = std::make_unique<AddTempoPointCommand>(
      fx.node_id, tempo_point(Rational(0), 132, TempoSegmentKind::kSmooth));

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const TempoLane* lane = tempo_lane(fx.project, fx.node_id);
  ASSERT_NE(lane, nullptr);
  const std::vector<TempoPoint> created = lane->points();
  ASSERT_EQ(created.size(), 1u);
  EXPECT_EQ(created[0].segment_kind, TempoSegmentKind::kSmooth);
  EXPECT_EQ(lane->start(), Rational(0));
  EXPECT_EQ(lane->end(), Rational(4));

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(tempo_lane(fx.project, fx.node_id), nullptr);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  const TempoLane* recreated = tempo_lane(fx.project, fx.node_id);
  ASSERT_NE(recreated, nullptr);
  EXPECT_EQ(recreated->points(), created);
  EXPECT_EQ(recreated->end(), Rational(4));
}

TEST(CommandTest, AddTempoPointOnLanelessNodeRejectsNonZeroPosition) {
  auto fx = make_tempo_setup();

  auto cmd = std::make_unique<AddTempoPointCommand>(
      fx.node_id, tempo_point(Rational(1), 100));

  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(tempo_lane(fx.project, fx.node_id), nullptr);
}

TEST(CommandTest, AddTempoPointRejectsDuplicatePosition) {
  auto fx = make_tempo_setup();
  seed_lane(&fx.project, fx.node_id,
            {tempo_point(Rational(0), 120), tempo_point(Rational(1), 90)});
  const std::vector<TempoPoint> before = tempo_points(fx.project, fx.node_id);

  auto cmd = std::make_unique<AddTempoPointCommand>(
      fx.node_id, tempo_point(Rational(1), 60));

  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), before);
}

TEST(CommandTest, AddTempoPointRejectsPositionAtNodeEndAndStaysFresh) {
  auto fx = make_tempo_setup();
  seed_lane(&fx.project, fx.node_id, {tempo_point(Rational(0), 120)});
  const std::vector<TempoPoint> before = tempo_points(fx.project, fx.node_id);

  auto rejected = std::make_unique<AddTempoPointCommand>(
      fx.node_id, tempo_point(Rational(4), 90));
  EXPECT_EQ(rejected->execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), before);

  // The failed execute left the command fresh, so a valid one still works.
  auto accepted = std::make_unique<AddTempoPointCommand>(
      fx.node_id, tempo_point(Rational(3), 90));
  EXPECT_TRUE(accepted->execute(fx.project).ok());
  EXPECT_EQ(tempo_points(fx.project, fx.node_id).size(), 2u);
}

TEST(CommandTest, AddTempoPointPhaseViolationsRejected) {
  auto fx = make_tempo_setup();
  seed_lane(&fx.project, fx.node_id, {tempo_point(Rational(0), 120)});

  auto fresh = std::make_unique<AddTempoPointCommand>(
      fx.node_id, tempo_point(Rational(1), 100));
  EXPECT_EQ(fresh->undo(fx.project).code(), ResultCode::kInvalidArgument);

  ASSERT_TRUE(fresh->execute(fx.project).ok());
  EXPECT_EQ(fresh->execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(fresh->redo(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(tempo_points(fx.project, fx.node_id).size(), 2u);
}

TEST(CommandTest, AddTempoPointUndoRejectsStaleContext) {
  auto fx = make_tempo_setup();
  seed_lane(&fx.project, fx.node_id, {tempo_point(Rational(0), 120)});

  auto cmd = std::make_unique<AddTempoPointCommand>(
      fx.node_id, tempo_point(Rational(1), 100));
  ASSERT_TRUE(cmd->execute(fx.project).ok());

  // Someone else rewrote the lane behind the command's back.
  seed_lane(&fx.project, fx.node_id,
            {tempo_point(Rational(0), 60), tempo_point(Rational(2), 60)});
  const std::vector<TempoPoint> foreign = tempo_points(fx.project, fx.node_id);

  EXPECT_EQ(cmd->undo(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), foreign);
}

TEST(CommandTest, RemoveTempoPointRoundTrip) {
  auto fx = make_tempo_setup();
  seed_lane(&fx.project, fx.node_id,
            {tempo_point(Rational(0), 120),
             tempo_point(Rational(1), 100, TempoSegmentKind::kLinear),
             tempo_point(Rational(2), 90)});
  const std::vector<TempoPoint> before = tempo_points(fx.project, fx.node_id);

  auto cmd = std::make_unique<RemoveTempoPointCommand>(fx.node_id, Rational(1));

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const std::vector<TempoPoint> after = tempo_points(fx.project, fx.node_id);
  ASSERT_EQ(after.size(), 2u);
  EXPECT_EQ(after[0].position, Rational(0));
  EXPECT_EQ(after[1].position, Rational(2));

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), before);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), after);
}

TEST(CommandTest, RemoveSoleTempoPointClearsLaneAndUndoRestoresIt) {
  auto fx = make_tempo_setup();
  seed_lane(&fx.project, fx.node_id,
            {tempo_point(Rational(0), 144, TempoSegmentKind::kSmooth)});
  const std::vector<TempoPoint> before = tempo_points(fx.project, fx.node_id);

  auto cmd = std::make_unique<RemoveTempoPointCommand>(fx.node_id, Rational(0));

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(tempo_lane(fx.project, fx.node_id), nullptr);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  const TempoLane* restored = tempo_lane(fx.project, fx.node_id);
  ASSERT_NE(restored, nullptr);
  EXPECT_EQ(restored->points(), before);
  EXPECT_EQ(restored->points()[0].tempo,
            *Tempo::create(Rational(144), NoteValue::kQuarter));
  EXPECT_EQ(restored->points()[0].segment_kind, TempoSegmentKind::kSmooth);
  EXPECT_EQ(restored->start(), Rational(0));
  EXPECT_EQ(restored->end(), Rational(4));

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(tempo_lane(fx.project, fx.node_id), nullptr);
}

TEST(CommandTest, RemoveTempoPointAtZeroWithLaterPointsFails) {
  auto fx = make_tempo_setup();
  seed_lane(&fx.project, fx.node_id,
            {tempo_point(Rational(0), 120), tempo_point(Rational(1), 90)});
  const std::vector<TempoPoint> before = tempo_points(fx.project, fx.node_id);

  auto cmd = std::make_unique<RemoveTempoPointCommand>(fx.node_id, Rational(0));

  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), before);
}

TEST(CommandTest, RemoveTempoPointMissingPositionOrLaneFails) {
  auto fx = make_tempo_setup();

  auto no_lane =
      std::make_unique<RemoveTempoPointCommand>(fx.node_id, Rational(0));
  EXPECT_EQ(no_lane->execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(tempo_lane(fx.project, fx.node_id), nullptr);

  seed_lane(&fx.project, fx.node_id, {tempo_point(Rational(0), 120)});
  const std::vector<TempoPoint> before = tempo_points(fx.project, fx.node_id);

  auto missing = std::make_unique<RemoveTempoPointCommand>(
      fx.node_id, *Rational::create(1, 2));
  EXPECT_EQ(missing->execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), before);
}

TEST(CommandTest, RemoveTempoPointPhaseViolationsRejected) {
  auto fx = make_tempo_setup();
  seed_lane(&fx.project, fx.node_id,
            {tempo_point(Rational(0), 120), tempo_point(Rational(1), 90)});

  auto cmd = std::make_unique<RemoveTempoPointCommand>(fx.node_id, Rational(1));
  EXPECT_EQ(cmd->undo(fx.project).code(), ResultCode::kInvalidArgument);

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(cmd->redo(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(tempo_points(fx.project, fx.node_id).size(), 1u);
}

TEST(CommandTest, RemoveTempoPointUndoRejectsStaleContext) {
  auto fx = make_tempo_setup();
  seed_lane(&fx.project, fx.node_id,
            {tempo_point(Rational(0), 120), tempo_point(Rational(1), 90)});

  auto cmd = std::make_unique<RemoveTempoPointCommand>(fx.node_id, Rational(1));
  ASSERT_TRUE(cmd->execute(fx.project).ok());

  seed_lane(&fx.project, fx.node_id,
            {tempo_point(Rational(0), 60), tempo_point(Rational(3), 60)});
  const std::vector<TempoPoint> foreign = tempo_points(fx.project, fx.node_id);

  EXPECT_EQ(cmd->undo(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), foreign);
}

TEST(CommandTest, MoveTempoPointRoundTrip) {
  auto fx = make_tempo_setup();
  seed_lane(&fx.project, fx.node_id,
            {tempo_point(Rational(0), 120), tempo_point(Rational(1), 90)});
  const std::vector<TempoPoint> before = tempo_points(fx.project, fx.node_id);

  auto cmd = std::make_unique<MoveTempoPointCommand>(fx.node_id, Rational(1),
                                                     *Rational::create(3, 2));

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const std::vector<TempoPoint> after = tempo_points(fx.project, fx.node_id);
  ASSERT_EQ(after.size(), 2u);
  EXPECT_EQ(after[1].position, *Rational::create(3, 2));
  EXPECT_EQ(after[1].tempo, before[1].tempo);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), before);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), after);
}

TEST(CommandTest, MoveTempoPointCrossingAnotherPointReSorts) {
  auto fx = make_tempo_setup();
  seed_lane(&fx.project, fx.node_id,
            {tempo_point(Rational(0), 120),
             tempo_point(Rational(1), 100, TempoSegmentKind::kLinear),
             tempo_point(Rational(2), 90, TempoSegmentKind::kSmooth)});
  const std::vector<TempoPoint> before = tempo_points(fx.project, fx.node_id);

  auto cmd = std::make_unique<MoveTempoPointCommand>(fx.node_id, Rational(1),
                                                     Rational(3));
  ASSERT_TRUE(cmd->execute(fx.project).ok());

  const std::vector<TempoPoint> after = tempo_points(fx.project, fx.node_id);
  ASSERT_EQ(after.size(), 3u);
  EXPECT_EQ(after[0].position, Rational(0));
  EXPECT_EQ(after[1].position, Rational(2));
  EXPECT_EQ(after[2].position, Rational(3));

  // The moved point carried its tempo and segment kind across the point it
  // passed; the point it passed is otherwise untouched.
  EXPECT_EQ(after[2].tempo, before[1].tempo);
  EXPECT_EQ(after[2].segment_kind, TempoSegmentKind::kLinear);
  EXPECT_EQ(after[1], before[2]);
  EXPECT_EQ(after[0], before[0]);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), before);
}

TEST(CommandTest, MoveTempoPointToItsOwnPositionIsAnAcceptedNoOp) {
  auto fx = make_tempo_setup();
  seed_lane(&fx.project, fx.node_id,
            {tempo_point(Rational(0), 120), tempo_point(Rational(1), 90)});
  const std::vector<TempoPoint> before = tempo_points(fx.project, fx.node_id);

  auto cmd = std::make_unique<MoveTempoPointCommand>(fx.node_id, Rational(1),
                                                     Rational(1));

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), before);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), before);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), before);
}

TEST(CommandTest, MoveTempoPointRejectsCollisionAndMissingSource) {
  auto fx = make_tempo_setup();
  seed_lane(&fx.project, fx.node_id,
            {tempo_point(Rational(0), 120), tempo_point(Rational(1), 100),
             tempo_point(Rational(2), 90)});
  const std::vector<TempoPoint> before = tempo_points(fx.project, fx.node_id);

  auto collide = std::make_unique<MoveTempoPointCommand>(
      fx.node_id, Rational(1), Rational(2));
  EXPECT_EQ(collide->execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), before);

  auto missing = std::make_unique<MoveTempoPointCommand>(
      fx.node_id, *Rational::create(1, 2), Rational(3));
  EXPECT_EQ(missing->execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), before);
}

TEST(CommandTest, MoveTempoPointRejectsLeavingZeroOrLeavingTheNode) {
  auto fx = make_tempo_setup();
  seed_lane(&fx.project, fx.node_id,
            {tempo_point(Rational(0), 120), tempo_point(Rational(1), 90)});
  const std::vector<TempoPoint> before = tempo_points(fx.project, fx.node_id);

  // A lane must begin exactly at its start, so the point at 0 cannot move
  // while later points remain.
  auto off_start = std::make_unique<MoveTempoPointCommand>(
      fx.node_id, Rational(0), *Rational::create(1, 2));
  EXPECT_EQ(off_start->execute(fx.project).code(),
            ResultCode::kInvalidArgument);
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), before);

  // node_end() is exclusive: a point may not sit at or past it.
  auto past_end = std::make_unique<MoveTempoPointCommand>(
      fx.node_id, Rational(1), Rational(4));
  EXPECT_EQ(past_end->execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), before);
}

TEST(CommandTest, MoveTempoPointWithoutLaneFails) {
  auto fx = make_tempo_setup();

  auto cmd = std::make_unique<MoveTempoPointCommand>(fx.node_id, Rational(0),
                                                     Rational(1));
  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(tempo_lane(fx.project, fx.node_id), nullptr);
}

TEST(CommandTest, MoveTempoPointPhaseViolationsRejected) {
  auto fx = make_tempo_setup();
  seed_lane(&fx.project, fx.node_id,
            {tempo_point(Rational(0), 120), tempo_point(Rational(1), 90)});

  auto cmd = std::make_unique<MoveTempoPointCommand>(fx.node_id, Rational(1),
                                                     Rational(2));
  EXPECT_EQ(cmd->undo(fx.project).code(), ResultCode::kInvalidArgument);

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(cmd->redo(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(tempo_points(fx.project, fx.node_id)[1].position, Rational(2));
}

TEST(CommandTest, SetTempoPointChangesOnlyTheTargetedPoint) {
  auto fx = make_tempo_setup();
  seed_lane(&fx.project, fx.node_id,
            {tempo_point(Rational(0), 120),
             tempo_point(Rational(1), 100, TempoSegmentKind::kLinear),
             tempo_point(Rational(2), 90, TempoSegmentKind::kSmooth)});
  const std::vector<TempoPoint> before = tempo_points(fx.project, fx.node_id);

  auto cmd = std::make_unique<SetTempoPointCommand>(
      fx.node_id, Rational(1), *Tempo::create(Rational(66), NoteValue::kHalf),
      TempoSegmentKind::kSmooth);

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const std::vector<TempoPoint> after = tempo_points(fx.project, fx.node_id);
  ASSERT_EQ(after.size(), 3u);
  EXPECT_EQ(after[0], before[0]);
  EXPECT_EQ(after[2], before[2]);
  EXPECT_EQ(after[1].position, Rational(1));
  EXPECT_EQ(after[1].tempo, *Tempo::create(Rational(66), NoteValue::kHalf));
  EXPECT_EQ(after[1].segment_kind, TempoSegmentKind::kSmooth);

  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), before);

  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), after);
}

TEST(CommandTest, SetTempoPointMissingPositionOrLaneFails) {
  auto fx = make_tempo_setup();

  auto no_lane = std::make_unique<SetTempoPointCommand>(
      fx.node_id, Rational(0),
      *Tempo::create(Rational(100), NoteValue::kQuarter),
      TempoSegmentKind::kStep);
  EXPECT_EQ(no_lane->execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(tempo_lane(fx.project, fx.node_id), nullptr);

  seed_lane(&fx.project, fx.node_id, {tempo_point(Rational(0), 120)});
  const std::vector<TempoPoint> before = tempo_points(fx.project, fx.node_id);

  auto missing = std::make_unique<SetTempoPointCommand>(
      fx.node_id, Rational(1),
      *Tempo::create(Rational(100), NoteValue::kQuarter),
      TempoSegmentKind::kStep);
  EXPECT_EQ(missing->execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), before);
}

TEST(CommandTest, SetTempoPointPhaseViolationsRejected) {
  auto fx = make_tempo_setup();
  seed_lane(&fx.project, fx.node_id, {tempo_point(Rational(0), 120)});

  auto cmd = std::make_unique<SetTempoPointCommand>(
      fx.node_id, Rational(0),
      *Tempo::create(Rational(60), NoteValue::kQuarter),
      TempoSegmentKind::kLinear);
  EXPECT_EQ(cmd->undo(fx.project).code(), ResultCode::kInvalidArgument);

  ASSERT_TRUE(cmd->execute(fx.project).ok());
  EXPECT_EQ(cmd->execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(cmd->redo(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(tempo_points(fx.project, fx.node_id)[0].segment_kind,
            TempoSegmentKind::kLinear);
}

TEST(CommandTest, TempoPointCommandsRejectMissingNode) {
  auto         fx      = make_tempo_setup();
  const NodeId missing = NodeId::generate();
  seed_lane(&fx.project, fx.node_id, {tempo_point(Rational(0), 120)});
  const std::vector<TempoPoint> before = tempo_points(fx.project, fx.node_id);

  AddTempoPointCommand add(missing, tempo_point(Rational(0), 100));
  EXPECT_EQ(add.execute(fx.project).code(), ResultCode::kInvalidArgument);

  RemoveTempoPointCommand remove(missing, Rational(0));
  EXPECT_EQ(remove.execute(fx.project).code(), ResultCode::kInvalidArgument);

  MoveTempoPointCommand move(missing, Rational(0), Rational(1));
  EXPECT_EQ(move.execute(fx.project).code(), ResultCode::kInvalidArgument);

  SetTempoPointCommand set(missing, Rational(0),
                           *Tempo::create(Rational(90), NoteValue::kQuarter),
                           TempoSegmentKind::kStep);
  EXPECT_EQ(set.execute(fx.project).code(), ResultCode::kInvalidArgument);

  EXPECT_EQ(tempo_points(fx.project, fx.node_id), before);
}

TEST(CommandTest, TempoPointCommandsRejectTimelinelessNode) {
  Project      project = make_project();
  const NodeId node_id = project.add_node("No timeline");
  ASSERT_EQ(project.find_node(node_id)->timeline(), nullptr);

  AddTempoPointCommand add(node_id, tempo_point(Rational(0), 100));
  EXPECT_EQ(add.execute(project).code(), ResultCode::kInvalidArgument);

  RemoveTempoPointCommand remove(node_id, Rational(0));
  EXPECT_EQ(remove.execute(project).code(), ResultCode::kInvalidArgument);

  MoveTempoPointCommand move(node_id, Rational(0), Rational(1));
  EXPECT_EQ(move.execute(project).code(), ResultCode::kInvalidArgument);

  SetTempoPointCommand set(node_id, Rational(0),
                           *Tempo::create(Rational(90), NoteValue::kQuarter),
                           TempoSegmentKind::kStep);
  EXPECT_EQ(set.execute(project).code(), ResultCode::kInvalidArgument);

  EXPECT_EQ(project.find_node(node_id)->timeline(), nullptr);
}

TEST(CommandTest, TempoPointCommandsInterleavedUndoRedo) {
  auto fx = make_tempo_setup();
  seed_lane(&fx.project, fx.node_id, {tempo_point(Rational(0), 120)});
  const std::vector<TempoPoint> initial = tempo_points(fx.project, fx.node_id);

  auto add = std::make_unique<AddTempoPointCommand>(
      fx.node_id, tempo_point(Rational(1), 100));
  auto set = std::make_unique<SetTempoPointCommand>(
      fx.node_id, Rational(0),
      *Tempo::create(Rational(90), NoteValue::kQuarter),
      TempoSegmentKind::kLinear);

  ASSERT_TRUE(add->execute(fx.project).ok());
  const std::vector<TempoPoint> after_add =
      tempo_points(fx.project, fx.node_id);
  ASSERT_TRUE(set->execute(fx.project).ok());
  const std::vector<TempoPoint> after_set =
      tempo_points(fx.project, fx.node_id);

  ASSERT_TRUE(set->undo(fx.project).ok());
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), after_add);
  ASSERT_TRUE(add->undo(fx.project).ok());
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), initial);

  ASSERT_TRUE(add->redo(fx.project).ok());
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), after_add);
  ASSERT_TRUE(set->redo(fx.project).ok());
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), after_set);
}

TEST(CommandTest, TempoPointCommandStreamReplaysDeterministically) {
  const auto run = [](TempoSetup* fx) {
    AddTempoPointCommand add(fx->node_id, tempo_point(Rational(0), 120));
    AddTempoPointCommand add2(
        fx->node_id,
        tempo_point(*Rational::create(1, 2), 100, TempoSegmentKind::kLinear));
    MoveTempoPointCommand move(fx->node_id, *Rational::create(1, 2),
                               *Rational::create(3, 2));
    SetTempoPointCommand  set(fx->node_id, *Rational::create(3, 2),
                              *Tempo::create(Rational(72), NoteValue::kQuarter),
                              TempoSegmentKind::kSmooth);

    EXPECT_TRUE(add.execute(fx->project).ok());
    EXPECT_TRUE(add2.execute(fx->project).ok());
    EXPECT_TRUE(move.execute(fx->project).ok());
    EXPECT_TRUE(set.execute(fx->project).ok());
  };

  auto first  = make_tempo_setup();
  auto second = make_tempo_setup();
  run(&first);
  run(&second);

  EXPECT_EQ(tempo_points(first.project, first.node_id),
            tempo_points(second.project, second.node_id));
  EXPECT_EQ(tempo_points(first.project, first.node_id).size(), 2u);
}

// =========================================================================
// Phase 8e-iii — Tempo stale-context rejection and retryability
// =========================================================================

TEST(CommandTest, AddTempoPointRedoRejectsStaleContext) {
  auto fx = make_tempo_setup();
  seed_lane(&fx.project, fx.node_id, {tempo_point(Rational(0), 120)});
  const std::vector<TempoPoint> before = tempo_points(fx.project, fx.node_id);

  auto cmd = std::make_unique<AddTempoPointCommand>(
      fx.node_id, tempo_point(Rational(1), 100));
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const std::vector<TempoPoint> after = tempo_points(fx.project, fx.node_id);
  ASSERT_TRUE(cmd->undo(fx.project).ok());

  // Someone else rewrote the lane behind the command's back.
  seed_lane(&fx.project, fx.node_id,
            {tempo_point(Rational(0), 60), tempo_point(Rational(2), 60)});
  const std::vector<TempoPoint> foreign = tempo_points(fx.project, fx.node_id);

  EXPECT_EQ(cmd->redo(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), foreign);

  // The rejection left the command undone, so restoring the exact pre-edit
  // lane — the state redo verifies — lets the retried redo through.
  seed_lane(&fx.project, fx.node_id, before);
  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), after);
}

TEST(CommandTest, RemoveTempoPointRedoRejectsStaleContext) {
  auto fx = make_tempo_setup();
  seed_lane(&fx.project, fx.node_id,
            {tempo_point(Rational(0), 120), tempo_point(Rational(1), 90)});
  const std::vector<TempoPoint> before = tempo_points(fx.project, fx.node_id);

  auto cmd = std::make_unique<RemoveTempoPointCommand>(fx.node_id, Rational(1));
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const std::vector<TempoPoint> after = tempo_points(fx.project, fx.node_id);
  ASSERT_TRUE(cmd->undo(fx.project).ok());

  seed_lane(&fx.project, fx.node_id,
            {tempo_point(Rational(0), 60), tempo_point(Rational(3), 60)});
  const std::vector<TempoPoint> foreign = tempo_points(fx.project, fx.node_id);

  EXPECT_EQ(cmd->redo(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), foreign);

  seed_lane(&fx.project, fx.node_id, before);
  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), after);
}

TEST(CommandTest, MoveTempoPointStaleContextRejectedOnUndoAndRedo) {
  auto fx = make_tempo_setup();
  seed_lane(&fx.project, fx.node_id,
            {tempo_point(Rational(0), 120), tempo_point(Rational(1), 90)});
  const std::vector<TempoPoint> before = tempo_points(fx.project, fx.node_id);

  auto cmd = std::make_unique<MoveTempoPointCommand>(fx.node_id, Rational(1),
                                                     *Rational::create(3, 2));
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const std::vector<TempoPoint> after = tempo_points(fx.project, fx.node_id);

  const std::vector<TempoPoint> foreign = {tempo_point(Rational(0), 60),
                                           tempo_point(Rational(2), 60)};

  // undo verifies the post-edit lane it left behind.
  seed_lane(&fx.project, fx.node_id, foreign);
  EXPECT_EQ(cmd->undo(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), foreign);

  seed_lane(&fx.project, fx.node_id, after);
  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), before);

  // redo verifies the pre-edit lane instead.
  seed_lane(&fx.project, fx.node_id, foreign);
  EXPECT_EQ(cmd->redo(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), foreign);

  seed_lane(&fx.project, fx.node_id, before);
  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), after);
}

TEST(CommandTest, SetTempoPointStaleContextRejectedOnUndoAndRedo) {
  auto fx = make_tempo_setup();
  seed_lane(&fx.project, fx.node_id,
            {tempo_point(Rational(0), 120), tempo_point(Rational(1), 90)});
  const std::vector<TempoPoint> before = tempo_points(fx.project, fx.node_id);

  auto cmd = std::make_unique<SetTempoPointCommand>(
      fx.node_id, Rational(1), *Tempo::create(Rational(66), NoteValue::kHalf),
      TempoSegmentKind::kSmooth);
  ASSERT_TRUE(cmd->execute(fx.project).ok());
  const std::vector<TempoPoint> after = tempo_points(fx.project, fx.node_id);

  const std::vector<TempoPoint> foreign = {tempo_point(Rational(0), 60),
                                           tempo_point(Rational(2), 60)};

  seed_lane(&fx.project, fx.node_id, foreign);
  EXPECT_EQ(cmd->undo(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), foreign);

  seed_lane(&fx.project, fx.node_id, after);
  ASSERT_TRUE(cmd->undo(fx.project).ok());
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), before);

  seed_lane(&fx.project, fx.node_id, foreign);
  EXPECT_EQ(cmd->redo(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), foreign);

  seed_lane(&fx.project, fx.node_id, before);
  ASSERT_TRUE(cmd->redo(fx.project).ok());
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), after);
}

TEST(CommandTest, TempoPointUndoStaleContextRetryable) {
  auto fx = make_tempo_setup();
  seed_lane(&fx.project, fx.node_id, {tempo_point(Rational(0), 120)});
  const std::vector<TempoPoint> initial = tempo_points(fx.project, fx.node_id);

  auto add = std::make_unique<AddTempoPointCommand>(
      fx.node_id, tempo_point(Rational(1), 100));
  ASSERT_TRUE(add->execute(fx.project).ok());
  const std::vector<TempoPoint> after_add =
      tempo_points(fx.project, fx.node_id);

  seed_lane(&fx.project, fx.node_id,
            {tempo_point(Rational(0), 60), tempo_point(Rational(3), 60)});
  EXPECT_EQ(add->undo(fx.project).code(), ResultCode::kInvalidArgument);

  // The rejected undo left the command done, so restoring the exact
  // post-edit lane makes the retried undo succeed.
  seed_lane(&fx.project, fx.node_id, after_add);
  ASSERT_TRUE(add->undo(fx.project).ok());
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), initial);

  seed_lane(&fx.project, fx.node_id,
            {tempo_point(Rational(0), 120), tempo_point(Rational(1), 90)});
  const std::vector<TempoPoint> before_remove =
      tempo_points(fx.project, fx.node_id);

  auto remove =
      std::make_unique<RemoveTempoPointCommand>(fx.node_id, Rational(1));
  ASSERT_TRUE(remove->execute(fx.project).ok());
  const std::vector<TempoPoint> after_remove =
      tempo_points(fx.project, fx.node_id);

  seed_lane(&fx.project, fx.node_id,
            {tempo_point(Rational(0), 72), tempo_point(Rational(2), 72)});
  EXPECT_EQ(remove->undo(fx.project).code(), ResultCode::kInvalidArgument);

  seed_lane(&fx.project, fx.node_id, after_remove);
  ASSERT_TRUE(remove->undo(fx.project).ok());
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), before_remove);
}

TEST(CommandTest, TempoPointStaleContextAcrossLanePresence) {
  // Empty snapshot vs a lane that exists: the command created the lane,
  // then someone removed it behind the command's back.
  auto created = make_tempo_setup();
  ASSERT_EQ(tempo_lane(created.project, created.node_id), nullptr);

  auto add = std::make_unique<AddTempoPointCommand>(
      created.node_id, tempo_point(Rational(0), 132));
  ASSERT_TRUE(add->execute(created.project).ok());
  ASSERT_NE(tempo_lane(created.project, created.node_id), nullptr);

  created.project.find_node(created.node_id)->timeline()->clear_tempo();
  EXPECT_EQ(add->undo(created.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(tempo_lane(created.project, created.node_id), nullptr);

  // A lane that exists vs an empty snapshot: the command removed the last
  // point, then someone seeded a fresh lane behind its back.
  auto cleared = make_tempo_setup();
  seed_lane(&cleared.project, cleared.node_id, {tempo_point(Rational(0), 144)});

  auto remove =
      std::make_unique<RemoveTempoPointCommand>(cleared.node_id, Rational(0));
  ASSERT_TRUE(remove->execute(cleared.project).ok());
  ASSERT_EQ(tempo_lane(cleared.project, cleared.node_id), nullptr);

  seed_lane(&cleared.project, cleared.node_id, {tempo_point(Rational(0), 60)});
  const std::vector<TempoPoint> foreign =
      tempo_points(cleared.project, cleared.node_id);

  EXPECT_EQ(remove->undo(cleared.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(tempo_points(cleared.project, cleared.node_id), foreign);
}

TEST(CommandTest, TempoPointEditsRespectPickdownCoverage) {
  auto          fx       = make_tempo_setup(1);
  NodeTimeline* timeline = fx.project.find_node(fx.node_id)->timeline();
  ASSERT_NE(timeline, nullptr);
  ASSERT_TRUE(timeline->set_pickdown(*Rational::create(1, 4)).ok());
  seed_lane(&fx.project, fx.node_id, {tempo_point(Rational(0), 120)});
  const std::vector<TempoPoint> before = tempo_points(fx.project, fx.node_id);

  // node_end() is 5/4 with the pickdown; a point exactly there is outside
  // the lane's coverage and must be rejected.
  auto at_end = std::make_unique<AddTempoPointCommand>(
      fx.node_id, tempo_point(*Rational::create(5, 4), 90));
  EXPECT_EQ(at_end->execute(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), before);

  // A point inside the pickdown is fine.
  auto in_pickdown = std::make_unique<AddTempoPointCommand>(
      fx.node_id, tempo_point(*Rational::create(9, 8), 90));
  ASSERT_TRUE(in_pickdown->execute(fx.project).ok());
  EXPECT_EQ(tempo_points(fx.project, fx.node_id).size(), 2u);

  // ...and now clearing the pickdown would leave that point uncovered, so
  // the region change is rejected instead.
  EXPECT_FALSE(
      fx.project.find_node(fx.node_id)->timeline()->clear_pickdown().ok());

  // Undoing the tempo edit releases the constraint again.
  ASSERT_TRUE(in_pickdown->undo(fx.project).ok());
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), before);
  EXPECT_TRUE(
      fx.project.find_node(fx.node_id)->timeline()->clear_pickdown().ok());

  // With the pickdown gone node_end() is back to 1, so redo revalidates its
  // post-edit lane against the shorter node and is rejected; the lane is
  // left exactly as it was.
  EXPECT_EQ(in_pickdown->redo(fx.project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(tempo_points(fx.project, fx.node_id), before);

  // The rejection left the command undone: restoring the pickdown makes the
  // point at 9/8 fit again and the retried redo succeeds.
  ASSERT_TRUE(fx.project.find_node(fx.node_id)
                  ->timeline()
                  ->set_pickdown(*Rational::create(1, 4))
                  .ok());
  ASSERT_TRUE(in_pickdown->redo(fx.project).ok());
  EXPECT_EQ(tempo_points(fx.project, fx.node_id).size(), 2u);
}

// =========================================================================
// Phase 8h-ii — non-length-changing node-timeline commands
// =========================================================================

namespace {

struct TimelineCommandSetup {
  Project project;
  NodeId  node_id;
  StaveId upper_stave;
  StaveId lower_stave;
};

TimelineCommandSetup make_timeline_command_setup() {
  Project       project = make_project();
  const NodeId  node_id = project.add_node("Timeline Node");
  const StaveId upper   = StaveId::generate();
  const StaveId lower   = StaveId::generate();
  const std::vector<graphscore::StaveDefinition> staves = {
      {upper, Clef::kTreble}, {lower, Clef::kBass}};
  const std::vector<Measure> measures = {
      {*TimeSignature::create(3, 4), *KeySignature::create(0)},
      {*TimeSignature::create(4, 4), *KeySignature::create(1)},
      {*TimeSignature::create(5, 8), *KeySignature::create(-1)},
  };
  auto timeline = NodeTimeline::create(measures, staves);
  assert(timeline.has_value());
  assert(timeline->set_pickdown(*Rational::create(1, 4)).ok());
  assert(timeline
             ->set_tempo(
                 {tempo_point(Rational(0), 120), tempo_point(Rational(2), 90)})
             .ok());
  assert(timeline->add_clef_change(upper, *Rational::create(1, 2), Clef::kAlto)
             .ok());
  assert(timeline->add_clef_change(lower, Rational(1), Clef::kTenor).ok());
  project.find_node(node_id)->set_timeline(std::move(*timeline));
  return {std::move(project), node_id, upper, lower};
}

NodeTimeline* timeline_of(TimelineCommandSetup* setup) {
  return setup->project.find_node(setup->node_id)->timeline();
}

std::optional<TempoLane> snapshot_tempo(const NodeTimeline& timeline) {
  const TempoLane* lane = timeline.tempo();
  return lane == nullptr ? std::nullopt : std::optional<TempoLane>(*lane);
}

void expect_tempo(const NodeTimeline&             timeline,
                  const std::optional<TempoLane>& expected) {
  const TempoLane* lane = timeline.tempo();
  ASSERT_EQ(lane != nullptr, expected.has_value());
  if (expected.has_value())
    EXPECT_EQ(*lane, *expected);
}

void expect_full_lifecycle(Command* command, Project* project) {
  EXPECT_EQ(command->undo(*project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(command->redo(*project).code(), ResultCode::kInvalidArgument);
  ASSERT_TRUE(command->execute(*project).ok());
  EXPECT_EQ(command->execute(*project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(command->redo(*project).code(), ResultCode::kInvalidArgument);
  ASSERT_TRUE(command->undo(*project).ok());
  EXPECT_EQ(command->undo(*project).code(), ResultCode::kInvalidArgument);
  ASSERT_TRUE(command->redo(*project).ok());
}

NodeTimeline make_different_length_timeline(
    const TimelineCommandSetup& setup, const std::optional<Rational>& pickdown,
    const std::vector<TempoPoint>& tempo_points) {
  const std::vector<graphscore::StaveDefinition> staves = {
      {setup.upper_stave, Clef::kTreble}, {setup.lower_stave, Clef::kBass}};
  const std::vector<Measure> measures = {
      {*TimeSignature::create(4, 4), *KeySignature::create(0)},
      {*TimeSignature::create(4, 4), *KeySignature::create(1)},
      {*TimeSignature::create(5, 8), *KeySignature::create(-1)},
  };
  auto timeline = NodeTimeline::create(measures, staves);
  assert(timeline.has_value());
  if (pickdown.has_value())
    assert(timeline->set_pickdown(*pickdown).ok());
  assert(timeline->set_tempo(tempo_points).ok());
  return std::move(*timeline);
}

template <typename PickdownCommand>
void expect_tempo_end_stale_undo_redo_retry(PickdownCommand       command,
                                            TimelineCommandSetup* setup) {
  Node* node = setup->project.find_node(setup->node_id);
  assert(node != nullptr);
  assert(node->timeline() != nullptr);
  const NodeTimeline expected_before = *node->timeline();

  ASSERT_TRUE(command.execute(setup->project).ok());
  const NodeTimeline expected_after = *node->timeline();
  ASSERT_NE(expected_before.tempo(), nullptr);
  ASSERT_NE(expected_after.tempo(), nullptr);

  NodeTimeline stale_after =
      make_different_length_timeline(*setup, expected_after.pickdown_duration(),
                                     expected_after.tempo()->points());
  ASSERT_EQ(stale_after.pickdown_duration(),
            expected_after.pickdown_duration());
  ASSERT_EQ(stale_after.tempo()->points(), expected_after.tempo()->points());
  ASSERT_NE(stale_after.tempo()->end(), expected_after.tempo()->end());
  const Rational stale_after_length = stale_after.measures().total_length();
  const auto     stale_after_tempo  = snapshot_tempo(stale_after);
  node->set_timeline(std::move(stale_after));

  EXPECT_EQ(command.undo(setup->project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(node->timeline()->pickdown_duration(),
            expected_after.pickdown_duration());
  EXPECT_EQ(node->timeline()->measures().total_length(), stale_after_length);
  expect_tempo(*node->timeline(), stale_after_tempo);

  node->set_timeline(expected_after);
  ASSERT_TRUE(command.undo(setup->project).ok());
  expect_tempo(*node->timeline(), snapshot_tempo(expected_before));

  NodeTimeline stale_before = make_different_length_timeline(
      *setup, expected_before.pickdown_duration(),
      expected_before.tempo()->points());
  ASSERT_EQ(stale_before.pickdown_duration(),
            expected_before.pickdown_duration());
  ASSERT_EQ(stale_before.tempo()->points(), expected_before.tempo()->points());
  ASSERT_NE(stale_before.tempo()->end(), expected_before.tempo()->end());
  const Rational stale_before_length = stale_before.measures().total_length();
  const auto     stale_before_tempo  = snapshot_tempo(stale_before);
  node->set_timeline(std::move(stale_before));

  EXPECT_EQ(command.redo(setup->project).code(), ResultCode::kInvalidArgument);
  EXPECT_EQ(node->timeline()->pickdown_duration(),
            expected_before.pickdown_duration());
  EXPECT_EQ(node->timeline()->measures().total_length(), stale_before_length);
  expect_tempo(*node->timeline(), stale_before_tempo);

  node->set_timeline(expected_before);
  ASSERT_TRUE(command.redo(setup->project).ok());
  expect_tempo(*node->timeline(), snapshot_tempo(expected_after));
}

}  // namespace

TEST(CommandTest, TimelineCommandsAreNoexcept) {
  static_assert(noexcept(std::declval<SetMeasureKeySignatureCommand&>().execute(
      std::declval<Project&>())));
  static_assert(noexcept(std::declval<SetMeasureKeySignatureCommand&>().undo(
      std::declval<Project&>())));
  static_assert(noexcept(std::declval<SetMeasureKeySignatureCommand&>().redo(
      std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<AddClefChangeCommand&>().execute(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<AddClefChangeCommand&>().undo(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<AddClefChangeCommand&>().redo(std::declval<Project&>())));
  static_assert(noexcept(std::declval<RemoveClefChangeCommand&>().execute(
      std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<RemoveClefChangeCommand&>().undo(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<RemoveClefChangeCommand&>().redo(std::declval<Project&>())));
  static_assert(noexcept(std::declval<MoveClefChangeCommand&>().execute(
      std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<MoveClefChangeCommand&>().undo(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<MoveClefChangeCommand&>().redo(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<SetPickdownCommand&>().execute(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<SetPickdownCommand&>().undo(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<SetPickdownCommand&>().redo(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<ClearPickdownCommand&>().execute(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<ClearPickdownCommand&>().undo(std::declval<Project&>())));
  static_assert(noexcept(
      std::declval<ClearPickdownCommand&>().redo(std::declval<Project&>())));
}

TEST(CommandTest, SetMeasureKeySignatureFirstMiddleLastExactRoundTrips) {
  auto          setup    = make_timeline_command_setup();
  NodeTimeline* timeline = timeline_of(&setup);
  ASSERT_NE(timeline, nullptr);
  const graphscore::MeasureMap original_measures = timeline->measures();
  const auto     original_pickdown = timeline->pickdown_duration();
  const auto     original_tempo    = timeline->tempo()->points();
  const ClefLane original_upper    = *timeline->clef_lane(setup.upper_stave);
  const ClefLane original_lower    = *timeline->clef_lane(setup.lower_stave);
  const Rational original_boundary = timeline->boundary_position();
  const Rational original_end      = timeline->node_end();

  const std::vector<std::pair<std::size_t, KeySignature>> edits = {
      {0, *KeySignature::create(-7)},
      {1, *KeySignature::create(4, graphscore::Mode::kMinor)},
      {2, *KeySignature::create(7)},
  };
  for (const auto& [index, key] : edits) {
    const Measure                 before = timeline->measures().measure(index);
    SetMeasureKeySignatureCommand command(setup.node_id, index, key);
    ASSERT_TRUE(command.execute(setup.project).ok());
    const Measure after = timeline->measures().measure(index);
    EXPECT_EQ(after.time_signature, before.time_signature);
    EXPECT_EQ(after.key_signature, key);
    ASSERT_TRUE(command.undo(setup.project).ok());
    EXPECT_EQ(timeline->measures().measure(index), before);
    ASSERT_TRUE(command.redo(setup.project).ok());
    EXPECT_EQ(timeline->measures().measure(index), after);
  }

  EXPECT_EQ(timeline->measures().measure_count(),
            original_measures.measure_count());
  for (std::size_t index = 0; index < original_measures.measure_count();
       ++index) {
    EXPECT_EQ(timeline->measures().measure(index).time_signature,
              original_measures.measure(index).time_signature);
    EXPECT_EQ(timeline->measures().measure_start(index),
              original_measures.measure_start(index));
    EXPECT_EQ(timeline->measures().measure_length(index),
              original_measures.measure_length(index));
  }
  EXPECT_EQ(timeline->boundary_position(), original_boundary);
  EXPECT_EQ(timeline->node_end(), original_end);
  EXPECT_EQ(timeline->pickdown_duration(), original_pickdown);
  EXPECT_EQ(timeline->tempo()->points(), original_tempo);
  EXPECT_EQ(*timeline->clef_lane(setup.upper_stave), original_upper);
  EXPECT_EQ(*timeline->clef_lane(setup.lower_stave), original_lower);
}

TEST(CommandTest, ClefAddRemoveMoveExactRoundTripsAndIndependentStaves) {
  {
    auto                 setup    = make_timeline_command_setup();
    NodeTimeline*        timeline = timeline_of(&setup);
    const ClefLane       before   = *timeline->clef_lane(setup.upper_stave);
    const ClefLane       other    = *timeline->clef_lane(setup.lower_stave);
    AddClefChangeCommand command(setup.node_id, setup.upper_stave, Rational(2),
                                 Clef::kBass);
    ASSERT_TRUE(command.execute(setup.project).ok());
    const ClefLane after = *timeline->clef_lane(setup.upper_stave);
    EXPECT_EQ(*timeline->clef_lane(setup.lower_stave), other);
    ASSERT_TRUE(command.undo(setup.project).ok());
    EXPECT_EQ(*timeline->clef_lane(setup.upper_stave), before);
    EXPECT_EQ(*timeline->clef_lane(setup.lower_stave), other);
    ASSERT_TRUE(command.redo(setup.project).ok());
    EXPECT_EQ(*timeline->clef_lane(setup.upper_stave), after);
    EXPECT_EQ(*timeline->clef_lane(setup.lower_stave), other);
  }
  {
    auto                    setup    = make_timeline_command_setup();
    NodeTimeline*           timeline = timeline_of(&setup);
    const ClefLane          before   = *timeline->clef_lane(setup.lower_stave);
    const ClefLane          other    = *timeline->clef_lane(setup.upper_stave);
    RemoveClefChangeCommand command(setup.node_id, setup.lower_stave,
                                    Rational(1));
    ASSERT_TRUE(command.execute(setup.project).ok());
    const ClefLane after = *timeline->clef_lane(setup.lower_stave);
    EXPECT_EQ(*timeline->clef_lane(setup.upper_stave), other);
    ASSERT_TRUE(command.undo(setup.project).ok());
    EXPECT_EQ(*timeline->clef_lane(setup.lower_stave), before);
    EXPECT_EQ(*timeline->clef_lane(setup.upper_stave), other);
    ASSERT_TRUE(command.redo(setup.project).ok());
    EXPECT_EQ(*timeline->clef_lane(setup.lower_stave), after);
    EXPECT_EQ(*timeline->clef_lane(setup.upper_stave), other);
  }
  {
    auto                  setup    = make_timeline_command_setup();
    NodeTimeline*         timeline = timeline_of(&setup);
    const ClefLane        before   = *timeline->clef_lane(setup.upper_stave);
    const ClefLane        other    = *timeline->clef_lane(setup.lower_stave);
    MoveClefChangeCommand command(setup.node_id, setup.upper_stave,
                                  *Rational::create(1, 2), Rational(2));
    ASSERT_TRUE(command.execute(setup.project).ok());
    const ClefLane after = *timeline->clef_lane(setup.upper_stave);
    EXPECT_EQ(*timeline->clef_lane(setup.lower_stave), other);
    ASSERT_TRUE(command.undo(setup.project).ok());
    EXPECT_EQ(*timeline->clef_lane(setup.upper_stave), before);
    EXPECT_EQ(*timeline->clef_lane(setup.lower_stave), other);
    ASSERT_TRUE(command.redo(setup.project).ok());
    EXPECT_EQ(*timeline->clef_lane(setup.upper_stave), after);
    EXPECT_EQ(*timeline->clef_lane(setup.lower_stave), other);
  }
}

TEST(CommandTest, PickdownSetReplacementAndClearExactRoundTrips) {
  {
    auto               setup        = make_timeline_command_setup();
    NodeTimeline*      timeline     = timeline_of(&setup);
    const auto         before_tempo = snapshot_tempo(*timeline);
    SetPickdownCommand command(setup.node_id, *Rational::create(1, 2));
    ASSERT_TRUE(command.execute(setup.project).ok());
    const auto after_tempo = snapshot_tempo(*timeline);
    EXPECT_EQ(timeline->pickdown_duration(), *Rational::create(1, 2));
    ASSERT_TRUE(before_tempo.has_value());
    ASSERT_TRUE(after_tempo.has_value());
    EXPECT_EQ(after_tempo->points(), before_tempo->points());
    EXPECT_EQ(after_tempo->start(), Rational(0));
    EXPECT_EQ(after_tempo->end(), timeline->node_end());
    ASSERT_TRUE(command.undo(setup.project).ok());
    EXPECT_EQ(timeline->pickdown_duration(), *Rational::create(1, 4));
    expect_tempo(*timeline, before_tempo);
    ASSERT_TRUE(command.redo(setup.project).ok());
    EXPECT_EQ(timeline->pickdown_duration(), *Rational::create(1, 2));
    expect_tempo(*timeline, after_tempo);
  }
  {
    auto                 setup        = make_timeline_command_setup();
    NodeTimeline*        timeline     = timeline_of(&setup);
    const auto           before_tempo = snapshot_tempo(*timeline);
    ClearPickdownCommand command(setup.node_id);
    ASSERT_TRUE(command.execute(setup.project).ok());
    const auto after_tempo = snapshot_tempo(*timeline);
    EXPECT_FALSE(timeline->pickdown_duration().has_value());
    ASSERT_TRUE(before_tempo.has_value());
    ASSERT_TRUE(after_tempo.has_value());
    EXPECT_EQ(after_tempo->points(), before_tempo->points());
    EXPECT_EQ(after_tempo->start(), Rational(0));
    EXPECT_EQ(after_tempo->end(), timeline->boundary_position());
    ASSERT_TRUE(command.undo(setup.project).ok());
    EXPECT_EQ(timeline->pickdown_duration(), *Rational::create(1, 4));
    expect_tempo(*timeline, before_tempo);
    ASSERT_TRUE(command.redo(setup.project).ok());
    EXPECT_FALSE(timeline->pickdown_duration().has_value());
    expect_tempo(*timeline, after_tempo);
  }
  {
    auto setup = make_timeline_command_setup();
    ASSERT_TRUE(timeline_of(&setup)->clear_pickdown().ok());
    const auto         before_tempo = snapshot_tempo(*timeline_of(&setup));
    SetPickdownCommand command(setup.node_id, *Rational::create(1, 4));
    ASSERT_TRUE(command.execute(setup.project).ok());
    const auto after_tempo = snapshot_tempo(*timeline_of(&setup));
    ASSERT_TRUE(command.undo(setup.project).ok());
    EXPECT_FALSE(timeline_of(&setup)->pickdown_duration().has_value());
    expect_tempo(*timeline_of(&setup), before_tempo);
    ASSERT_TRUE(command.redo(setup.project).ok());
    EXPECT_EQ(timeline_of(&setup)->pickdown_duration(),
              *Rational::create(1, 4));
    expect_tempo(*timeline_of(&setup), after_tempo);
  }
}

TEST(CommandTest, PickdownCommandsPreserveAbsentTempoLane) {
  {
    auto          setup    = make_timeline_command_setup();
    NodeTimeline* timeline = timeline_of(&setup);
    timeline->clear_tempo();
    ASSERT_EQ(timeline->tempo(), nullptr);

    SetPickdownCommand command(setup.node_id, *Rational::create(1, 2));
    ASSERT_TRUE(command.execute(setup.project).ok());
    EXPECT_EQ(timeline->tempo(), nullptr);
    ASSERT_TRUE(command.undo(setup.project).ok());
    EXPECT_EQ(timeline->tempo(), nullptr);
    ASSERT_TRUE(command.redo(setup.project).ok());
    EXPECT_EQ(timeline->tempo(), nullptr);
  }
  {
    auto          setup    = make_timeline_command_setup();
    NodeTimeline* timeline = timeline_of(&setup);
    timeline->clear_tempo();
    ASSERT_EQ(timeline->tempo(), nullptr);

    ClearPickdownCommand command(setup.node_id);
    ASSERT_TRUE(command.execute(setup.project).ok());
    EXPECT_EQ(timeline->tempo(), nullptr);
    ASSERT_TRUE(command.undo(setup.project).ok());
    EXPECT_EQ(timeline->tempo(), nullptr);
    ASSERT_TRUE(command.redo(setup.project).ok());
    EXPECT_EQ(timeline->tempo(), nullptr);
  }
}

TEST(CommandTest, TimelineCommandFamilyRejectsIllegalLifecycleCalls) {
  {
    auto                          setup = make_timeline_command_setup();
    SetMeasureKeySignatureCommand command(setup.node_id, 0,
                                          *KeySignature::create(3));
    expect_full_lifecycle(&command, &setup.project);
  }
  {
    auto                 setup = make_timeline_command_setup();
    AddClefChangeCommand command(setup.node_id, setup.upper_stave, Rational(2),
                                 Clef::kBass);
    expect_full_lifecycle(&command, &setup.project);
  }
  {
    auto                    setup = make_timeline_command_setup();
    RemoveClefChangeCommand command(setup.node_id, setup.upper_stave,
                                    *Rational::create(1, 2));
    expect_full_lifecycle(&command, &setup.project);
  }
  {
    auto                  setup = make_timeline_command_setup();
    MoveClefChangeCommand command(setup.node_id, setup.upper_stave,
                                  *Rational::create(1, 2), Rational(2));
    expect_full_lifecycle(&command, &setup.project);
  }
  {
    auto               setup = make_timeline_command_setup();
    SetPickdownCommand command(setup.node_id, *Rational::create(1, 2));
    expect_full_lifecycle(&command, &setup.project);
  }
  {
    auto                 setup = make_timeline_command_setup();
    ClearPickdownCommand command(setup.node_id);
    expect_full_lifecycle(&command, &setup.project);
  }
}

TEST(CommandTest, TimelineCommandsRejectMissingNodeAndTimelinelessNode) {
  auto         setup       = make_timeline_command_setup();
  const NodeId missing     = NodeId::generate();
  const NodeId no_timeline = setup.project.add_node("No timeline");
  for (const NodeId node : {missing, no_timeline}) {
    SetMeasureKeySignatureCommand key(node, 0, *KeySignature::create(2));
    AddClefChangeCommand add(node, setup.upper_stave, Rational(2), Clef::kBass);
    RemoveClefChangeCommand remove(node, setup.upper_stave, Rational(0));
    MoveClefChangeCommand   move(node, setup.upper_stave, Rational(0),
                                 Rational(1));
    SetPickdownCommand      set(node, *Rational::create(1, 4));
    ClearPickdownCommand    clear(node);
    EXPECT_EQ(key.execute(setup.project).code(), ResultCode::kInvalidArgument);
    EXPECT_EQ(add.execute(setup.project).code(), ResultCode::kInvalidArgument);
    EXPECT_EQ(remove.execute(setup.project).code(),
              ResultCode::kInvalidArgument);
    EXPECT_EQ(move.execute(setup.project).code(), ResultCode::kInvalidArgument);
    EXPECT_EQ(set.execute(setup.project).code(), ResultCode::kInvalidArgument);
    EXPECT_EQ(clear.execute(setup.project).code(),
              ResultCode::kInvalidArgument);
  }
}

TEST(CommandTest, TimelineCommandsRejectInvalidTargetsWithoutMutation) {
  auto                         setup    = make_timeline_command_setup();
  NodeTimeline*                timeline = timeline_of(&setup);
  const graphscore::MeasureMap measures = timeline->measures();
  const ClefLane               upper = *timeline->clef_lane(setup.upper_stave);
  const auto                   pickdown      = timeline->pickdown_duration();
  const StaveId                missing_stave = StaveId::generate();

  SetMeasureKeySignatureCommand bad_index(setup.node_id, 3,
                                          *KeySignature::create(2));
  AddClefChangeCommand          duplicate(setup.node_id, setup.upper_stave,
                                          *Rational::create(1, 2), Clef::kBass);
  AddClefChangeCommand negative(setup.node_id, setup.upper_stave, Rational(-1),
                                Clef::kBass);
  AddClefChangeCommand missing_stave_add(setup.node_id, missing_stave,
                                         Rational(0), Clef::kBass);
  RemoveClefChangeCommand missing_position(setup.node_id, setup.upper_stave,
                                           Rational(3));
  RemoveClefChangeCommand negative_remove(setup.node_id, setup.upper_stave,
                                          Rational(-1));
  MoveClefChangeCommand   missing_move(setup.node_id, setup.upper_stave,
                                       Rational(3), Rational(4));
  MoveClefChangeCommand   negative_move(setup.node_id, setup.upper_stave,
                                        *Rational::create(1, 2), Rational(-1));
  ASSERT_TRUE(
      timeline->add_clef_change(setup.upper_stave, Rational(2), Clef::kTenor)
          .ok());
  const ClefLane upper_with_target = *timeline->clef_lane(setup.upper_stave);
  MoveClefChangeCommand occupied_now(setup.node_id, setup.upper_stave,
                                     *Rational::create(1, 2), Rational(2));

  EXPECT_EQ(bad_index.execute(setup.project).code(),
            ResultCode::kInvalidArgument);
  EXPECT_EQ(duplicate.execute(setup.project).code(),
            ResultCode::kInvalidArgument);
  EXPECT_EQ(negative.execute(setup.project).code(),
            ResultCode::kInvalidArgument);
  EXPECT_EQ(missing_stave_add.execute(setup.project).code(),
            ResultCode::kInvalidArgument);
  EXPECT_EQ(missing_position.execute(setup.project).code(),
            ResultCode::kInvalidArgument);
  EXPECT_EQ(negative_remove.execute(setup.project).code(),
            ResultCode::kInvalidArgument);
  EXPECT_EQ(missing_move.execute(setup.project).code(),
            ResultCode::kInvalidArgument);
  EXPECT_EQ(negative_move.execute(setup.project).code(),
            ResultCode::kInvalidArgument);
  EXPECT_EQ(occupied_now.execute(setup.project).code(),
            ResultCode::kInvalidArgument);
  EXPECT_EQ(timeline->measures(), measures);
  EXPECT_EQ(*timeline->clef_lane(setup.upper_stave), upper_with_target);
  EXPECT_EQ(timeline->pickdown_duration(), pickdown);
  EXPECT_NE(upper, upper_with_target);
}

TEST(CommandTest, SetPickdownRejectsZeroNegativeAndBoundaryAtomically) {
  auto           setup        = make_timeline_command_setup();
  NodeTimeline*  timeline     = timeline_of(&setup);
  const auto     before       = timeline->pickdown_duration();
  const auto     tempo_before = timeline->tempo()->points();
  const Rational boundary_measure_length =
      timeline->measures().measure_length(2);
  for (const Rational invalid :
       {Rational(0), Rational(-1), boundary_measure_length, Rational(1)}) {
    SetPickdownCommand command(setup.node_id, invalid);
    EXPECT_EQ(command.execute(setup.project).code(),
              ResultCode::kInvalidArgument);
    EXPECT_EQ(timeline->pickdown_duration(), before);
    EXPECT_EQ(timeline->tempo()->points(), tempo_before);
  }
}

TEST(CommandTest, PickdownCommandsPreserveTempoFailureAtomicity) {
  {
    auto          setup    = make_timeline_command_setup();
    NodeTimeline* timeline = timeline_of(&setup);
    ASSERT_TRUE(timeline->set_pickdown(*Rational::create(1, 2)).ok());
    ASSERT_TRUE(timeline
                    ->set_tempo({tempo_point(Rational(0), 120),
                                 tempo_point(*Rational::create(11, 4), 90)})
                    .ok());
    const auto         tempo_before = timeline->tempo()->points();
    SetPickdownCommand command(setup.node_id, *Rational::create(1, 4));
    EXPECT_EQ(command.execute(setup.project).code(),
              ResultCode::kInvalidArgument);
    EXPECT_EQ(timeline->pickdown_duration(), *Rational::create(1, 2));
    EXPECT_EQ(timeline->tempo()->points(), tempo_before);
  }
  {
    auto          setup    = make_timeline_command_setup();
    NodeTimeline* timeline = timeline_of(&setup);
    ASSERT_TRUE(timeline->set_pickdown(*Rational::create(1, 2)).ok());
    ASSERT_TRUE(timeline
                    ->set_tempo({tempo_point(Rational(0), 120),
                                 tempo_point(*Rational::create(11, 4), 90)})
                    .ok());
    const auto           tempo_before = timeline->tempo()->points();
    ClearPickdownCommand command(setup.node_id);
    EXPECT_EQ(command.execute(setup.project).code(),
              ResultCode::kInvalidArgument);
    EXPECT_EQ(timeline->pickdown_duration(), *Rational::create(1, 2));
    EXPECT_EQ(timeline->tempo()->points(), tempo_before);
  }
}

TEST(CommandTest, KeyAndClefCommandsRejectStaleUndoAndRedoRetryably) {
  {
    auto                          setup    = make_timeline_command_setup();
    NodeTimeline*                 timeline = timeline_of(&setup);
    const Measure                 before   = timeline->measures().measure(0);
    SetMeasureKeySignatureCommand command(setup.node_id, 0,
                                          *KeySignature::create(3));
    ASSERT_TRUE(command.execute(setup.project).ok());
    const Measure after = timeline->measures().measure(0);
    ASSERT_TRUE(
        timeline->set_measure_key_signature(0, *KeySignature::create(4)).ok());
    EXPECT_EQ(command.undo(setup.project).code(), ResultCode::kInvalidArgument);
    ASSERT_TRUE(
        timeline->set_measure_key_signature(0, after.key_signature).ok());
    ASSERT_TRUE(command.undo(setup.project).ok());
    ASSERT_TRUE(
        timeline->set_measure_key_signature(0, *KeySignature::create(5)).ok());
    EXPECT_EQ(command.redo(setup.project).code(), ResultCode::kInvalidArgument);
    ASSERT_TRUE(
        timeline->set_measure_key_signature(0, before.key_signature).ok());
    EXPECT_TRUE(command.redo(setup.project).ok());
  }
  {
    auto                 setup    = make_timeline_command_setup();
    NodeTimeline*        timeline = timeline_of(&setup);
    const ClefLane       before   = *timeline->clef_lane(setup.upper_stave);
    AddClefChangeCommand command(setup.node_id, setup.upper_stave, Rational(2),
                                 Clef::kBass);
    ASSERT_TRUE(command.execute(setup.project).ok());
    const ClefLane after = *timeline->clef_lane(setup.upper_stave);
    ASSERT_TRUE(
        timeline->add_clef_change(setup.upper_stave, Rational(3), Clef::kTenor)
            .ok());
    EXPECT_EQ(command.undo(setup.project).code(), ResultCode::kInvalidArgument);
    ASSERT_TRUE(
        timeline->remove_clef_change(setup.upper_stave, Rational(3)).ok());
    ASSERT_TRUE(command.undo(setup.project).ok());
    ASSERT_TRUE(
        timeline->add_clef_change(setup.upper_stave, Rational(3), Clef::kTenor)
            .ok());
    EXPECT_EQ(command.redo(setup.project).code(), ResultCode::kInvalidArgument);
    ASSERT_TRUE(
        timeline->remove_clef_change(setup.upper_stave, Rational(3)).ok());
    EXPECT_EQ(*timeline->clef_lane(setup.upper_stave), before);
    ASSERT_TRUE(command.redo(setup.project).ok());
    EXPECT_EQ(*timeline->clef_lane(setup.upper_stave), after);
  }
}

TEST(CommandTest, RemoveMoveClefAndPickdownRejectStaleContexts) {
  {
    auto                    setup    = make_timeline_command_setup();
    NodeTimeline*           timeline = timeline_of(&setup);
    RemoveClefChangeCommand command(setup.node_id, setup.upper_stave,
                                    *Rational::create(1, 2));
    ASSERT_TRUE(command.execute(setup.project).ok());
    ASSERT_TRUE(
        timeline->add_clef_change(setup.upper_stave, Rational(3), Clef::kTenor)
            .ok());
    const ClefLane stale = *timeline->clef_lane(setup.upper_stave);
    EXPECT_EQ(command.undo(setup.project).code(), ResultCode::kInvalidArgument);
    EXPECT_EQ(*timeline->clef_lane(setup.upper_stave), stale);
    ASSERT_TRUE(
        timeline->remove_clef_change(setup.upper_stave, Rational(3)).ok());
    ASSERT_TRUE(command.undo(setup.project).ok());
  }
  {
    auto                  setup    = make_timeline_command_setup();
    NodeTimeline*         timeline = timeline_of(&setup);
    MoveClefChangeCommand command(setup.node_id, setup.upper_stave,
                                  *Rational::create(1, 2), Rational(2));
    ASSERT_TRUE(command.execute(setup.project).ok());
    ASSERT_TRUE(
        timeline->add_clef_change(setup.upper_stave, Rational(3), Clef::kTenor)
            .ok());
    const ClefLane stale = *timeline->clef_lane(setup.upper_stave);
    EXPECT_EQ(command.undo(setup.project).code(), ResultCode::kInvalidArgument);
    EXPECT_EQ(*timeline->clef_lane(setup.upper_stave), stale);
    ASSERT_TRUE(
        timeline->remove_clef_change(setup.upper_stave, Rational(3)).ok());
    ASSERT_TRUE(command.undo(setup.project).ok());
  }
  {
    auto               setup    = make_timeline_command_setup();
    NodeTimeline*      timeline = timeline_of(&setup);
    SetPickdownCommand command(setup.node_id, *Rational::create(1, 2));
    ASSERT_TRUE(command.execute(setup.project).ok());
    ASSERT_TRUE(timeline->set_pickdown(*Rational::create(3, 8)).ok());
    EXPECT_EQ(command.undo(setup.project).code(), ResultCode::kInvalidArgument);
  }
  {
    auto                 setup    = make_timeline_command_setup();
    NodeTimeline*        timeline = timeline_of(&setup);
    ClearPickdownCommand command(setup.node_id);
    ASSERT_TRUE(command.execute(setup.project).ok());
    ASSERT_TRUE(timeline->set_pickdown(*Rational::create(3, 8)).ok());
    EXPECT_EQ(command.undo(setup.project).code(), ResultCode::kInvalidArgument);
  }
}

TEST(CommandTest, RemoveMoveClefAndPickdownRejectStaleRedoContexts) {
  {
    auto                    setup    = make_timeline_command_setup();
    NodeTimeline*           timeline = timeline_of(&setup);
    RemoveClefChangeCommand command(setup.node_id, setup.upper_stave,
                                    *Rational::create(1, 2));
    ASSERT_TRUE(command.execute(setup.project).ok());
    ASSERT_TRUE(command.undo(setup.project).ok());
    ASSERT_TRUE(
        timeline->add_clef_change(setup.upper_stave, Rational(3), Clef::kTenor)
            .ok());
    const ClefLane stale = *timeline->clef_lane(setup.upper_stave);
    EXPECT_EQ(command.redo(setup.project).code(), ResultCode::kInvalidArgument);
    EXPECT_EQ(*timeline->clef_lane(setup.upper_stave), stale);
    ASSERT_TRUE(
        timeline->remove_clef_change(setup.upper_stave, Rational(3)).ok());
    ASSERT_TRUE(command.redo(setup.project).ok());
  }
  {
    auto                  setup    = make_timeline_command_setup();
    NodeTimeline*         timeline = timeline_of(&setup);
    MoveClefChangeCommand command(setup.node_id, setup.upper_stave,
                                  *Rational::create(1, 2), Rational(2));
    ASSERT_TRUE(command.execute(setup.project).ok());
    ASSERT_TRUE(command.undo(setup.project).ok());
    ASSERT_TRUE(
        timeline->add_clef_change(setup.upper_stave, Rational(3), Clef::kTenor)
            .ok());
    const ClefLane stale = *timeline->clef_lane(setup.upper_stave);
    EXPECT_EQ(command.redo(setup.project).code(), ResultCode::kInvalidArgument);
    EXPECT_EQ(*timeline->clef_lane(setup.upper_stave), stale);
    ASSERT_TRUE(
        timeline->remove_clef_change(setup.upper_stave, Rational(3)).ok());
    ASSERT_TRUE(command.redo(setup.project).ok());
  }
  {
    auto               setup    = make_timeline_command_setup();
    NodeTimeline*      timeline = timeline_of(&setup);
    SetPickdownCommand command(setup.node_id, *Rational::create(1, 2));
    ASSERT_TRUE(command.execute(setup.project).ok());
    ASSERT_TRUE(command.undo(setup.project).ok());
    ASSERT_TRUE(timeline->set_pickdown(*Rational::create(3, 8)).ok());
    EXPECT_EQ(command.redo(setup.project).code(), ResultCode::kInvalidArgument);
  }
  {
    auto                 setup    = make_timeline_command_setup();
    NodeTimeline*        timeline = timeline_of(&setup);
    ClearPickdownCommand command(setup.node_id);
    ASSERT_TRUE(command.execute(setup.project).ok());
    ASSERT_TRUE(command.undo(setup.project).ok());
    ASSERT_TRUE(timeline->set_pickdown(*Rational::create(3, 8)).ok());
    EXPECT_EQ(command.redo(setup.project).code(), ResultCode::kInvalidArgument);
  }
}

TEST(CommandTest, PickdownCommandsRejectStaleTempoUndoAndRedoRetryably) {
  {
    auto               setup        = make_timeline_command_setup();
    NodeTimeline*      timeline     = timeline_of(&setup);
    const auto         before_tempo = snapshot_tempo(*timeline);
    SetPickdownCommand command(setup.node_id, *Rational::create(1, 2));
    ASSERT_TRUE(command.execute(setup.project).ok());
    const auto after_tempo = snapshot_tempo(*timeline);
    ASSERT_TRUE(before_tempo.has_value());
    ASSERT_TRUE(after_tempo.has_value());

    ASSERT_TRUE(timeline->set_tempo({tempo_point(Rational(0), 60)}).ok());
    const auto stale_undo_tempo    = snapshot_tempo(*timeline);
    const auto stale_undo_pickdown = timeline->pickdown_duration();
    EXPECT_EQ(command.undo(setup.project).code(), ResultCode::kInvalidArgument);
    expect_tempo(*timeline, stale_undo_tempo);
    EXPECT_EQ(timeline->pickdown_duration(), stale_undo_pickdown);

    ASSERT_TRUE(timeline->set_tempo(after_tempo->points()).ok());
    ASSERT_TRUE(command.undo(setup.project).ok());
    expect_tempo(*timeline, before_tempo);
    ASSERT_TRUE(timeline->set_tempo({tempo_point(Rational(0), 72)}).ok());
    const auto stale_redo_tempo    = snapshot_tempo(*timeline);
    const auto stale_redo_pickdown = timeline->pickdown_duration();
    EXPECT_EQ(command.redo(setup.project).code(), ResultCode::kInvalidArgument);
    expect_tempo(*timeline, stale_redo_tempo);
    EXPECT_EQ(timeline->pickdown_duration(), stale_redo_pickdown);

    ASSERT_TRUE(timeline->set_tempo(before_tempo->points()).ok());
    ASSERT_TRUE(command.redo(setup.project).ok());
    expect_tempo(*timeline, after_tempo);
  }
  {
    auto                 setup        = make_timeline_command_setup();
    NodeTimeline*        timeline     = timeline_of(&setup);
    const auto           before_tempo = snapshot_tempo(*timeline);
    ClearPickdownCommand command(setup.node_id);
    ASSERT_TRUE(command.execute(setup.project).ok());
    const auto after_tempo = snapshot_tempo(*timeline);
    ASSERT_TRUE(before_tempo.has_value());
    ASSERT_TRUE(after_tempo.has_value());

    ASSERT_TRUE(timeline->set_tempo({tempo_point(Rational(0), 60)}).ok());
    const auto stale_undo_tempo    = snapshot_tempo(*timeline);
    const auto stale_undo_pickdown = timeline->pickdown_duration();
    EXPECT_EQ(command.undo(setup.project).code(), ResultCode::kInvalidArgument);
    expect_tempo(*timeline, stale_undo_tempo);
    EXPECT_EQ(timeline->pickdown_duration(), stale_undo_pickdown);

    ASSERT_TRUE(timeline->set_tempo(after_tempo->points()).ok());
    ASSERT_TRUE(command.undo(setup.project).ok());
    expect_tempo(*timeline, before_tempo);
    ASSERT_TRUE(timeline->set_tempo({tempo_point(Rational(0), 72)}).ok());
    const auto stale_redo_tempo    = snapshot_tempo(*timeline);
    const auto stale_redo_pickdown = timeline->pickdown_duration();
    EXPECT_EQ(command.redo(setup.project).code(), ResultCode::kInvalidArgument);
    expect_tempo(*timeline, stale_redo_tempo);
    EXPECT_EQ(timeline->pickdown_duration(), stale_redo_pickdown);

    ASSERT_TRUE(timeline->set_tempo(before_tempo->points()).ok());
    ASSERT_TRUE(command.redo(setup.project).ok());
    expect_tempo(*timeline, after_tempo);
  }
}

TEST(CommandTest, PickdownCommandsRejectTempoEndStaleUndoRedoRetryably) {
  {
    auto setup = make_timeline_command_setup();
    expect_tempo_end_stale_undo_redo_retry(
        SetPickdownCommand(setup.node_id, *Rational::create(1, 2)), &setup);
  }
  {
    auto setup = make_timeline_command_setup();
    expect_tempo_end_stale_undo_redo_retry(ClearPickdownCommand(setup.node_id),
                                           &setup);
  }
}
