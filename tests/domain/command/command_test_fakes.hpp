// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "command_test_support.hpp"

#include <string>
#include <utility>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>

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
