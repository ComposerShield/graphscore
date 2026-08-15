// SPDX-License-Identifier: Apache-2.0

#include "selection/selection_test_support.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <variant>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>
#include <graphscore/notation/graphscore_notation.hpp>

namespace {

constexpr Voice kVoice = *Voice::create(1);

constexpr Rational q(std::int64_t numerator, std::int64_t denominator) {
  return *Rational::create(numerator, denominator);
}

Selection range(Fixture& fixture, Rational start, Rational end) {
  return Selection{*ArbitraryRangeSet::create({ArbitraryRangeItem{
      fixture.node_id, fixture.track_ids.front(), fixture.stave_id(), kVoice,
      MusicalSpan{start, end}}})};
}

VoiceContent& voice(Fixture& fixture) {
  return fixture.project.find_node(fixture.node_id)
      ->lane(fixture.track_ids.front())
      ->stave(fixture.stave_id())
      ->voice(kVoice);
}

std::vector<NotationEntityId> append_eighths(Fixture&    fixture,
                                             std::size_t count) {
  std::vector<NotationEntityId> ids;
  const Duration duration = *Duration::create(NoteValue::kEighth, 0);
  for (std::size_t index = 0; index < count; ++index) {
    Note note = make_note(*SpelledPitch::create(Letter::kC, 4), duration);
    ids.push_back(note.id);
    EXPECT_TRUE(voice(fixture).append(note).ok());
  }
  EXPECT_TRUE(voice(fixture)
                  .normalize(fixture.project.find_node(fixture.node_id)
                                 ->timeline()
                                 ->node_end())
                  .ok());
  return ids;
}

Selection marking(Fixture& fixture, NotationEntityId anchor) {
  return Selection{*MarkingSet::create({MarkingItem{
      fixture.node_id, fixture.track_ids.front(), fixture.stave_id(), kVoice,
      MarkingKind::kTuplet, anchor, std::nullopt}})};
}

}  // namespace

TEST(TupletEditTest, ExactRangeCreatesChangesRemovesAndUndoRedoes) {
  Fixture         fixture(1);
  const auto      ids      = append_eighths(fixture, 3);
  const Selection selected = range(fixture, Rational(0), q(3, 8));
  auto            create   = graphscore::make_tuplet_create_command(
      fixture.project, selected, *TupletRatio::create(3, 2));
  ASSERT_NE(create, nullptr);
  ASSERT_TRUE(create->execute(fixture.project).ok());
  const auto group = graphscore::event_tuplet_group(voice(fixture).events()[0]);
  ASSERT_TRUE(group.has_value());
  EXPECT_EQ(graphscore::event_tuplet_group(voice(fixture).events()[1]), group);
  ASSERT_TRUE(create->undo(fixture.project).ok());
  EXPECT_FALSE(
      graphscore::event_tuplet_group(voice(fixture).events()[0]).has_value());
  ASSERT_TRUE(create->redo(fixture.project).ok());

  const Selection selected_marking = marking(fixture, ids.front());
  auto            change           = graphscore::make_tuplet_change_command(
      fixture.project, selected_marking, *TupletRatio::create(3, 1));
  ASSERT_NE(change, nullptr);
  ASSERT_TRUE(change->execute(fixture.project).ok());
  EXPECT_EQ(graphscore::event_duration(voice(fixture).events()[0]).tuplet(),
            TupletRatio::create(3, 1));
  ASSERT_TRUE(change->undo(fixture.project).ok());

  auto remove =
      graphscore::make_tuplet_remove_command(fixture.project, selected_marking);
  ASSERT_NE(remove, nullptr);
  ASSERT_TRUE(remove->execute(fixture.project).ok());
  EXPECT_FALSE(
      graphscore::event_tuplet_group(voice(fixture).events()[0]).has_value());
  ASSERT_TRUE(remove->undo(fixture.project).ok());
}

TEST(TupletEditTest, ArbitraryTenToNineAndLabelPolicy) {
  EXPECT_TRUE(
      graphscore::is_conventional_tuplet_ratio(*TupletRatio::create(3, 2)));
  EXPECT_TRUE(
      graphscore::is_conventional_tuplet_ratio(*TupletRatio::create(5, 4)));
  EXPECT_FALSE(
      graphscore::is_conventional_tuplet_ratio(*TupletRatio::create(10, 9)));
  EXPECT_EQ(graphscore::tuplet_label(*TupletRatio::create(3, 2)), "3");
  EXPECT_EQ(graphscore::tuplet_label(*TupletRatio::create(5, 4)), "5");
  EXPECT_EQ(graphscore::tuplet_label(*TupletRatio::create(10, 9)), "10:9");

  Fixture fixture(2);
  append_eighths(fixture, 10);
  const Selection selected = range(fixture, Rational(0), q(5, 4));
  auto            command  = graphscore::make_tuplet_create_command(
      fixture.project, selected, *TupletRatio::create(10, 9));
  ASSERT_NE(command, nullptr);
  EXPECT_TRUE(command->execute(fixture.project).ok());
  FixedMetrics metrics;
  const auto   layout =
      layout_notation(fixture.project, fixture.node_id, metrics);
  ASSERT_TRUE(layout.layout.has_value());
  bool found_colon = false;
  for (const auto& item : layout.layout->commands) {
    const auto* glyph = std::get_if<GlyphCommand>(&item);
    if (glyph != nullptr &&
        glyph->code_point ==
            graphscore::smufl_codepoint(graphscore::SmuflGlyph::kTupletColon)) {
      found_colon = true;
    }
  }
  EXPECT_TRUE(found_colon);
}

TEST(TupletEditTest, RejectsPartialMixedAndNestedRanges) {
  Fixture    fixture(1);
  const auto ids     = append_eighths(fixture, 3);
  const auto triplet = *TupletRatio::create(3, 2);
  EXPECT_EQ(graphscore::make_tuplet_create_command(
                fixture.project, range(fixture, q(1, 16), q(3, 8)), triplet),
            nullptr);

  auto create = graphscore::make_tuplet_create_command(
      fixture.project, range(fixture, Rational(0), q(3, 8)), triplet);
  ASSERT_NE(create, nullptr);
  ASSERT_TRUE(create->execute(fixture.project).ok());
  EXPECT_EQ(graphscore::make_tuplet_create_command(
                fixture.project, range(fixture, Rational(0), q(1, 4)), triplet),
            nullptr);

  auto second = FullMeasureSet::create({FullMeasureItem{
      fixture.node_id, fixture.track_ids.front(), fixture.stave_id(), 0}});
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(graphscore::make_tuplet_create_command(fixture.project,
                                                   Selection{*second}, triplet),
            nullptr);
  EXPECT_FALSE(ids.empty());

  Fixture incompatible(1);
  append_eighths(incompatible, 2);
  EXPECT_EQ(graphscore::make_tuplet_create_command(
                incompatible.project, range(incompatible, Rational(0), q(1, 4)),
                triplet),
            nullptr);
}

TEST(TupletEditTest, AdjacentEqualGroupsEngraveAndHitAsDistinctMarkings) {
  Fixture    fixture(1);
  const auto ids   = append_eighths(fixture, 6);
  const auto ratio = *TupletRatio::create(3, 2);
  auto       left  = graphscore::make_tuplet_create_command(
      fixture.project, range(fixture, Rational(0), q(3, 8)), ratio);
  ASSERT_NE(left, nullptr);
  ASSERT_TRUE(left->execute(fixture.project).ok());
  auto right = graphscore::make_tuplet_create_command(
      fixture.project, range(fixture, q(1, 4), q(5, 8)), ratio);
  ASSERT_NE(right, nullptr);
  ASSERT_TRUE(right->execute(fixture.project).ok());
  ASSERT_NE(graphscore::event_tuplet_group(voice(fixture).events()[0]),
            graphscore::event_tuplet_group(voice(fixture).events()[3]));

  FixedMetrics metrics;
  auto result = layout_notation(fixture.project, fixture.node_id, metrics);
  ASSERT_TRUE(result.layout.has_value());
  for (const NotationEntityId anchor : {ids[0], ids[3]}) {
    const std::string   target = anchor.to_string() + "/tuplet/digit/0";
    const GlyphCommand* glyph  = nullptr;
    for (const auto& command : result.layout->commands) {
      const auto* candidate = std::get_if<GlyphCommand>(&command);
      if (candidate != nullptr && candidate->id.value == target)
        glyph = candidate;
    }
    ASSERT_NE(glyph, nullptr);
    const auto selection = resolve_selection_at(
        fixture.project, *result.layout, NotePaletteState{}, glyph->origin);
    ASSERT_TRUE(selection.has_value());
    const auto* set = std::get_if<MarkingSet>(&*selection);
    ASSERT_NE(set, nullptr);
    EXPECT_EQ(set->items().front().anchor, anchor);
  }
}

TEST(TupletEditTest, NoteEntryRejectsUngroupedTupletPaletteDuration) {
  Fixture fixture(1);
  append_eighths(fixture, 1);
  const NotePaletteState palette =
      NotePaletteState{}.with_tuplet(*TupletRatio::create(3, 2));
  const auto spec = palette.next_entry_spec();
  EXPECT_EQ(graphscore::make_note_entry_command(
                fixture.project, fixture.node_id, fixture.track_ids.front(),
                fixture.stave_id(), Rational(0), spec,
                SpelledPitch::create(Letter::kD, 4)),
            nullptr);
}
