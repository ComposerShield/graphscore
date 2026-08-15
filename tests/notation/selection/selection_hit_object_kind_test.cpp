// SPDX-License-Identifier: Apache-2.0

#include "selection_test_support.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <graphscore/notation/graphscore_notation.hpp>

namespace {

// ---- kNotehead hit -> NoteheadSet ----

TEST(SelectionResolverTest, ClickingAPlainNoteheadSelectsThatNotehead) {
  Fixture            fixture(1);
  const SpelledPitch pitch = *SpelledPitch::create(Letter::kE, 4);
  ASSERT_TRUE(
      fixture.voice()
          .append(make_note(pitch, *Duration::create(NoteValue::kQuarter, 0)))
          .ok());
  const Note note = std::get<Note>(fixture.voice().events().back());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point = notehead_origin(layout, note.id);

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  ASSERT_TRUE(selection.has_value());
  const auto* notehead_set = std::get_if<NoteheadSet>(&*selection);
  ASSERT_NE(notehead_set, nullptr);
  ASSERT_EQ(notehead_set->items().size(), 1u);
  const NoteheadItem& item = notehead_set->items().front();
  EXPECT_EQ(item.node, fixture.node_id);
  EXPECT_EQ(item.track, fixture.track_ids[0]);
  EXPECT_EQ(item.stave, fixture.stave_id());
  EXPECT_EQ(item.voice, *Voice::create(1));
  EXPECT_EQ(item.entity, note.id);
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

TEST(SelectionResolverTest,
     ClickingOneNoteheadOfAChordSelectsJustThatNotehead) {
  Fixture                      fixture(1);
  const std::vector<ChordNote> notes = {
      {NotationEntityId::generate(), *SpelledPitch::create(Letter::kC, 4),
       false},
      {NotationEntityId::generate(), *SpelledPitch::create(Letter::kE, 4),
       false},
  };
  const Chord chord =
      make_chord(*Duration::create(NoteValue::kQuarter, 0), notes);
  ASSERT_TRUE(fixture.voice().append(chord).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point = notehead_origin(layout, notes[1].id);

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  ASSERT_TRUE(selection.has_value());
  const auto* notehead_set = std::get_if<NoteheadSet>(&*selection);
  ASSERT_NE(notehead_set, nullptr);
  ASSERT_EQ(notehead_set->items().size(), 1u);
  EXPECT_EQ(notehead_set->items().front().entity, notes[1].id);
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

// ---- kEvent hit -> ChordSet / RestSet / NoteheadSet ----

TEST(SelectionResolverTest, ClickingAChordsStemSelectsTheWholeChord) {
  Fixture                      fixture(1);
  const std::vector<ChordNote> notes = {
      {NotationEntityId::generate(), *SpelledPitch::create(Letter::kC, 4),
       false},
      {NotationEntityId::generate(), *SpelledPitch::create(Letter::kE, 4),
       false},
  };
  const Chord chord =
      make_chord(*Duration::create(NoteValue::kQuarter, 0), notes);
  ASSERT_TRUE(fixture.voice().append(chord).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point = stem_click_point(layout, chord.id);

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  ASSERT_TRUE(selection.has_value());
  const auto* chord_set = std::get_if<ChordSet>(&*selection);
  ASSERT_NE(chord_set, nullptr);
  ASSERT_EQ(chord_set->items().size(), 1u);
  const ChordItem& item = chord_set->items().front();
  EXPECT_EQ(item.entity, chord.id);
  EXPECT_EQ(item.track, fixture.track_ids[0]);
  EXPECT_EQ(item.stave, fixture.stave_id());
  EXPECT_EQ(item.voice, *Voice::create(1));
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

TEST(SelectionResolverTest, ClickingAPlainNotesStemSelectsItsOneNotehead) {
  // Judgement call: a plain (non-chord) Note's stem carries the same
  // semantic id as its own sole notehead, so the resolver treats it exactly
  // like a direct notehead click rather than inventing a distinct
  // single-note "event" selection the domain has no arm for.
  Fixture    fixture(1);
  const Note note = make_note(*SpelledPitch::create(Letter::kC, 4),
                              *Duration::create(NoteValue::kQuarter, 0));
  ASSERT_TRUE(fixture.voice().append(note).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point = stem_click_point(layout, note.id);

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  ASSERT_TRUE(selection.has_value());
  const auto* notehead_set = std::get_if<NoteheadSet>(&*selection);
  ASSERT_NE(notehead_set, nullptr);
  ASSERT_EQ(notehead_set->items().size(), 1u);
  EXPECT_EQ(notehead_set->items().front().entity, note.id);
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

TEST(SelectionResolverTest,
     ClickingAChordNoteheadsOwnAccidentalSelectsJustThatNotehead) {
  Fixture                      fixture(1);
  const std::vector<ChordNote> notes = {
      {NotationEntityId::generate(),
       *SpelledPitch::create(Letter::kC, 4, graphscore::Accidental::kSharp),
       false},
      {NotationEntityId::generate(), *SpelledPitch::create(Letter::kE, 4),
       false},
  };
  const Chord chord =
      make_chord(*Duration::create(NoteValue::kQuarter, 0), notes);
  ASSERT_TRUE(fixture.voice().append(chord).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point =
      glyph_origin(layout, notes[0].id.to_string() + "/accidental/column-0");

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  ASSERT_TRUE(selection.has_value());
  const auto* notehead_set = std::get_if<NoteheadSet>(&*selection);
  ASSERT_NE(notehead_set, nullptr);
  ASSERT_EQ(notehead_set->items().size(), 1u);
  EXPECT_EQ(notehead_set->items().front().entity, notes[0].id);
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

TEST(SelectionResolverTest, ClickingARestSelectsTheRest) {
  Fixture    fixture(1);
  const Rest rest = make_rest(*Duration::create(NoteValue::kQuarter, 0));
  ASSERT_TRUE(fixture.voice().append(rest).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point = rest_origin(layout, rest);

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  ASSERT_TRUE(selection.has_value());
  const auto* rest_set = std::get_if<RestSet>(&*selection);
  ASSERT_NE(rest_set, nullptr);
  ASSERT_EQ(rest_set->items().size(), 1u);
  const RestItem& item = rest_set->items().front();
  EXPECT_EQ(item.entity, rest.id);
  EXPECT_EQ(item.voice, *Voice::create(1));
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

// ---- Notehead priority still wins where a stem region overlaps it ----

TEST(SelectionResolverTest,
     NoteheadPriorityWinsOverAnOverlappingStemHitRegion) {
  Fixture    fixture(1);
  const Note note = make_note(*SpelledPitch::create(Letter::kC, 4),
                              *Duration::create(NoteValue::kQuarter, 0));
  ASSERT_TRUE(fixture.voice().append(note).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  ASSERT_TRUE(layout.hit_test(notehead_origin(layout, note.id)).has_value());
  const HitResult notehead_hit =
      *layout.hit_test(notehead_origin(layout, note.id));
  EXPECT_EQ(notehead_hit.role, HitRole::kNotehead);

  const auto selection = resolve_selection_at(
      fixture.project, layout, note_state(), notehead_origin(layout, note.id));
  ASSERT_TRUE(selection.has_value());
  ASSERT_NE(std::get_if<NoteheadSet>(&*selection), nullptr);
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

// ---- Grace notes: kNotehead hit resolves; grace-stem emits no hit region ----

TEST(SelectionResolverTest, ClickingAGraceNoteheadSelectsThatGraceNote) {
  Fixture fixture(1);
  // A leading note keeps the principal event's onset off the very first
  // beat: a grace group attached to the first event of a piece can engrave
  // to the left of x == 0 (past the system's own left edge), which is an
  // unrelated pre-existing engraving property this test does not exercise.
  const Note lead = make_note(*SpelledPitch::create(Letter::kC, 4),
                              *Duration::create(NoteValue::kQuarter, 0));
  ASSERT_TRUE(fixture.voice().append(lead).ok());
  const Note principal = make_note(*SpelledPitch::create(Letter::kC, 5),
                                   *Duration::create(NoteValue::kQuarter, 0));
  ASSERT_TRUE(fixture.voice().append(principal).ok());
  const graphscore::GraceNote grace{NotationEntityId::generate(),
                                    *SpelledPitch::create(Letter::kB, 4),
                                    *Duration::create(NoteValue::kEighth, 0)};
  ASSERT_TRUE(
      fixture.voice()
          .add_grace_group(graphscore::make_grace_group(principal.id, {grace}))
          .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point =
      glyph_origin(layout, grace.id.to_string() + "/grace-notehead");

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  ASSERT_TRUE(selection.has_value());
  const auto* notehead_set = std::get_if<NoteheadSet>(&*selection);
  ASSERT_NE(notehead_set, nullptr);
  EXPECT_EQ(notehead_set->items().front().entity, grace.id);
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());

  EXPECT_TRUE(
      std::ranges::none_of(layout.hit_regions, [](const HitRegion& region) {
        return region.id.value.find("grace-stem") != std::string::npos;
      }));
}

// ---- kMarking hit -> MarkingSet, all seven MarkingKinds ----

TEST(SelectionResolverTest, ClickingADynamicMarkingSelectsIt) {
  Fixture    fixture(1);
  const Note note = make_note(*SpelledPitch::create(Letter::kC, 4),
                              *Duration::create(NoteValue::kQuarter, 0));
  ASSERT_TRUE(fixture.voice().append(note).ok());
  const DynamicMarking dynamic = make_dynamic_marking(note.id, Dynamic::kMf);
  ASSERT_TRUE(fixture.voice().add_dynamic(dynamic).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  // "mf" engraves two glyphs (one per character); both must resolve to the
  // same MarkingItem.
  const NotationPoint glyph0 =
      glyph_origin(layout, dynamic.id.to_string() + "/glyph/0");
  const NotationPoint glyph1 =
      glyph_origin(layout, dynamic.id.to_string() + "/glyph/1");
  ASSERT_TRUE(layout.hit_test(glyph0).has_value());
  EXPECT_EQ(layout.hit_test(glyph0)->role, HitRole::kMarking);

  const auto selection0 =
      resolve_selection_at(fixture.project, layout, note_state(), glyph0);
  const auto selection1 =
      resolve_selection_at(fixture.project, layout, note_state(), glyph1);
  ASSERT_TRUE(selection0.has_value());
  ASSERT_TRUE(selection1.has_value());
  const auto* set0 = std::get_if<MarkingSet>(&*selection0);
  const auto* set1 = std::get_if<MarkingSet>(&*selection1);
  ASSERT_NE(set0, nullptr);
  ASSERT_NE(set1, nullptr);
  ASSERT_EQ(set0->items().size(), 1u);
  const MarkingItem& item = set0->items().front();
  EXPECT_EQ(item.kind, MarkingKind::kDynamic);
  EXPECT_EQ(item.anchor, dynamic.id);
  ASSERT_TRUE(item.voice.has_value());
  EXPECT_EQ(*item.voice, *Voice::create(1));
  EXPECT_FALSE(item.articulation.has_value());
  EXPECT_EQ(item, set1->items().front());
  EXPECT_TRUE(validate_selection(fixture.project, *selection0).empty());
}

TEST(SelectionResolverTest, ClickingAHairpinSegmentSelectsTheHairpin) {
  Fixture    fixture(1);
  const Note first  = make_note(*SpelledPitch::create(Letter::kC, 4),
                                *Duration::create(NoteValue::kQuarter, 0));
  const Note second = make_note(*SpelledPitch::create(Letter::kD, 4),
                                *Duration::create(NoteValue::kQuarter, 0));
  ASSERT_TRUE(fixture.voice().append(first).ok());
  ASSERT_TRUE(fixture.voice().append(second).ok());
  const Hairpin hairpin =
      make_hairpin(first.id, second.id, HairpinDirection::kCrescendo);
  ASSERT_TRUE(fixture.voice().add_hairpin(hairpin).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point = hit_region_center(
      layout, hairpin.id.to_string() + "/hairpin/segment/system-0/hit");

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  ASSERT_TRUE(selection.has_value());
  const auto* set = std::get_if<MarkingSet>(&*selection);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 1u);
  const MarkingItem& item = set->items().front();
  EXPECT_EQ(item.kind, MarkingKind::kHairpin);
  EXPECT_EQ(item.anchor, hairpin.id);
  ASSERT_TRUE(item.voice.has_value());
  EXPECT_EQ(*item.voice, *Voice::create(1));
  EXPECT_FALSE(item.articulation.has_value());
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

TEST(SelectionResolverTest, ClickingASlurSegmentSelectsTheSlur) {
  Fixture    fixture(1);
  const Note first  = make_note(*SpelledPitch::create(Letter::kC, 4),
                                *Duration::create(NoteValue::kQuarter, 0));
  const Note second = make_note(*SpelledPitch::create(Letter::kD, 4),
                                *Duration::create(NoteValue::kQuarter, 0));
  ASSERT_TRUE(fixture.voice().append(first).ok());
  ASSERT_TRUE(fixture.voice().append(second).ok());
  const Slur slur = make_slur(first.id, second.id);
  ASSERT_TRUE(fixture.voice().add_slur(slur).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point = hit_region_center(
      layout, slur.id.to_string() + "/slur/segment/system-0/hit");

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  ASSERT_TRUE(selection.has_value());
  const auto* set = std::get_if<MarkingSet>(&*selection);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 1u);
  const MarkingItem& item = set->items().front();
  EXPECT_EQ(item.kind, MarkingKind::kSlur);
  EXPECT_EQ(item.anchor, slur.id);
  ASSERT_TRUE(item.voice.has_value());
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

TEST(SelectionResolverTest, ClickingAPedalSpanSelectsItWithVoiceDisengaged) {
  Fixture fixture(1);
  ASSERT_TRUE(fixture.voice()
                  .append(make_rest(*Duration::create(NoteValue::kWhole, 0)))
                  .ok());
  auto* lane =
      fixture.project.find_node(fixture.node_id)->lane(fixture.track_ids[0]);
  const PedalSpan span = make_pedal_span(Rational(0), Rational(1));
  ASSERT_TRUE(lane->add_pedal_span(fixture.stave_id(), span).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const std::string   segment = span.id.to_string() + "/pedal/segment/system-0";
  const NotationPoint down    = glyph_origin(layout, segment + "/down");
  const NotationPoint up      = glyph_origin(layout, segment + "/up");
  const NotationPoint main    = hit_region_center(layout, segment + "/hit");

  const auto down_selection =
      resolve_selection_at(fixture.project, layout, note_state(), down);
  const auto up_selection =
      resolve_selection_at(fixture.project, layout, note_state(), up);
  const auto main_selection =
      resolve_selection_at(fixture.project, layout, note_state(), main);
  ASSERT_TRUE(down_selection.has_value());
  ASSERT_TRUE(up_selection.has_value());
  ASSERT_TRUE(main_selection.has_value());
  const auto* down_set = std::get_if<MarkingSet>(&*down_selection);
  const auto* up_set   = std::get_if<MarkingSet>(&*up_selection);
  const auto* main_set = std::get_if<MarkingSet>(&*main_selection);
  ASSERT_NE(down_set, nullptr);
  ASSERT_NE(up_set, nullptr);
  ASSERT_NE(main_set, nullptr);
  const MarkingItem& item = down_set->items().front();
  EXPECT_EQ(item.kind, MarkingKind::kPedalSpan);
  EXPECT_EQ(item.anchor, span.id);
  EXPECT_FALSE(item.voice.has_value());
  EXPECT_FALSE(item.articulation.has_value());
  EXPECT_EQ(item, up_set->items().front());
  EXPECT_EQ(item, main_set->items().front());
  EXPECT_TRUE(validate_selection(fixture.project, *down_selection).empty());
}

TEST(SelectionResolverTest, ClickingAnArticulationSelectsThatArticulation) {
  Fixture    fixture(1);
  const Note note = make_note(*SpelledPitch::create(Letter::kC, 4),
                              *Duration::create(NoteValue::kQuarter, 0), false,
                              {Articulation::kAccent});
  ASSERT_TRUE(fixture.voice().append(note).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point =
      glyph_origin(layout, note.id.to_string() + "/articulation/0");
  ASSERT_TRUE(layout.hit_test(point).has_value());
  EXPECT_EQ(layout.hit_test(point)->role, HitRole::kMarking);

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  ASSERT_TRUE(selection.has_value());
  const auto* set = std::get_if<MarkingSet>(&*selection);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 1u);
  const MarkingItem& item = set->items().front();
  EXPECT_EQ(item.kind, MarkingKind::kArticulation);
  EXPECT_EQ(item.anchor, note.id);
  ASSERT_TRUE(item.articulation.has_value());
  EXPECT_EQ(*item.articulation, Articulation::kAccent);
  ASSERT_TRUE(item.voice.has_value());
  EXPECT_EQ(*item.voice, *Voice::create(1));
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

TEST(SelectionResolverTest,
     ArticulationIndexResolvesTheTrueVectorPositionDespiteAnEmittedGap) {
  // Two duration articulations (staccato, tenuto) on one event makes the
  // engraver skip *both* rather than just the extras, so only index 1
  // (accent) is ever emitted -- a genuine gap in the emitted indices. This
  // pins that the resolver reads the numeric segment out of the hit id
  // itself (the true event_articulations() vector position), not a
  // position among however many glyphs got drawn.
  Fixture    fixture(1);
  const Note note = make_note(
      *SpelledPitch::create(Letter::kC, 4),
      *Duration::create(NoteValue::kQuarter, 0), false,
      {Articulation::kStaccato, Articulation::kAccent, Articulation::kTenuto});
  ASSERT_TRUE(fixture.voice().append(note).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  EXPECT_TRUE(
      std::ranges::none_of(layout.hit_regions, [&](const HitRegion& region) {
        return region.id.value == note.id.to_string() + "/articulation/0/hit";
      }));
  const NotationPoint point =
      glyph_origin(layout, note.id.to_string() + "/articulation/1");

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  ASSERT_TRUE(selection.has_value());
  const auto* set = std::get_if<MarkingSet>(&*selection);
  ASSERT_NE(set, nullptr);
  ASSERT_TRUE(set->items().front().articulation.has_value());
  EXPECT_EQ(*set->items().front().articulation, Articulation::kAccent);
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

TEST(SelectionResolverTest, ClickingATieSegmentSelectsTheTie) {
  Fixture            fixture(1);
  const SpelledPitch pitch = *SpelledPitch::create(Letter::kC, 4);
  const Note         first =
      make_note(pitch, *Duration::create(NoteValue::kQuarter, 0), true);
  const Note second =
      make_note(pitch, *Duration::create(NoteValue::kQuarter, 0));
  ASSERT_TRUE(fixture.voice().append(first).ok());
  ASSERT_TRUE(fixture.voice().append(second).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point = hit_region_center(
      layout, first.id.to_string() + "/tie/segment/system-0/sub/4/hit");
  ASSERT_TRUE(layout.hit_test(point).has_value());
  EXPECT_EQ(layout.hit_test(point)->role, HitRole::kMarking);

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  ASSERT_TRUE(selection.has_value());
  const auto* set = std::get_if<MarkingSet>(&*selection);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 1u);
  const MarkingItem& item = set->items().front();
  EXPECT_EQ(item.kind, MarkingKind::kTie);
  EXPECT_EQ(item.anchor, first.id);
  EXPECT_FALSE(item.articulation.has_value());
  ASSERT_TRUE(item.voice.has_value());
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

TEST(SelectionResolverTest, ClickingATieOnAChordNoteSelectsThatNotesTie) {
  Fixture            fixture(1);
  const SpelledPitch tied_pitch = *SpelledPitch::create(Letter::kC, 4);
  const std::vector<ChordNote> first_notes = {
      {NotationEntityId::generate(), tied_pitch, true},
      {NotationEntityId::generate(), *SpelledPitch::create(Letter::kE, 4),
       false},
  };
  const Chord first =
      make_chord(*Duration::create(NoteValue::kQuarter, 0), first_notes);
  const std::vector<ChordNote> second_notes = {
      {NotationEntityId::generate(), tied_pitch, false},
      {NotationEntityId::generate(), *SpelledPitch::create(Letter::kE, 4),
       false},
  };
  const Chord second =
      make_chord(*Duration::create(NoteValue::kQuarter, 0), second_notes);
  ASSERT_TRUE(fixture.voice().append(first).ok());
  ASSERT_TRUE(fixture.voice().append(second).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point =
      hit_region_center(layout, first_notes[0].id.to_string() +
                                    "/tie/segment/system-0/sub/4/hit");

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  ASSERT_TRUE(selection.has_value());
  const auto* set = std::get_if<MarkingSet>(&*selection);
  ASSERT_NE(set, nullptr);
  const MarkingItem& item = set->items().front();
  EXPECT_EQ(item.kind, MarkingKind::kTie);
  EXPECT_EQ(item.anchor, first_notes[0].id);
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

TEST(SelectionResolverTest, ClickingATupletDigitSelectsTheTupletRun) {
  Fixture            fixture(1);
  const auto         ratio   = *TupletRatio::create(3, 2);
  const Duration     triplet = *Duration::create(NoteValue::kEighth, 0, ratio);
  const SpelledPitch pitch   = *SpelledPitch::create(Letter::kE, 4);
  std::vector<Note>  notes;
  for (int index = 0; index < 6; ++index) {
    notes.push_back(make_note(pitch, triplet));
    ASSERT_TRUE(fixture.voice().append(notes.back()).ok());
  }

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point =
      glyph_origin(layout, notes[0].id.to_string() + "/tuplet/digit/0");
  ASSERT_TRUE(layout.hit_test(point).has_value());
  EXPECT_EQ(layout.hit_test(point)->role, HitRole::kMarking);

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  ASSERT_TRUE(selection.has_value());
  const auto* set = std::get_if<MarkingSet>(&*selection);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 1u);
  const MarkingItem& item = set->items().front();
  EXPECT_EQ(item.kind, MarkingKind::kTuplet);
  EXPECT_EQ(item.anchor, notes[0].id);
  EXPECT_FALSE(item.articulation.has_value());
  ASSERT_TRUE(item.voice.has_value());
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}
}  // namespace
