// SPDX-License-Identifier: Apache-2.0

#include "selection/selection_test_support.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>
#include <graphscore/notation/graphscore_notation.hpp>

namespace {

constexpr Voice kVoice = *Voice::create(1);

using graphscore::MarkingEdit;

VoiceContent& voice(Fixture& fixture, std::uint8_t index = 1) {
  return fixture.voice(index);
}

Rational node_end(const Fixture& fixture) {
  return fixture.project.find_node(fixture.node_id)->timeline()->node_end();
}

Duration quarter() {
  return *Duration::create(NoteValue::kQuarter, 0);
}

Duration half() {
  return *Duration::create(NoteValue::kHalf, 0);
}

Rational at(std::int64_t numerator, std::int64_t denominator) {
  return *Rational::create(numerator, denominator);
}

// Appends `durations` as C4 notes in voice 1, then normalizes the voice.
std::vector<NotationEntityId> append_notes(
    Fixture& fixture, const std::vector<Duration>& values) {
  std::vector<NotationEntityId> ids;
  for (const Duration& value : values) {
    Note note = make_note(*SpelledPitch::create(Letter::kC, 4), value);
    ids.push_back(note.id);
    EXPECT_TRUE(voice(fixture).append(std::move(note)).ok());
  }
  EXPECT_TRUE(voice(fixture).normalize(node_end(fixture)).ok());
  return ids;
}

NotationEntityId append_chord(Fixture& fixture) {
  Chord chord =
      make_chord(quarter(), {{NotationEntityId::generate(),
                              *SpelledPitch::create(Letter::kC, 4), false},
                             {NotationEntityId::generate(),
                              *SpelledPitch::create(Letter::kE, 4), false}});
  const auto id = chord.id;
  EXPECT_TRUE(voice(fixture).append(std::move(chord)).ok());
  EXPECT_TRUE(voice(fixture).normalize(node_end(fixture)).ok());
  return id;
}

// Pedal commands require every voice of the addressed stave to be complete.
void complete_every_voice(Fixture& fixture) {
  for (std::uint8_t index = Voice::kMin; index <= Voice::kMax; ++index) {
    EXPECT_TRUE(voice(fixture, index).normalize(node_end(fixture)).ok());
  }
}

Selection note_selection(Fixture& fixture, NotationEntityId id) {
  return Selection{*NoteheadSet::create(
      {NoteheadItem{fixture.node_id, fixture.track_ids.front(),
                    fixture.stave_id(), kVoice, id}})};
}

Selection chord_selection(Fixture& fixture, NotationEntityId id) {
  return Selection{
      *ChordSet::create({ChordItem{fixture.node_id, fixture.track_ids.front(),
                                   fixture.stave_id(), kVoice, id}})};
}

Selection marking_selection(Fixture& fixture, MarkingKind kind,
                            NotationEntityId     id,
                            std::optional<Voice> item_voice = kVoice) {
  return Selection{*MarkingSet::create(
      {MarkingItem{fixture.node_id, fixture.track_ids.front(),
                   fixture.stave_id(), item_voice, kind, id, std::nullopt}})};
}

Selection range_selection(Fixture& fixture, Rational start, Rational end,
                          Voice item_voice = kVoice) {
  return Selection{*ArbitraryRangeSet::create({ArbitraryRangeItem{
      fixture.node_id, fixture.track_ids.front(), fixture.stave_id(),
      item_voice, MusicalSpan{start, end}}})};
}

NotationEntityId add_dynamic(Fixture& fixture, NotationEntityId at_event,
                             Dynamic value) {
  const DynamicMarking marking = make_dynamic_marking(at_event, value);
  EXPECT_TRUE(voice(fixture).add_dynamic(marking).ok());
  return marking.id;
}

NotationEntityId add_hairpin(Fixture& fixture, NotationEntityId start_event,
                             NotationEntityId end_event,
                             HairpinDirection direction) {
  const Hairpin hairpin = make_hairpin(start_event, end_event, direction);
  EXPECT_TRUE(voice(fixture).add_hairpin(hairpin).ok());
  return hairpin.id;
}

const std::vector<PedalSpan>* pedal_spans(const Fixture& fixture) {
  return fixture.project.find_node(fixture.node_id)
      ->lane(fixture.track_ids.front())
      ->pedal_spans(fixture.stave_id());
}

// The engraved y of a point dynamic's first glyph -- the lane the greedy
// allocator gave it, read out of the real layout.
double dynamic_lane_y(const NotationLayout& layout, NotationEntityId marking) {
  return glyph_origin(layout, marking.to_string() + "/glyph/0").y;
}

// The engraved lane of a hairpin's own system segment. The wedge's two lines
// open away from the lane line by a direction-dependent amount, so the hit
// band -- which is centred on the lane itself -- is the direction-independent
// readout of the lane assignment.
double hairpin_lane_y(const NotationLayout& layout, NotationEntityId marking) {
  return hit_region_center(
             layout, marking.to_string() + "/hairpin/segment/system-0/hit")
      .y;
}

}  // namespace

TEST(MarkingStyleEditTest, AppliesEveryDynamicToNotesAndChords) {
  for (const Dynamic value : graphscore::kAllDynamics) {
    Fixture    note_fixture(1);
    const auto note_id = append_notes(note_fixture, {quarter()}).front();
    auto       applied = graphscore::make_dynamic_edit_command(
        note_fixture.project, note_selection(note_fixture, note_id),
        MarkingEdit::kApply, value);
    ASSERT_TRUE(applied.available()) << applied.unavailable_reason;
    ASSERT_TRUE(applied.command->execute(note_fixture.project).ok());
    ASSERT_EQ(voice(note_fixture).dynamics().size(), 1u);
    EXPECT_EQ(voice(note_fixture).dynamics().front().value, value);
    EXPECT_EQ(voice(note_fixture).dynamics().front().at_event, note_id);

    Fixture    chord_fixture(1);
    const auto chord_id = append_chord(chord_fixture);
    auto       on_chord = graphscore::make_dynamic_edit_command(
        chord_fixture.project, chord_selection(chord_fixture, chord_id),
        MarkingEdit::kApply, value);
    ASSERT_TRUE(on_chord.available()) << on_chord.unavailable_reason;
    ASSERT_TRUE(on_chord.command->execute(chord_fixture.project).ok());
    EXPECT_EQ(voice(chord_fixture).dynamics().front().at_event, chord_id);
  }
}

TEST(MarkingStyleEditTest, DynamicChangeAndRemoveHavePreciseReasons) {
  Fixture         fixture(1);
  const auto      ids     = append_notes(fixture, {quarter(), quarter()});
  const auto      marking = add_dynamic(fixture, ids.front(), Dynamic::kMf);
  const Selection selected =
      marking_selection(fixture, MarkingKind::kDynamic, marking);
  const VoiceContent before = voice(fixture);

  auto unknown = graphscore::make_dynamic_edit_command(
      fixture.project, selected, MarkingEdit::kChange,
      static_cast<Dynamic>(200));
  EXPECT_FALSE(unknown.available());
  EXPECT_EQ(unknown.unavailable_reason, "unknown dynamic");
  EXPECT_EQ(voice(fixture), before);

  auto unknown_edit = graphscore::make_dynamic_edit_command(
      fixture.project, selected, static_cast<MarkingEdit>(200), Dynamic::kF);
  EXPECT_FALSE(unknown_edit.available());
  EXPECT_EQ(unknown_edit.unavailable_reason, "unknown marking edit");
  EXPECT_EQ(voice(fixture), before);

  auto no_op = graphscore::make_dynamic_edit_command(
      fixture.project, selected, MarkingEdit::kChange, Dynamic::kMf);
  EXPECT_FALSE(no_op.available());
  EXPECT_EQ(no_op.unavailable_reason, "dynamic is already set");
  EXPECT_EQ(voice(fixture), before);

  auto wrong_arm = graphscore::make_dynamic_edit_command(
      fixture.project, note_selection(fixture, ids.front()),
      MarkingEdit::kChange, Dynamic::kF);
  EXPECT_FALSE(wrong_arm.available());
  EXPECT_EQ(wrong_arm.unavailable_reason, "requires one live dynamic marking");
  EXPECT_EQ(voice(fixture), before);

  auto stale_apply = graphscore::make_dynamic_edit_command(
      fixture.project, note_selection(fixture, NotationEntityId::generate()),
      MarkingEdit::kApply, Dynamic::kF);
  EXPECT_FALSE(stale_apply.available());
  EXPECT_EQ(stale_apply.unavailable_reason,
            "requires one live note or chord event");
  EXPECT_EQ(voice(fixture), before);

  auto change = graphscore::make_dynamic_edit_command(
      fixture.project, selected, MarkingEdit::kChange, Dynamic::kFff);
  ASSERT_TRUE(change.available()) << change.unavailable_reason;
  ASSERT_TRUE(change.command->execute(fixture.project).ok());
  ASSERT_EQ(voice(fixture).dynamics().size(), 1u);
  EXPECT_EQ(voice(fixture).dynamics().front().id, marking);
  EXPECT_EQ(voice(fixture).dynamics().front().value, Dynamic::kFff);

  auto removed = graphscore::make_dynamic_edit_command(
      fixture.project, selected, MarkingEdit::kRemove, Dynamic::kMf);
  ASSERT_TRUE(removed.available()) << removed.unavailable_reason;
  ASSERT_TRUE(removed.command->execute(fixture.project).ok());
  EXPECT_TRUE(voice(fixture).dynamics().empty());
}

TEST(MarkingStyleEditTest, DynamicApplyRejectsGraceNotesWithoutMutation) {
  Fixture               fixture(1);
  const auto            principal = append_notes(fixture, {quarter()}).front();
  graphscore::GraceNote grace{NotationEntityId::generate(),
                              *SpelledPitch::create(Letter::kD, 4),
                              *Duration::create(NoteValue::kEighth, 0)};
  const auto            grace_id = grace.id;
  ASSERT_TRUE(voice(fixture)
                  .add_grace_group(make_grace_group(principal, {grace}))
                  .ok());
  const VoiceContent before = voice(fixture);
  auto               result = graphscore::make_dynamic_edit_command(
      fixture.project, note_selection(fixture, grace_id), MarkingEdit::kApply,
      Dynamic::kMf);
  EXPECT_FALSE(result.available());
  EXPECT_EQ(result.unavailable_reason,
            "grace notes do not support marking editing");
  EXPECT_EQ(voice(fixture), before);
}

TEST(MarkingStyleEditTest, HairpinApplyConsumesACompleteTwoEventRange) {
  Fixture    fixture(1);
  const auto ids = append_notes(fixture, {half(), quarter(), quarter()});
  const VoiceContent before = voice(fixture);

  auto partial = graphscore::make_hairpin_edit_command(
      fixture.project, range_selection(fixture, at(0, 1), at(1, 4)),
      MarkingEdit::kApply, HairpinDirection::kCrescendo);
  EXPECT_FALSE(partial.available());
  EXPECT_EQ(partial.unavailable_reason,
            "requires a range of complete events on one staff and voice");
  EXPECT_EQ(voice(fixture), before);

  auto single = graphscore::make_hairpin_edit_command(
      fixture.project, range_selection(fixture, at(0, 1), at(1, 2)),
      MarkingEdit::kApply, HairpinDirection::kCrescendo);
  EXPECT_FALSE(single.available());
  EXPECT_EQ(single.unavailable_reason,
            "requires a range of at least two events");
  EXPECT_EQ(voice(fixture), before);

  auto unknown = graphscore::make_hairpin_edit_command(
      fixture.project, range_selection(fixture, at(0, 1), at(3, 4)),
      MarkingEdit::kApply, static_cast<HairpinDirection>(200));
  EXPECT_FALSE(unknown.available());
  EXPECT_EQ(unknown.unavailable_reason, "unknown hairpin direction");
  EXPECT_EQ(voice(fixture), before);

  auto wrong_arm = graphscore::make_hairpin_edit_command(
      fixture.project, note_selection(fixture, ids.front()),
      MarkingEdit::kApply, HairpinDirection::kCrescendo);
  EXPECT_FALSE(wrong_arm.available());
  EXPECT_EQ(wrong_arm.unavailable_reason,
            "requires a range of complete events on one staff and voice");
  EXPECT_EQ(voice(fixture), before);

  auto applied = graphscore::make_hairpin_edit_command(
      fixture.project, range_selection(fixture, at(0, 1), at(3, 4)),
      MarkingEdit::kApply, HairpinDirection::kDiminuendo);
  ASSERT_TRUE(applied.available()) << applied.unavailable_reason;
  ASSERT_TRUE(applied.command->execute(fixture.project).ok());
  ASSERT_EQ(voice(fixture).hairpins().size(), 1u);
  EXPECT_EQ(voice(fixture).hairpins().front().start_event, ids[0]);
  EXPECT_EQ(voice(fixture).hairpins().front().end_event, ids[1]);
  EXPECT_EQ(voice(fixture).hairpins().front().direction,
            HairpinDirection::kDiminuendo);
}

TEST(MarkingStyleEditTest, HairpinChangeAndRemoveHavePreciseReasons) {
  Fixture         fixture(1);
  const auto      ids     = append_notes(fixture, {quarter(), quarter()});
  const auto      marking = add_hairpin(fixture, ids.front(), ids.back(),
                                        HairpinDirection::kCrescendo);
  const Selection selected =
      marking_selection(fixture, MarkingKind::kHairpin, marking);
  const VoiceContent before = voice(fixture);

  auto no_op = graphscore::make_hairpin_edit_command(
      fixture.project, selected, MarkingEdit::kChange,
      HairpinDirection::kCrescendo);
  EXPECT_FALSE(no_op.available());
  EXPECT_EQ(no_op.unavailable_reason, "hairpin direction is already set");
  EXPECT_EQ(voice(fixture), before);

  auto wrong_arm = graphscore::make_hairpin_edit_command(
      fixture.project, note_selection(fixture, ids.front()),
      MarkingEdit::kChange, HairpinDirection::kDiminuendo);
  EXPECT_FALSE(wrong_arm.available());
  EXPECT_EQ(wrong_arm.unavailable_reason, "requires one live hairpin marking");
  EXPECT_EQ(voice(fixture), before);

  auto change = graphscore::make_hairpin_edit_command(
      fixture.project, selected, MarkingEdit::kChange,
      HairpinDirection::kDiminuendo);
  ASSERT_TRUE(change.available()) << change.unavailable_reason;
  ASSERT_TRUE(change.command->execute(fixture.project).ok());
  ASSERT_EQ(voice(fixture).hairpins().size(), 1u);
  EXPECT_EQ(voice(fixture).hairpins().front().id, marking);
  EXPECT_EQ(voice(fixture).hairpins().front().direction,
            HairpinDirection::kDiminuendo);

  auto removed = graphscore::make_hairpin_edit_command(
      fixture.project, selected, MarkingEdit::kRemove,
      HairpinDirection::kCrescendo);
  ASSERT_TRUE(removed.available()) << removed.unavailable_reason;
  ASSERT_TRUE(removed.command->execute(fixture.project).ok());
  EXPECT_TRUE(voice(fixture).hairpins().empty());
}

// Collection order is observable in engraving: notation_index assigns each
// marking an order key in source order, the engraver walks a system's markings
// in order-key order, and its lane allocator is greedy first-fit over a shared
// below-staff lane list. A style-only change that reinserted the marking after
// its peers would therefore restack every marking it overlaps horizontally.
TEST(MarkingStyleEditTest, DynamicChangeKeepsOverlappingLaneAssignment) {
  Fixture    fixture(1);
  const auto ids = append_notes(fixture, {quarter(), quarter()});
  // Both dynamics sit on the same event, so their glyph runs overlap in x and
  // the allocator must stack them in two different lanes.
  const auto lower = add_dynamic(fixture, ids.front(), Dynamic::kMf);
  const auto upper = add_dynamic(fixture, ids.front(), Dynamic::kP);

  const FixedMetrics              metrics;
  const NotationLayoutOptions     options;
  graphscore::NotationLayoutCache cache;
  const auto                      seeded =
      cache.update(fixture.project, fixture.node_id, metrics, options, {});
  ASSERT_TRUE(seeded);
  const double lower_y = dynamic_lane_y(*seeded.layout, lower);
  const double upper_y = dynamic_lane_y(*seeded.layout, upper);
  ASSERT_NE(lower_y, upper_y);

  auto change = graphscore::make_dynamic_edit_command(
      fixture.project, marking_selection(fixture, MarkingKind::kDynamic, lower),
      MarkingEdit::kChange, Dynamic::kFff);
  ASSERT_TRUE(change.available()) << change.unavailable_reason;
  ASSERT_TRUE(change.command->execute(fixture.project).ok());
  ASSERT_EQ(voice(fixture).dynamics().size(), 2u);
  EXPECT_EQ(voice(fixture).dynamics().front().id, lower);
  EXPECT_EQ(voice(fixture).dynamics().back().id, upper);

  const auto updated = cache.update(
      fixture.project, fixture.node_id, metrics, options,
      {{graphscore::NotationInvalidationKind::kLocalContent, 0, 0}});
  ASSERT_TRUE(updated);
  EXPECT_EQ(dynamic_lane_y(*updated.layout, lower), lower_y);
  EXPECT_EQ(dynamic_lane_y(*updated.layout, upper), upper_y);

  const auto fresh =
      layout_notation(fixture.project, fixture.node_id, metrics, options);
  ASSERT_TRUE(fresh.layout.has_value());
  EXPECT_EQ(*updated.layout, *fresh.layout);
}

TEST(MarkingStyleEditTest, HairpinChangeKeepsOverlappingLaneAssignment) {
  Fixture    fixture(1);
  const auto ids =
      append_notes(fixture, {quarter(), quarter(), quarter(), quarter()});
  const auto lower =
      add_hairpin(fixture, ids[0], ids[2], HairpinDirection::kCrescendo);
  const auto upper =
      add_hairpin(fixture, ids[1], ids[3], HairpinDirection::kCrescendo);

  const FixedMetrics              metrics;
  const NotationLayoutOptions     options;
  graphscore::NotationLayoutCache cache;
  const auto                      seeded =
      cache.update(fixture.project, fixture.node_id, metrics, options, {});
  ASSERT_TRUE(seeded);
  const double lower_y = hairpin_lane_y(*seeded.layout, lower);
  const double upper_y = hairpin_lane_y(*seeded.layout, upper);
  ASSERT_NE(lower_y, upper_y);

  auto change = graphscore::make_hairpin_edit_command(
      fixture.project, marking_selection(fixture, MarkingKind::kHairpin, lower),
      MarkingEdit::kChange, HairpinDirection::kDiminuendo);
  ASSERT_TRUE(change.available()) << change.unavailable_reason;
  ASSERT_TRUE(change.command->execute(fixture.project).ok());
  ASSERT_EQ(voice(fixture).hairpins().size(), 2u);
  EXPECT_EQ(voice(fixture).hairpins().front().id, lower);
  EXPECT_EQ(voice(fixture).hairpins().back().id, upper);

  const auto updated = cache.update(
      fixture.project, fixture.node_id, metrics, options,
      {{graphscore::NotationInvalidationKind::kLocalContent, 0, 0}});
  ASSERT_TRUE(updated);
  EXPECT_EQ(hairpin_lane_y(*updated.layout, lower), lower_y);
  EXPECT_EQ(hairpin_lane_y(*updated.layout, upper), upper_y);

  const auto fresh =
      layout_notation(fixture.project, fixture.node_id, metrics, options);
  ASSERT_TRUE(fresh.layout.has_value());
  EXPECT_EQ(*updated.layout, *fresh.layout);
}

TEST(MarkingStyleEditTest, PedalSpanAppliesOverAStaveScopedRange) {
  Fixture fixture(1);
  append_notes(fixture, {quarter(), quarter()});
  complete_every_voice(fixture);

  auto changed = graphscore::make_pedal_span_edit_command(
      fixture.project, range_selection(fixture, at(0, 1), at(1, 2)),
      MarkingEdit::kChange);
  EXPECT_FALSE(changed.available());
  EXPECT_EQ(changed.unavailable_reason, "pedal spans cannot be changed");

  auto wrong_arm = graphscore::make_pedal_span_edit_command(
      fixture.project, note_selection(fixture, NotationEntityId::generate()),
      MarkingEdit::kApply);
  EXPECT_FALSE(wrong_arm.available());
  EXPECT_EQ(wrong_arm.unavailable_reason, "requires a range on one staff");

  // Pedal is stave-scoped, so a range covering two voices of one stave is a
  // legitimate target even though it is not a legitimate hairpin one.
  const Selection two_voices = Selection{*ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fixture.node_id, fixture.track_ids.front(),
                          fixture.stave_id(), kVoice,
                          MusicalSpan{at(0, 1), at(1, 4)}},
       ArbitraryRangeItem{fixture.node_id, fixture.track_ids.front(),
                          fixture.stave_id(), *Voice::create(2),
                          MusicalSpan{at(1, 4), at(1, 1)}}})};
  auto            applied    = graphscore::make_pedal_span_edit_command(
      fixture.project, two_voices, MarkingEdit::kApply);
  ASSERT_TRUE(applied.available()) << applied.unavailable_reason;
  ASSERT_TRUE(applied.command->execute(fixture.project).ok());
  ASSERT_NE(pedal_spans(fixture), nullptr);
  ASSERT_EQ(pedal_spans(fixture)->size(), 1u);
  EXPECT_EQ(pedal_spans(fixture)->front().start, at(0, 1));
  EXPECT_EQ(pedal_spans(fixture)->front().end, node_end(fixture));

  const auto span_id = pedal_spans(fixture)->front().id;
  auto       removed = graphscore::make_pedal_span_edit_command(
      fixture.project,
      marking_selection(fixture, MarkingKind::kPedalSpan, span_id,
                              std::nullopt),
      MarkingEdit::kRemove);
  ASSERT_TRUE(removed.available()) << removed.unavailable_reason;
  ASSERT_TRUE(removed.command->execute(fixture.project).ok());
  EXPECT_TRUE(pedal_spans(fixture)->empty());
}

TEST(MarkingStyleEditTest, PedalSpanRejectsIncompleteLanesAndStaleMarkings) {
  Fixture fixture(1);
  append_notes(fixture, {quarter(), quarter()});
  auto incomplete = graphscore::make_pedal_span_edit_command(
      fixture.project, range_selection(fixture, at(0, 1), at(1, 2)),
      MarkingEdit::kApply);
  EXPECT_FALSE(incomplete.available());
  EXPECT_EQ(incomplete.unavailable_reason,
            "requires complete rhythm in every voice on the staff");
  EXPECT_EQ(pedal_spans(fixture), nullptr);

  complete_every_voice(fixture);
  auto stale = graphscore::make_pedal_span_edit_command(
      fixture.project,
      marking_selection(fixture, MarkingKind::kPedalSpan,
                        NotationEntityId::generate(), std::nullopt),
      MarkingEdit::kRemove);
  EXPECT_FALSE(stale.available());
  EXPECT_EQ(stale.unavailable_reason, "requires one live pedal span marking");

  auto unknown_edit = graphscore::make_pedal_span_edit_command(
      fixture.project, range_selection(fixture, at(0, 1), at(1, 2)),
      static_cast<MarkingEdit>(200));
  EXPECT_FALSE(unknown_edit.available());
  EXPECT_EQ(unknown_edit.unavailable_reason, "unknown marking edit");
}

// A degenerate range is a well-formed selection, so it reaches the pedal
// factory's own timeline guard. That guard is what keeps AddPedalSpanCommand's
// kPedalSpanNotOrdered/kPedalSpanOutOfRange diagnostics unreachable from here.
TEST(MarkingStyleEditTest, PedalSpanRejectsARangeOutsideTheNodeTimeline) {
  Fixture fixture(1);
  append_notes(fixture, {quarter(), quarter()});
  complete_every_voice(fixture);

  auto empty = graphscore::make_pedal_span_edit_command(
      fixture.project, range_selection(fixture, at(1, 4), at(1, 4)),
      MarkingEdit::kApply);
  EXPECT_FALSE(empty.available());
  EXPECT_EQ(empty.unavailable_reason,
            "requires a non-empty range inside the node timeline");
  EXPECT_EQ(pedal_spans(fixture), nullptr);

  auto past_end = graphscore::make_pedal_span_edit_command(
      fixture.project,
      range_selection(fixture, at(1, 4), node_end(fixture) + at(1, 4)),
      MarkingEdit::kApply);
  EXPECT_FALSE(past_end.available());
  EXPECT_EQ(past_end.unavailable_reason,
            "requires a non-empty range inside the node timeline");
  EXPECT_EQ(pedal_spans(fixture), nullptr);
}

// Documented in the action table (§10.4): a pedal span's bounds are the union
// of the range's own bounds, with no contiguity requirement, so two disjoint
// selected chunks yield one span covering the gap between them.
TEST(MarkingStyleEditTest, PedalSpanUnionsDisjointRangeChunks) {
  Fixture fixture(1);
  append_notes(fixture, {quarter(), quarter(), quarter(), quarter()});
  complete_every_voice(fixture);

  const Selection disjoint = Selection{*ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fixture.node_id, fixture.track_ids.front(),
                          fixture.stave_id(), kVoice,
                          MusicalSpan{at(0, 1), at(1, 4)}},
       ArbitraryRangeItem{fixture.node_id, fixture.track_ids.front(),
                          fixture.stave_id(), kVoice,
                          MusicalSpan{at(3, 4), at(1, 1)}}})};
  auto            applied  = graphscore::make_pedal_span_edit_command(
      fixture.project, disjoint, MarkingEdit::kApply);
  ASSERT_TRUE(applied.available()) << applied.unavailable_reason;
  ASSERT_TRUE(applied.command->execute(fixture.project).ok());
  ASSERT_NE(pedal_spans(fixture), nullptr);
  ASSERT_EQ(pedal_spans(fixture)->size(), 1u);
  EXPECT_EQ(pedal_spans(fixture)->front().start, at(0, 1));
  EXPECT_EQ(pedal_spans(fixture)->front().end, at(1, 1));
}

TEST(MarkingStyleEditTest, RangeResolutionRejectsMultiStaffAndMultiItemSets) {
  Fixture fixture({StaffLayout::grand_staff()}, 1);
  for (std::size_t stave = 0; stave < 2u; ++stave) {
    for (std::uint8_t index = Voice::kMin; index <= Voice::kMax; ++index) {
      EXPECT_TRUE(
          fixture.voice(index, 0, stave).normalize(node_end(fixture)).ok());
    }
  }

  // Pedal is stave-scoped, so a range straddling two staves has no single
  // stave to carry the span.
  const Selection two_staves = Selection{*ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fixture.node_id, fixture.track_ids.front(),
                          fixture.stave_id(0, 0), kVoice,
                          MusicalSpan{at(0, 1), at(1, 2)}},
       ArbitraryRangeItem{fixture.node_id, fixture.track_ids.front(),
                          fixture.stave_id(0, 1), kVoice,
                          MusicalSpan{at(0, 1), at(1, 2)}}})};
  auto            pedal      = graphscore::make_pedal_span_edit_command(
      fixture.project, two_staves, MarkingEdit::kApply);
  EXPECT_FALSE(pedal.available());
  EXPECT_EQ(pedal.unavailable_reason, "requires a range on one staff");
  auto slur_staves = graphscore::make_slur_edit_command(
      fixture.project, two_staves, MarkingEdit::kApply);
  EXPECT_FALSE(slur_staves.available());
  EXPECT_EQ(slur_staves.unavailable_reason,
            "requires a range of complete events on one staff and voice");

  // A hairpin is voice-scoped and consumes exactly one range item, so even a
  // two-item set naming one staff and one voice is rejected.
  const Selection two_items = Selection{*ArbitraryRangeSet::create(
      {ArbitraryRangeItem{fixture.node_id, fixture.track_ids.front(),
                          fixture.stave_id(0, 0), kVoice,
                          MusicalSpan{at(0, 1), at(1, 4)}},
       ArbitraryRangeItem{fixture.node_id, fixture.track_ids.front(),
                          fixture.stave_id(0, 0), kVoice,
                          MusicalSpan{at(1, 4), at(1, 2)}}})};
  auto            hairpin   = graphscore::make_hairpin_edit_command(
      fixture.project, two_items, MarkingEdit::kApply,
      HairpinDirection::kCrescendo);
  EXPECT_FALSE(hairpin.available());
  EXPECT_EQ(hairpin.unavailable_reason,
            "requires a range of complete events on one staff and voice");
  auto slur_items = graphscore::make_slur_edit_command(
      fixture.project, two_items, MarkingEdit::kApply);
  EXPECT_FALSE(slur_items.available());
  EXPECT_EQ(slur_items.unavailable_reason,
            "requires a range of complete events on one staff and voice");
}

TEST(MarkingStyleEditTest, TieApplyAndRemoveTargetExactlyOneNotehead) {
  Fixture    fixture(1);
  const auto ids = append_notes(fixture, {quarter(), quarter(), quarter()});
  const Selection selected = note_selection(fixture, ids.front());

  auto applied = graphscore::make_tie_edit_command(fixture.project, selected,
                                                   MarkingEdit::kApply);
  ASSERT_TRUE(applied.available()) << applied.unavailable_reason;
  ASSERT_TRUE(applied.command->execute(fixture.project).ok());
  EXPECT_TRUE(std::get<Note>(voice(fixture).events().front()).tied_to_next);
  ASSERT_TRUE(applied.command->undo(fixture.project).ok());
  EXPECT_FALSE(std::get<Note>(voice(fixture).events().front()).tied_to_next);
  ASSERT_TRUE(applied.command->redo(fixture.project).ok());

  auto duplicate = graphscore::make_tie_edit_command(fixture.project, selected,
                                                     MarkingEdit::kApply);
  EXPECT_FALSE(duplicate.available());
  EXPECT_EQ(duplicate.unavailable_reason, "notehead is already tied");

  auto removed = graphscore::make_tie_edit_command(fixture.project, selected,
                                                   MarkingEdit::kRemove);
  ASSERT_TRUE(removed.available()) << removed.unavailable_reason;
  ASSERT_TRUE(removed.command->execute(fixture.project).ok());
  EXPECT_FALSE(std::get<Note>(voice(fixture).events().front()).tied_to_next);

  auto absent = graphscore::make_tie_edit_command(fixture.project, selected,
                                                  MarkingEdit::kRemove);
  EXPECT_FALSE(absent.available());
  EXPECT_EQ(absent.unavailable_reason, "notehead has no tie to remove");

  auto wrong = graphscore::make_tie_edit_command(
      fixture.project, chord_selection(fixture, ids[1]), MarkingEdit::kApply);
  EXPECT_FALSE(wrong.available());
  EXPECT_EQ(wrong.unavailable_reason, "requires exactly one live notehead");
}

TEST(MarkingStyleEditTest, TieApplyRequiresImmediateMatchingPitch) {
  Fixture    fixture(1);
  Note       first = make_note(*SpelledPitch::create(Letter::kC, 4), quarter());
  const auto first_id = first.id;
  ASSERT_TRUE(voice(fixture).append(std::move(first)).ok());
  ASSERT_TRUE(
      voice(fixture)
          .append(make_note(*SpelledPitch::create(Letter::kD, 4), quarter()))
          .ok());
  ASSERT_TRUE(voice(fixture).normalize(node_end(fixture)).ok());

  auto mismatch = graphscore::make_tie_edit_command(
      fixture.project, note_selection(fixture, first_id), MarkingEdit::kApply);
  EXPECT_FALSE(mismatch.available());
  EXPECT_EQ(mismatch.unavailable_reason,
            "requires an immediately following event with the same pitch");
  EXPECT_FALSE(std::get<Note>(voice(fixture).events().front()).tied_to_next);
}

TEST(MarkingStyleEditTest, TieTargetsOneNoteheadWithinAChord) {
  Fixture     fixture(1);
  const auto  chord_id = append_chord(fixture);
  const auto* chord    = std::get_if<Chord>(&voice(fixture).events().front());
  ASSERT_NE(chord, nullptr);
  const auto selected_notehead = chord->notes.front().id;
  ASSERT_TRUE(
      voice(fixture)
          .replace_event(at(1, 4),
                         make_note(chord->notes.front().pitch, quarter()),
                         node_end(fixture))
          .ok());

  auto applied = graphscore::make_tie_edit_command(
      fixture.project, note_selection(fixture, selected_notehead),
      MarkingEdit::kApply);
  ASSERT_TRUE(applied.available()) << applied.unavailable_reason;
  ASSERT_TRUE(applied.command->execute(fixture.project).ok());
  const auto* changed = std::get_if<Chord>(&voice(fixture).events().front());
  ASSERT_NE(changed, nullptr);
  EXPECT_EQ(changed->id, chord_id);
  EXPECT_TRUE(changed->notes.front().tied_to_next);
  EXPECT_FALSE(changed->notes.back().tied_to_next);
}

TEST(MarkingStyleEditTest, SlurApplyAndRemoveRoundTripWithPreciseFeedback) {
  Fixture    fixture(1);
  const auto ids = append_notes(fixture, {quarter(), quarter(), quarter()});
  const Selection range = range_selection(fixture, Rational(0), at(3, 4));

  auto applied = graphscore::make_slur_edit_command(fixture.project, range,
                                                    MarkingEdit::kApply);
  ASSERT_TRUE(applied.available()) << applied.unavailable_reason;
  ASSERT_TRUE(applied.command->execute(fixture.project).ok());
  ASSERT_EQ(voice(fixture).slurs().size(), 1u);
  EXPECT_EQ(voice(fixture).slurs().front().start_event, ids.front());
  EXPECT_EQ(voice(fixture).slurs().front().end_event, ids[2]);
  const auto slur_id = voice(fixture).slurs().front().id;
  ASSERT_TRUE(applied.command->undo(fixture.project).ok());
  EXPECT_TRUE(voice(fixture).slurs().empty());
  ASSERT_TRUE(applied.command->redo(fixture.project).ok());

  auto duplicate = graphscore::make_slur_edit_command(fixture.project, range,
                                                      MarkingEdit::kApply);
  EXPECT_FALSE(duplicate.available());
  EXPECT_EQ(duplicate.unavailable_reason,
            "slur already exists across this range");

  const Selection selected =
      marking_selection(fixture, MarkingKind::kSlur, slur_id);
  auto removed = graphscore::make_slur_edit_command(fixture.project, selected,
                                                    MarkingEdit::kRemove);
  ASSERT_TRUE(removed.available()) << removed.unavailable_reason;
  ASSERT_TRUE(removed.command->execute(fixture.project).ok());
  EXPECT_TRUE(voice(fixture).slurs().empty());

  auto stale = graphscore::make_slur_edit_command(fixture.project, selected,
                                                  MarkingEdit::kRemove);
  EXPECT_FALSE(stale.available());
  EXPECT_EQ(stale.unavailable_reason, "requires one live slur marking");
}

TEST(MarkingStyleEditTest, SlurRejectsPartialSingleAndRestEndpointRanges) {
  Fixture fixture(1);
  ASSERT_TRUE(voice(fixture).append(make_rest(quarter())).ok());
  ASSERT_TRUE(
      voice(fixture)
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), quarter()))
          .ok());
  ASSERT_TRUE(voice(fixture).normalize(node_end(fixture)).ok());

  auto partial = graphscore::make_slur_edit_command(
      fixture.project, range_selection(fixture, Rational(0), at(3, 8)),
      MarkingEdit::kApply);
  EXPECT_FALSE(partial.available());
  EXPECT_EQ(partial.unavailable_reason,
            "requires a range of complete events on one staff and voice");

  auto single = graphscore::make_slur_edit_command(
      fixture.project, range_selection(fixture, at(1, 4), at(1, 2)),
      MarkingEdit::kApply);
  EXPECT_FALSE(single.available());
  EXPECT_EQ(single.unavailable_reason,
            "requires a range of at least two events");

  auto rest_endpoint = graphscore::make_slur_edit_command(
      fixture.project, range_selection(fixture, Rational(0), at(1, 2)),
      MarkingEdit::kApply);
  EXPECT_FALSE(rest_endpoint.available());
  EXPECT_EQ(rest_endpoint.unavailable_reason,
            "slur endpoints must be notes or chords");
}

TEST(MarkingStyleEditTest, PedalValidationIsLimitedToTheAddressedStaff) {
  Fixture fixture({StaffLayout::grand_staff()}, 1);
  append_notes(fixture, {quarter(), quarter()});
  for (std::uint8_t index = Voice::kMin; index <= Voice::kMax; ++index) {
    EXPECT_TRUE(fixture.voice(index, 0, 0).normalize(node_end(fixture)).ok());
  }
  // Every voice on the other staff remains empty and incomplete.
  auto applied = graphscore::make_pedal_span_edit_command(
      fixture.project, range_selection(fixture, Rational(0), at(1, 2)),
      MarkingEdit::kApply);
  ASSERT_TRUE(applied.available()) << applied.unavailable_reason;
  EXPECT_TRUE(applied.command->execute(fixture.project).ok());
}
