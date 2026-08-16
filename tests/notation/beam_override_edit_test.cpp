// SPDX-License-Identifier: Apache-2.0

#include "selection/selection_test_support.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>
#include <graphscore/notation/graphscore_notation.hpp>

namespace {

constexpr Voice kVoice1 = *Voice::create(1);
constexpr Voice kVoice2 = *Voice::create(2);

using graphscore::BeamOverride;
using graphscore::GraceNote;
using graphscore::LineCommand;
using graphscore::MarkingEdit;

Duration duration(NoteValue value) {
  return *Duration::create(value, 0);
}

Rational at(std::int64_t numerator, std::int64_t denominator) {
  return *Rational::create(numerator, denominator);
}

VoiceContent& voice(Fixture& fixture, Voice selected = kVoice1) {
  return fixture.voice(selected.index());
}

std::vector<NotationEntityId> append_notes(Fixture& fixture, NoteValue value,
                                           std::size_t count,
                                           Voice       selected = kVoice1) {
  std::vector<NotationEntityId> ids;
  for (std::size_t index = 0; index < count; ++index) {
    Note note =
        make_note(*SpelledPitch::create(Letter::kC, 4), duration(value));
    ids.push_back(note.id);
    EXPECT_TRUE(voice(fixture, selected).append(std::move(note)).ok());
  }
  const auto* node = fixture.project.find_node(fixture.node_id);
  EXPECT_NE(node, nullptr);
  if (node != nullptr && node->timeline() != nullptr) {
    EXPECT_TRUE(
        voice(fixture, selected).normalize(node->timeline()->node_end()).ok());
  }
  return ids;
}

Selection range(Fixture& fixture, Rational start, Rational end,
                Voice selected = kVoice1) {
  return Selection{*ArbitraryRangeSet::create({ArbitraryRangeItem{
      fixture.node_id, fixture.track_ids.front(), fixture.stave_id(), selected,
      MusicalSpan{start, end}}})};
}

graphscore::NotationEditCommandResult build(
    Fixture& fixture, const Selection& selection, MarkingEdit edit,
    BeamOverride::Kind kind = BeamOverride::Kind::kBreak) {
  return graphscore::make_beam_override_edit_command(fixture.project, selection,
                                                     edit, kind);
}

}  // namespace

TEST(BeamOverrideEditTest, AppliesBreakAndJoinAcrossEverySelectedPair) {
  for (const BeamOverride::Kind kind :
       {BeamOverride::Kind::kBreak, BeamOverride::Kind::kJoin}) {
    Fixture    fixture(1);
    const auto ids      = append_notes(fixture, NoteValue::kEighth, 3);
    const auto selected = range(fixture, Rational(0), at(3, 8));
    auto       applied  = build(fixture, selected, MarkingEdit::kApply, kind);
    ASSERT_TRUE(applied.available()) << applied.unavailable_reason;
    ASSERT_TRUE(applied.command->execute(fixture.project).ok());
    ASSERT_EQ(voice(fixture).beam_overrides().size(), 1u);
    const BeamOverride& override_ = voice(fixture).beam_overrides().front();
    EXPECT_EQ(override_.kind, kind);
    EXPECT_EQ(override_.events, ids);

    const auto stable_id = override_.id;
    ASSERT_TRUE(applied.command->undo(fixture.project).ok());
    EXPECT_TRUE(voice(fixture).beam_overrides().empty());
    ASSERT_TRUE(applied.command->redo(fixture.project).ok());
    EXPECT_EQ(voice(fixture).beam_overrides().front().id, stable_id);
  }
}

TEST(BeamOverrideEditTest,
     OppositeKindReplacementPreservesOrderAndRenderedPrecedence) {
  Fixture    fixture(1);
  const auto ids = append_notes(fixture, NoteValue::kEighth, 3);
  const auto original =
      make_beam_override(BeamOverride::Kind::kJoin, {ids[0], ids[1]});
  const auto overlapping = make_beam_override(BeamOverride::Kind::kJoin, ids);
  ASSERT_TRUE(voice(fixture).add_beam_override(original).ok());
  ASSERT_TRUE(voice(fixture).add_beam_override(overlapping).ok());
  const Selection selected = range(fixture, Rational(0), at(1, 4));

  const auto beam_exists = [&] {
    const FixedMetrics metrics;
    const auto         layout =
        layout_notation(fixture.project, fixture.node_id, metrics);
    EXPECT_TRUE(layout);
    if (!layout)
      return false;
    const std::string id =
        ids[0].to_string() + "/beam/to/" + ids[1].to_string();
    return std::ranges::any_of(layout.layout->commands, [&](const auto& item) {
      const auto* line = std::get_if<LineCommand>(&item);
      return line != nullptr && line->id.value.starts_with(id);
    });
  };

  auto replaced =
      build(fixture, selected, MarkingEdit::kApply, BeamOverride::Kind::kBreak);
  ASSERT_TRUE(replaced.available()) << replaced.unavailable_reason;
  ASSERT_TRUE(replaced.command->execute(fixture.project).ok());
  ASSERT_EQ(voice(fixture).beam_overrides().size(), 2u);
  EXPECT_EQ(voice(fixture).beam_overrides().front().id, original.id);
  EXPECT_EQ(voice(fixture).beam_overrides().front().kind,
            BeamOverride::Kind::kBreak);
  EXPECT_EQ(voice(fixture).beam_overrides().back(), overlapping);
  EXPECT_TRUE(beam_exists());
  ASSERT_TRUE(replaced.command->undo(fixture.project).ok());
  EXPECT_EQ(voice(fixture).beam_overrides(),
            (std::vector<BeamOverride>{original, overlapping}));
  EXPECT_TRUE(beam_exists());
  ASSERT_TRUE(replaced.command->redo(fixture.project).ok());
  EXPECT_EQ(voice(fixture).beam_overrides().front().id, original.id);
  EXPECT_EQ(voice(fixture).beam_overrides().front().kind,
            BeamOverride::Kind::kBreak);
  EXPECT_EQ(voice(fixture).beam_overrides().back(), overlapping);
  EXPECT_TRUE(beam_exists());
}

TEST(BeamOverrideEditTest, RemoveMatchesOnlyTheExactSelectedRange) {
  Fixture    fixture(1);
  const auto ids = append_notes(fixture, NoteValue::kSixteenth, 4);
  const auto override_ =
      make_beam_override(BeamOverride::Kind::kJoin, {ids[0], ids[1], ids[2]});
  ASSERT_TRUE(voice(fixture).add_beam_override(override_).ok());

  auto not_exact = build(fixture, range(fixture, Rational(0), at(1, 8)),
                         MarkingEdit::kRemove);
  EXPECT_FALSE(not_exact.available());
  EXPECT_EQ(not_exact.unavailable_reason,
            "no beam override exists on this exact range");
  EXPECT_EQ(voice(fixture).beam_overrides().size(), 1u);

  auto removed = build(fixture, range(fixture, Rational(0), at(3, 16)),
                       MarkingEdit::kRemove);
  ASSERT_TRUE(removed.available()) << removed.unavailable_reason;
  ASSERT_TRUE(removed.command->execute(fixture.project).ok());
  EXPECT_TRUE(voice(fixture).beam_overrides().empty());
  ASSERT_TRUE(removed.command->undo(fixture.project).ok());
  EXPECT_EQ(voice(fixture).beam_overrides().front(), override_);
  ASSERT_TRUE(removed.command->redo(fixture.project).ok());
  EXPECT_TRUE(voice(fixture).beam_overrides().empty());
}

TEST(BeamOverrideEditTest, RejectsDuplicateAndAmbiguousExactRangeState) {
  Fixture    fixture(1);
  const auto ids = append_notes(fixture, NoteValue::kEighth, 2);
  ASSERT_TRUE(voice(fixture)
                  .add_beam_override(
                      make_beam_override(BeamOverride::Kind::kBreak, ids))
                  .ok());
  const Selection    selected = range(fixture, Rational(0), at(1, 4));
  const VoiceContent before   = voice(fixture);
  auto               duplicate =
      build(fixture, selected, MarkingEdit::kApply, BeamOverride::Kind::kBreak);
  EXPECT_FALSE(duplicate.available());
  EXPECT_EQ(duplicate.unavailable_reason,
            "beam break is already applied to this exact range");
  EXPECT_EQ(voice(fixture), before);

  ASSERT_TRUE(
      voice(fixture)
          .add_beam_override(make_beam_override(BeamOverride::Kind::kJoin, ids))
          .ok());
  for (const MarkingEdit edit : {MarkingEdit::kApply, MarkingEdit::kRemove}) {
    auto ambiguous = build(fixture, selected, edit, BeamOverride::Kind::kJoin);
    EXPECT_FALSE(ambiguous.available());
    EXPECT_EQ(ambiguous.unavailable_reason,
              "conflicting beam overrides exist on this exact range");
  }
}

TEST(BeamOverrideEditTest, RejectsInvalidTargetFamiliesWithoutMutation) {
  Fixture    fixture(1);
  const auto ids = append_notes(fixture, NoteValue::kEighth, 4);
  ASSERT_TRUE(voice(fixture, kVoice2)
                  .append(make_note(*SpelledPitch::create(Letter::kD, 4),
                                    duration(NoteValue::kEighth)))
                  .ok());
  const VoiceContent before        = voice(fixture);
  const auto         expect_reason = [&](const Selection&   selected,
                                 const std::string& reason) {
    auto result = build(fixture, selected, MarkingEdit::kApply);
    EXPECT_FALSE(result.available());
    EXPECT_EQ(result.unavailable_reason, reason);
    EXPECT_EQ(voice(fixture), before);
  };

  expect_reason(Selection{*NoteheadSet::create(
                    {NoteheadItem{fixture.node_id, fixture.track_ids.front(),
                                  fixture.stave_id(), kVoice1, ids.front()}})},
                "requires an exact range of complete events on one live staff "
                "and voice");
  expect_reason(range(fixture, Rational(0), at(3, 16)),
                "requires an exact range of complete events on one live staff "
                "and voice");
  expect_reason(range(fixture, Rational(0), Rational(0)),
                "requires an exact range of complete events on one live staff "
                "and voice");
  expect_reason(range(fixture, Rational(0), at(1, 8)),
                "requires a range of at least two events");

  const Selection mixed = Selection{*ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fixture.node_id, fixture.track_ids.front(),
                          fixture.stave_id(), kVoice1,
                          MusicalSpan{Rational(0), at(1, 8)}},
       ArbitraryRangeItem{fixture.node_id, fixture.track_ids.front(),
                          fixture.stave_id(), kVoice2,
                          MusicalSpan{Rational(0), at(1, 8)}}})};
  expect_reason(mixed,
                "requires an exact range of complete events on one live staff "
                "and voice");

  const Selection disjoint = Selection{*ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fixture.node_id, fixture.track_ids.front(),
                          fixture.stave_id(), kVoice1,
                          MusicalSpan{Rational(0), at(1, 8)}},
       ArbitraryRangeItem{fixture.node_id, fixture.track_ids.front(),
                          fixture.stave_id(), kVoice1,
                          MusicalSpan{at(1, 4), at(3, 8)}}})};
  expect_reason(disjoint, "selected events must be contiguous");

  Selection stale     = range(fixture, Rational(0), at(1, 4));
  auto*     stale_set = std::get_if<ArbitraryRangeSet>(&stale);
  ASSERT_NE(stale_set, nullptr);
  const Selection stale_selection{*ArbitraryRangeSet::create(
      {ArbitraryRangeItem{NodeId::generate(), stale_set->items().front().track,
                          stale_set->items().front().stave, kVoice1,
                          stale_set->items().front().span}})};
  expect_reason(stale_selection,
                "requires an exact range of complete events on one live staff "
                "and voice");
}

TEST(BeamOverrideEditTest, RejectsMixedStaffRange) {
  Fixture fixture({StaffLayout::grand_staff()}, 1);
  append_notes(fixture, NoteValue::kEighth, 2);
  VoiceContent& second = fixture.voice(kVoice1.index(), 0, 1);
  ASSERT_TRUE(second
                  .append(make_note(*SpelledPitch::create(Letter::kC, 4),
                                    duration(NoteValue::kEighth)))
                  .ok());
  ASSERT_TRUE(second
                  .append(make_note(*SpelledPitch::create(Letter::kD, 4),
                                    duration(NoteValue::kEighth)))
                  .ok());
  ASSERT_TRUE(second
                  .normalize(fixture.project.find_node(fixture.node_id)
                                 ->timeline()
                                 ->node_end())
                  .ok());
  const Selection selected = Selection{*ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fixture.node_id, fixture.track_ids.front(),
                          fixture.stave_id(0, 0), kVoice1,
                          MusicalSpan{Rational(0), at(1, 4)}},
       ArbitraryRangeItem{fixture.node_id, fixture.track_ids.front(),
                          fixture.stave_id(0, 1), kVoice1,
                          MusicalSpan{Rational(0), at(1, 4)}}})};
  auto            result   = build(fixture, selected, MarkingEdit::kApply);
  EXPECT_FALSE(result.available());
  EXPECT_EQ(result.unavailable_reason,
            "requires an exact range of complete events on one live staff and "
            "voice");
  EXPECT_TRUE(voice(fixture).beam_overrides().empty());
  EXPECT_TRUE(second.beam_overrides().empty());
}

TEST(BeamOverrideEditTest, RejectsRestsNonBeamableEventsAndGraceSelections) {
  Fixture rest_fixture(1);
  ASSERT_TRUE(
      voice(rest_fixture).append(make_rest(duration(NoteValue::kEighth))).ok());
  append_notes(rest_fixture, NoteValue::kEighth, 1);
  auto rests = build(rest_fixture, range(rest_fixture, Rational(0), at(1, 4)),
                     MarkingEdit::kApply);
  EXPECT_FALSE(rests.available());
  EXPECT_EQ(rests.unavailable_reason, "every selected event must be beamable");

  Fixture quarter_fixture(1);
  append_notes(quarter_fixture, NoteValue::kQuarter, 2);
  auto quarters =
      build(quarter_fixture, range(quarter_fixture, Rational(0), at(1, 2)),
            MarkingEdit::kApply);
  EXPECT_FALSE(quarters.available());
  EXPECT_EQ(quarters.unavailable_reason,
            "every selected event must be beamable");

  Fixture    grace_fixture(1);
  const auto principal =
      append_notes(grace_fixture, NoteValue::kEighth, 2).front();
  GraceNote  grace{NotationEntityId::generate(),
                  *SpelledPitch::create(Letter::kD, 4),
                  duration(NoteValue::kSixteenth)};
  const auto grace_id = grace.id;
  ASSERT_TRUE(voice(grace_fixture)
                  .add_grace_group(make_grace_group(principal, {grace}))
                  .ok());
  const Selection grace_selected{*NoteheadSet::create(
      {NoteheadItem{grace_fixture.node_id, grace_fixture.track_ids.front(),
                    grace_fixture.stave_id(), kVoice1, grace_id}})};
  auto grace_result = build(grace_fixture, grace_selected, MarkingEdit::kApply);
  EXPECT_FALSE(grace_result.available());
  EXPECT_EQ(grace_result.unavailable_reason,
            "requires an exact range of complete events on one live staff and "
            "voice");
}

TEST(BeamOverrideEditTest, RejectsUnknownOperationsBeforeMutation) {
  Fixture fixture(1);
  append_notes(fixture, NoteValue::kEighth, 2);
  const Selection selected = range(fixture, Rational(0), at(1, 4));
  auto            change   = build(fixture, selected, MarkingEdit::kChange);
  EXPECT_FALSE(change.available());
  EXPECT_EQ(change.unavailable_reason, "beam overrides cannot be changed");
  auto unknown = build(fixture, selected, MarkingEdit::kApply,
                       static_cast<BeamOverride::Kind>(200));
  EXPECT_FALSE(unknown.available());
  EXPECT_EQ(unknown.unavailable_reason, "unknown beam override kind");
  EXPECT_TRUE(voice(fixture).beam_overrides().empty());
}
