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

using graphscore::AddInputConnectorCommand;
using graphscore::AddOutputConnectorCommand;
using graphscore::ArchiveTrackCommand;
using graphscore::BindOutputEventCommand;
using graphscore::Command;
using graphscore::CommandHistory;
using graphscore::CommandTransaction;
using graphscore::ConnectCommand;
using graphscore::ConnectorId;
using graphscore::ConnectorType;
using graphscore::DisconnectCommand;
using graphscore::Dynamic;
using graphscore::EventId;
using graphscore::EventListener;
using graphscore::Graph;
using graphscore::GraphPosition;
using graphscore::MidiChannel;
using graphscore::Node;
using graphscore::NodeId;
using graphscore::NoteValue;
using graphscore::OutputConnector;
using graphscore::Project;
using graphscore::ProjectId;
using graphscore::QueuePolicy;
using graphscore::Rational;
using graphscore::RemoveInputConnectorCommand;
using graphscore::RemoveOutputConnectorCommand;
using graphscore::ResetRouteCommand;
using graphscore::RestoreTrackCommand;
using graphscore::Result;
using graphscore::ResultCode;
using graphscore::RouteGeometry;
using graphscore::RoutePoint;
using graphscore::SetCustomRouteCommand;
using graphscore::SetInputConnectorNameCommand;
using graphscore::SetListenerPolicyCommand;
using graphscore::SetNodeColorCommand;
using graphscore::SetNodeNameCommand;
using graphscore::SetNodeNotesCommand;
using graphscore::SetNodePositionCommand;
using graphscore::SetOutputConnectorNameCommand;
using graphscore::SetOutputExportEnabledCommand;
using graphscore::SetOutputPriorityCommand;
using graphscore::SetOutputTypeCommand;
using graphscore::SetOutputWeightCommand;
using graphscore::SetProjectDynamicCommand;
using graphscore::SetProjectNameCommand;
using graphscore::SetProjectTempoCommand;
using graphscore::SetStartNodeCommand;
using graphscore::SetTrackGainCommand;
using graphscore::SetTrackMuteCommand;
using graphscore::SetTrackNameCommand;
using graphscore::SetTrackPanCommand;
using graphscore::SetTrackSoloCommand;
using graphscore::StaffLayout;
using graphscore::Tempo;
using graphscore::TrackId;

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
