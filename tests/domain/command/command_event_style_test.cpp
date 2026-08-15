// SPDX-License-Identifier: Apache-2.0

#include "command_test_support.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <memory>
#include <utility>
#include <vector>

namespace {

constexpr Voice kVoice = *Voice::create(1);

VoiceContent& voice(NotationSetup& setup) {
  return setup.project.find_node(setup.node_id)
      ->lane(setup.track_id)
      ->stave(setup.stave_id)
      ->voice(kVoice);
}

const VoiceContent& voice(const NotationSetup& setup) {
  return setup.project.find_node(setup.node_id)
      ->lane(setup.track_id)
      ->stave(setup.stave_id)
      ->voice(kVoice);
}

NotationEntityId append_note(
    NotationSetup& setup, std::vector<graphscore::Articulation> values = {}) {
  Note note = make_note(pitch_c4(), quarter(), false, std::move(values));
  const NotationEntityId id = note.id;
  EXPECT_TRUE(voice(setup).append(std::move(note)).ok());
  EXPECT_TRUE(voice(setup).normalize(setup.node_end).ok());
  return id;
}

NotationEntityId append_chord(NotationSetup&                        setup,
                              std::vector<graphscore::Articulation> values) {
  const ChordNote c{NotationEntityId::generate(), pitch_c4(), false};
  const ChordNote g{NotationEntityId::generate(), pitch_g4(), false};
  Chord           chord     = make_chord(quarter(), {c, g});
  chord.articulations       = std::move(values);
  const NotationEntityId id = chord.id;
  EXPECT_TRUE(voice(setup).append(std::move(chord)).ok());
  EXPECT_TRUE(voice(setup).normalize(setup.node_end).ok());
  return id;
}

const Note& note(const NotationSetup& setup) {
  return std::get<Note>(setup.project.find_node(setup.node_id)
                            ->lane(setup.track_id)
                            ->stave(setup.stave_id)
                            ->voice(kVoice)
                            .events()
                            .front());
}

const std::vector<graphscore::Articulation>& articulations(
    const NotationSetup& setup) {
  return *graphscore::event_articulations(voice(setup).events().front());
}

graphscore::EventStyleCommand articulation_command(
    const NotationSetup& setup, NotationEntityId id,
    graphscore::ArticulationEdit edit, graphscore::Articulation value,
    std::optional<graphscore::Articulation> replaced = std::nullopt) {
  return {setup.node_id, setup.track_id, setup.stave_id, kVoice, id,
          edit,          value,          replaced};
}

class FailingInverseCommand final : public graphscore::Command {
 public:
  FailingInverseCommand(std::unique_ptr<graphscore::Command> inner,
                        int fail_undo_at, int fail_redo_at)
      : inner_(std::move(inner)),
        fail_undo_at_(fail_undo_at),
        fail_redo_at_(fail_redo_at) {}

  graphscore::Result execute(Project& project) noexcept override {
    return inner_->execute(project);
  }

  graphscore::Result undo(Project& project) noexcept override {
    ++undo_calls_;
    return undo_calls_ == fail_undo_at_
               ? graphscore::Result(graphscore::ResultCode::kInternalError)
               : inner_->undo(project);
  }

  graphscore::Result redo(Project& project) noexcept override {
    ++redo_calls_;
    return redo_calls_ == fail_redo_at_
               ? graphscore::Result(graphscore::ResultCode::kInternalError)
               : inner_->redo(project);
  }

  graphscore::Result compensate_undo(Project& project) noexcept override {
    return inner_->compensate_undo(project);
  }

  graphscore::Result compensate_redo(Project& project) noexcept override {
    return inner_->compensate_redo(project);
  }

 private:
  std::unique_ptr<graphscore::Command> inner_;
  int                                  fail_undo_at_;
  int                                  fail_redo_at_;
  int                                  undo_calls_ = 0;
  int                                  redo_calls_ = 0;
};

}  // namespace

TEST(EventStyleCommandTest, AppliesEveryArticulationAndUndoRedoes) {
  for (const graphscore::Articulation value : graphscore::kAllArticulations) {
    NotationSetup setup   = make_notation_setup();
    const auto    id      = append_note(setup);
    auto          command = articulation_command(
        setup, id, graphscore::ArticulationEdit::kApply, value);
    ASSERT_TRUE(command.execute(setup.project).ok());
    EXPECT_EQ(note(setup).articulations, std::vector{value});
    ASSERT_TRUE(command.undo(setup.project).ok());
    EXPECT_TRUE(note(setup).articulations.empty());
    ASSERT_TRUE(command.redo(setup.project).ok());
    EXPECT_EQ(note(setup).articulations, std::vector{value});
  }
}

TEST(EventStyleCommandTest, ChangesAndRemovesArticulationsReversibly) {
  NotationSetup setup = make_notation_setup();
  const auto    id    = append_note(setup, {graphscore::Articulation::kAccent});
  auto          change = articulation_command(
      setup, id, graphscore::ArticulationEdit::kChange,
      graphscore::Articulation::kMarcato, graphscore::Articulation::kAccent);
  ASSERT_TRUE(change.execute(setup.project).ok());
  EXPECT_EQ(note(setup).articulations,
            std::vector{graphscore::Articulation::kMarcato});
  ASSERT_TRUE(change.undo(setup.project).ok());

  auto remove = articulation_command(
      setup, id, graphscore::ArticulationEdit::kRemove,
      graphscore::Articulation::kAccent, graphscore::Articulation::kAccent);
  ASSERT_TRUE(remove.execute(setup.project).ok());
  EXPECT_TRUE(note(setup).articulations.empty());
  ASSERT_TRUE(remove.undo(setup.project).ok());
  EXPECT_EQ(note(setup).articulations,
            std::vector{graphscore::Articulation::kAccent});
}

TEST(EventStyleCommandTest,
     ChangePreservesMultiArticulationOrderForNoteAndChord) {
  struct Case {
    std::vector<graphscore::Articulation> values;
    std::size_t                           index;
    graphscore::Articulation              replacement;
  };

  const std::array cases = {Case{{graphscore::Articulation::kAccent,
                                  graphscore::Articulation::kStaccato},
                                 0u,
                                 graphscore::Articulation::kMarcato},
                            Case{{graphscore::Articulation::kAccent,
                                  graphscore::Articulation::kStaccato,
                                  graphscore::Articulation::kMarcato},
                                 1u,
                                 graphscore::Articulation::kTenuto},
                            Case{{graphscore::Articulation::kStaccato,
                                  graphscore::Articulation::kAccent},
                                 1u,
                                 graphscore::Articulation::kMarcato}};
  for (const bool chord : {false, true}) {
    for (const Case& test_case : cases) {
      NotationSetup setup   = make_notation_setup();
      const auto    id      = chord ? append_chord(setup, test_case.values)
                                    : append_note(setup, test_case.values);
      auto          command = articulation_command(
          setup, id, graphscore::ArticulationEdit::kChange,
          test_case.replacement, test_case.values[test_case.index]);
      ASSERT_TRUE(command.execute(setup.project).ok());
      auto expected             = test_case.values;
      expected[test_case.index] = test_case.replacement;
      EXPECT_EQ(articulations(setup), expected);
      ASSERT_TRUE(command.undo(setup.project).ok());
      EXPECT_EQ(articulations(setup), test_case.values);
    }
  }
}

TEST(EventStyleCommandTest, ConflictingChangePreservesMultiArticulationOrder) {
  NotationSetup     setup   = make_notation_setup();
  const std::vector initial = {graphscore::Articulation::kAccent,
                               graphscore::Articulation::kStaccato};
  const auto        id      = append_note(setup, initial);
  const auto        before  = articulations(setup);
  auto              command = articulation_command(
      setup, id, graphscore::ArticulationEdit::kChange,
      graphscore::Articulation::kTenuto, graphscore::Articulation::kAccent);
  EXPECT_FALSE(command.execute(setup.project).ok());
  EXPECT_EQ(articulations(setup), before);
}

TEST(EventStyleCommandTest, DuplicateConflictAndStaleTargetsFailAtomically) {
  NotationSetup setup = make_notation_setup();
  const auto    id = append_note(setup, {graphscore::Articulation::kStaccato});
  const VoiceContent before = voice(setup);
  auto               duplicate =
      articulation_command(setup, id, graphscore::ArticulationEdit::kApply,
                           graphscore::Articulation::kStaccato);
  EXPECT_FALSE(duplicate.execute(setup.project).ok());
  EXPECT_EQ(voice(setup).events(), before.events());
  auto conflict =
      articulation_command(setup, id, graphscore::ArticulationEdit::kApply,
                           graphscore::Articulation::kTenuto);
  EXPECT_FALSE(conflict.execute(setup.project).ok());
  EXPECT_EQ(voice(setup).events(), before.events());
  auto stale = articulation_command(setup, NotationEntityId::generate(),
                                    graphscore::ArticulationEdit::kApply,
                                    graphscore::Articulation::kAccent);
  EXPECT_FALSE(stale.execute(setup.project).ok());
  EXPECT_EQ(voice(setup).events(), before.events());
  graphscore::EventStyleCommand invalid_stem(
      setup.node_id, setup.track_id, setup.stave_id, kVoice, id,
      static_cast<graphscore::StemDirection>(255));
  EXPECT_FALSE(invalid_stem.execute(setup.project).ok());
  EXPECT_EQ(voice(setup).events(), before.events());
}

TEST(EventStyleCommandTest, SetsAndClearsStemOverridesWithUndoRedo) {
  NotationSetup                 setup = make_notation_setup();
  const auto                    id    = append_note(setup);
  graphscore::EventStyleCommand up(setup.node_id, setup.track_id,
                                   setup.stave_id, kVoice, id,
                                   graphscore::StemDirection::kUp);
  ASSERT_TRUE(up.execute(setup.project).ok());
  EXPECT_EQ(note(setup).stem, graphscore::StemDirection::kUp);
  ASSERT_TRUE(up.undo(setup.project).ok());
  EXPECT_EQ(note(setup).stem, graphscore::StemDirection::kAuto);
  ASSERT_TRUE(up.redo(setup.project).ok());
  graphscore::EventStyleCommand automatic(setup.node_id, setup.track_id,
                                          setup.stave_id, kVoice, id,
                                          graphscore::StemDirection::kAuto);
  ASSERT_TRUE(automatic.execute(setup.project).ok());
  EXPECT_EQ(note(setup).stem, graphscore::StemDirection::kAuto);
  graphscore::EventStyleCommand down(setup.node_id, setup.track_id,
                                     setup.stave_id, kVoice, id,
                                     graphscore::StemDirection::kDown);
  ASSERT_TRUE(down.execute(setup.project).ok());
  EXPECT_EQ(note(setup).stem, graphscore::StemDirection::kDown);
}

TEST(EventStyleCommandTest, HistoryPublicationRollbackBypassesFailingInverse) {
  {
    NotationSetup              setup = make_notation_setup();
    const auto                 id    = append_note(setup);
    graphscore::CommandHistory history;
    auto command = std::make_unique<FailingInverseCommand>(
        std::make_unique<graphscore::EventStyleCommand>(
            setup.node_id, setup.track_id, setup.stave_id, kVoice, id,
            graphscore::StemDirection::kUp),
        0, 1);
    ASSERT_TRUE(history.execute_new(std::move(command), setup.project).ok());
    ASSERT_TRUE(history.undo(setup.project).ok());
    ASSERT_TRUE(history.rollback_last_undo(setup.project).ok());
    EXPECT_EQ(note(setup).stem, graphscore::StemDirection::kUp);
    EXPECT_EQ(history.undo_stack_size(), 1u);
    EXPECT_EQ(history.redo_stack_size(), 0u);
  }
  {
    NotationSetup              setup = make_notation_setup();
    const auto                 id    = append_note(setup);
    graphscore::CommandHistory history;
    auto command = std::make_unique<FailingInverseCommand>(
        std::make_unique<graphscore::EventStyleCommand>(
            setup.node_id, setup.track_id, setup.stave_id, kVoice, id,
            graphscore::StemDirection::kUp),
        2, 0);
    ASSERT_TRUE(history.execute_new(std::move(command), setup.project).ok());
    ASSERT_TRUE(history.undo(setup.project).ok());
    ASSERT_TRUE(history.redo(setup.project).ok());
    ASSERT_TRUE(history.rollback_last_redo(setup.project).ok());
    EXPECT_EQ(note(setup).stem, graphscore::StemDirection::kAuto);
    EXPECT_EQ(history.undo_stack_size(), 0u);
    EXPECT_EQ(history.redo_stack_size(), 1u);
  }
}
