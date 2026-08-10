// SPDX-License-Identifier: Apache-2.0

#include <graphscore/notation/graphscore_notation.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

using graphscore::Articulation;
using graphscore::Chord;
using graphscore::ChordItem;
using graphscore::ChordNote;
using graphscore::ChordSet;
using graphscore::Clef;
using graphscore::Duration;
using graphscore::Dynamic;
using graphscore::DynamicMarking;
using graphscore::GlyphCommand;
using graphscore::GlyphMetrics;
using graphscore::GlyphMetricsValue;
using graphscore::Hairpin;
using graphscore::HairpinDirection;
using graphscore::HitRegion;
using graphscore::HitResult;
using graphscore::HitRole;
using graphscore::InsertionCaretItem;
using graphscore::InsertionCaretSet;
using graphscore::KeySignature;
using graphscore::layout_notation;
using graphscore::Letter;
using graphscore::make_chord;
using graphscore::make_dynamic_marking;
using graphscore::make_hairpin;
using graphscore::make_note;
using graphscore::make_pedal_span;
using graphscore::make_rest;
using graphscore::make_slur;
using graphscore::MarkingItem;
using graphscore::MarkingKind;
using graphscore::MarkingSet;
using graphscore::Measure;
using graphscore::MidiChannel;
using graphscore::Node;
using graphscore::NodeId;
using graphscore::NodeTimeline;
using graphscore::NotationEntityId;
using graphscore::NotationId;
using graphscore::NotationLayout;
using graphscore::NotationLayoutOptions;
using graphscore::NotationPoint;
using graphscore::NotationRect;
using graphscore::Note;
using graphscore::NoteheadItem;
using graphscore::NoteheadSet;
using graphscore::NotePaletteEntryKind;
using graphscore::NotePaletteState;
using graphscore::NoteValue;
using graphscore::PedalSpan;
using graphscore::Project;
using graphscore::ProjectId;
using graphscore::Rational;
using graphscore::resolve_selection_at;
using graphscore::Rest;
using graphscore::RestItem;
using graphscore::RestSet;
using graphscore::Selection;
using graphscore::Slur;
using graphscore::SpelledPitch;
using graphscore::StaffLayout;
using graphscore::StaveId;
using graphscore::TimeSignature;
using graphscore::TrackId;
using graphscore::TupletRatio;
using graphscore::validate_selection;
using graphscore::Voice;
using graphscore::VoiceContent;

class FixedMetrics final : public GlyphMetrics {
 public:
  [[nodiscard]] GlyphMetricsValue glyph_metrics(
      char32_t /*code_point*/, double staff_space) const override {
    return GlyphMetricsValue{
        NotationRect{-staff_space * 0.25, -staff_space * 0.5, staff_space * 1.5,
                     staff_space * 2.0},
        staff_space * 1.5};
  }

  [[nodiscard]] double kerning(char32_t /*left*/, char32_t /*right*/,
                               double /*staff_space*/) const override {
    return 0.0;
  }
};

[[nodiscard]] Measure measure(std::uint8_t  numerator   = 4,
                              std::uint16_t denominator = 4) {
  return Measure{*TimeSignature::create(numerator, denominator),
                 KeySignature{}};
}

struct Fixture {
  Project              project{ProjectId::generate(), "Resolver"};
  NodeId               node_id;
  std::vector<TrackId> track_ids;

  explicit Fixture(std::vector<StaffLayout> layouts,
                   std::size_t              measure_count) {
    std::vector<graphscore::StaveDefinition> staves;
    std::uint8_t                             channel = 0;
    for (StaffLayout& layout : layouts) {
      for (const auto& stave : layout.staves()) {
        staves.push_back(stave);
      }
      const auto added = project.add_track("Track", std::move(layout),
                                           *MidiChannel::create(channel));
      EXPECT_TRUE(added.has_value());
      track_ids.push_back(*added);
      ++channel;
    }
    node_id = project.add_node("Node");
    for (std::size_t track = 0; track < project.active_tracks().size();
         ++track) {
      auto* lane = project.find_node(node_id)->lane(track_ids[track]);
      for (const auto& stave :
           project.active_tracks()[track].layout().staves()) {
        lane->ensure_stave(stave.id);
      }
    }
    std::vector<Measure> measures(measure_count, measure());
    auto timeline = NodeTimeline::create(std::move(measures), staves);
    EXPECT_TRUE(timeline.has_value());
    project.find_node(node_id)->set_timeline(std::move(*timeline));
  }

  explicit Fixture(std::size_t measure_count, Clef clef = Clef::kTreble)
      : Fixture(std::vector<StaffLayout>{StaffLayout::single_staff(clef)},
                measure_count) {}

  [[nodiscard]] StaveId stave_id(std::size_t track_index = 0,
                                 std::size_t stave_index = 0) const {
    return project.active_tracks()[track_index]
        .layout()
        .staves()[stave_index]
        .id;
  }

  [[nodiscard]] VoiceContent& voice(std::uint8_t voice_index = 1,
                                    std::size_t  track_index = 0,
                                    std::size_t  stave_index = 0) {
    return project.find_node(node_id)
        ->lane(track_ids[track_index])
        ->stave(stave_id(track_index, stave_index))
        ->voice(*Voice::create(voice_index));
  }
};

[[nodiscard]] NotationLayout require_layout(
    const graphscore::NotationLayoutResult& result) {
  EXPECT_TRUE(result);
  return *result.layout;
}

[[nodiscard]] NotePaletteState note_state(std::uint8_t voice_index = 1) {
  return *NotePaletteState::create(NoteValue::kQuarter, 0,
                                   NotePaletteEntryKind::kNote,
                                   *Voice::create(voice_index));
}

// Finds a GlyphCommand by exact id suffix (e.g. "<id>/notehead",
// "<id>/articulation/0") and returns its origin -- ground truth read out of
// the real layout, never a reproduction of notation.cpp's own placement
// formulas.
[[nodiscard]] NotationPoint glyph_origin(const NotationLayout& layout,
                                         const std::string&    target) {
  const auto found =
      std::ranges::find_if(layout.commands, [&](const auto& command) {
        const auto* glyph = std::get_if<GlyphCommand>(&command);
        return glyph != nullptr && glyph->id.value == target;
      });
  EXPECT_NE(found, layout.commands.end());
  return std::get<GlyphCommand>(*found).origin;
}

[[nodiscard]] NotationPoint notehead_origin(const NotationLayout&   layout,
                                            const NotationEntityId& id) {
  return glyph_origin(layout, id.to_string() + "/notehead");
}

[[nodiscard]] NotationPoint rest_origin(const NotationLayout& layout,
                                        const Rest&           rest,
                                        std::uint8_t          voice_index = 1) {
  return glyph_origin(layout, rest.id.to_string() + "/voice/" +
                                  std::to_string(voice_index) + "/rest");
}

[[nodiscard]] NotationPoint staff_center(const NotationLayout& layout,
                                         std::size_t           staff_index = 0,
                                         std::size_t measure_index = 0) {
  const auto& staff = layout.systems[0].staves[staff_index];
  return NotationPoint{staff.measure_bounds[measure_index].x +
                           staff.measure_bounds[measure_index].width * 0.5,
                       staff.bounds.y + staff.bounds.height * 0.5};
}

// Finds the "<entity>/stem/hit" HitRegion and returns a point inside it
// that is guaranteed clear of the notehead's own (higher-priority, but
// narrower) hit region: the far end of the stem, away from the notehead.
[[nodiscard]] NotationPoint stem_click_point(const NotationLayout&   layout,
                                             const NotationEntityId& entity) {
  const std::string target = entity.to_string() + "/stem/hit";
  const auto        found  = std::ranges::find_if(
      layout.hit_regions,
      [&](const HitRegion& region) { return region.id.value == target; });
  EXPECT_NE(found, layout.hit_regions.end());
  return NotationPoint{found->bounds.x + found->bounds.width * 0.5,
                       found->bounds.y + found->bounds.height * 0.9};
}

// Finds a HitRegion by exact id (e.g. a span-family marking's own
// "<id>/<role>/segment/system-N/hit" region, which -- unlike a glyph's own
// hit region -- has no single GlyphCommand origin to read a click point
// from) and returns the center of its bounds.
[[nodiscard]] NotationPoint hit_region_center(const NotationLayout& layout,
                                              const std::string&    target) {
  const auto found = std::ranges::find_if(
      layout.hit_regions,
      [&](const HitRegion& region) { return region.id.value == target; });
  EXPECT_NE(found, layout.hit_regions.end());
  return NotationPoint{found->bounds.x + found->bounds.width * 0.5,
                       found->bounds.y + found->bounds.height * 0.5};
}

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
      layout, first.id.to_string() + "/tie/segment/system-0/hit");
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
  const NotationPoint point = hit_region_center(
      layout, first_notes[0].id.to_string() + "/tie/segment/system-0/hit");

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

// ---- HIGH-3 regression: a tuplet run spanning a system break normalizes
// to the run's true first event, not merely the event the per-system
// engraver happened to anchor the digit against (see
// docs/plan/05-notation-editor.md's cross-system tuplet hazard). ----

TEST(SelectionResolverTest,
     TupletDigitAcrossASystemBreakNormalizesToTheRunsTrueFirstEvent) {
  Fixture            fixture(2);
  const auto         ratio   = *TupletRatio::create(3, 2);
  const Duration     triplet = *Duration::create(NoteValue::kEighth, 0, ratio);
  const SpelledPitch pitch   = *SpelledPitch::create(Letter::kE, 4);
  std::vector<Note>  notes;
  // 12 eighth-note triplets exactly fill measure 0 (12 * 1/12 whole note ==
  // 1 whole note); 3 more open measure 1 with the same ratio, so the true
  // run (15 events, a whole multiple of 3) spans the barline. The
  // engraver's own per-system fragment for measure 1 prepends only measure
  // 0's own last event (notes[11]) as lookback context, so its local scan
  // anchors measure 1's digit at notes[11] -- a mid-run event, not the
  // run's true first event, notes[0].
  for (int index = 0; index < 15; ++index) {
    notes.push_back(make_note(pitch, triplet));
    ASSERT_TRUE(fixture.voice().append(notes.back()).ok());
  }

  const FixedMetrics    metrics;
  NotationLayoutOptions options;
  options.system_width        = 50.0;
  options.left_margin         = 1.0;
  options.right_margin        = 1.0;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics, options));
  ASSERT_EQ(layout.systems.size(), 2u);
  ASSERT_EQ(layout.systems[1].first_measure, 1u);

  const NotationPoint point =
      glyph_origin(layout, notes[11].id.to_string() + "/tuplet/digit/0");
  ASSERT_TRUE(layout.hit_test(point).has_value());
  EXPECT_EQ(layout.hit_test(point)->role, HitRole::kMarking);
  EXPECT_EQ(layout.hit_test(point)->semantic_id.value,
            notes[11].id.to_string());

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  ASSERT_TRUE(selection.has_value());
  const auto* set = std::get_if<MarkingSet>(&*selection);
  ASSERT_NE(set, nullptr);
  const MarkingItem& item = set->items().front();
  EXPECT_EQ(item.kind, MarkingKind::kTuplet);
  EXPECT_EQ(item.anchor, notes[0].id);
  EXPECT_NE(item.anchor, notes[11].id);
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

// ---- Stale-layout guards: a kMarking hit whose named marking can no
// longer be found, or no longer carries the shape its kind requires,
// yields std::nullopt rather than a Selection validate_selection would
// reject. ----

TEST(SelectionResolverTest,
     ADynamicMarkingRemovedAfterLayoutBuiltYieldsNoSelection) {
  Fixture    fixture(1);
  const Note note = make_note(*SpelledPitch::create(Letter::kC, 4),
                              *Duration::create(NoteValue::kQuarter, 0));
  ASSERT_TRUE(fixture.voice().append(note).ok());
  const DynamicMarking dynamic = make_dynamic_marking(note.id, Dynamic::kMf);
  ASSERT_TRUE(fixture.voice().add_dynamic(dynamic).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point =
      glyph_origin(layout, dynamic.id.to_string() + "/glyph/0");

  ASSERT_TRUE(fixture.voice().remove_dynamic(dynamic.id).ok());
  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  EXPECT_FALSE(selection.has_value());
}

TEST(SelectionResolverTest,
     AnArticulationIndexBeyondTheEventsCurrentCountYieldsNoSelection) {
  Fixture    fixture(1);
  const Note note = make_note(*SpelledPitch::create(Letter::kC, 4),
                              *Duration::create(NoteValue::kQuarter, 0), false,
                              {Articulation::kAccent});
  ASSERT_TRUE(fixture.voice().append(note).ok());

  const FixedMetrics metrics;
  NotationLayout     layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const std::string target = note.id.to_string() + "/articulation/0/hit";
  const auto        found  = std::ranges::find_if(
      layout.hit_regions,
      [&](const HitRegion& region) { return region.id.value == target; });
  ASSERT_NE(found, layout.hit_regions.end());
  const NotationPoint point{found->bounds.x + found->bounds.width * 0.5,
                            found->bounds.y + found->bounds.height * 0.5};
  found->id = NotationId{note.id.to_string() + "/articulation/9/hit"};

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  EXPECT_FALSE(selection.has_value());
}

TEST(SelectionResolverTest, ATieHitRegionOnAnUntiedNoteYieldsNoSelection) {
  Fixture    fixture(1);
  const Note note = make_note(*SpelledPitch::create(Letter::kC, 4),
                              *Duration::create(NoteValue::kQuarter, 0));
  ASSERT_TRUE(fixture.voice().append(note).ok());

  const FixedMetrics metrics;
  NotationLayout     layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point = notehead_origin(layout, note.id);
  // Fabricate a "tie segment" hit region over the note's own position,
  // naming the note as its semantic entity -- something no real emitter
  // does for an untied note (add_span_segment's tie branch only ever fires
  // when tied_to_next is set), simulating a future engraver defect.
  layout.hit_regions.push_back(
      HitRegion{NotationId{note.id.to_string() + "/tie/segment/system-0/hit"},
                NotationId{note.id.to_string()}, HitRole::kMarking,
                NotationRect{point.x - 1.0, point.y - 1.0, 2.0, 2.0}, 100});

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  EXPECT_FALSE(selection.has_value());
}

// ---- Blank-area / no-hit fallback -> InsertionCaretSet ----

TEST(SelectionResolverTest,
     ClickingBlankStaffAreaYieldsAnInsertionCaretAtTheNearestOnset) {
  Fixture        fixture(1);
  const Duration quarter = *Duration::create(NoteValue::kQuarter, 0);
  for (int index = 0; index < 4; ++index) {
    ASSERT_TRUE(fixture.voice().append(make_rest(quarter)).ok());
  }

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point = staff_center(layout);
  ASSERT_TRUE(layout.hit_test(point).has_value());
  const HitRole blank_role = layout.hit_test(point)->role;
  EXPECT_TRUE(blank_role == HitRole::kSystem ||
              blank_role == HitRole::kMeasure ||
              blank_role == HitRole::kStaff || blank_role == HitRole::kVoice);

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  ASSERT_TRUE(selection.has_value());
  const auto* caret_set = std::get_if<InsertionCaretSet>(&*selection);
  ASSERT_NE(caret_set, nullptr);
  ASSERT_EQ(caret_set->items().size(), 1u);
  const InsertionCaretItem& item = caret_set->items().front();
  EXPECT_EQ(item.node, fixture.node_id);
  EXPECT_EQ(item.track, fixture.track_ids[0]);
  EXPECT_EQ(item.stave, fixture.stave_id());
  EXPECT_EQ(item.voice, *Voice::create(1));
  // Caret legality (selection.hpp): must be an event boundary or
  // TrackLane::total_length(). A caret snapped to a raw x<->time value
  // rather than an existing onset would fail this.
  EXPECT_TRUE(fixture.voice().find_event_index_at(item.position).has_value() ||
              item.position == Rational(0));
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

TEST(SelectionResolverTest,
     NoHitAtAllStillYieldsAnInsertionCaretWhenTheStaffResolves) {
  Fixture        fixture(1);
  const Duration quarter = *Duration::create(NoteValue::kQuarter, 0);
  for (int index = 0; index < 4; ++index) {
    ASSERT_TRUE(fixture.voice().append(make_rest(quarter)).ok());
  }

  const FixedMetrics metrics;
  NotationLayout     layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point = staff_center(layout);
  layout.hit_regions.clear();
  EXPECT_FALSE(layout.hit_test(point).has_value());

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  ASSERT_TRUE(selection.has_value());
  const auto* caret_set = std::get_if<InsertionCaretSet>(&*selection);
  ASSERT_NE(caret_set, nullptr);
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

TEST(SelectionResolverTest, PointOutsideEverySystemYieldsNoSelection) {
  Fixture fixture(1);
  ASSERT_TRUE(fixture.voice()
                  .append(make_rest(*Duration::create(NoteValue::kWhole, 0)))
                  .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point{-10'000.0, -10'000.0};

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  EXPECT_FALSE(selection.has_value());
}

TEST(SelectionResolverTest, NonFinitePointYieldsNoSelection) {
  Fixture fixture(1);
  ASSERT_TRUE(fixture.voice()
                  .append(make_rest(*Duration::create(NoteValue::kWhole, 0)))
                  .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point{std::numeric_limits<double>::quiet_NaN(), 0.0};

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  EXPECT_FALSE(selection.has_value());
}

// ---- Typed-id recovery: correct track/stave/voice without a UUID parser ----

TEST(SelectionResolverTest, MultiStaffResolutionPicksTheCorrectTrackAndStave) {
  Fixture    fixture({StaffLayout::single_staff(Clef::kTreble),
                      StaffLayout::single_staff(Clef::kBass)},
                     1);
  const Note top    = make_note(*SpelledPitch::create(Letter::kC, 5),
                                *Duration::create(NoteValue::kQuarter, 0));
  const Note bottom = make_note(*SpelledPitch::create(Letter::kC, 3),
                                *Duration::create(NoteValue::kQuarter, 0));
  ASSERT_TRUE(fixture.voice(1, 0).append(top).ok());
  ASSERT_TRUE(fixture.voice(1, 1).append(bottom).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  const auto top_selection = resolve_selection_at(
      fixture.project, layout, note_state(), notehead_origin(layout, top.id));
  ASSERT_TRUE(top_selection.has_value());
  const auto* top_set = std::get_if<NoteheadSet>(&*top_selection);
  ASSERT_NE(top_set, nullptr);
  EXPECT_EQ(top_set->items().front().track, fixture.track_ids[0]);
  EXPECT_EQ(top_set->items().front().stave, fixture.stave_id(0));

  const auto bottom_selection =
      resolve_selection_at(fixture.project, layout, note_state(),
                           notehead_origin(layout, bottom.id));
  ASSERT_TRUE(bottom_selection.has_value());
  const auto* bottom_set = std::get_if<NoteheadSet>(&*bottom_selection);
  ASSERT_NE(bottom_set, nullptr);
  EXPECT_EQ(bottom_set->items().front().track, fixture.track_ids[1]);
  EXPECT_EQ(bottom_set->items().front().stave, fixture.stave_id(1));
  EXPECT_TRUE(validate_selection(fixture.project, *top_selection).empty());
  EXPECT_TRUE(validate_selection(fixture.project, *bottom_selection).empty());
}

TEST(SelectionResolverTest,
     MultiVoiceResolutionPicksTheOwningVoiceNotTheArmedOne) {
  Fixture    fixture(1);
  const Note voice_two_note =
      make_note(*SpelledPitch::create(Letter::kC, 4),
                *Duration::create(NoteValue::kQuarter, 0));
  ASSERT_TRUE(fixture.voice(2).append(voice_two_note).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  // Voice 1 is armed, but the click lands on a Voice 2 note.
  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(1),
                           notehead_origin(layout, voice_two_note.id));
  ASSERT_TRUE(selection.has_value());
  const auto* notehead_set = std::get_if<NoteheadSet>(&*selection);
  ASSERT_NE(notehead_set, nullptr);
  EXPECT_EQ(notehead_set->items().front().voice, *Voice::create(2));
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

// ---- Shared suffix constants: a hit whose id disagrees with its resolved
// entity kind is rejected rather than trusted (defends the "silent drift"
// concern the shared kHitSuffix* constants exist to prevent). ----

TEST(SelectionResolverTest,
     ANoteheadRoleHitWithAnUnrecognizedIdSuffixYieldsNoSelection) {
  Fixture    fixture(1);
  const Note note = make_note(*SpelledPitch::create(Letter::kC, 4),
                              *Duration::create(NoteValue::kQuarter, 0));
  ASSERT_TRUE(fixture.voice().append(note).ok());

  const FixedMetrics metrics;
  NotationLayout     layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point = notehead_origin(layout, note.id);
  const auto          found =
      std::ranges::find_if(layout.hit_regions, [&](const HitRegion& region) {
        return region.id.value == note.id.to_string() + "/notehead/hit";
      });
  ASSERT_NE(found, layout.hit_regions.end());
  found->id = NotationId{note.id.to_string() + "/bogus/hit"};

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  EXPECT_FALSE(selection.has_value());
}

TEST(SelectionResolverTest, AStemSuffixedHitPointingAtARestYieldsNoSelection) {
  Fixture    fixture(1);
  const Rest rest = make_rest(*Duration::create(NoteValue::kQuarter, 0));
  ASSERT_TRUE(fixture.voice().append(rest).ok());

  const FixedMetrics metrics;
  NotationLayout     layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point = rest_origin(layout, rest);
  // Fabricate a "stem" hit region over the rest glyph's own position,
  // naming the rest as its semantic entity -- something no real emitter
  // does (rests never grow stems), simulating a future engraver defect
  // that reuses the "stem" suffix incorrectly.
  layout.hit_regions.push_back(
      HitRegion{NotationId{rest.id.to_string() + "/stem/hit"},
                NotationId{rest.id.to_string()}, HitRole::kEvent,
                NotationRect{point.x - 1.0, point.y - 1.0, 2.0, 2.0}, 100});

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  EXPECT_FALSE(selection.has_value());
}

// ---- HIGH-1 regression: an insertion caret is never built at an onset the
// domain itself would reject (validate_insertion_caret_set,
// graphscore/domain/selection.cpp). ----

TEST(SelectionResolverTest,
     EmptyArmedVoiceOverMultipleMeasuresNeverYieldsAnIllegalCaret) {
  // Two measures, nothing in any voice at all: TrackLane::total_length() ==
  // 0, so the only legal caret position is 0. The hypothetical
  // measure-aligned rest fill preview_note_entry/resolve_insertion_site
  // read for an empty voice puts an onset at the start of measure 1 (== 1),
  // which is neither position 0 nor total_length() nor an existing event
  // boundary -- exactly the shape HIGH-1 found unguarded.
  Fixture fixture(2);

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point = staff_center(layout, 0, 1);

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  if (selection.has_value()) {
    EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
  }

  // Reachability proof: staff 0's second measure is not itself unresolvable
  // geometry (resolve_staff_at/resolve_measure_at rejecting this x/y) --
  // the same click into that same measure, once the armed voice actually
  // carries content there, resolves to a legal caret. That pins the
  // contract check above to the caret-legality guard this test exists for,
  // not to the click never reaching resolve_insertion_site's onset scan at
  // all.
  Fixture        reachable(2);
  const Duration quarter = *Duration::create(NoteValue::kQuarter, 0);
  for (int index = 0; index < 8; ++index) {
    ASSERT_TRUE(reachable.voice().append(make_rest(quarter)).ok());
  }
  const NotationLayout reachable_layout = require_layout(
      layout_notation(reachable.project, reachable.node_id, metrics));
  const NotationPoint reachable_point = staff_center(reachable_layout, 0, 1);
  const auto          reachable_selection = resolve_selection_at(
      reachable.project, reachable_layout, note_state(), reachable_point);
  ASSERT_TRUE(reachable_selection.has_value());
  EXPECT_TRUE(
      validate_selection(reachable.project, *reachable_selection).empty());
}

TEST(SelectionResolverTest,
     EmptyArmedVoiceAlongsideAFullSecondVoiceNeverYieldsAnIllegalCaret) {
  // Voice 1 (armed) stays entirely empty; voice 2 carries eight quarters
  // across both measures, so TrackLane::total_length() == 2 -- the
  // "ordinary start a new voice on an existing score" shape the reviewer
  // called out, not a corner case. Legality is still evaluated against the
  // armed voice's own content, not voice 2's.
  Fixture        fixture(2);
  const Duration quarter = *Duration::create(NoteValue::kQuarter, 0);
  for (int index = 0; index < 8; ++index) {
    ASSERT_TRUE(fixture.voice(2).append(make_rest(quarter)).ok());
  }

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point = staff_center(layout, 0, 1);

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(1), point);
  if (selection.has_value()) {
    EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
  }

  // Reachability proof: the identical click point resolves to a legal
  // caret when voice 2 -- the voice that actually has content there -- is
  // armed instead, so the nullopt above is the caret-legality guard
  // rejecting voice 1's empty content at this onset, not this point going
  // unresolved by resolve_staff_at/resolve_measure_at.
  const auto voice2_selection =
      resolve_selection_at(fixture.project, layout, note_state(2), point);
  ASSERT_TRUE(voice2_selection.has_value());
  EXPECT_TRUE(validate_selection(fixture.project, *voice2_selection).empty());
}

TEST(SelectionResolverTest,
     EmptyArmedVoiceStillYieldsACaretAtTheVeryFirstMeasure) {
  // The fix must not overcorrect: position 0 is always legal
  // (validate_insertion_caret_set), so the very first click into a
  // never-touched voice must still produce a usable caret.
  Fixture fixture(2);

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point = staff_center(layout, 0, 0);

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  ASSERT_TRUE(selection.has_value());
  const auto* caret_set = std::get_if<InsertionCaretSet>(&*selection);
  ASSERT_NE(caret_set, nullptr);
  EXPECT_EQ(caret_set->items().front().position, Rational(0));
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

// ---- HIGH-2 regression: a hit's owning staff comes from resolve_hit_entity
// scanning the layout, not from re-deriving a staff from the click point --
// a ledger-line notehead legitimately falls outside resolve_staff_at's own
// bounded proximity window. ----

TEST(SelectionResolverTest,
     ALedgerLineNoteheadBeyondTheStaffProximityWindowStillResolves) {
  Fixture    fixture(1, Clef::kBass);
  const Note note = make_note(*SpelledPitch::create(Letter::kC, 6),
                              *Duration::create(NoteValue::kQuarter, 0));
  ASSERT_TRUE(fixture.voice().append(note).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point = notehead_origin(layout, note.id);
  ASSERT_TRUE(layout.hit_test(point).has_value());
  EXPECT_EQ(layout.hit_test(point)->role, HitRole::kNotehead);

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  ASSERT_TRUE(selection.has_value());
  const auto* notehead_set = std::get_if<NoteheadSet>(&*selection);
  ASSERT_NE(notehead_set, nullptr);
  ASSERT_EQ(notehead_set->items().size(), 1u);
  EXPECT_EQ(notehead_set->items().front().entity, note.id);
  EXPECT_EQ(notehead_set->items().front().track, fixture.track_ids[0]);
  EXPECT_EQ(notehead_set->items().front().stave, fixture.stave_id());
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

// ---- MEDIUM-1: a stemless (whole-note) event has no kEvent geometry, so it
// can only ever resolve to a NoteheadSet, never a ChordSet -- pins the
// documented exclusion in resolve_selection_at's own contract comment. ----

TEST(SelectionResolverTest,
     AWholeNoteChordHasNoStemGeometryAndOnlyResolvesThroughItsNotehead) {
  Fixture                      fixture(1);
  const std::vector<ChordNote> notes = {
      {NotationEntityId::generate(), *SpelledPitch::create(Letter::kC, 4),
       false},
      {NotationEntityId::generate(), *SpelledPitch::create(Letter::kE, 4),
       false},
  };
  const Chord chord =
      make_chord(*Duration::create(NoteValue::kWhole, 0), notes);
  ASSERT_TRUE(fixture.voice().append(chord).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  EXPECT_TRUE(
      std::ranges::none_of(layout.hit_regions, [&](const HitRegion& region) {
        return region.id.value == chord.id.to_string() + "/stem/hit";
      }));

  const NotationPoint point = notehead_origin(layout, notes[0].id);
  const auto          selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  ASSERT_TRUE(selection.has_value());
  const auto* notehead_set = std::get_if<NoteheadSet>(&*selection);
  ASSERT_NE(notehead_set, nullptr);
  ASSERT_EQ(notehead_set->items().size(), 1u);
  EXPECT_EQ(notehead_set->items().front().entity, notes[0].id);
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

}  // namespace
