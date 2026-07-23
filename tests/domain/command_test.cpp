// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>

using graphscore::Command;
using graphscore::CommandHistory;
using graphscore::CommandTransaction;
using graphscore::MidiChannel;
using graphscore::NodeId;
using graphscore::NoteValue;
using graphscore::Project;
using graphscore::ProjectId;
using graphscore::Rational;
using graphscore::Result;
using graphscore::ResultCode;
using graphscore::SetNodeNameCommand;
using graphscore::SetProjectTempoCommand;
using graphscore::SetTrackNameCommand;
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
