// SPDX-License-Identifier: Apache-2.0

#include "command_test_support.hpp"

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <optional>
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

TrackLane* lane(NotationSetup& setup) {
  return setup.project.find_node(setup.node_id)->lane(setup.track_id);
}

// Two quarter notes followed by the normalized rest fill: the two endpoint
// events a hairpin needs, and the anchor a dynamic needs.
std::pair<NotationEntityId, NotationEntityId> append_two_notes(
    NotationSetup& setup) {
  Note                   first     = make_note(pitch_c4(), quarter());
  Note                   second    = make_note(pitch_e4(), quarter());
  const NotationEntityId first_id  = first.id;
  const NotationEntityId second_id = second.id;
  EXPECT_TRUE(voice(setup).append(std::move(first)).ok());
  EXPECT_TRUE(voice(setup).append(std::move(second)).ok());
  EXPECT_TRUE(voice(setup).normalize(setup.node_end).ok());
  return {first_id, second_id};
}

NotationEntityId add_dynamic(NotationSetup& setup, NotationEntityId at_event,
                             graphscore::Dynamic value) {
  const DynamicMarking marking = make_dynamic_marking(at_event, value);
  EXPECT_TRUE(voice(setup).add_dynamic(marking).ok());
  return marking.id;
}

NotationEntityId add_hairpin(NotationSetup& setup, NotationEntityId start_event,
                             NotationEntityId end_event,
                             HairpinDirection direction) {
  const Hairpin hairpin = make_hairpin(start_event, end_event, direction);
  EXPECT_TRUE(voice(setup).add_hairpin(hairpin).ok());
  return hairpin.id;
}

const DynamicMarking* find_dynamic(const NotationSetup& setup,
                                   NotationEntityId     id) {
  for (const DynamicMarking& marking : voice(setup).dynamics()) {
    if (marking.id == id)
      return &marking;
  }
  return nullptr;
}

const Hairpin* find_hairpin(const NotationSetup& setup, NotationEntityId id) {
  for (const Hairpin& hairpin : voice(setup).hairpins()) {
    if (hairpin.id == id)
      return &hairpin;
  }
  return nullptr;
}

std::size_t pedal_count(const NotationSetup& setup) {
  const TrackLane* value =
      setup.project.find_node(setup.node_id)->lane(setup.track_id);
  const std::vector<PedalSpan>* spans = value->pedal_spans(setup.stave_id);
  return spans == nullptr ? 0u : spans->size();
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

using CommandBuilder =
    std::function<std::unique_ptr<graphscore::Command>(NotationSetup&)>;
using MarkingCounter = std::function<std::size_t(const NotationSetup&)>;

// A successful undo/redo whose separate publication step fails must be
// reversible without re-running the inverse operation that failed, which is
// exactly what CommandHistory::rollback_last_undo/rollback_last_redo drive
// through Command::compensate_undo/compensate_redo. The wrapper makes the
// normal inverse fail, so a compensation that merely called it would fail too.
void check_publication_compensation(const CommandBuilder& build,
                                    const MarkingCounter& count) {
  {
    NotationSetup     setup  = make_notation_setup();
    auto              inner  = build(setup);
    const std::size_t before = count(setup);
    CommandHistory    history;
    ASSERT_TRUE(history
                    .execute_new(std::make_unique<FailingInverseCommand>(
                                     std::move(inner), 0, 1),
                                 setup.project)
                    .ok());
    const std::size_t after = count(setup);
    EXPECT_NE(before, after);
    ASSERT_TRUE(history.undo(setup.project).ok());
    EXPECT_EQ(count(setup), before);
    ASSERT_TRUE(history.rollback_last_undo(setup.project).ok());
    EXPECT_EQ(count(setup), after);
    EXPECT_EQ(history.undo_stack_size(), 1u);
    EXPECT_EQ(history.redo_stack_size(), 0u);
  }
  {
    NotationSetup     setup  = make_notation_setup();
    auto              inner  = build(setup);
    const std::size_t before = count(setup);
    CommandHistory    history;
    ASSERT_TRUE(history
                    .execute_new(std::make_unique<FailingInverseCommand>(
                                     std::move(inner), 2, 0),
                                 setup.project)
                    .ok());
    ASSERT_TRUE(history.undo(setup.project).ok());
    ASSERT_TRUE(history.redo(setup.project).ok());
    ASSERT_TRUE(history.rollback_last_redo(setup.project).ok());
    EXPECT_EQ(count(setup), before);
    EXPECT_EQ(history.undo_stack_size(), 0u);
    EXPECT_EQ(history.redo_stack_size(), 1u);
  }
}

}  // namespace

TEST(MarkingStyleCommandTest, ChangesEveryDynamicValueWithUndoRedo) {
  for (const graphscore::Dynamic value : graphscore::kAllDynamics) {
    const graphscore::Dynamic initial = value == graphscore::Dynamic::kPpp
                                            ? graphscore::Dynamic::kFff
                                            : graphscore::Dynamic::kPpp;
    NotationSetup             setup   = make_notation_setup();
    const auto                events  = append_two_notes(setup);
    const auto marking = add_dynamic(setup, events.first, initial);
    graphscore::MarkingStyleCommand command(
        setup.node_id, setup.track_id, setup.stave_id, kVoice, marking, value);
    ASSERT_TRUE(command.execute(setup.project).ok());
    ASSERT_NE(find_dynamic(setup, marking), nullptr);
    EXPECT_EQ(find_dynamic(setup, marking)->value, value);
    EXPECT_EQ(find_dynamic(setup, marking)->at_event, events.first);
    ASSERT_TRUE(command.undo(setup.project).ok());
    EXPECT_EQ(find_dynamic(setup, marking)->value, initial);
    ASSERT_TRUE(command.redo(setup.project).ok());
    EXPECT_EQ(find_dynamic(setup, marking)->value, value);
    EXPECT_EQ(voice(setup).dynamics().size(), 1u);
  }
}

TEST(MarkingStyleCommandTest, ChangesHairpinDirectionBothWaysWithUndoRedo) {
  for (const HairpinDirection direction :
       {HairpinDirection::kCrescendo, HairpinDirection::kDiminuendo}) {
    const HairpinDirection initial = direction == HairpinDirection::kCrescendo
                                         ? HairpinDirection::kDiminuendo
                                         : HairpinDirection::kCrescendo;
    NotationSetup          setup   = make_notation_setup();
    const auto             events  = append_two_notes(setup);
    const auto             marking =
        add_hairpin(setup, events.first, events.second, initial);
    graphscore::MarkingStyleCommand command(setup.node_id, setup.track_id,
                                            setup.stave_id, kVoice, marking,
                                            direction);
    ASSERT_TRUE(command.execute(setup.project).ok());
    ASSERT_NE(find_hairpin(setup, marking), nullptr);
    EXPECT_EQ(find_hairpin(setup, marking)->direction, direction);
    EXPECT_EQ(find_hairpin(setup, marking)->start_event, events.first);
    EXPECT_EQ(find_hairpin(setup, marking)->end_event, events.second);
    ASSERT_TRUE(command.undo(setup.project).ok());
    EXPECT_EQ(find_hairpin(setup, marking)->direction, initial);
    ASSERT_TRUE(command.redo(setup.project).ok());
    EXPECT_EQ(find_hairpin(setup, marking)->direction, direction);
    EXPECT_EQ(voice(setup).hairpins().size(), 1u);
  }
}

TEST(MarkingStyleCommandTest, StaleNoOpAndInvalidTargetsFailAtomically) {
  NotationSetup setup  = make_notation_setup();
  const auto    events = append_two_notes(setup);
  const auto    dynamic =
      add_dynamic(setup, events.first, graphscore::Dynamic::kP);
  const auto         hairpin = add_hairpin(setup, events.first, events.second,
                                           HairpinDirection::kCrescendo);
  const VoiceContent before  = voice(setup);

  graphscore::MarkingStyleCommand stale(
      setup.node_id, setup.track_id, setup.stave_id, kVoice,
      NotationEntityId::generate(), graphscore::Dynamic::kF);
  EXPECT_FALSE(stale.execute(setup.project).ok());
  EXPECT_EQ(voice(setup), before);

  graphscore::MarkingStyleCommand no_op(setup.node_id, setup.track_id,
                                        setup.stave_id, kVoice, dynamic,
                                        graphscore::Dynamic::kP);
  EXPECT_FALSE(no_op.execute(setup.project).ok());
  EXPECT_EQ(voice(setup), before);

  graphscore::MarkingStyleCommand unknown_value(
      setup.node_id, setup.track_id, setup.stave_id, kVoice, dynamic,
      static_cast<graphscore::Dynamic>(200));
  EXPECT_FALSE(unknown_value.execute(setup.project).ok());
  EXPECT_EQ(voice(setup), before);

  graphscore::MarkingStyleCommand wrong_family(setup.node_id, setup.track_id,
                                               setup.stave_id, kVoice, hairpin,
                                               graphscore::Dynamic::kF);
  EXPECT_FALSE(wrong_family.execute(setup.project).ok());
  EXPECT_EQ(voice(setup), before);

  graphscore::MarkingStyleCommand unknown_direction(
      setup.node_id, setup.track_id, setup.stave_id, kVoice, hairpin,
      static_cast<HairpinDirection>(200));
  EXPECT_FALSE(unknown_direction.execute(setup.project).ok());
  EXPECT_EQ(voice(setup), before);

  graphscore::MarkingStyleCommand same_direction(
      setup.node_id, setup.track_id, setup.stave_id, kVoice, hairpin,
      HairpinDirection::kCrescendo);
  EXPECT_FALSE(same_direction.execute(setup.project).ok());
  EXPECT_EQ(voice(setup), before);

  graphscore::MarkingStyleCommand stale_stave(setup.node_id, setup.track_id,
                                              StaveId::generate(), kVoice,
                                              dynamic, graphscore::Dynamic::kF);
  EXPECT_FALSE(stale_stave.execute(setup.project).ok());
  EXPECT_EQ(voice(setup), before);
}

TEST(MarkingStyleCommandTest, UndoRedoRejectAnIndependentlyChangedVoice) {
  NotationSetup setup  = make_notation_setup();
  const auto    events = append_two_notes(setup);
  const auto    marking =
      add_dynamic(setup, events.first, graphscore::Dynamic::kP);
  graphscore::MarkingStyleCommand command(setup.node_id, setup.track_id,
                                          setup.stave_id, kVoice, marking,
                                          graphscore::Dynamic::kF);
  ASSERT_TRUE(command.execute(setup.project).ok());
  EXPECT_TRUE(voice(setup)
                  .add_dynamic(make_dynamic_marking(events.second,
                                                    graphscore::Dynamic::kMf))
                  .ok());
  EXPECT_FALSE(command.undo(setup.project).ok());
  EXPECT_EQ(find_dynamic(setup, marking)->value, graphscore::Dynamic::kF);
}

TEST(MarkingStyleCommandTest, PublicationRollbackBypassesFailingInverse) {
  check_publication_compensation(
      [](NotationSetup& setup) -> std::unique_ptr<graphscore::Command> {
        const auto events = append_two_notes(setup);
        return std::make_unique<graphscore::MarkingStyleCommand>(
            setup.node_id, setup.track_id, setup.stave_id, kVoice,
            add_dynamic(setup, events.first, graphscore::Dynamic::kP),
            graphscore::Dynamic::kF);
      },
      [](const NotationSetup& setup) {
        return voice(setup).dynamics().front().value == graphscore::Dynamic::kF
                   ? 1u
                   : 0u;
      });
}

TEST(MarkingStyleCommandTest, MarkingLifecycleCommandsCompensatePublication) {
  const MarkingCounter dynamics = [](const NotationSetup& setup) {
    return voice(setup).dynamics().size();
  };
  const MarkingCounter hairpins = [](const NotationSetup& setup) {
    return voice(setup).hairpins().size();
  };
  const MarkingCounter pedals = [](const NotationSetup& setup) {
    return pedal_count(setup);
  };

  check_publication_compensation(
      [](NotationSetup& setup) -> std::unique_ptr<graphscore::Command> {
        const auto events = append_two_notes(setup);
        return std::make_unique<AddDynamicCommand>(
            setup.node_id, setup.track_id, setup.stave_id, kVoice,
            make_dynamic_marking(events.first, graphscore::Dynamic::kMf));
      },
      dynamics);

  check_publication_compensation(
      [](NotationSetup& setup) -> std::unique_ptr<graphscore::Command> {
        const auto events = append_two_notes(setup);
        return std::make_unique<RemoveDynamicCommand>(
            setup.node_id, setup.track_id, setup.stave_id, kVoice,
            add_dynamic(setup, events.first, graphscore::Dynamic::kMf));
      },
      dynamics);

  check_publication_compensation(
      [](NotationSetup& setup) -> std::unique_ptr<graphscore::Command> {
        const auto events = append_two_notes(setup);
        return std::make_unique<AddHairpinCommand>(
            setup.node_id, setup.track_id, setup.stave_id, kVoice,
            make_hairpin(events.first, events.second,
                         HairpinDirection::kCrescendo));
      },
      hairpins);

  check_publication_compensation(
      [](NotationSetup& setup) -> std::unique_ptr<graphscore::Command> {
        const auto events = append_two_notes(setup);
        return std::make_unique<RemoveHairpinCommand>(
            setup.node_id, setup.track_id, setup.stave_id, kVoice,
            add_hairpin(setup, events.first, events.second,
                        HairpinDirection::kCrescendo));
      },
      hairpins);

  check_publication_compensation(
      [](NotationSetup& setup) -> std::unique_ptr<graphscore::Command> {
        fill_all_voices(lane(setup), setup.stave_id, setup.node_end);
        return std::make_unique<AddPedalSpanCommand>(
            setup.node_id, setup.track_id, setup.stave_id,
            make_pedal_span(Rational(0), setup.node_end));
      },
      pedals);

  check_publication_compensation(
      [](NotationSetup& setup) -> std::unique_ptr<graphscore::Command> {
        fill_all_voices(lane(setup), setup.stave_id, setup.node_end);
        const PedalSpan span = make_pedal_span(Rational(0), setup.node_end);
        EXPECT_TRUE(lane(setup)->add_pedal_span(setup.stave_id, span).ok());
        return std::make_unique<RemovePedalSpanCommand>(
            setup.node_id, setup.track_id, setup.stave_id, span.id);
      },
      pedals);
}
