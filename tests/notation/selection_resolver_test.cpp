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

using graphscore::ActiveTool;
using graphscore::ArbitraryRangeItem;
using graphscore::ArbitraryRangeSet;
using graphscore::Articulation;
using graphscore::build_range_highlight_rects;
using graphscore::Chord;
using graphscore::ChordItem;
using graphscore::ChordNote;
using graphscore::ChordSet;
using graphscore::Clef;
using graphscore::Duration;
using graphscore::Dynamic;
using graphscore::DynamicMarking;
using graphscore::extract_fragment;
using graphscore::FragmentExtraction;
using graphscore::FullMeasureItem;
using graphscore::FullMeasureSet;
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
using graphscore::MusicalSpan;
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
using graphscore::preview_note_entry;
using graphscore::Project;
using graphscore::ProjectId;
using graphscore::Rational;
using graphscore::resolve_measure_selection_at;
using graphscore::resolve_range_selection;
using graphscore::resolve_selection_at;
using graphscore::Rest;
using graphscore::RestItem;
using graphscore::RestSet;
using graphscore::Selection;
using graphscore::SelectionDragState;
using graphscore::Slur;
using graphscore::SpelledPitch;
using graphscore::StaffLayout;
using graphscore::StaffSystemLayout;
using graphscore::StaveDefinition;
using graphscore::StaveId;
using graphscore::SystemLayout;
using graphscore::TimeSignature;
using graphscore::TrackId;
using graphscore::TrackLane;
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

// Finds a HitRegion by exact id, or nullptr when the layout emits none --
// the ground truth for both "this region exists with these properties" and
// "this region is deliberately absent" assertions.
[[nodiscard]] const HitRegion* find_hit_region(const NotationLayout& layout,
                                               const std::string&    target) {
  const auto found = std::ranges::find_if(
      layout.hit_regions,
      [&](const HitRegion& region) { return region.id.value == target; });
  return found == layout.hit_regions.end() ? nullptr : &*found;
}

// Finds a HitRegion by exact id (e.g. a span-family marking's own
// "<id>/<role>/segment/system-N/hit" region, which -- unlike a glyph's own
// hit region -- has no single GlyphCommand origin to read a click point
// from) and returns the center of its bounds.
[[nodiscard]] NotationPoint hit_region_center(const NotationLayout& layout,
                                              const std::string&    target) {
  const HitRegion* found = find_hit_region(layout, target);
  EXPECT_NE(found, nullptr);
  if (found == nullptr) {
    return NotationPoint{};
  }
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
  // The id uses the current subdivided-segment format to match the actual
  // emitter.
  layout.hit_regions.push_back(HitRegion{
      NotationId{note.id.to_string() + "/tie/segment/system-0/sub/0/hit"},
      NotationId{note.id.to_string()}, HitRole::kMarking,
      NotationRect{point.x - 1.0, point.y - 1.0, 2.0, 2.0}, 100, std::nullopt,
      std::nullopt});

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
  // staff_center lands inside this measure's own staff-measure region,
  // which outranks the coarser system/measure/staff/voice containers it
  // overlaps (HitRegion::priority), so the role is deterministically
  // kStaffMeasure -- but resolve_selection_at's own fall-through treats
  // every one of the five container roles alike, so the resulting
  // Selection below is unaffected either way.
  EXPECT_EQ(blank_role, HitRole::kStaffMeasure);

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
                NotationRect{point.x - 1.0, point.y - 1.0, 2.0, 2.0}, 100,
                std::nullopt, std::nullopt});

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

// ---- A stemless (whole-note) event draws no stem and so emits no stem hit
// region; in its place the engraver emits one kEvent "notehead-column"
// region spanning the bounding box of the event's own noteheads, which is
// what lets a whole-note chord be selected as a whole event at all.
//
// The column occupies a rank of its own on the ladder documented at
// HitRegion::priority (graphscore_notation.hpp): strictly above every
// container region, so a click in the column beats the insertion caret,
// and strictly below every region naming an engraved object, so each
// notehead and each per-notehead accidental or augmentation dot the column
// overlaps keeps selecting its own ChordNote. That rank is deliberately
// *not* the stem region's -- the stem is an engraved object and ranks with
// the other glyphs, above the column. Do not collapse the two. ----

[[nodiscard]] std::string column_hit_id(const NotationEntityId& entity) {
  return entity.to_string() + "/notehead-column/hit";
}

// A point in the vertical gap between two noteheads: inside neither
// notehead's own hit region, but inside the column region that spans both.
// Read out of the real layout, never a reproduction of notation.cpp's own
// placement formulas.
[[nodiscard]] NotationPoint notehead_gap_point(const NotationLayout&   layout,
                                               const NotationEntityId& lower,
                                               const NotationEntityId& upper) {
  const NotationPoint low  = notehead_origin(layout, lower);
  const NotationPoint high = notehead_origin(layout, upper);
  return NotationPoint{low.x, (low.y + high.y) * 0.5};
}

// A two-note chord a third apart, both noteheads on staff lines (E4 bottom
// line, G4 second line) so the gap between them lies inside the staff's own
// bounds -- the click there must beat the container hit regions, not merely
// land outside them.
[[nodiscard]] std::vector<ChordNote> two_chord_notes(
    graphscore::Accidental lower_accidental =
        graphscore::Accidental::kNatural) {
  return {
      {NotationEntityId::generate(),
       *SpelledPitch::create(Letter::kE, 4, lower_accidental), false},
      {NotationEntityId::generate(), *SpelledPitch::create(Letter::kG, 4),
       false},
  };
}

TEST(SelectionResolverTest,
     AWholeNoteChordEmitsNoStemButANoteheadColumnOverItsNoteheadsBoundingBox) {
  Fixture                      fixture(1);
  const std::vector<ChordNote> notes = two_chord_notes();
  const Chord                  chord =
      make_chord(*Duration::create(NoteValue::kWhole, 0), notes);
  ASSERT_TRUE(fixture.voice().append(chord).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  EXPECT_EQ(find_hit_region(layout, chord.id.to_string() + "/stem/hit"),
            nullptr);

  const HitRegion* lower =
      find_hit_region(layout, notes[0].id.to_string() + "/notehead/hit");
  const HitRegion* upper =
      find_hit_region(layout, notes[1].id.to_string() + "/notehead/hit");
  const HitRegion* column = find_hit_region(layout, column_hit_id(chord.id));
  ASSERT_NE(lower, nullptr);
  ASSERT_NE(upper, nullptr);
  ASSERT_NE(column, nullptr);

  EXPECT_EQ(column->role, HitRole::kEvent);
  EXPECT_EQ(column->semantic_id.value, chord.id.to_string());

  // The rank itself is swept against a layout rich enough to span the
  // ladder by the sibling test below; here only the local relationship.
  EXPECT_EQ(column->priority, 5);
  EXPECT_LT(column->priority, lower->priority);
  EXPECT_LT(column->priority, upper->priority);

  // Exactly the union of the two noteheads' own hit regions.
  const double left   = std::min(lower->bounds.x, upper->bounds.x);
  const double right  = std::max(lower->bounds.x + lower->bounds.width,
                                 upper->bounds.x + upper->bounds.width);
  const double top    = std::min(lower->bounds.y, upper->bounds.y);
  const double bottom = std::max(lower->bounds.y + lower->bounds.height,
                                 upper->bounds.y + upper->bounds.height);
  EXPECT_EQ(column->bounds.x, left);
  EXPECT_EQ(column->bounds.y, top);
  EXPECT_EQ(column->bounds.width, right - left);
  EXPECT_EQ(column->bounds.height, bottom - top);
  EXPECT_GT(column->bounds.height, lower->bounds.height);
}

// Sweeps every region of a layout deliberately built to put a
// representative of each engraved-object rank on the page beside a
// stemless chord's column, so that "the column ranks below every engraved
// object" is checked against the ladder rather than against two noteheads.
// The fixture engraves, in two measures of one voice:
//
//   * a dotted whole chord (E#4 + G4), both notes tied into
//   * a half chord on the same two pitches, and
//   * a slur and a dynamic anchored on the pair,
//
// which between them emit noteheads, an accidental per E#4, an
// augmentation dot per notehead, the half chord's stem, the dynamic's two
// character glyphs, a tie segment per tied pitch, and a slur segment --
// covering kHitPriorityGlyph, kHitPrioritySpanSegment and
// kHitPriorityNotehead. The named look-ups below assert that material is
// really present, so the sweep cannot quietly become trivial again if the
// fixture is later simplified.
TEST(SelectionResolverTest,
     TheNoteheadColumnRanksAboveEveryContainerAndBelowEveryEngravedObject) {
  Fixture                      fixture(2);
  const std::vector<ChordNote> tied = {
      {NotationEntityId::generate(),
       *SpelledPitch::create(Letter::kE, 4, graphscore::Accidental::kSharp),
       true},
      {NotationEntityId::generate(), *SpelledPitch::create(Letter::kG, 4),
       true},
  };
  const Chord first = make_chord(*Duration::create(NoteValue::kWhole, 1), tied);
  ASSERT_TRUE(fixture.voice().append(first).ok());
  const std::vector<ChordNote> target = {
      {NotationEntityId::generate(),
       *SpelledPitch::create(Letter::kE, 4, graphscore::Accidental::kSharp),
       false},
      {NotationEntityId::generate(), *SpelledPitch::create(Letter::kG, 4),
       false},
  };
  const Chord second =
      make_chord(*Duration::create(NoteValue::kHalf, 0), target);
  ASSERT_TRUE(fixture.voice().append(second).ok());
  const Slur slur = make_slur(first.id, second.id);
  ASSERT_TRUE(fixture.voice().add_slur(slur).ok());
  const DynamicMarking dynamic = make_dynamic_marking(first.id, Dynamic::kMf);
  ASSERT_TRUE(fixture.voice().add_dynamic(dynamic).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const HitRegion* column = find_hit_region(layout, column_hit_id(first.id));
  ASSERT_NE(column, nullptr);

  for (const std::string& required : {
           tied[0].id.to_string() + "/notehead/hit",
           tied[0].id.to_string() + "/accidental/column-0/hit",
           tied[0].id.to_string() + "/dot/0/hit",
           tied[1].id.to_string() + "/dot/0/hit",
           second.id.to_string() + "/stem/hit",
           dynamic.id.to_string() + "/glyph/0/hit",
           tied[0].id.to_string() + "/tie/segment/system-0/sub/4/hit",
           slur.id.to_string() + "/slur/segment/system-0/hit",
       }) {
    EXPECT_NE(find_hit_region(layout, required), nullptr) << required;
  }

  std::size_t containers = 0;
  std::size_t columns    = 0;
  std::size_t objects    = 0;
  for (const HitRegion& region : layout.hit_regions) {
    const bool container =
        region.role == HitRole::kSystem || region.role == HitRole::kMeasure ||
        region.role == HitRole::kStaff || region.role == HitRole::kVoice ||
        region.role == HitRole::kStaffMeasure;
    if (container) {
      ++containers;
      EXPECT_LT(region.priority, column->priority) << region.id.value;
    } else if (region.id.value.ends_with("/notehead-column/hit")) {
      // The rank holds notehead-column regions and nothing else; a layout
      // may of course contain several of them.
      ++columns;
      EXPECT_EQ(region.priority, column->priority) << region.id.value;
    } else {
      ++objects;
      EXPECT_GT(region.priority, column->priority) << region.id.value;
    }
  }
  // Today: 10 containers (system, two measures, staff, four voices, two
  // staff-measure regions), the one column, and 14 engraved-object regions
  // -- 7 at kHitPriorityGlyph (two
  // accidentals, two augmentation dots, the half chord's stem, the
  // dynamic's two character glyphs), 3 at kHitPrioritySpanSegment (two tie
  // segments, one slur segment) and 4 at kHitPriorityNotehead. The floor is
  // a backstop for the named look-ups above: it catches the fixture losing
  // material, while still tolerating the engraver gaining a region.
  EXPECT_GT(containers, 0u);
  EXPECT_EQ(columns, 1u);
  EXPECT_GE(objects, 14u);
}

TEST(SelectionResolverTest,
     ClickingBetweenAWholeNoteChordsNoteheadsSelectsTheWholeChord) {
  Fixture                      fixture(1);
  const std::vector<ChordNote> notes = two_chord_notes();
  const Chord                  chord =
      make_chord(*Duration::create(NoteValue::kWhole, 0), notes);
  ASSERT_TRUE(fixture.voice().append(chord).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point =
      notehead_gap_point(layout, notes[0].id, notes[1].id);

  const auto hit = layout.hit_test(point);
  ASSERT_TRUE(hit.has_value());
  EXPECT_EQ(hit->id.value, column_hit_id(chord.id));
  EXPECT_EQ(hit->role, HitRole::kEvent);

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  ASSERT_TRUE(selection.has_value());
  const auto* chord_set = std::get_if<ChordSet>(&*selection);
  ASSERT_NE(chord_set, nullptr);
  ASSERT_EQ(chord_set->items().size(), 1u);
  const ChordItem& item = chord_set->items().front();
  EXPECT_EQ(item.node, fixture.node_id);
  EXPECT_EQ(item.track, fixture.track_ids[0]);
  EXPECT_EQ(item.stave, fixture.stave_id());
  EXPECT_EQ(item.voice, *Voice::create(1));
  EXPECT_EQ(item.entity, chord.id);
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

TEST(SelectionResolverTest,
     ANoteheadOfAWholeNoteChordStillOutranksTheNoteheadColumn) {
  Fixture                      fixture(1);
  const std::vector<ChordNote> notes = two_chord_notes();
  const Chord                  chord =
      make_chord(*Duration::create(NoteValue::kWhole, 0), notes);
  ASSERT_TRUE(fixture.voice().append(chord).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  for (const ChordNote& note : notes) {
    const NotationPoint point = notehead_origin(layout, note.id);
    const auto          hit   = layout.hit_test(point);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->role, HitRole::kNotehead);

    const auto selection =
        resolve_selection_at(fixture.project, layout, note_state(), point);
    ASSERT_TRUE(selection.has_value());
    const auto* notehead_set = std::get_if<NoteheadSet>(&*selection);
    ASSERT_NE(notehead_set, nullptr);
    ASSERT_EQ(notehead_set->items().size(), 1u);
    EXPECT_EQ(notehead_set->items().front().entity, note.id);
    EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
  }
}

// The accidental and augmentation-dot regions add_glyph emits for a chord
// note carry that note's own semantic id and outrank the column, so a click
// on one selects that ChordNote however the two overlap. Neither the
// smaller-area tie-break nor the engraver's placement offsets would deliver
// that: the tie-break's outcome depends on the font's glyph metrics, and
// the offsets are measured from the clicked note's own head_x while the
// column spans every notehead's -- the clustered-seconds regressions below
// are exactly the cases where those two arguments fail.
TEST(SelectionResolverTest,
     AWholeNoteChordNotesAccidentalStillOutranksTheNoteheadColumn) {
  Fixture                      fixture(1);
  const std::vector<ChordNote> notes =
      two_chord_notes(graphscore::Accidental::kSharp);
  const Chord chord =
      make_chord(*Duration::create(NoteValue::kWhole, 0), notes);
  ASSERT_TRUE(fixture.voice().append(chord).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  ASSERT_NE(find_hit_region(layout, column_hit_id(chord.id)), nullptr);
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

TEST(SelectionResolverTest,
     ADottedWholeNoteChordsDotStillOutranksTheNoteheadColumn) {
  // Two measures: a dotted whole is 3/2, which does not fit in one 4/4
  // measure's own length.
  Fixture                      fixture(2);
  const std::vector<ChordNote> notes = two_chord_notes();
  const Chord                  chord =
      make_chord(*Duration::create(NoteValue::kWhole, 1), notes);
  ASSERT_TRUE(fixture.voice().append(chord).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  ASSERT_NE(find_hit_region(layout, column_hit_id(chord.id)), nullptr);
  const NotationPoint point =
      glyph_origin(layout, notes[1].id.to_string() + "/dot/0");

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  ASSERT_TRUE(selection.has_value());
  const auto* notehead_set = std::get_if<NoteheadSet>(&*selection);
  ASSERT_NE(notehead_set, nullptr);
  ASSERT_EQ(notehead_set->items().size(), 1u);
  EXPECT_EQ(notehead_set->items().front().entity, notes[1].id);
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

// Scope containment: a stemmed chord already has a kEvent region (its stem),
// so it gets no column region, and a click in the gap between its noteheads
// keeps resolving exactly as it does without this feature -- through a
// container region (today, the staff-measure region, which outranks the
// coarser system/measure/staff/voice containers it overlaps), to an
// insertion caret.
TEST(SelectionResolverTest, AStemmedChordEmitsNoNoteheadColumnRegion) {
  Fixture                      fixture(1);
  const std::vector<ChordNote> notes = two_chord_notes();
  const Chord                  chord =
      make_chord(*Duration::create(NoteValue::kQuarter, 0), notes);
  ASSERT_TRUE(fixture.voice().append(chord).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  EXPECT_EQ(find_hit_region(layout, column_hit_id(chord.id)), nullptr);
  EXPECT_NE(find_hit_region(layout, chord.id.to_string() + "/stem/hit"),
            nullptr);

  const NotationPoint point =
      notehead_gap_point(layout, notes[0].id, notes[1].id);
  const auto hit = layout.hit_test(point);
  ASSERT_TRUE(hit.has_value());
  EXPECT_EQ(hit->role, HitRole::kStaffMeasure);

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  ASSERT_TRUE(selection.has_value());
  const auto* caret_set = std::get_if<InsertionCaretSet>(&*selection);
  ASSERT_NE(caret_set, nullptr);
  ASSERT_EQ(caret_set->items().size(), 1u);
  EXPECT_EQ(caret_set->items().front().position, Rational(0));
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

// ---- Clustered seconds: the cases that rule out separating the column
// from the per-notehead glyph regions by geometry. Two engraver rules
// displace a notehead horizontally -- the seconds rule (space * 0.75) and
// the voice-collision rule (space * 0.22) -- and the column spans every
// displaced head_x while an accidental/dot offset is measured from its own
// note's head_x. Either rule can therefore carry the column out past the
// glyph, putting the glyph's exact origin inside the column. Each of these
// asserts that containment before asserting the selection, so the case
// cannot silently stop being a regression test if placement ever changes.
// ----

// An adjacent-second dyad. The second-listed pitch is displaced by the
// seconds rule, away from the stem: right in a stem-down voice, left in a
// stem-up one.
[[nodiscard]] std::vector<ChordNote> clustered_chord_notes(
    Letter                 upper_letter,
    graphscore::Accidental lower_accidental = graphscore::Accidental::kNatural,
    graphscore::Accidental upper_accidental =
        graphscore::Accidental::kNatural) {
  return {
      {NotationEntityId::generate(),
       *SpelledPitch::create(Letter::kE, 4, lower_accidental), false},
      {NotationEntityId::generate(),
       *SpelledPitch::create(upper_letter, 4, upper_accidental), false},
  };
}

void expect_selects_only(const Fixture& fixture, const NotationLayout& layout,
                         const HitRegion* column, NotationPoint point,
                         const NotationEntityId& expected) {
  ASSERT_NE(column, nullptr);
  EXPECT_TRUE(column->bounds.contains(point));

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  ASSERT_TRUE(selection.has_value());
  const auto* notehead_set = std::get_if<NoteheadSet>(&*selection);
  ASSERT_NE(notehead_set, nullptr);
  ASSERT_EQ(notehead_set->items().size(), 1u);
  EXPECT_EQ(notehead_set->items().front().entity, expected);
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

TEST(SelectionResolverTest,
     AStemDownClusteredChordsDisplacedAccidentalInsideTheColumnStillWins) {
  Fixture fixture(1);
  // Voice 2 is a stem-down voice, so the seconds rule displaces F#4 to the
  // right and its accidental lands near the column's own centre.
  const std::vector<ChordNote> notes =
      clustered_chord_notes(Letter::kF, graphscore::Accidental::kNatural,
                            graphscore::Accidental::kSharp);
  const Chord chord =
      make_chord(*Duration::create(NoteValue::kWhole, 0), notes);
  ASSERT_TRUE(fixture.voice(2).append(chord).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point =
      glyph_origin(layout, notes[1].id.to_string() + "/accidental/column-0");
  expect_selects_only(fixture, layout,
                      find_hit_region(layout, column_hit_id(chord.id)), point,
                      notes[1].id);
}

TEST(SelectionResolverTest,
     AStemUpClusteredChordsAccidentalInsideTheColumnStillWins) {
  Fixture fixture(1);
  // Voice 1 is stem-up, so the seconds rule displaces F4 to the *left*,
  // widening the column past E#4's own accidental instead.
  const std::vector<ChordNote> notes =
      clustered_chord_notes(Letter::kF, graphscore::Accidental::kSharp);
  const Chord chord =
      make_chord(*Duration::create(NoteValue::kWhole, 0), notes);
  ASSERT_TRUE(fixture.voice().append(chord).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point =
      glyph_origin(layout, notes[0].id.to_string() + "/accidental/column-0");
  expect_selects_only(fixture, layout,
                      find_hit_region(layout, column_hit_id(chord.id)), point,
                      notes[0].id);
}

TEST(SelectionResolverTest, AClusteredDottedChordsDotInsideTheColumnStillWins) {
  // Two measures: a dotted whole is 3/2, which does not fit in one 4/4
  // measure's own length.
  Fixture                      fixture(2);
  const std::vector<ChordNote> notes = clustered_chord_notes(Letter::kF);
  const Chord                  chord =
      make_chord(*Duration::create(NoteValue::kWhole, 1), notes);
  ASSERT_TRUE(fixture.voice(2).append(chord).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point =
      glyph_origin(layout, notes[0].id.to_string() + "/dot/0");
  expect_selects_only(fixture, layout,
                      find_hit_region(layout, column_hit_id(chord.id)), point,
                      notes[0].id);
}

// The tie hit region is now subdivided into 8 segments, each bound to
// the local curve extent rather than a single rectangle spanning the
// whole envelope (add_span_segment, kHitRoleTie branch in
// src/notation/notation.cpp).  With both notes tied in a close-voiced
// stemless chord (E4 + G4, a third apart on adjacent staff lines), each
// tie's sub-segment rects near the endpoints stay close to the lane --
// below the notehead y + space where the tie is drawn -- so the gap
// between the two tie bands at the column centre remains clear.
// A direct click on the tie curve itself still selects the tie (tested
// separately below).
TEST(SelectionResolverTest,
     ATiedWholeNoteChordsGapClickReachesTheChordAwayFromTheTieCurve) {
  Fixture fixture(2);
  // E4 (bottom line) + G4 (second line in treble clef) -- a third apart,
  // both tied -- the original close-voiced case the single-rectangle tie
  // region used to shadow.
  const std::vector<ChordNote> tied = {
      {NotationEntityId::generate(), *SpelledPitch::create(Letter::kE, 4),
       true},
      {NotationEntityId::generate(), *SpelledPitch::create(Letter::kG, 4),
       true},
  };
  const Chord first = make_chord(*Duration::create(NoteValue::kWhole, 0), tied);
  ASSERT_TRUE(fixture.voice().append(first).ok());
  const Chord second =
      make_chord(*Duration::create(NoteValue::kWhole, 0), two_chord_notes());
  ASSERT_TRUE(fixture.voice().append(second).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const HitRegion* column = find_hit_region(layout, column_hit_id(first.id));
  ASSERT_NE(column, nullptr);

  // Click at the column's own centre -- the gap between the two noteheads
  // (E4 at staff line, G4 one line above).  The subdivided tie regions are
  // tight to the actual cubic curves: near the endpoints (t ≈ 0 and t ≈ 1),
  // the curve y is close to lane = pitch_y + space, so the per-segment
  // rects for the sub/0 and sub/7 segments extend only ~0.2*space below
  // lane, nowhere near the column centre.  The column therefore wins the
  // hit at priority 5, and even though the tie regions outrank it
  // (priority 7), none of them cover this point.
  const NotationPoint point{column->bounds.x + column->bounds.width * 0.5,
                            column->bounds.y + column->bounds.height * 0.5};
  EXPECT_TRUE(column->bounds.contains(point));

  const auto hit = layout.hit_test(point);
  ASSERT_TRUE(hit.has_value());
  EXPECT_EQ(hit->role, HitRole::kEvent);

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  ASSERT_TRUE(selection.has_value());
  const auto* chord_set = std::get_if<ChordSet>(&*selection);
  ASSERT_NE(chord_set, nullptr);
  ASSERT_EQ(chord_set->items().size(), 1u);
  EXPECT_EQ(chord_set->items().front().entity, first.id);
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

// A stemless single Note emits a column too, coinciding exactly with its
// sole notehead's region and fully shadowed by it -- the header states this;
// nothing tested it.
TEST(SelectionResolverTest,
     AStemlessSingleNotesColumnCoincidesWithAndLosesToItsNotehead) {
  Fixture    fixture(1);
  const Note note = make_note(*SpelledPitch::create(Letter::kE, 4),
                              *Duration::create(NoteValue::kWhole, 0));
  ASSERT_TRUE(fixture.voice().append(note).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const HitRegion* column = find_hit_region(layout, column_hit_id(note.id));
  const HitRegion* head =
      find_hit_region(layout, note.id.to_string() + "/notehead/hit");
  ASSERT_NE(column, nullptr);
  ASSERT_NE(head, nullptr);
  EXPECT_EQ(column->bounds, head->bounds);
  EXPECT_LT(column->priority, head->priority);

  const NotationPoint point = notehead_origin(layout, note.id);
  const auto          hit   = layout.hit_test(point);
  ASSERT_TRUE(hit.has_value());
  EXPECT_EQ(hit->role, HitRole::kNotehead);

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  ASSERT_TRUE(selection.has_value());
  const auto* notehead_set = std::get_if<NoteheadSet>(&*selection);
  ASSERT_NE(notehead_set, nullptr);
  ASSERT_EQ(notehead_set->items().size(), 1u);
  EXPECT_EQ(notehead_set->items().front().entity, note.id);
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

// ---- Defect Family 1 (M5-phase-16h): a tie's tightened hit region no
// longer shadows articulation glyphs on the same chord away from the
// actually drawn tie curve. ----

TEST(SelectionResolverTest,
     AnArticulationOnATiedChordOutranksTheTieRegionAwayFromTheCurve) {
  // Two measures: a dotted whole is 3/2 > 4/4.
  Fixture            fixture(2);
  const SpelledPitch tied_pitch = *SpelledPitch::create(Letter::kC, 4);
  const Note         first =
      make_note(tied_pitch, *Duration::create(NoteValue::kWhole, 1), true,
                {Articulation::kAccent});
  ASSERT_TRUE(fixture.voice().append(first).ok());
  const Note second =
      make_note(tied_pitch, *Duration::create(NoteValue::kQuarter, 0));
  ASSERT_TRUE(fixture.voice().append(second).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint articulation_point =
      glyph_origin(layout, first.id.to_string() + "/articulation/0");

  // The articulation glyph sits above the notehead; the tightened tie band
  // no longer reaches it.
  ASSERT_TRUE(layout.hit_test(articulation_point).has_value());
  EXPECT_EQ(layout.hit_test(articulation_point)->role, HitRole::kMarking);

  const auto selection = resolve_selection_at(fixture.project, layout,
                                              note_state(), articulation_point);
  ASSERT_TRUE(selection.has_value());
  const auto* set = std::get_if<MarkingSet>(&*selection);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 1u);
  EXPECT_EQ(set->items().front().kind, MarkingKind::kArticulation);
  ASSERT_TRUE(set->items().front().articulation.has_value());
  EXPECT_EQ(*set->items().front().articulation, Articulation::kAccent);
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

// ---- The tightened tie region still selects the tie itself when the
// click lands directly on the drawn tie curve. ----

TEST(SelectionResolverTest, AClickOnTheTieCurveItselfStillSelectsTheTie) {
  Fixture            fixture(2);
  const SpelledPitch pitch = *SpelledPitch::create(Letter::kC, 4);
  const Note         first =
      make_note(pitch, *Duration::create(NoteValue::kWhole, 0), true);
  const Note second =
      make_note(pitch, *Duration::create(NoteValue::kQuarter, 0));
  ASSERT_TRUE(fixture.voice().append(first).ok());
  ASSERT_TRUE(fixture.voice().append(second).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  // The subdivided tie's middle sub-segment (sub/4) covers the curve apex,
  // the deepest point of the arch -- well away from both noteheads where
  // the curve is closest to the lane.  Clicking at the centre of that
  // sub-segment's hit region is a click on the actual drawn curve.
  const std::string tie_hit_id =
      first.id.to_string() + "/tie/segment/system-0/sub/4/hit";
  const NotationPoint point = hit_region_center(layout, tie_hit_id);
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
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

// Clicking a point inside the single-segment old-rectangle formula but
// away from every actual subdivided tie sub-segment does not select the tie
// -- the subdivided curve rects are tight to the actual bezier, so the old
// universal band is reachable through the column or a container.
TEST(SelectionResolverTest,
     APointInsideGlobalTieEnvelopeButAwayFromTheLocalCurveDoesNotSelectTheTie) {
  Fixture            fixture(2);
  const SpelledPitch pitch = *SpelledPitch::create(Letter::kC, 4);
  const Note         first =
      make_note(pitch, *Duration::create(NoteValue::kWhole, 0), true);
  const Note second =
      make_note(pitch, *Duration::create(NoteValue::kQuarter, 0));
  ASSERT_TRUE(fixture.voice().append(first).ok());
  ASSERT_TRUE(fixture.voice().append(second).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint notehead = notehead_origin(layout, first.id);

  // A point near the start notehead's x, at the full arch depth
  // (the apex is only reached at the midpoint, not near the endpoints).
  const double        space = 10.0;  // default staff_space in FixedMetrics
  const NotationPoint point{notehead.x + space * 0.5, notehead.y - space * 0.8};

  // 1. Prove the point is inside the old single-rectangle envelope formula.
  // The old non-subdivided tie hit region was a single rectangle:
  //   x = min(from.x, to.x)
  //   y = lane - 2 * space   where lane = pitch_y + space
  //   width = abs(to.x - from.x)
  //   height = 4 * space
  // For C4 in treble clef with space=10: lane ≈ 40 + 10 = 50, so the
  // rectangle extends y ∈ [30, 70].
  const double       from_x     = notehead_origin(layout, first.id).x;
  const double       to_x       = notehead_origin(layout, second.id).x;
  const double       old_left   = std::min(from_x, to_x);
  const double       old_top    = notehead.y + space - space * 2.0;
  const double       old_width  = std::abs(to_x - from_x);
  const double       old_height = space * 4.0;
  const NotationRect old_envelope{old_left, old_top, old_width, old_height};
  EXPECT_TRUE(old_envelope.contains(point));

  // 2. Prove the point is outside every subdivided tie sub-segment hit
  // region (it is near the start x, far vertical from the local curve).
  for (const HitRegion& region : layout.hit_regions) {
    if (region.role == HitRole::kMarking &&
        region.id.value.starts_with(first.id.to_string() + "/tie/segment/")) {
      EXPECT_FALSE(region.bounds.contains(point))
          << "point inside tie sub-region " << region.id.value;
    }
  }

  // 3. The point must not resolve to a tie selection.
  const auto hit = layout.hit_test(point);
  if (hit.has_value()) {
    EXPECT_NE(hit->role, HitRole::kMarking);
  }
  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  if (selection.has_value()) {
    const auto* set = std::get_if<MarkingSet>(&*selection);
    if (set != nullptr) {
      ASSERT_FALSE(set->items().empty());
      EXPECT_NE(set->items().front().kind, MarkingKind::kTie);
    }
  }
}

// ---- Defect Family 3 (M5-phase-16h) tie geometry: short tie segment
// (small dx) subdivision, and per-system clipping assertions for ties
// spanning a system break.  The subdivided curve rects must be strictly
// within the owning system's bounds. ----

TEST(SelectionResolverTest, ShortTieSubSegmentsAllClipToSystemBounds) {
  // Two notes very close together (small dx): the tie's cubic arch is
  // shallow, so each sub-segment rect is short and near the lane.  Every
  // sub-segment must be strictly within the system bounds.
  Fixture            fixture(1);
  const SpelledPitch pitch = *SpelledPitch::create(Letter::kE, 4);
  const Note         first =
      make_note(pitch, *Duration::create(NoteValue::kEighth, 0), true);
  const Note second =
      make_note(pitch, *Duration::create(NoteValue::kEighth, 0));
  ASSERT_TRUE(fixture.voice().append(first).ok());
  ASSERT_TRUE(fixture.voice().append(second).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  ASSERT_EQ(layout.systems.size(), 1u);
  const NotationRect sys_bounds = layout.systems[0].bounds;

  // Count and verify every tie sub-segment hit region.
  std::size_t sub_count = 0;
  for (const HitRegion& region : layout.hit_regions) {
    if (!region.id.value.starts_with(first.id.to_string() +
                                     "/tie/segment/system-0/sub/")) {
      continue;
    }
    ++sub_count;
    // Each sub-segment rect must be finite and within system bounds.
    EXPECT_GE(region.bounds.x, sys_bounds.x) << "sub " << sub_count - 1;
    EXPECT_GE(region.bounds.y, sys_bounds.y) << "sub " << sub_count - 1;
    EXPECT_LE(region.bounds.x + region.bounds.width,
              sys_bounds.x + sys_bounds.width)
        << "sub " << sub_count - 1;
    EXPECT_LE(region.bounds.y + region.bounds.height,
              sys_bounds.y + sys_bounds.height)
        << "sub " << sub_count - 1;
    EXPECT_GT(region.bounds.width, 0.0) << "sub " << sub_count - 1;
    EXPECT_GT(region.bounds.height, 0.0) << "sub " << sub_count - 1;
  }
  // Exactly 8 sub-segments for the subdivided curve.
  EXPECT_EQ(sub_count, 8u);

  // The tie must be selectable: click the middle sub-segment's centre.
  const NotationPoint point = hit_region_center(
      layout, first.id.to_string() + "/tie/segment/system-0/sub/4/hit");
  const auto hit = layout.hit_test(point);
  ASSERT_TRUE(hit.has_value());
  EXPECT_EQ(hit->role, HitRole::kMarking);

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  ASSERT_TRUE(selection.has_value());
  const auto* set = std::get_if<MarkingSet>(&*selection);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 1u);
  EXPECT_EQ(set->items().front().kind, MarkingKind::kTie);
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

TEST(SelectionResolverTest, LongTieSubSegmentsAllClipToSystemBounds) {
  // Two whole notes across a wide span (nearly the full system width):
  // the tie is long so each sub-segment covers a distinct x range.  The
  // global envelope extends far from the actual curve at the endpoints;
  // the subdivided rects must be visibly narrower near the endpoints.
  Fixture            fixture(1);
  const SpelledPitch pitch = *SpelledPitch::create(Letter::kE, 4);
  const Note         first =
      make_note(pitch, *Duration::create(NoteValue::kWhole, 0), true);
  const Note second = make_note(pitch, *Duration::create(NoteValue::kWhole, 0));
  ASSERT_TRUE(fixture.voice().append(first).ok());
  ASSERT_TRUE(fixture.voice().append(second).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  ASSERT_EQ(layout.systems.size(), 1u);
  const NotationRect sys_bounds = layout.systems[0].bounds;

  // Count tie sub-segments and verify each is within system bounds.
  std::size_t sub_count = 0;
  for (const HitRegion& region : layout.hit_regions) {
    if (!region.id.value.starts_with(first.id.to_string() +
                                     "/tie/segment/system-0/sub/")) {
      continue;
    }
    ++sub_count;
    // Each sub-segment rect must be within all four system bounds and
    // have positive dimensions.
    EXPECT_GE(region.bounds.x, sys_bounds.x) << "sub " << sub_count - 1;
    EXPECT_GE(region.bounds.y, sys_bounds.y) << "sub " << sub_count - 1;
    EXPECT_LE(region.bounds.x + region.bounds.width,
              sys_bounds.x + sys_bounds.width)
        << "sub " << sub_count - 1;
    EXPECT_LE(region.bounds.y + region.bounds.height,
              sys_bounds.y + sys_bounds.height)
        << "sub " << sub_count - 1;
    EXPECT_GT(region.bounds.width, 0.0) << "sub " << sub_count - 1;
    EXPECT_GT(region.bounds.height, 0.0) << "sub " << sub_count - 1;
  }
  EXPECT_EQ(sub_count, 8u);

  // The tie is selectable at the middle of the curve.  Assert exact
  // MarkingKind and anchor, not merely validate_selection success.
  const NotationPoint point = hit_region_center(
      layout, first.id.to_string() + "/tie/segment/system-0/sub/4/hit");
  const auto hit = layout.hit_test(point);
  ASSERT_TRUE(hit.has_value());
  EXPECT_EQ(hit->role, HitRole::kMarking);

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  ASSERT_TRUE(selection.has_value());
  const auto* set = std::get_if<MarkingSet>(&*selection);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 1u);
  EXPECT_EQ(set->items().front().kind, MarkingKind::kTie);
  EXPECT_EQ(set->items().front().anchor, first.id);
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

TEST(SelectionResolverTest,
     CrossSystemTieHitRegionsAreClippedToEachOwnedSystem) {
  // A tie crossing a system break: the engraver emits separate tie
  // segments on each system, each clipped to its own system's bounds.
  // The first system's end segment must not extend past its right edge,
  // and the second system's start segment must not start before its
  // left edge.
  Fixture            fixture(2);
  const SpelledPitch pitch = *SpelledPitch::create(Letter::kC, 4);
  // A whole note in measure 0, tied into a half note in measure 1.
  const Note first =
      make_note(pitch, *Duration::create(NoteValue::kWhole, 0), true);
  ASSERT_TRUE(fixture.voice().append(first).ok());
  const Note second = make_note(pitch, *Duration::create(NoteValue::kHalf, 0));
  ASSERT_TRUE(fixture.voice().append(second).ok());

  const FixedMetrics    metrics;
  NotationLayoutOptions options;
  options.system_width        = 50.0;
  options.left_margin         = 1.0;
  options.right_margin        = 1.0;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics, options));
  ASSERT_EQ(layout.systems.size(), 2u);

  // Verify tie segments exist on both systems and are fully clipped.
  const NotationRect sys0_bounds = layout.systems[0].bounds;
  const NotationRect sys1_bounds = layout.systems[1].bounds;
  std::size_t        sys0_segs   = 0;
  std::size_t        sys1_segs   = 0;
  for (const HitRegion& region : layout.hit_regions) {
    if (region.id.value.find("/tie/segment/system-0/sub/") !=
        std::string::npos) {
      ++sys0_segs;
      EXPECT_GE(region.bounds.x, sys0_bounds.x);
      EXPECT_GE(region.bounds.y, sys0_bounds.y);
      EXPECT_LE(region.bounds.x + region.bounds.width,
                sys0_bounds.x + sys0_bounds.width);
      EXPECT_LE(region.bounds.y + region.bounds.height,
                sys0_bounds.y + sys0_bounds.height);
      EXPECT_GT(region.bounds.width, 0.0);
      EXPECT_GT(region.bounds.height, 0.0);
    }
    if (region.id.value.find("/tie/segment/system-1/sub/") !=
        std::string::npos) {
      ++sys1_segs;
      EXPECT_GE(region.bounds.x, sys1_bounds.x);
      EXPECT_GE(region.bounds.y, sys1_bounds.y);
      EXPECT_LE(region.bounds.x + region.bounds.width,
                sys1_bounds.x + sys1_bounds.width);
      EXPECT_LE(region.bounds.y + region.bounds.height,
                sys1_bounds.y + sys1_bounds.height);
      EXPECT_GT(region.bounds.width, 0.0);
      EXPECT_GT(region.bounds.height, 0.0);
    }
  }
  EXPECT_EQ(sys0_segs, 8u);
  EXPECT_EQ(sys1_segs, 8u);

  // The tie is selectable on a representative sub-segment in each system.
  {
    const NotationPoint sys0_point = hit_region_center(
        layout, first.id.to_string() + "/tie/segment/system-0/sub/4/hit");
    const auto hit = layout.hit_test(sys0_point);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->role, HitRole::kMarking);

    const auto selection =
        resolve_selection_at(fixture.project, layout, note_state(), sys0_point);
    ASSERT_TRUE(selection.has_value());
    const auto* set = std::get_if<MarkingSet>(&*selection);
    ASSERT_NE(set, nullptr);
    ASSERT_EQ(set->items().size(), 1u);
    EXPECT_EQ(set->items().front().kind, MarkingKind::kTie);
    EXPECT_EQ(set->items().front().anchor, first.id);
    EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
  }
  {
    const NotationPoint sys1_point = hit_region_center(
        layout, first.id.to_string() + "/tie/segment/system-1/sub/3/hit");
    const auto hit = layout.hit_test(sys1_point);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->role, HitRole::kMarking);

    const auto selection =
        resolve_selection_at(fixture.project, layout, note_state(), sys1_point);
    ASSERT_TRUE(selection.has_value());
    const auto* set = std::get_if<MarkingSet>(&*selection);
    ASSERT_NE(set, nullptr);
    ASSERT_EQ(set->items().size(), 1u);
    EXPECT_EQ(set->items().front().kind, MarkingKind::kTie);
    EXPECT_EQ(set->items().front().anchor, first.id);
    EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
  }
}

// ---- Defect Family 3 (M5-phase-16h): two stemless chords in different
// voices at the same onset emit overlapping equal-area notehead-column
// regions.  hit_test's semantic_id tie-break depends on UUID ordering, so
// the result is not deterministic across IDs.  resolve_selection_at uses
// the palette's armed voice as a preference: when the armed voice owns one
// of the coincident columns, that voice's chord is selected regardless of
// which column hit_test returned.  Direct notehead/glyph hits are
// unaffected and always resolve to their actual owning voice. ----

// Helper: appends a whole-note chord with the given notes to a voice and
// returns the chord's id.
[[nodiscard]] NotationEntityId append_stemless_chord(
    Fixture& fixture, std::vector<ChordNote> notes,
    std::uint8_t voice_index = 1) {
  const Chord chord =
      make_chord(*Duration::create(NoteValue::kWhole, 0), std::move(notes));
  const NotationEntityId id = chord.id;
  EXPECT_TRUE(fixture.voice(voice_index).append(chord).ok());
  return id;
}

TEST(SelectionResolverTest,
     StemlessChordColumnPrefersArmedVoiceWhenColumnsCoincide) {
  // Two voices, each with a stemless whole-note chord at the same onset on
  // the same two pitches (E4 + G4).  Both emit notehead-column regions
  // whose bounds have the same area (same pitch span and same width, with
  // voice-collision horizontal displacement that preserves the column
  // dimensions).  A click in the overlap region can hit either column;
  // hit_test breaks the tie by area first, and when area is equal, falls
  // to semantic_id (UUID) ordering, which is not deterministic across
  // IDs.  resolve_selection_at uses the palette's armed voice as a
  // preference only among equal-priority, equal-area candidates at the
  // same staff and onset.
  Fixture fixture(1);

  const std::vector<ChordNote> v1_notes = two_chord_notes();
  const std::vector<ChordNote> v2_notes = two_chord_notes();

  const NotationEntityId voice1_id =
      append_stemless_chord(fixture, v1_notes, 1);
  const NotationEntityId voice2_id =
      append_stemless_chord(fixture, v2_notes, 2);

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  const HitRegion* col1 = find_hit_region(layout, column_hit_id(voice1_id));
  const HitRegion* col2 = find_hit_region(layout, column_hit_id(voice2_id));
  ASSERT_NE(col1, nullptr);
  ASSERT_NE(col2, nullptr);
  EXPECT_EQ(col1->priority, col2->priority);
  // Columns have equal area because they span the same pitches and have
  // the same width (voice-collision offset only moves them horizontally).
  const double area1 = col1->bounds.width * col1->bounds.height;
  const double area2 = col2->bounds.width * col2->bounds.height;
  EXPECT_DOUBLE_EQ(area1, area2);
  EXPECT_GT(col1->bounds.width, 0.0);
  EXPECT_GT(col2->bounds.width, 0.0);

  // Compute a point inside both columns: the midpoint of their
  // intersection in x, at the common vertical centre.
  const double overlap_left  = std::max(col1->bounds.x, col2->bounds.x);
  const double overlap_right = std::min(col1->bounds.x + col1->bounds.width,
                                        col2->bounds.x + col2->bounds.width);
  ASSERT_LT(overlap_left, overlap_right);
  const double        overlap_x = (overlap_left + overlap_right) * 0.5;
  const double        overlap_y = col1->bounds.y + col1->bounds.height * 0.5;
  const NotationPoint point{overlap_x, overlap_y};

  ASSERT_TRUE(col1->bounds.contains(point));
  ASSERT_TRUE(col2->bounds.contains(point));

  // Both chords are at musical onset 0: whole notes at measure start.
  // The armed-voice override verifies equal onset before swapping, so this
  // verifies the override fires only for genuinely simultaneous chords.

  // When Voice 1 is armed, the Voice 1 chord is selected -- regardless of
  // which column hit_test would return on its own UUID-based tie-break.
  {
    const auto voice1_selection =
        resolve_selection_at(fixture.project, layout, note_state(1), point);
    ASSERT_TRUE(voice1_selection.has_value());
    const auto* chord_set = std::get_if<ChordSet>(&*voice1_selection);
    ASSERT_NE(chord_set, nullptr);
    ASSERT_EQ(chord_set->items().size(), 1u);
    EXPECT_EQ(chord_set->items().front().entity, voice1_id);
    EXPECT_EQ(chord_set->items().front().voice, *Voice::create(1));
    EXPECT_TRUE(validate_selection(fixture.project, *voice1_selection).empty());
  }

  // When Voice 2 is armed, the Voice 2 chord is selected -- the same
  // assertion, verifying the armed-voice preference and not mere UUID
  // ordering.
  {
    const auto voice2_selection =
        resolve_selection_at(fixture.project, layout, note_state(2), point);
    ASSERT_TRUE(voice2_selection.has_value());
    const auto* chord_set = std::get_if<ChordSet>(&*voice2_selection);
    ASSERT_NE(chord_set, nullptr);
    ASSERT_EQ(chord_set->items().size(), 1u);
    EXPECT_EQ(chord_set->items().front().entity, voice2_id);
    EXPECT_EQ(chord_set->items().front().voice, *Voice::create(2));
    EXPECT_TRUE(validate_selection(fixture.project, *voice2_selection).empty());
  }
}

TEST(SelectionResolverTest,
     DirectNoteheadHitOnMultivoiceChordResolvesToTheOwningVoice) {
  // Direct notehead hit must still resolve to the actual owning voice, not
  // the armed voice -- even when another voice also has a coincident
  // column at the same onset.
  Fixture fixture(1);

  const Note voice1_note = make_note(*SpelledPitch::create(Letter::kC, 5),
                                     *Duration::create(NoteValue::kWhole, 0));
  const Note voice2_note = make_note(*SpelledPitch::create(Letter::kE, 4),
                                     *Duration::create(NoteValue::kWhole, 0));
  ASSERT_TRUE(fixture.voice(1).append(voice1_note).ok());
  ASSERT_TRUE(fixture.voice(2).append(voice2_note).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  // Click on voice 2's notehead while voice 1 is armed -- still gets
  // voice 2's notehead (direct notehead hit, not a column hit).
  {
    const NotationPoint point = notehead_origin(layout, voice2_note.id);
    const auto          selection =
        resolve_selection_at(fixture.project, layout, note_state(1), point);
    ASSERT_TRUE(selection.has_value());
    const auto* notehead_set = std::get_if<NoteheadSet>(&*selection);
    ASSERT_NE(notehead_set, nullptr);
    ASSERT_EQ(notehead_set->items().size(), 1u);
    EXPECT_EQ(notehead_set->items().front().entity, voice2_note.id);
    EXPECT_EQ(notehead_set->items().front().voice, *Voice::create(2));
    EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
  }

  // Click on voice 1's notehead while voice 2 is armed -- still gets
  // voice 1's notehead.
  {
    const NotationPoint point = notehead_origin(layout, voice1_note.id);
    const auto          selection =
        resolve_selection_at(fixture.project, layout, note_state(2), point);
    ASSERT_TRUE(selection.has_value());
    const auto* notehead_set = std::get_if<NoteheadSet>(&*selection);
    ASSERT_NE(notehead_set, nullptr);
    ASSERT_EQ(notehead_set->items().size(), 1u);
    EXPECT_EQ(notehead_set->items().front().entity, voice1_note.id);
    EXPECT_EQ(notehead_set->items().front().voice, *Voice::create(1));
    EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
  }
}

// ---- Defect Family 2 (M5-phase-16h): armed-voice column preference is
// correctly scoped to genuinely tied candidates only. ----

// Two overlapping columns with different areas (different pitch spans):
// the smaller-area column wins geometrically regardless of armed voice.
TEST(SelectionResolverTest,
     UnequalAreaColumnWinsEvenWhenArmedVoiceHasLargerColumn) {
  Fixture fixture(1);

  // Voice 1: wide-spaced chord (C5 + G5, a fifth) -- larger column area.
  const std::vector<ChordNote> wide_notes = {
      {NotationEntityId::generate(), *SpelledPitch::create(Letter::kC, 5),
       false},
      {NotationEntityId::generate(), *SpelledPitch::create(Letter::kG, 5),
       false},
  };
  const NotationEntityId wide_id =
      append_stemless_chord(fixture, wide_notes, 1);

  // Voice 2: close-spaced chord (C5 + E5, a third) -- smaller column area.
  const std::vector<ChordNote> narrow_notes = {
      {NotationEntityId::generate(), *SpelledPitch::create(Letter::kC, 5),
       false},
      {NotationEntityId::generate(), *SpelledPitch::create(Letter::kE, 5),
       false},
  };
  const NotationEntityId narrow_id =
      append_stemless_chord(fixture, narrow_notes, 2);

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  const HitRegion* wide_col = find_hit_region(layout, column_hit_id(wide_id));
  const HitRegion* narrow_col =
      find_hit_region(layout, column_hit_id(narrow_id));
  ASSERT_NE(wide_col, nullptr);
  ASSERT_NE(narrow_col, nullptr);

  // Narrow column has strictly smaller area.
  const double wide_area = wide_col->bounds.width * wide_col->bounds.height;
  const double narrow_area =
      narrow_col->bounds.width * narrow_col->bounds.height;
  ASSERT_LT(narrow_area, wide_area);

  // Click inside the overlap region.
  const double overlap_left =
      std::max(wide_col->bounds.x, narrow_col->bounds.x);
  const double overlap_right =
      std::min(wide_col->bounds.x + wide_col->bounds.width,
               narrow_col->bounds.x + narrow_col->bounds.width);
  ASSERT_LT(overlap_left, overlap_right);
  const NotationPoint point{
      (overlap_left + overlap_right) * 0.5,
      narrow_col->bounds.y + narrow_col->bounds.height * 0.5,
  };

  // hit_test picks the smaller-area column (Voice 2), not the Voice 1 one.
  const auto hit = layout.hit_test(point);
  ASSERT_TRUE(hit.has_value());
  EXPECT_EQ(hit->id.value, narrow_col->id.value);

  // Voice 1 armed -- the unequal-area override is suppressed; the geometric
  // winner (Voice 2) is preserved.
  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(1), point);
  ASSERT_TRUE(selection.has_value());
  const auto* chord_set = std::get_if<ChordSet>(&*selection);
  ASSERT_NE(chord_set, nullptr);
  ASSERT_EQ(chord_set->items().size(), 1u);
  EXPECT_EQ(chord_set->items().front().entity, narrow_id);
  EXPECT_EQ(chord_set->items().front().voice, *Voice::create(2));
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

// An adjacent-onset column cannot cross-select: the armed voice's column at
// a different onset should not override the hit_test winner.
TEST(SelectionResolverTest, AdjacentOnsetColumnDoesNotOverrideTheWinner) {
  Fixture fixture(2);  // two measures so we can offset onsets

  // Voice 1: whole-note chord at measure start (onset 0).
  const Note v1_note = make_note(*SpelledPitch::create(Letter::kE, 4),
                                 *Duration::create(NoteValue::kWhole, 0));
  ASSERT_TRUE(fixture.voice(1).append(v1_note).ok());

  // Voice 2: quarter rest + dotted half chord, so the chord starts at
  // onset = quarter (not measure start).
  const Rest rest = make_rest(*Duration::create(NoteValue::kQuarter, 0));
  ASSERT_TRUE(fixture.voice(2).append(rest).ok());
  const Note v2_note =
      make_note(*SpelledPitch::create(Letter::kG, 4),
                *Duration::create(NoteValue::kHalf, 1));  // dotted half
  ASSERT_TRUE(fixture.voice(2).append(v2_note).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  // Click on Voice 1's single-note stemless column (coincident with its
  // notehead).  Voice 2's note is at a different onset and has a stem
  // (half note), so it emits no column.
  const NotationPoint point = notehead_origin(layout, v1_note.id);
  const auto          hit   = layout.hit_test(point);
  ASSERT_TRUE(hit.has_value());
  EXPECT_EQ(hit->role, HitRole::kNotehead);

  // Voice 2 armed.  The Voice 1 notehead is a direct kNotehead hit, not a
  // column, so the armed-voice column override path is never entered.  The
  // result is Voice 1's notehead regardless.
  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(2), point);
  ASSERT_TRUE(selection.has_value());
  const auto* notehead_set = std::get_if<NoteheadSet>(&*selection);
  ASSERT_NE(notehead_set, nullptr);
  EXPECT_EQ(notehead_set->items().front().entity, v1_note.id);
  EXPECT_EQ(notehead_set->items().front().voice, *Voice::create(1));
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

// A column on a different staff/system cannot override the winner -- the
// staff pointer comparison in the armed-voice scan prevents cross-staff
// selection.
TEST(SelectionResolverTest, OtherStaffColumnDoesNotOverrideTheWinner) {
  Fixture fixture({StaffLayout::grand_staff()}, 1);

  // Upper staff: a stemless chord.
  const NotationEntityId upper_id =
      append_stemless_chord(fixture, two_chord_notes(), 1);
  // Lower staff: another stemless chord (Voice 1 on the lower staff).
  auto&       lower_voice = fixture.voice(1, 0, 1);
  const Chord lower_chord =
      make_chord(*Duration::create(NoteValue::kWhole, 0), two_chord_notes());
  const auto append_result = lower_voice.append(lower_chord);
  ASSERT_TRUE(append_result.ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  // Click on the upper staff's column centre.
  const HitRegion* upper_col = find_hit_region(layout, column_hit_id(upper_id));
  const HitRegion* lower_col =
      find_hit_region(layout, column_hit_id(lower_chord.id));
  ASSERT_NE(upper_col, nullptr);
  ASSERT_NE(lower_col, nullptr);
  const NotationPoint point{
      upper_col->bounds.x + upper_col->bounds.width * 0.5,
      upper_col->bounds.y + upper_col->bounds.height * 0.5};
  ASSERT_TRUE(upper_col->bounds.contains(point));
  // The lower staff's column does not contain this point -- the y is far
  // from the lower staff's own vertical span, so column override is not
  // reachable regardless of guard state.
  EXPECT_FALSE(lower_col->bounds.contains(point));

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(1), point);
  ASSERT_TRUE(selection.has_value());
  const auto* chord_set = std::get_if<ChordSet>(&*selection);
  ASSERT_NE(chord_set, nullptr);
  // The result must be the upper staff's chord -- never the lower staff's.
  EXPECT_EQ(chord_set->items().front().entity, upper_id);
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

// ---- Defect Family 3 (M5-phase-16h): adjacent-onset column override
// guard.  Two stemless chords at different onsets in different voices
// each emit a notehead-column region.  The click must resolve to the
// column that actually covers the click point, not to the armed voice's
// column at a different onset (which has the same priority/area
// coincidentally).  Unlike the existing
// AdjacentOnsetColumnDoesNotOverrideTheWinner test (which clicks a
// kNotehead region), this test clicks a column overlap point so the
// column-override path is actually entered. ----

TEST(SelectionResolverTest, AdjacentOnsetColumnsDoNotCrossOverride) {
  // Two measures, two voices, both at the same onset so that columns
  // have overlapping bounds.  Voice 1 gets a stemless chord at onset 0;
  // voice 2 gets a stemless chord shifted to a later onset (different
  // measure position), so the columns have different x positions and
  // do not both contain the midpoint click on voice 1's column.
  Fixture fixture(1);

  // Voice 1: two stemless chords at onset 0 (same pitches for both voices
  // so columns have equal area).
  const std::vector<ChordNote> notes1 = two_chord_notes();
  const NotationEntityId v1_id = append_stemless_chord(fixture, notes1, 1);

  // Voice 2: a quarter rest then a stemless chord at onset = quarter.
  const Rest rest = make_rest(*Duration::create(NoteValue::kQuarter, 0));
  ASSERT_TRUE(fixture.voice(2).append(rest).ok());
  const std::vector<ChordNote> notes2 = two_chord_notes();
  const Chord                  v2_chord =
      make_chord(*Duration::create(NoteValue::kWhole, 0), notes2);
  ASSERT_TRUE(fixture.voice(2).append(v2_chord).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  const HitRegion* col1 = find_hit_region(layout, column_hit_id(v1_id));
  const HitRegion* col2 = find_hit_region(layout, column_hit_id(v2_chord.id));
  ASSERT_NE(col1, nullptr);
  ASSERT_NE(col2, nullptr);
  // The columns have equal area (same pitches, same width) and equal
  // priority, but different x positions (different onsets).
  EXPECT_EQ(col1->priority, col2->priority);
  EXPECT_DOUBLE_EQ(col1->bounds.width * col1->bounds.height,
                   col2->bounds.width * col2->bounds.height);
  EXPECT_NE(col1->bounds.x, col2->bounds.x);

  // Click at voice 1's column centre.  Voice 2's column is at a
  // different x and does not contain this point.
  const NotationPoint point{col1->bounds.x + col1->bounds.width * 0.5,
                            col1->bounds.y + col1->bounds.height * 0.5};
  ASSERT_TRUE(col1->bounds.contains(point));
  EXPECT_FALSE(col2->bounds.contains(point));

  // Voice 2 armed.  The column override scan checks point containment:
  // voice 2's column does not contain this click point, so it cannot
  // be an alternative.  The result is voice 1's chord (the geometric
  // winner), which may differ from the armed voice.
  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(2), point);
  ASSERT_TRUE(selection.has_value());
  const auto* chord_set = std::get_if<ChordSet>(&*selection);
  ASSERT_NE(chord_set, nullptr);
  EXPECT_EQ(chord_set->items().front().entity, v1_id);
  EXPECT_NE(chord_set->items().front().voice, *Voice::create(2));
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

// genuinely tied equal-area columns.
TEST(SelectionResolverTest,
     ThreeWayColumnTieChoosesArmedVoiceAmongTiedCandidates) {
  Fixture fixture(1);

  const std::vector<ChordNote> notes = two_chord_notes();
  const NotationEntityId       v1_id = append_stemless_chord(fixture, notes, 1);
  const NotationEntityId       v2_id = append_stemless_chord(fixture, notes, 2);
  const NotationEntityId       v3_id = append_stemless_chord(fixture, notes, 3);

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  const HitRegion* col1 = find_hit_region(layout, column_hit_id(v1_id));
  const HitRegion* col2 = find_hit_region(layout, column_hit_id(v2_id));
  const HitRegion* col3 = find_hit_region(layout, column_hit_id(v3_id));
  ASSERT_NE(col1, nullptr);
  ASSERT_NE(col2, nullptr);
  ASSERT_NE(col3, nullptr);

  // Point inside all three columns.
  const double overlap_left =
      std::max({col1->bounds.x, col2->bounds.x, col3->bounds.x});
  const double overlap_right = std::min({col1->bounds.x + col1->bounds.width,
                                         col2->bounds.x + col2->bounds.width,
                                         col3->bounds.x + col3->bounds.width});
  ASSERT_LT(overlap_left, overlap_right);
  const NotationPoint point{
      (overlap_left + overlap_right) * 0.5,
      col1->bounds.y + col1->bounds.height * 0.5,
  };

  // Voice 3 armed -- its chord is selected among the three tied columns.
  {
    const auto selection =
        resolve_selection_at(fixture.project, layout, note_state(3), point);
    ASSERT_TRUE(selection.has_value());
    const auto* chord_set = std::get_if<ChordSet>(&*selection);
    ASSERT_NE(chord_set, nullptr);
    EXPECT_EQ(chord_set->items().front().entity, v3_id);
    EXPECT_EQ(chord_set->items().front().voice, *Voice::create(3));
    EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
  }

  // Voice 2 armed -- its chord is selected.
  {
    const auto selection =
        resolve_selection_at(fixture.project, layout, note_state(2), point);
    ASSERT_TRUE(selection.has_value());
    const auto* chord_set = std::get_if<ChordSet>(&*selection);
    ASSERT_NE(chord_set, nullptr);
    EXPECT_EQ(chord_set->items().front().entity, v2_id);
    EXPECT_EQ(chord_set->items().front().voice, *Voice::create(2));
    EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
  }
}

// A stale alternative whose semantic entity cannot be resolved is skipped
// rather than crashing or selecting the wrong chord.
TEST(SelectionResolverTest,
     StaleAlternativeColumnIsSkippedInArmedVoiceOverride) {
  Fixture fixture(1);

  const NotationEntityId v1_id =
      append_stemless_chord(fixture, two_chord_notes(), 1);
  const NotationEntityId v2_id =
      append_stemless_chord(fixture, two_chord_notes(), 2);

  const FixedMetrics metrics;
  NotationLayout     layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  // Inject a fake column region with a semantic_id that does not name any
  // real domain entity -- simulating a stale layout.
  const HitRegion* col1 = find_hit_region(layout, column_hit_id(v1_id));
  ASSERT_NE(col1, nullptr);

  // Copy bounds and priority before push_back, so no pointer into
  // layout.hit_regions is retained across vector mutation.
  const NotationRect col1_bounds   = col1->bounds;
  const int          col1_priority = col1->priority;

  const HitRegion stale_col{NotationId{"stale/notehead-column/hit"},
                            NotationId{"dead-beef-9999"},
                            HitRole::kEvent,
                            col1_bounds,
                            col1_priority,
                            std::nullopt,
                            std::nullopt};
  layout.hit_regions.push_back(stale_col);

  const NotationPoint point{
      col1_bounds.x + col1_bounds.width * 0.5,
      col1_bounds.y + col1_bounds.height * 0.5,
  };
  ASSERT_TRUE(col1_bounds.contains(point));
  ASSERT_TRUE(stale_col.bounds.contains(point));

  // Voice 2 armed.  The stale column coincidentally covers the point and
  // has equal area/priority with the real columns, but its semantic entity
  // cannot be resolved, so it is skipped.  The Voice 2 chord is selected
  // through the real column.
  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(2), point);
  ASSERT_TRUE(selection.has_value());
  const auto* chord_set = std::get_if<ChordSet>(&*selection);
  ASSERT_NE(chord_set, nullptr);
  ASSERT_EQ(chord_set->items().size(), 1u);
  EXPECT_EQ(chord_set->items().front().entity, v2_id);
  EXPECT_EQ(chord_set->items().front().voice, *Voice::create(2));
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

// ---- Defect Family 2 (M5-phase-16h): controlled isolation tests that
// prove each target guard (onset, staff) individually prevents an
// otherwise-eligible armed-voice override.  Each test constructs a scenario
// where every precondition except the guard under test passes -- same
// priority, equal area, containing point, resolvable chord, armed
// alternative present -- then asserts the exact winner.  The test must be
// designed so that removing the guard would change the outcome.

// Onset guard: the intended winner (lexically smaller id) at onset 0, the
// armed alternative at onset quarter.  Inject the alternative's column with
// exact equal bounds at the winner's position.  All other guards pass.
TEST(SelectionResolverTest, OnsetGuardPreventsArmedOverride) {
  Fixture     fixture(1);
  const auto  notes = two_chord_notes();
  const Chord a = make_chord(*Duration::create(NoteValue::kWhole, 0), notes);
  const Chord b = make_chord(*Duration::create(NoteValue::kWhole, 0),
                             two_chord_notes(graphscore::Accidental::kNatural));
  const bool  a_wins             = a.id.to_string() < b.id.to_string();
  const NotationEntityId win_id  = a_wins ? a.id : b.id;
  const NotationEntityId lose_id = a_wins ? b.id : a.id;

  // Winner at onset 0 (voice 1, unarmed in the default note state).
  ASSERT_TRUE(fixture.voice(1).append(a_wins ? a : b).ok());
  // Loser at onset quarter in voice 2 (after a quarter rest).
  const Rest rest = make_rest(*Duration::create(NoteValue::kQuarter, 0));
  ASSERT_TRUE(fixture.voice(2).append(rest).ok());
  ASSERT_TRUE(fixture.voice(2).append(a_wins ? b : a).ok());

  const FixedMetrics metrics;
  NotationLayout     layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  const HitRegion* win_col  = find_hit_region(layout, column_hit_id(win_id));
  const HitRegion* lose_col = find_hit_region(layout, column_hit_id(lose_id));
  ASSERT_NE(win_col, nullptr);
  ASSERT_NE(lose_col, nullptr);
  EXPECT_EQ(win_col->priority, lose_col->priority);
  EXPECT_DOUBLE_EQ(win_col->bounds.width * win_col->bounds.height,
                   lose_col->bounds.width * lose_col->bounds.height);
  EXPECT_EQ(win_col->owner_system_id, lose_col->owner_system_id);
  EXPECT_EQ(win_col->owner_staff_id, lose_col->owner_staff_id);

  const NotationPoint point{win_col->bounds.x + win_col->bounds.width * 0.5,
                            win_col->bounds.y + win_col->bounds.height * 0.5};
  ASSERT_TRUE(win_col->bounds.contains(point));

  // Verify deterministic winner: smaller semantic id column wins hit_test.
  const auto pre_hit = layout.hit_test(point);
  ASSERT_TRUE(pre_hit.has_value());
  EXPECT_EQ(pre_hit->id, win_col->id);

  // Inject a synthetic at the winner's position with the loser's semantic
  // and ownership.  Same system, staff, priority, exact equal area, point
  // contained — every guard except onset passes.
  HitRegion synthetic{NotationId{"syn/onset-guard/notehead-column/hit"},
                      lose_col->semantic_id,
                      HitRole::kEvent,
                      win_col->bounds,
                      win_col->priority,
                      lose_col->owner_system_id,
                      lose_col->owner_staff_id};
  layout.hit_regions.push_back(synthetic);

  // Voice 2 armed.  Without the onset guard this would override; the
  // onset guard keeps the winner.
  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(2), point);
  ASSERT_TRUE(selection.has_value());
  const auto* chord_set = std::get_if<ChordSet>(&*selection);
  ASSERT_NE(chord_set, nullptr);
  EXPECT_EQ(chord_set->items().front().entity, win_id);
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

// Staff guard: the intended winner (lexically smaller id) on upper staff,
// the armed alternative on lower staff.  Inject the alternative's column
// with exact equal bounds at the winner's position while retaining distinct
// owner_staff_id.  All other guards pass.
TEST(SelectionResolverTest, StaffGuardPreventsArmedOverride) {
  Fixture     fixture({StaffLayout::grand_staff()}, 1);
  const auto  notes = two_chord_notes();
  const Chord a = make_chord(*Duration::create(NoteValue::kWhole, 0), notes);
  const Chord b = make_chord(*Duration::create(NoteValue::kWhole, 0),
                             two_chord_notes(graphscore::Accidental::kNatural));
  const bool  a_wins             = a.id.to_string() < b.id.to_string();
  const NotationEntityId win_id  = a_wins ? a.id : b.id;
  const NotationEntityId lose_id = a_wins ? b.id : a.id;

  // Winner on upper staff (voice 1, stave 0).
  ASSERT_TRUE(fixture.voice(1).append(a_wins ? a : b).ok());
  // Loser on lower staff (voice 2, stave 1).
  auto& lower_voice = fixture.voice(2, 0, 1);
  ASSERT_TRUE(lower_voice.append(a_wins ? b : a).ok());

  const FixedMetrics metrics;
  NotationLayout     layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  const HitRegion* win_col  = find_hit_region(layout, column_hit_id(win_id));
  const HitRegion* lose_col = find_hit_region(layout, column_hit_id(lose_id));
  ASSERT_NE(win_col, nullptr);
  ASSERT_NE(lose_col, nullptr);
  ASSERT_TRUE(win_col->owner_staff_id.has_value());
  ASSERT_TRUE(lose_col->owner_staff_id.has_value());
  EXPECT_NE(*win_col->owner_staff_id, *lose_col->owner_staff_id);
  EXPECT_EQ(*win_col->owner_system_id, *lose_col->owner_system_id);
  EXPECT_EQ(win_col->priority, lose_col->priority);
  EXPECT_DOUBLE_EQ(win_col->bounds.width * win_col->bounds.height,
                   lose_col->bounds.width * lose_col->bounds.height);

  const NotationPoint point{win_col->bounds.x + win_col->bounds.width * 0.5,
                            win_col->bounds.y + win_col->bounds.height * 0.5};
  ASSERT_TRUE(win_col->bounds.contains(point));

  const auto pre_hit = layout.hit_test(point);
  ASSERT_TRUE(pre_hit.has_value());
  EXPECT_EQ(pre_hit->id, win_col->id);

  // Inject a synthetic at the winner's position with the loser's semantic
  // and lower-staff ownership.  Same system, priority, exact equal area,
  // point contained.  Only the staff guard differs.
  HitRegion synthetic{NotationId{"syn/staff-guard/notehead-column/hit"},
                      lose_col->semantic_id,
                      HitRole::kEvent,
                      win_col->bounds,
                      win_col->priority,
                      lose_col->owner_system_id,
                      lose_col->owner_staff_id};
  layout.hit_regions.push_back(synthetic);

  // Voice 2 armed (lower staff).  The staff guard must reject.
  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(2), point);
  ASSERT_TRUE(selection.has_value());
  const auto* chord_set = std::get_if<ChordSet>(&*selection);
  ASSERT_NE(chord_set, nullptr);
  EXPECT_EQ(chord_set->items().front().entity, win_id);
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

// A stemless chord column genuinely emitted in a later system carries
// that later SystemLayout's id and resolves correctly.  This is a direct
// integration check, not a synthetic guard-isolation test: the layout is
// produced by the real engraver with enough content to force a system
// break, and the column's owner_system_id is read from the emitted
// HitRegion rather than hand-built.
TEST(SelectionResolverTest, ColumnInLaterSystemCarriesThatSystemsId) {
  Fixture        fixture(3);
  const Duration quarter = *Duration::create(NoteValue::kQuarter, 0);
  // Fill measure 0 with enough rests to push a stemless chord to a
  // later measure, where it lands in a later system.
  for (int index = 0; index < 4; ++index) {
    ASSERT_TRUE(fixture.voice(1).append(make_rest(quarter)).ok());
  }

  const std::vector<ChordNote> notes = two_chord_notes();
  const Chord                  chord =
      make_chord(*Duration::create(NoteValue::kWhole, 0), notes);
  ASSERT_TRUE(fixture.voice(1).append(chord).ok());

  NotationLayoutOptions options;
  options.system_width = 60.0;
  options.left_margin  = 1.0;
  options.right_margin = 1.0;
  const FixedMetrics metrics;
  NotationLayout     layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics, options));
  ASSERT_GE(layout.systems.size(), 2u);

  const HitRegion* col = find_hit_region(layout, column_hit_id(chord.id));
  ASSERT_NE(col, nullptr);
  ASSERT_TRUE(col->owner_system_id.has_value());

  // The column was emitted in a later system -- verify the
  // owner_system_id matches that system's actual id and does not point
  // to system 0.
  const SystemLayout* owner_system = nullptr;
  for (const SystemLayout& sys : layout.systems) {
    if (sys.id == *col->owner_system_id) {
      owner_system = &sys;
      break;
    }
  }
  ASSERT_NE(owner_system, nullptr);
  EXPECT_NE(*col->owner_system_id, layout.systems[0].id);
  EXPECT_GE(owner_system->first_measure, 1u);

  // Click on the column and verify correct resolution.
  const NotationPoint point{col->bounds.x + col->bounds.width * 0.5,
                            col->bounds.y + col->bounds.height * 0.5};
  ASSERT_TRUE(col->bounds.contains(point));

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(1), point);
  ASSERT_TRUE(selection.has_value());
  const auto* chord_set = std::get_if<ChordSet>(&*selection);
  ASSERT_NE(chord_set, nullptr);
  EXPECT_EQ(chord_set->items().front().entity, chord.id);
  EXPECT_EQ(chord_set->items().front().track, fixture.track_ids[0]);
  EXPECT_EQ(chord_set->items().front().voice, *Voice::create(1));
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

// Forged/stale-owner guard: a synthetic column that copies the winner's
// owner_system_id/owner_staff_id but carries a semantic_id from a chord
// that actually lives on a different staff/system.  The genuine winner's
// semantic id is deterministically arranged to be lexically smaller so
// that hit_test always returns the genuine region after injection,
// removing any dependence on UUID ordering.  Owner-constrained resolution
// on a forged-winner synthetic (tested via fail-closed) is the separate
// backstop.
TEST(SelectionResolverTest, ForgedOwnerMetadataBlockedByConstrainedResolution) {
  Fixture fixture({StaffLayout::single_staff(), StaffLayout::single_staff()},
                  1);

  // Construct chords first so the lexically smaller semantic id can be
  // deterministically assigned to the genuine winner.
  const std::vector<ChordNote> upper_notes = two_chord_notes();
  const std::vector<ChordNote> other_notes =
      two_chord_notes(graphscore::Accidental::kSharp);
  const Chord upper_chord =
      make_chord(*Duration::create(NoteValue::kWhole, 0), upper_notes);
  const Chord other_chord =
      make_chord(*Duration::create(NoteValue::kWhole, 0), other_notes);

  // Deterministic assignment: lexically smaller semantic id to the
  // genuine winner (unarmed voice 1 on track 0).
  const bool upper_wins =
      upper_chord.id.to_string() < other_chord.id.to_string();
  const NotationEntityId win_id  = upper_wins ? upper_chord.id : other_chord.id;
  const NotationEntityId lose_id = upper_wins ? other_chord.id : upper_chord.id;

  // Winner on track 0 (voice 1, unarmed in this test).
  ASSERT_TRUE(
      fixture.voice(1, 0).append(upper_wins ? upper_chord : other_chord).ok());
  // Armed chord on track 1 (voice 2).
  ASSERT_TRUE(
      fixture.voice(2, 1).append(upper_wins ? other_chord : upper_chord).ok());

  const FixedMetrics metrics;
  NotationLayout     layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  ASSERT_EQ(layout.systems.size(), 1u);

  const HitRegion* win_col = find_hit_region(layout, column_hit_id(win_id));
  ASSERT_NE(win_col, nullptr);
  ASSERT_TRUE(win_col->owner_system_id.has_value());
  ASSERT_TRUE(win_col->owner_staff_id.has_value());

  const NotationPoint point{win_col->bounds.x + win_col->bounds.width * 0.5,
                            win_col->bounds.y + win_col->bounds.height * 0.5};
  ASSERT_TRUE(win_col->bounds.contains(point));

  // Copy every needed winner property before push_back, so no pointer
  // into layout.hit_regions is retained across vector mutation.
  const NotationId                win_id_copy   = win_col->id;
  const NotationRect              win_bounds    = win_col->bounds;
  const int                       win_priority  = win_col->priority;
  const std::optional<NotationId> win_owner_sys = win_col->owner_system_id;
  const std::optional<NotationId> win_owner_stf = win_col->owner_staff_id;

  // Synthetic: copies the winner's owner IDs (track 0's staff) but names
  // the losing chord as its semantic entity.  The synthetic's id prefix
  // 'z' sorts after every hex digit and letter, guaranteeing the genuine
  // winner's own column id always wins the lexicographic tie-break.
  HitRegion synthetic{NotationId{"zzz/forged-owner/notehead-column/hit"},
                      NotationId{{lose_id.to_string()}},
                      HitRole::kEvent,
                      win_bounds,
                      win_priority,
                      win_owner_sys,
                      win_owner_stf};
  layout.hit_regions.push_back(synthetic);

  // Post-injection: the genuine column wins hit_test deterministically
  // because its semantic_id is the lexically smaller of the two chord
  // ids (assigned above) and its column id sorts before 'z'.
  const auto post_hit = layout.hit_test(point);
  ASSERT_TRUE(post_hit.has_value());
  EXPECT_EQ(post_hit->id, win_id_copy);

  // Voice 2 armed (track 1).  Resolver selects the genuine winner.
  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(2), point);
  ASSERT_TRUE(selection.has_value());
  const auto* chord_set = std::get_if<ChordSet>(&*selection);
  ASSERT_NE(chord_set, nullptr);
  EXPECT_EQ(chord_set->items().front().entity, win_id);
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

// Area guard: same staff/system/onset, the intended geometric winner
// (lexically smaller id) has equal area with the armed alternative.
// Inject the alternative's column at the winner's position with width
// perturbed by std::nextafter so the area differs by < 1e-9.
// The exact-area guard preserves the winner.
TEST(SelectionResolverTest, TinyAreaDeltaPreservesWinner) {
  Fixture     fixture(1);
  const auto  notes = two_chord_notes();
  const Chord a = make_chord(*Duration::create(NoteValue::kWhole, 0), notes);
  const Chord b = make_chord(*Duration::create(NoteValue::kWhole, 0),
                             two_chord_notes(graphscore::Accidental::kNatural));
  const bool  a_wins             = a.id.to_string() < b.id.to_string();
  const NotationEntityId win_id  = a_wins ? a.id : b.id;
  const NotationEntityId lose_id = a_wins ? b.id : a.id;
  ASSERT_TRUE(fixture.voice(1).append(a_wins ? a : b).ok());
  ASSERT_TRUE(fixture.voice(2).append(a_wins ? b : a).ok());

  const FixedMetrics metrics;
  NotationLayout     layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  const HitRegion* win_col  = find_hit_region(layout, column_hit_id(win_id));
  const HitRegion* lose_col = find_hit_region(layout, column_hit_id(lose_id));
  ASSERT_NE(win_col, nullptr);
  ASSERT_NE(lose_col, nullptr);
  EXPECT_EQ(win_col->priority, lose_col->priority);
  const double win_area = win_col->bounds.width * win_col->bounds.height;
  EXPECT_DOUBLE_EQ(win_area, lose_col->bounds.width * lose_col->bounds.height);
  EXPECT_EQ(win_col->owner_system_id, lose_col->owner_system_id);
  EXPECT_EQ(win_col->owner_staff_id, lose_col->owner_staff_id);

  const double overlap_left = std::max(win_col->bounds.x, lose_col->bounds.x);
  const double overlap_right =
      std::min(win_col->bounds.x + win_col->bounds.width,
               lose_col->bounds.x + lose_col->bounds.width);
  ASSERT_LT(overlap_left, overlap_right);
  const NotationPoint point{
      (overlap_left + overlap_right) * 0.5,
      win_col->bounds.y + win_col->bounds.height * 0.5,
  };
  ASSERT_TRUE(win_col->bounds.contains(point));
  ASSERT_TRUE(lose_col->bounds.contains(point));

  const auto pre_hit = layout.hit_test(point);
  ASSERT_TRUE(pre_hit.has_value());
  EXPECT_EQ(pre_hit->id, win_col->id);

  // Copy every needed winner property before any erase/push_back, so
  // no pointer/reference/iterator into layout.hit_regions is retained
  // across vector mutation.
  const NotationId                win_id_copy    = win_col->id;
  const NotationRect              win_bounds     = win_col->bounds;
  const int                       win_priority   = win_col->priority;
  const NotationId                lose_semantic  = lose_col->semantic_id;
  const std::optional<NotationId> lose_owner_sys = lose_col->owner_system_id;
  const std::optional<NotationId> lose_owner_stf = lose_col->owner_staff_id;
  const auto                      lose_pos       = std::ranges::find_if(
      layout.hit_regions,
      [&](const HitRegion& r) { return r.id == lose_col->id; });
  ASSERT_NE(lose_pos, layout.hit_regions.end());
  layout.hit_regions.erase(lose_pos);

  // Replace it with a synthetic that has the same semantic/ownership but
  // a width perturbed by one ULP, so the area differs by < 1e-9.
  NotationRect alt_bounds = win_bounds;
  alt_bounds.width        = std::nextafter(alt_bounds.width, INFINITY);
  HitRegion synthetic{NotationId{"syn/tiny-area/notehead-column/hit"},
                      lose_semantic,
                      HitRole::kEvent,
                      alt_bounds,
                      win_priority,
                      lose_owner_sys,
                      lose_owner_stf};
  layout.hit_regions.push_back(synthetic);

  const double synth_area = alt_bounds.width * alt_bounds.height;
  EXPECT_NE(synth_area, win_area);
  EXPECT_GT(synth_area, win_area);
  const double delta = synth_area - win_area;
  EXPECT_GT(delta, 0.0);
  EXPECT_LT(delta, 1e-9);

  // Post-mutation: hit_test still returns the smaller-area winner,
  // asserted against the pre-mutation copy.
  const auto post_hit = layout.hit_test(point);
  ASSERT_TRUE(post_hit.has_value());
  EXPECT_EQ(post_hit->id, win_id_copy);

  // Voice 2 armed.  The exact-area guard rejects the synthetic; winner
  // remains unchanged.
  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(2), point);
  ASSERT_TRUE(selection.has_value());
  const auto* chord_set = std::get_if<ChordSet>(&*selection);
  ASSERT_NE(chord_set, nullptr);
  EXPECT_EQ(chord_set->items().front().entity, win_id);
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

// ---- resolve_measure_selection_at: HitRole::kStaffMeasure -> FullMeasureSet
// ----

TEST(MeasureSelectionTest, BlankMeasureClickSelectsOneFullMeasure) {
  Fixture fixture(1);

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point = staff_center(layout);

  const auto selection =
      resolve_measure_selection_at(fixture.project, layout, point);
  ASSERT_TRUE(selection.has_value());
  const auto* set = std::get_if<FullMeasureSet>(&*selection);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 1u);
  const FullMeasureItem& item = set->items().front();
  EXPECT_EQ(item.node, fixture.node_id);
  EXPECT_EQ(item.track, fixture.track_ids[0]);
  EXPECT_EQ(item.stave, fixture.stave_id());
  EXPECT_EQ(item.measure_index, 0u);
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

TEST(MeasureSelectionTest,
     GrandStaffClicksNameTheSameMeasureOnDifferentStaves) {
  Fixture fixture({StaffLayout::grand_staff()}, 1);

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint upper_point = staff_center(layout, 0, 0);
  const NotationPoint lower_point = staff_center(layout, 1, 0);

  const auto upper_selection =
      resolve_measure_selection_at(fixture.project, layout, upper_point);
  const auto lower_selection =
      resolve_measure_selection_at(fixture.project, layout, lower_point);
  ASSERT_TRUE(upper_selection.has_value());
  ASSERT_TRUE(lower_selection.has_value());
  const auto* upper_set = std::get_if<FullMeasureSet>(&*upper_selection);
  const auto* lower_set = std::get_if<FullMeasureSet>(&*lower_selection);
  ASSERT_NE(upper_set, nullptr);
  ASSERT_NE(lower_set, nullptr);
  ASSERT_EQ(upper_set->items().size(), 1u);
  ASSERT_EQ(lower_set->items().size(), 1u);
  const FullMeasureItem& upper_item = upper_set->items().front();
  const FullMeasureItem& lower_item = lower_set->items().front();
  EXPECT_EQ(upper_item.track, lower_item.track);
  EXPECT_EQ(upper_item.measure_index, lower_item.measure_index);
  EXPECT_NE(upper_item.stave, lower_item.stave);
  EXPECT_TRUE(validate_selection(fixture.project, *upper_selection).empty());
  EXPECT_TRUE(validate_selection(fixture.project, *lower_selection).empty());
}

TEST(MeasureSelectionTest, MultiTrackClickNamesTheClickedTrackNotTheFirst) {
  Fixture fixture({StaffLayout::single_staff(), StaffLayout::single_staff()},
                  1);

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point = staff_center(layout, 1, 0);

  const auto selection =
      resolve_measure_selection_at(fixture.project, layout, point);
  ASSERT_TRUE(selection.has_value());
  const auto* set = std::get_if<FullMeasureSet>(&*selection);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 1u);
  EXPECT_EQ(set->items().front().track, fixture.track_ids[1]);
  EXPECT_NE(set->items().front().track, fixture.track_ids[0]);
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

TEST(MeasureSelectionTest,
     SecondSystemClickNamesTheGlobalMeasureOrdinalNotASystemLocalIndex) {
  Fixture        fixture(3);
  const Duration quarter = *Duration::create(NoteValue::kQuarter, 0);
  for (int index = 0; index < 12; ++index) {
    ASSERT_TRUE(fixture.voice().append(make_rest(quarter)).ok());
  }

  const FixedMetrics    metrics;
  NotationLayoutOptions options;
  options.system_width        = 50.0;
  options.left_margin         = 1.0;
  options.right_margin        = 1.0;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics, options));
  ASSERT_GE(layout.systems.size(), 2u);
  const auto& second_system = layout.systems[1];
  ASSERT_FALSE(second_system.measures.empty());
  const std::size_t expected_ordinal = second_system.measures[0].ordinal;
  // A system-local index for the first measure of a non-first system would
  // be 0; the global ordinal must not be.
  ASSERT_GT(expected_ordinal, 0u);

  const auto&         staff = second_system.staves[0];
  const NotationPoint point{second_system.measures[0].bounds.x +
                                second_system.measures[0].bounds.width * 0.5,
                            staff.bounds.y + staff.bounds.height * 0.5};

  const auto selection =
      resolve_measure_selection_at(fixture.project, layout, point);
  ASSERT_TRUE(selection.has_value());
  const auto* set = std::get_if<FullMeasureSet>(&*selection);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 1u);
  EXPECT_EQ(set->items().front().measure_index, expected_ordinal);
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

TEST(MeasureSelectionTest, ClickingANoteheadYieldsNoMeasureSelection) {
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
  const auto          hit   = layout.hit_test(point);
  ASSERT_TRUE(hit.has_value());
  EXPECT_EQ(hit->role, HitRole::kNotehead);

  EXPECT_FALSE(
      resolve_measure_selection_at(fixture.project, layout, point).has_value());
}

TEST(MeasureSelectionTest, ClickingARestYieldsNoMeasureSelection) {
  Fixture    fixture(1);
  const Rest rest = make_rest(*Duration::create(NoteValue::kQuarter, 0));
  ASSERT_TRUE(fixture.voice().append(rest).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point = rest_origin(layout, rest);
  const auto          hit   = layout.hit_test(point);
  ASSERT_TRUE(hit.has_value());
  EXPECT_EQ(hit->role, HitRole::kEvent);

  EXPECT_FALSE(
      resolve_measure_selection_at(fixture.project, layout, point).has_value());
}

TEST(MeasureSelectionTest, ClickingAStemYieldsNoMeasureSelection) {
  Fixture    fixture(1);
  const Note note = make_note(*SpelledPitch::create(Letter::kC, 4),
                              *Duration::create(NoteValue::kQuarter, 0));
  ASSERT_TRUE(fixture.voice().append(note).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point = stem_click_point(layout, note.id);
  const auto          hit   = layout.hit_test(point);
  ASSERT_TRUE(hit.has_value());
  EXPECT_EQ(hit->role, HitRole::kEvent);

  EXPECT_FALSE(
      resolve_measure_selection_at(fixture.project, layout, point).has_value());
}

TEST(MeasureSelectionTest,
     ClickingAStemlessChordsNoteheadColumnYieldsNoMeasureSelection) {
  Fixture                      fixture(1);
  const std::vector<ChordNote> notes = two_chord_notes();
  const Chord                  chord =
      make_chord(*Duration::create(NoteValue::kWhole, 0), notes);
  ASSERT_TRUE(fixture.voice().append(chord).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point =
      notehead_gap_point(layout, notes[0].id, notes[1].id);
  const auto hit = layout.hit_test(point);
  ASSERT_TRUE(hit.has_value());
  EXPECT_EQ(hit->role, HitRole::kEvent);

  EXPECT_FALSE(
      resolve_measure_selection_at(fixture.project, layout, point).has_value());
}

TEST(MeasureSelectionTest, PointOutsideEverySystemYieldsNoMeasureSelection) {
  Fixture fixture(1);
  ASSERT_TRUE(fixture.voice()
                  .append(make_rest(*Duration::create(NoteValue::kWhole, 0)))
                  .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point{-10'000.0, -10'000.0};

  EXPECT_FALSE(
      resolve_measure_selection_at(fixture.project, layout, point).has_value());
}

TEST(MeasureSelectionTest, NonFinitePointYieldsNoMeasureSelection) {
  Fixture fixture(1);

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point{std::numeric_limits<double>::quiet_NaN(), 0.0};

  EXPECT_FALSE(
      resolve_measure_selection_at(fixture.project, layout, point).has_value());
}

// layout_notation only ever emits a staff-measure region for a measure
// inside the node's own NodeTimeline main region (system.measures, built
// from timeline->measures(), which is exactly the main region --
// measure_map.hpp); it never lays out the pickdown at all. So a click past
// the last drawn measure's own right edge -- spatially where the
// pickdown's own material would sit -- never names a HitRole::kStaffMeasure
// region in the first place, and resolve_measure_selection_at rejects it
// through that same role check every other non-staff-measure hit is
// rejected through, before validate_selection is even consulted.
//
// This is as close as this resolver can come to exercising
// validate_full_measure_set's own TimelineRegion::kPickdown check
// (src/domain/selection.cpp): that check can only fire for a
// FullMeasureItem::measure_index that is simultaneously < measure_count()
// (so it passes the preceding range check) and whose own measure_start is
// >= boundary_position() -- and measure_start is strictly increasing while
// bounded above by boundary_position() for every ordinal < measure_count(),
// so no such ordinal exists. The domain's own test coverage
// (SelectionTest.ValidateFullMeasureRejectsPickdownOrdinal,
// tests/domain/selection_test.cpp) hits the identical situation and
// likewise only ever observes kMeasureIndexOutOfRange, never
// kMeasureIndexInPickdown, corroborating that the pickdown-specific branch
// is unreachable from any project state today -- a pre-existing domain
// property, not something this resolver can be exercised against.
TEST(MeasureSelectionTest,
     ClickPastTheLastDrawnMeasureWherePickdownMaterialWouldSitYieldsNothing) {
  Project    project{ProjectId::generate(), "Pickdown"};
  const auto track_id = project.add_track("Track", StaffLayout::single_staff(),
                                          *MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());
  const StaveId stave_id =
      project.find_active_track(*track_id)->layout().staves()[0].id;
  const NodeId node_id = project.add_node("Node");

  std::vector<Measure> measures{measure()};
  auto                 timeline = NodeTimeline::create(
      measures, {StaveDefinition{stave_id, Clef::kTreble}});
  ASSERT_TRUE(timeline.has_value());
  ASSERT_TRUE(timeline->set_pickdown(*Rational::create(1, 4)).ok());
  const_cast<Node*>(project.find_node(node_id))
      ->set_timeline(std::move(*timeline));
  const_cast<TrackLane*>(project.find_node(node_id)->lane(*track_id))
      ->ensure_stave(stave_id);

  const FixedMetrics   metrics;
  const NotationLayout layout =
      require_layout(layout_notation(project, node_id, metrics));
  ASSERT_EQ(layout.systems.size(), 1u);
  ASSERT_EQ(layout.systems[0].measures.size(), 1u);
  const auto& staff = layout.systems[0].staves[0];
  ASSERT_EQ(staff.measure_bounds.size(), 1u);

  const NotationPoint point{
      staff.measure_bounds[0].x + staff.measure_bounds[0].width + 5.0,
      staff.bounds.y + staff.bounds.height * 0.5};
  const auto hit = layout.hit_test(point);
  ASSERT_TRUE(hit.has_value());
  EXPECT_NE(hit->role, HitRole::kStaffMeasure);

  EXPECT_FALSE(
      resolve_measure_selection_at(project, layout, point).has_value());
}

// resolve_staff_at's own generous ledger/marking lane (6 staff-spaces above
// or below a staff's own five lines) is what preview_note_entry and
// resolve_selection_at's insertion-caret arm use to attribute an
// off-stave click to the nearest staff. hit_test's own per-region bounds
// carry no such padding for the two staff-tight container roles:
// HitRole::kStaffMeasure's own bounds are exactly staff.measure_bounds'
// tight extent, and HitRole::kStaff's are staff.bounds' -- neither reaches
// into the ledger lane. kMeasure and kSystem are not staff-tight, though:
// both are built from the full system height (see layout_internal), so the
// ledger lane sits inside them, and a point there resolves to kMeasure (it
// outranks kSystem). That is still never a HitRole::kStaffMeasure region --
// resolve_measure_selection_at deliberately does not fall back to the
// nearest staff the way the insertion-caret path does: naming a whole
// measure is a more deliberate act than placing a note, and a point this
// far outside a staff's own drawn region should not silently select a
// measure on a staff the composer did not visibly click.
TEST(MeasureSelectionTest,
     LedgerLineLaneAboveTheStaffAttributesToTheStaffButNamesNoMeasure) {
  Fixture fixture(1);
  ASSERT_TRUE(fixture.voice()
                  .append(make_rest(*Duration::create(NoteValue::kWhole, 0)))
                  .ok());
  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const auto&         staff = layout.systems[0].staves[0];
  const double        space = staff.bounds.height / 4.0;
  const double        x     = layout.systems[0].measures[0].bounds.x + 10.0;
  const NotationPoint point{x, staff.bounds.y - space * 2.0};

  const auto preview =
      preview_note_entry(fixture.project, layout, note_state(), point);
  ASSERT_TRUE(preview.has_value());
  EXPECT_EQ(preview->stave_id, fixture.stave_id());

  const auto hit = layout.hit_test(point);
  ASSERT_TRUE(hit.has_value());
  EXPECT_NE(hit->role, HitRole::kStaffMeasure);
  EXPECT_FALSE(
      resolve_measure_selection_at(fixture.project, layout, point).has_value());
}

// The staff-measure rank must win by rank, not by hit_test's smaller-area or
// semantic_id tie-break, on a real layout the ordinary engraver produces --
// not on hand-built regions. A single-measure system is the case where the
// tie-break would otherwise decide: there, a staff's one staff-measure
// region and its containing kVoice region share exactly the same bounds
// (staff.bounds), so an equal or lower rank would fall through to the
// smaller-area comparison (a tie, since the areas are equal) and then to
// the ascending semantic_id comparison, which happens to still pick the
// staff-measure region today only because "staff-measure" sorts before
// "voice" lexically. This test bypasses both tie-breaks entirely by
// asserting the rank comparison directly: EXPECT_GT fails outright at
// kHitPriorityStaffMeasure == kHitPriorityVoice (3), regardless of area or
// id, so it fails specifically when the rank stops being its own.
TEST(MeasureSelectionTest,
     StaffMeasureRegionOutranksVoiceRegionOnTheSameStaffBySeparateRank) {
  Fixture fixture({StaffLayout::grand_staff()}, 1);

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  ASSERT_EQ(layout.systems.size(), 1u);
  const auto& system = layout.systems[0];
  ASSERT_EQ(system.measures.size(), 1u);
  ASSERT_EQ(system.staves.size(), 2u);

  for (const auto& staff : system.staves) {
    ASSERT_EQ(staff.measure_bounds.size(), 1u);
    ASSERT_FALSE(staff.voices.empty());

    const HitRegion* staff_measure_region = nullptr;
    for (const HitRegion& region : layout.hit_regions) {
      if (region.role == HitRole::kStaffMeasure &&
          region.bounds == staff.measure_bounds[0]) {
        staff_measure_region = &region;
        break;
      }
    }
    ASSERT_NE(staff_measure_region, nullptr);

    const HitRegion* voice_region = nullptr;
    for (const HitRegion& region : layout.hit_regions) {
      if (region.role == HitRole::kVoice &&
          region.semantic_id.value == staff.voices[0].id.value) {
        voice_region = &region;
        break;
      }
    }
    ASSERT_NE(voice_region, nullptr);

    // The precondition the tie-break case depends on: equal bounds, so an
    // equal rank would indeed reach hit_test's tie-break rather than being
    // decided by area.
    ASSERT_EQ(staff_measure_region->bounds, voice_region->bounds);
    EXPECT_GT(staff_measure_region->priority, voice_region->priority);
  }
}

// Isolation test: exercises hit_test's own priority/tie-break mechanics
// directly on two hand-built regions of exactly equal bounds and area, with
// hand-chosen ids -- it never touches the production ladder
// (kHitPriorityStaffMeasure et al.) or a real engraver-produced layout, so
// it does not by itself establish that any particular numeric rank is
// necessary there. What it does establish is the "loses to every engraved
// object" direction of the ladder: a region ranked strictly below another
// always loses the hit, regardless of area or id, while two regions of
// equal rank and area fall through to the id tie-break, where the outcome
// depends on how the ids happen to be spelled -- exactly the dependence
// the header comment on HitRegion::priority (staff-measure rank) explains
// giving the staff-measure region a rank of its own removes. The rank's
// own necessity on a real layout is established separately, by the
// production-layout test above and by the pre-existing sweep at
// TheNoteheadColumnRanksAboveEveryContainerAndBelowEveryEngravedObject.
TEST(MeasureSelectionTest,
     HitTestsPriorityTieBreakIsolatedFromProductionLadder) {
  const NotationRect shared_bounds{0.0, 0.0, 4.0, 4.0};

  // Production ladder: staff-measure (4) strictly below notehead (8).
  // Equal-area, overlapping regions -- the notehead still wins outright,
  // regardless of area or id, because its rank is strictly higher.
  NotationLayout correct;
  correct.hit_regions = {
      HitRegion{NotationId{"sm/hit"}, NotationId{"sm"}, HitRole::kStaffMeasure,
                shared_bounds, 4, std::nullopt, std::nullopt},
      HitRegion{NotationId{"note/notehead/hit"}, NotationId{"note"},
                HitRole::kNotehead, shared_bounds, 8, std::nullopt,
                std::nullopt},
  };
  const auto correct_hit = correct.hit_test(NotationPoint{2.0, 2.0});
  ASSERT_TRUE(correct_hit.has_value());
  EXPECT_EQ(correct_hit->role, HitRole::kNotehead);

  // Misranked: give the staff-measure region the notehead's own rank
  // instead (the cheaper "let the area/id tie-break sort it out"
  // alternative HitRegion::priority's own comment rules out). With equal
  // priority and equal area, hit_test falls to the semantic_id tie-break,
  // ascending -- and this pair of ids is chosen so the staff-measure
  // region's own semantic_id sorts first ("a-staff-measure" <
  // "b-notehead"). The staff-measure region now wins: exactly the wrong
  // click outcome resolve_measure_selection_at depends on the production
  // rank (strictly below every engraved-object rank) making impossible.
  NotationLayout misranked;
  misranked.hit_regions = {
      HitRegion{NotationId{"a-staff-measure/hit"},
                NotationId{"a-staff-measure"}, HitRole::kStaffMeasure,
                shared_bounds, 8, std::nullopt, std::nullopt},
      HitRegion{NotationId{"b-notehead/hit"}, NotationId{"b-notehead"},
                HitRole::kNotehead, shared_bounds, 8, std::nullopt,
                std::nullopt},
  };
  const auto misranked_hit = misranked.hit_test(NotationPoint{2.0, 2.0});
  ASSERT_TRUE(misranked_hit.has_value());
  EXPECT_EQ(misranked_hit->role, HitRole::kStaffMeasure);
}

// ---- resolve_range_selection: dedicated selection-tool pointer drag ----

// A measure's own left/right edges are exact time_at_x fixed points
// (fraction 0 and fraction 1, per time_at_x's own clamp), so a drag across
// them yields the exact measure_start/measure_start+measure_length span
// without reproducing any engraving placement formula.
[[nodiscard]] NotationPoint measure_left_edge(const NotationLayout& layout,
                                              std::size_t system_index,
                                              std::size_t staff_index,
                                              std::size_t measure_index) {
  const auto& staff   = layout.systems[system_index].staves[staff_index];
  const auto& measure = layout.systems[system_index].measures[measure_index];
  return NotationPoint{measure.bounds.x,
                       staff.bounds.y + staff.bounds.height * 0.5};
}

[[nodiscard]] NotationPoint measure_right_edge(const NotationLayout& layout,
                                               std::size_t system_index,
                                               std::size_t staff_index,
                                               std::size_t measure_index) {
  const auto& staff   = layout.systems[system_index].staves[staff_index];
  const auto& measure = layout.systems[system_index].measures[measure_index];
  return NotationPoint{measure.bounds.x + measure.bounds.width,
                       staff.bounds.y + staff.bounds.height * 0.5};
}

TEST(RangeSelectionTest,
     SingleStaffSingleVoiceDragProducesOneItemWithTheExpectedSpan) {
  Fixture        fixture(1);
  const Duration quarter = *Duration::create(NoteValue::kQuarter, 0);
  for (int index = 0; index < 4; ++index) {
    ASSERT_TRUE(
        fixture.voice()
            .append(make_note(*SpelledPitch::create(Letter::kC, 4), quarter))
            .ok());
  }

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint anchor = measure_left_edge(layout, 0, 0, 0);
  const NotationPoint focus  = measure_right_edge(layout, 0, 0, 0);

  const auto selection =
      resolve_range_selection(fixture.project, layout, anchor, focus);
  ASSERT_TRUE(selection.has_value());
  const auto* set = std::get_if<ArbitraryRangeSet>(&*selection);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 1u);
  const ArbitraryRangeItem& item = set->items().front();
  EXPECT_EQ(item.node, fixture.node_id);
  EXPECT_EQ(item.track, fixture.track_ids[0]);
  EXPECT_EQ(item.stave, fixture.stave_id());
  EXPECT_EQ(item.voice, *Voice::create(1));
  EXPECT_EQ(item.span, (MusicalSpan{Rational(0), Rational(1)}));
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

TEST(RangeSelectionTest,
     DragAcrossTwoVoicesOnOneStaveProducesTwoItemsWithIdenticalSpans) {
  Fixture        fixture(1);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice(1)
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());
  ASSERT_TRUE(
      fixture.voice(2)
          .append(make_note(*SpelledPitch::create(Letter::kG, 3), whole))
          .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint anchor = measure_left_edge(layout, 0, 0, 0);
  const NotationPoint focus  = measure_right_edge(layout, 0, 0, 0);

  const auto selection =
      resolve_range_selection(fixture.project, layout, anchor, focus);
  ASSERT_TRUE(selection.has_value());
  const auto* set = std::get_if<ArbitraryRangeSet>(&*selection);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 2u);
  const MusicalSpan  expected_span{Rational(0), Rational(1)};
  std::vector<Voice> voices;
  for (const ArbitraryRangeItem& item : set->items()) {
    EXPECT_EQ(item.span, expected_span);
    EXPECT_EQ(item.track, fixture.track_ids[0]);
    EXPECT_EQ(item.stave, fixture.stave_id());
    voices.push_back(item.voice);
  }
  std::ranges::sort(voices);
  EXPECT_EQ(voices, (std::vector<Voice>{*Voice::create(1), *Voice::create(2)}));
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

TEST(RangeSelectionTest,
     DragAcrossMultipleStavesAndTracksProducesOneItemPerContentfulVoice) {
  Fixture        fixture({StaffLayout::single_staff(Clef::kTreble),
                          StaffLayout::single_staff(Clef::kBass)},
                         1);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice(1, 0)
          .append(make_note(*SpelledPitch::create(Letter::kC, 5), whole))
          .ok());
  ASSERT_TRUE(
      fixture.voice(1, 1)
          .append(make_note(*SpelledPitch::create(Letter::kC, 3), whole))
          .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint anchor = measure_left_edge(layout, 0, 0, 0);
  const NotationPoint focus  = measure_right_edge(layout, 0, 1, 0);

  const auto selection =
      resolve_range_selection(fixture.project, layout, anchor, focus);
  ASSERT_TRUE(selection.has_value());
  const auto* set = std::get_if<ArbitraryRangeSet>(&*selection);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 2u);
  const MusicalSpan    expected_span{Rational(0), Rational(1)};
  std::vector<TrackId> tracks;
  for (const ArbitraryRangeItem& item : set->items()) {
    EXPECT_EQ(item.span, expected_span);
    EXPECT_EQ(item.voice, *Voice::create(1));
    tracks.push_back(item.track);
  }
  EXPECT_NE(std::ranges::find(tracks, fixture.track_ids[0]), tracks.end());
  EXPECT_NE(std::ranges::find(tracks, fixture.track_ids[1]), tracks.end());
  EXPECT_NE(fixture.track_ids[0], fixture.track_ids[1]);
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

TEST(RangeSelectionTest,
     VoiceContentOutsideTheDraggedSpanIsExcludedEvenThoughNonEmpty) {
  Fixture        fixture(2);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  // Voice 1 spans both measures, so its second whole note occupies
  // measure 1's own span.
  ASSERT_TRUE(
      fixture.voice(1)
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());
  ASSERT_TRUE(
      fixture.voice(1)
          .append(make_note(*SpelledPitch::create(Letter::kD, 4), whole))
          .ok());
  // Voice 2 is non-empty but stops exactly at the end of measure 0 -- its
  // one event's own extent, [0, 1), ends exactly at measure 1's own span
  // start, so it does not overlap a drag confined to measure 1.
  ASSERT_TRUE(
      fixture.voice(2)
          .append(make_note(*SpelledPitch::create(Letter::kG, 3), whole))
          .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint anchor = measure_left_edge(layout, 0, 0, 1);
  const NotationPoint focus  = measure_right_edge(layout, 0, 0, 1);

  const auto selection =
      resolve_range_selection(fixture.project, layout, anchor, focus);
  ASSERT_TRUE(selection.has_value());
  const auto* set = std::get_if<ArbitraryRangeSet>(&*selection);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 1u);
  EXPECT_EQ(set->items().front().voice, *Voice::create(1));
  EXPECT_EQ(set->items().front().span, (MusicalSpan{Rational(1), Rational(2)}));
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

TEST(RangeSelectionTest,
     NoVoiceOverlappingTheDraggedSpanAnywhereYieldsNoSelection) {
  Fixture        fixture(2);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  // Voice 1's only content is measure 0; measure 1 has no content in any
  // voice at all, so a drag confined to measure 1 reaches the voice scan
  // (staff and measure both resolve, the span is non-degenerate) but finds
  // nothing overlapping anywhere in the resolved staff range.
  ASSERT_TRUE(
      fixture.voice(1)
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());

  const FixedMetrics   metrics;
  const NotationLayout empty_layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint anchor = measure_left_edge(empty_layout, 0, 0, 1);
  const NotationPoint focus  = measure_right_edge(empty_layout, 0, 0, 1);

  EXPECT_FALSE(
      resolve_range_selection(fixture.project, empty_layout, anchor, focus)
          .has_value());

  // Positive control: identical fixture and identical (staff, measure)
  // drag geometry, except measure 1 now has content -- proves the nullopt
  // above comes from the empty voice scan, not from the drag geometry
  // itself failing to resolve a staff/measure/span.
  ASSERT_TRUE(
      fixture.voice(1)
          .append(make_note(*SpelledPitch::create(Letter::kD, 4), whole))
          .ok());
  const NotationLayout filled_layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint filled_anchor = measure_left_edge(filled_layout, 0, 0, 1);
  const NotationPoint filled_focus = measure_right_edge(filled_layout, 0, 0, 1);

  const auto selection = resolve_range_selection(fixture.project, filled_layout,
                                                 filled_anchor, filled_focus);
  ASSERT_TRUE(selection.has_value());
  const auto* set = std::get_if<ArbitraryRangeSet>(&*selection);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 1u);
  EXPECT_EQ(set->items().front().voice, *Voice::create(1));
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

TEST(RangeSelectionTest,
     AnInteriorStaffIsIncludedAndAnOutOfRangeStaffIsExcluded) {
  Fixture        fixture({StaffLayout::single_staff(Clef::kTreble),
                          StaffLayout::single_staff(Clef::kTreble),
                          StaffLayout::single_staff(Clef::kTreble)},
                         1);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice(1, 0)
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());
  ASSERT_TRUE(
      fixture.voice(1, 1)
          .append(make_note(*SpelledPitch::create(Letter::kE, 4), whole))
          .ok());
  ASSERT_TRUE(
      fixture.voice(1, 2)
          .append(make_note(*SpelledPitch::create(Letter::kG, 4), whole))
          .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint staff0_left  = measure_left_edge(layout, 0, 0, 0);
  const NotationPoint staff0_right = measure_right_edge(layout, 0, 0, 0);
  const NotationPoint staff1_right = measure_right_edge(layout, 0, 1, 0);
  const NotationPoint staff2_left  = measure_left_edge(layout, 0, 2, 0);
  const NotationPoint staff2_right = measure_right_edge(layout, 0, 2, 0);

  // Case 1: staff 0 -> staff 2 includes the interior staff 1's own content.
  const auto downward_selection = resolve_range_selection(
      fixture.project, layout, staff0_left, staff2_right);
  ASSERT_TRUE(downward_selection.has_value());
  const auto* downward_set =
      std::get_if<ArbitraryRangeSet>(&*downward_selection);
  ASSERT_NE(downward_set, nullptr);
  ASSERT_EQ(downward_set->items().size(), 3u);
  {
    std::vector<TrackId> tracks;
    for (const ArbitraryRangeItem& item : downward_set->items()) {
      tracks.push_back(item.track);
    }
    EXPECT_NE(std::ranges::find(tracks, fixture.track_ids[0]), tracks.end());
    EXPECT_NE(std::ranges::find(tracks, fixture.track_ids[1]), tracks.end());
    EXPECT_NE(std::ranges::find(tracks, fixture.track_ids[2]), tracks.end());
  }

  // Case 2: staff 0 -> staff 1 excludes the out-of-range staff 2.
  const auto narrow_selection = resolve_range_selection(
      fixture.project, layout, staff0_left, staff1_right);
  ASSERT_TRUE(narrow_selection.has_value());
  const auto* narrow_set = std::get_if<ArbitraryRangeSet>(&*narrow_selection);
  ASSERT_NE(narrow_set, nullptr);
  ASSERT_EQ(narrow_set->items().size(), 2u);
  std::vector<TrackId> narrow_tracks;
  for (const ArbitraryRangeItem& item : narrow_set->items()) {
    narrow_tracks.push_back(item.track);
  }
  EXPECT_NE(std::ranges::find(narrow_tracks, fixture.track_ids[0]),
            narrow_tracks.end());
  EXPECT_NE(std::ranges::find(narrow_tracks, fixture.track_ids[1]),
            narrow_tracks.end());
  EXPECT_EQ(std::ranges::find(narrow_tracks, fixture.track_ids[2]),
            narrow_tracks.end());

  // Case 3: the upward drag staff 2 -> staff 0 yields the identical set of
  // items as the downward drag in case 1 -- also the only coverage of
  // std::minmax over the two score_order iterators.
  const auto upward_selection = resolve_range_selection(
      fixture.project, layout, staff2_left, staff0_right);
  ASSERT_TRUE(upward_selection.has_value());
  EXPECT_EQ(*downward_selection, *upward_selection);
}

TEST(RangeSelectionTest, ABackwardsOrUpwardDragProducesTheSameSpanAsForward) {
  Fixture        fixture(1);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice()
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint forward_anchor    = measure_left_edge(layout, 0, 0, 0);
  const NotationPoint forward_focus     = measure_right_edge(layout, 0, 0, 0);
  const auto          forward_selection = resolve_range_selection(
      fixture.project, layout, forward_anchor, forward_focus);
  ASSERT_TRUE(forward_selection.has_value());

  // The backward drag's anchor is both to the right of and (trivially,
  // same staff) not below the focus, and its focus is to the left of the
  // anchor -- the "focus left of / above anchor" case.
  const NotationPoint backward_anchor    = measure_right_edge(layout, 0, 0, 0);
  const NotationPoint backward_focus     = measure_left_edge(layout, 0, 0, 0);
  const auto          backward_selection = resolve_range_selection(
      fixture.project, layout, backward_anchor, backward_focus);
  ASSERT_TRUE(backward_selection.has_value());

  EXPECT_EQ(*forward_selection, *backward_selection);
  EXPECT_TRUE(validate_selection(fixture.project, *backward_selection).empty());
}

TEST(RangeSelectionTest, ADragSpanningASystemBreakProducesOneContiguousSpan) {
  Fixture        fixture(2);
  const Duration quarter = *Duration::create(NoteValue::kQuarter, 0);
  for (int index = 0; index < 8; ++index) {
    ASSERT_TRUE(
        fixture.voice()
            .append(make_note(*SpelledPitch::create(Letter::kC, 4), quarter))
            .ok());
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

  const NotationPoint anchor = measure_left_edge(layout, 0, 0, 0);
  const NotationPoint focus  = measure_right_edge(layout, 1, 0, 0);

  const auto selection =
      resolve_range_selection(fixture.project, layout, anchor, focus);
  ASSERT_TRUE(selection.has_value());
  const auto* set = std::get_if<ArbitraryRangeSet>(&*selection);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 1u);
  EXPECT_EQ(set->items().front().span, (MusicalSpan{Rational(0), Rational(2)}));
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

TEST(RangeSelectionTest, ADegenerateZeroLengthDragYieldsNoSelection) {
  Fixture        fixture(1);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice()
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint point = measure_left_edge(layout, 0, 0, 0);

  const auto selection =
      resolve_range_selection(fixture.project, layout, point, point);
  EXPECT_FALSE(selection.has_value());
}

TEST(RangeSelectionTest, EitherEndpointOffAnyStaveYieldsNoSelection) {
  Fixture        fixture(1);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice()
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint on_staff = measure_left_edge(layout, 0, 0, 0);
  const NotationPoint off_staff{-10'000.0, -10'000.0};

  EXPECT_FALSE(
      resolve_range_selection(fixture.project, layout, on_staff, off_staff)
          .has_value());
  EXPECT_FALSE(
      resolve_range_selection(fixture.project, layout, off_staff, on_staff)
          .has_value());
}

TEST(RangeSelectionTest, EitherEndpointBeingNonFiniteYieldsNoSelection) {
  Fixture        fixture(1);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice()
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint on_staff = measure_left_edge(layout, 0, 0, 0);
  const NotationPoint nan_point{std::numeric_limits<double>::quiet_NaN(),
                                std::numeric_limits<double>::quiet_NaN()};
  const NotationPoint infinite_point{std::numeric_limits<double>::infinity(),
                                     std::numeric_limits<double>::infinity()};

  EXPECT_FALSE(
      resolve_range_selection(fixture.project, layout, on_staff, nan_point)
          .has_value());
  EXPECT_FALSE(
      resolve_range_selection(fixture.project, layout, nan_point, on_staff)
          .has_value());
  EXPECT_FALSE(
      resolve_range_selection(fixture.project, layout, on_staff, infinite_point)
          .has_value());
  EXPECT_FALSE(
      resolve_range_selection(fixture.project, layout, infinite_point, on_staff)
          .has_value());
}

TEST(RangeSelectionTest,
     AMultiItemRangeSelectionRoundTripsThroughExtractArbitraryRange) {
  Fixture        fixture(1);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice(1)
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());
  ASSERT_TRUE(
      fixture.voice(2)
          .append(make_note(*SpelledPitch::create(Letter::kG, 3), whole))
          .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint anchor = measure_left_edge(layout, 0, 0, 0);
  const NotationPoint focus  = measure_right_edge(layout, 0, 0, 0);

  const auto selection =
      resolve_range_selection(fixture.project, layout, anchor, focus);
  ASSERT_TRUE(selection.has_value());
  const auto* set = std::get_if<ArbitraryRangeSet>(&*selection);
  ASSERT_NE(set, nullptr);
  ASSERT_EQ(set->items().size(), 2u);
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());

  const FragmentExtraction extraction =
      extract_fragment(fixture.project, *selection);
  EXPECT_TRUE(extraction.status.ok());
  ASSERT_TRUE(extraction.fragment.has_value());
  std::vector<Voice> part_voices;
  for (const auto& part : extraction.fragment->parts()) {
    part_voices.push_back(part.voice);
  }
  std::ranges::sort(part_voices);
  EXPECT_EQ(part_voices,
            (std::vector<Voice>{*Voice::create(1), *Voice::create(2)}));
}

// ---- SelectionDragState lifecycle -------------------------------------

TEST(SelectionDragLifecycleTest, BeginWithKSelectionEntersDraggingState) {
  SelectionDragState  drag;
  const NotationPoint anchor{100.0, 200.0};
  EXPECT_TRUE(drag.begin(ActiveTool::kSelection, anchor));
  EXPECT_TRUE(drag.is_dragging());
  EXPECT_EQ(drag.anchor(), anchor);
  EXPECT_EQ(drag.active_tool(), ActiveTool::kSelection);
}

TEST(SelectionDragLifecycleTest, BeginWithNonFiniteAnchorIsRejected) {
  SelectionDragState  drag;
  const NotationPoint nan_anchor{std::numeric_limits<double>::quiet_NaN(), 0.0};
  EXPECT_FALSE(drag.begin(ActiveTool::kSelection, nan_anchor));
  EXPECT_FALSE(drag.is_dragging());
}

TEST(SelectionDragLifecycleTest, BeginWithNonSelectionToolIsRejected) {
  SelectionDragState  drag;
  const NotationPoint anchor{100.0, 200.0};
  EXPECT_FALSE(drag.begin(ActiveTool::kNoteEntry, anchor));
  EXPECT_FALSE(drag.is_dragging());
}

TEST(SelectionDragLifecycleTest, UpdateResolvesLiveExtentAndReturnsIt) {
  Fixture        fixture(1);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice()
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint anchor = measure_left_edge(layout, 0, 0, 0);
  const NotationPoint focus  = measure_right_edge(layout, 0, 0, 0);

  SelectionDragState drag;
  ASSERT_TRUE(drag.begin(ActiveTool::kSelection, anchor));
  const std::optional<Selection> live =
      drag.update(fixture.project, layout, focus);
  ASSERT_TRUE(live.has_value());
  const auto* set = std::get_if<ArbitraryRangeSet>(&*live);
  ASSERT_NE(set, nullptr);
  EXPECT_EQ(set->items().size(), 1u);

  // live_extent() returns the same resolved Selection.
  ASSERT_TRUE(drag.live_extent().has_value());
  EXPECT_EQ(*drag.live_extent(), *live);

  // validate the result through the domain.
  EXPECT_TRUE(validate_selection(fixture.project, *live).empty());
}

TEST(SelectionDragLifecycleTest, UpdateWithoutBeginReturnsNullopt) {
  SelectionDragState drag;
  // No fixture needed -- update() returns nullopt before touching
  // project or layout.
  Fixture              fixture(1);
  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint focus{100.0, 200.0};
  EXPECT_FALSE(drag.update(fixture.project, layout, focus).has_value());
}

TEST(SelectionDragLifecycleTest, CommitReturnsLastLiveExtentAndClearsDragging) {
  Fixture        fixture(1);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice()
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint anchor = measure_left_edge(layout, 0, 0, 0);
  const NotationPoint focus  = measure_right_edge(layout, 0, 0, 0);

  SelectionDragState drag;
  ASSERT_TRUE(drag.begin(ActiveTool::kSelection, anchor));
  const std::optional<Selection> live =
      drag.update(fixture.project, layout, focus);
  ASSERT_TRUE(live.has_value());

  const std::optional<Selection> committed = drag.commit();
  EXPECT_TRUE(committed.has_value());
  EXPECT_EQ(*committed, *live);
  EXPECT_FALSE(drag.is_dragging());

  // committed_selection() survives the clear.
  ASSERT_TRUE(drag.committed_selection().has_value());
  EXPECT_EQ(*drag.committed_selection(), *live);

  // live_extent() is cleared.
  EXPECT_FALSE(drag.live_extent().has_value());
}

TEST(SelectionDragLifecycleTest, CommitWithoutSuccessfulUpdateReturnsNullopt) {
  Fixture        fixture(1);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice()
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  SelectionDragState drag;
  ASSERT_TRUE(
      drag.begin(ActiveTool::kSelection, NotationPoint{-10'000.0, -10'000.0}));
  // update() returns nullopt because the anchor is off-stave.
  const std::optional<Selection> live =
      drag.update(fixture.project, layout, NotationPoint{-10'001.0, -10'001.0});
  EXPECT_FALSE(live.has_value());

  const std::optional<Selection> committed = drag.commit();
  EXPECT_FALSE(committed.has_value());
  EXPECT_FALSE(drag.is_dragging());
  EXPECT_FALSE(drag.committed_selection().has_value());
}

TEST(SelectionDragLifecycleTest,
     CancelClearsDragButPreservesCommittedSelection) {
  Fixture        fixture(1);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice()
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint anchor = measure_left_edge(layout, 0, 0, 0);
  const NotationPoint focus  = measure_right_edge(layout, 0, 0, 0);

  SelectionDragState drag;
  ASSERT_TRUE(drag.begin(ActiveTool::kSelection, anchor));
  ASSERT_TRUE(drag.update(fixture.project, layout, focus).has_value());
  const std::optional<Selection> committed = drag.commit();
  ASSERT_TRUE(committed.has_value());

  // Start a second drag and cancel it.
  ASSERT_TRUE(drag.begin(ActiveTool::kSelection, anchor));
  drag.cancel();
  EXPECT_FALSE(drag.is_dragging());
  EXPECT_FALSE(drag.live_extent().has_value());

  // The first commit's result is still there.
  ASSERT_TRUE(drag.committed_selection().has_value());
  EXPECT_EQ(*drag.committed_selection(), *committed);
}

TEST(SelectionDragLifecycleTest,
     BeginWhileDraggingCancelsPreviousAndStartsNew) {
  Fixture        fixture(1);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice()
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint anchor1 = measure_left_edge(layout, 0, 0, 0);
  const NotationPoint anchor2{anchor1.x + 50.0, anchor1.y};

  SelectionDragState drag;
  ASSERT_TRUE(drag.begin(ActiveTool::kSelection, anchor1));
  ASSERT_TRUE(drag.is_dragging());

  // A second begin() cancels the first and starts a new drag.
  ASSERT_TRUE(drag.begin(ActiveTool::kSelection, anchor2));
  EXPECT_TRUE(drag.is_dragging());
  EXPECT_EQ(drag.anchor(), anchor2);
  // The first drag's state is gone.
  EXPECT_FALSE(drag.live_extent().has_value());
}

TEST(SelectionDragLifecycleTest,
     ReleaseWithoutUpdateDoesNotLeakAStaleSelection) {
  // If the user presses, never moves (update() never called), and releases:
  // commit() returns nullopt and no selection leaks.
  Fixture        fixture(1);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice()
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint anchor = measure_left_edge(layout, 0, 0, 0);

  SelectionDragState drag;
  ASSERT_TRUE(drag.begin(ActiveTool::kSelection, anchor));
  const std::optional<Selection> committed = drag.commit();
  EXPECT_FALSE(committed.has_value());
  EXPECT_FALSE(drag.committed_selection().has_value());
}

TEST(SelectionDragLifecycleTest, NulloptUpdatePreservesDragState) {
  // A failing update() still leaves the drag in progress so the user can
  // move back to a valid position.
  Fixture        fixture(1);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice()
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint anchor = measure_left_edge(layout, 0, 0, 0);

  SelectionDragState drag;
  ASSERT_TRUE(drag.begin(ActiveTool::kSelection, anchor));

  // Move to an off-stave point -- resolution fails.
  EXPECT_FALSE(
      drag.update(fixture.project, layout, NotationPoint{-10'000.0, -10'000.0})
          .has_value());
  // But the drag is still in progress.
  EXPECT_TRUE(drag.is_dragging());

  // A subsequent valid update succeeds.
  const NotationPoint            focus = measure_right_edge(layout, 0, 0, 0);
  const std::optional<Selection> live =
      drag.update(fixture.project, layout, focus);
  ASSERT_TRUE(live.has_value());
  EXPECT_TRUE(drag.is_dragging());
}

TEST(SelectionDragLifecycleTest, LiveExtentPreservesResolvedSpanOrdering) {
  // A forward drag (left → right) and a backward drag (right → left) over
  // the same two endpoints produce identical selections: both resolve to
  // the same start→end span because resolve_range_selection orders the
  // span by musical time, not by spatial drag direction.
  Fixture        fixture(1);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice()
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint left  = measure_left_edge(layout, 0, 0, 0);
  const NotationPoint right = measure_right_edge(layout, 0, 0, 0);

  SelectionDragState forward_drag;
  ASSERT_TRUE(forward_drag.begin(ActiveTool::kSelection, left));
  const auto forward = forward_drag.update(fixture.project, layout, right);
  ASSERT_TRUE(forward.has_value());

  SelectionDragState backward_drag;
  ASSERT_TRUE(backward_drag.begin(ActiveTool::kSelection, right));
  const auto backward = backward_drag.update(fixture.project, layout, left);
  ASSERT_TRUE(backward.has_value());

  EXPECT_EQ(*forward, *backward);

  // Both directions resolve to the exact same span: the full measure
  // [0, 1).  Assert the exact values, not only equality.
  const MusicalSpan kExpected{Rational(0), Rational(1)};
  {
    const auto* fwd_set = std::get_if<ArbitraryRangeSet>(&*forward);
    ASSERT_NE(fwd_set, nullptr);
    ASSERT_EQ(fwd_set->items().size(), 1u);
    EXPECT_EQ(fwd_set->items().front().span, kExpected);
  }
  {
    const auto* bwd_set = std::get_if<ArbitraryRangeSet>(&*backward);
    ASSERT_NE(bwd_set, nullptr);
    ASSERT_EQ(bwd_set->items().size(), 1u);
    EXPECT_EQ(bwd_set->items().front().span, kExpected);
  }
}

TEST(SelectionDragLifecycleTest, CommittedSelectionPersistsAcrossToolChanges) {
  Fixture        fixture(1);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice()
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint anchor = measure_left_edge(layout, 0, 0, 0);
  const NotationPoint focus  = measure_right_edge(layout, 0, 0, 0);

  SelectionDragState drag;
  ASSERT_TRUE(drag.begin(ActiveTool::kSelection, anchor));
  ASSERT_TRUE(drag.update(fixture.project, layout, focus).has_value());
  const std::optional<Selection> first = drag.commit();
  ASSERT_TRUE(first.has_value());

  // After commit, the committed selection is available even across
  // subsequent tool operations.
  drag.cancel();
  ASSERT_TRUE(drag.committed_selection().has_value());

  // Starting a new drag with a non-selection tool does not overwrite
  // committed_selection().
  EXPECT_FALSE(drag.begin(ActiveTool::kNoteEntry, anchor));
  ASSERT_TRUE(drag.committed_selection().has_value());
  EXPECT_EQ(*drag.committed_selection(), *first);
}

TEST(SelectionDragLifecycleTest,
     BeginWithNonSelectionToolWhileDraggingCancelsDrag) {
  Fixture        fixture(1);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice()
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint anchor = measure_left_edge(layout, 0, 0, 0);

  SelectionDragState drag;
  ASSERT_TRUE(drag.begin(ActiveTool::kSelection, anchor));
  ASSERT_TRUE(drag.is_dragging());

  // A begin() with a non-Selection tool cancels the drag and returns false.
  EXPECT_FALSE(drag.begin(ActiveTool::kNoteEntry, anchor));
  EXPECT_FALSE(drag.is_dragging());
  EXPECT_FALSE(drag.live_extent().has_value());
  // committed_selection_ was never set — remains nullopt.
  EXPECT_FALSE(drag.committed_selection().has_value());
}

TEST(SelectionDragLifecycleTest,
     BeginWithNonFiniteAnchorWhileDraggingCancelsDrag) {
  Fixture        fixture(1);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice()
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint anchor     = measure_left_edge(layout, 0, 0, 0);
  const NotationPoint nan_anchor = {std::numeric_limits<double>::quiet_NaN(),
                                    0.0};

  SelectionDragState drag;
  ASSERT_TRUE(drag.begin(ActiveTool::kSelection, anchor));
  ASSERT_TRUE(drag.is_dragging());

  // A begin() with a non-finite anchor cancels the drag and returns false.
  EXPECT_FALSE(drag.begin(ActiveTool::kSelection, nan_anchor));
  EXPECT_FALSE(drag.is_dragging());
  EXPECT_FALSE(drag.live_extent().has_value());
  // committed_selection_ was never set — remains nullopt.
  EXPECT_FALSE(drag.committed_selection().has_value());
}

TEST(SelectionDragLifecycleTest,
     RejectedBeginWhileDraggingPreservesCommittedSelection) {
  Fixture        fixture(1);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice()
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint anchor = measure_left_edge(layout, 0, 0, 0);
  const NotationPoint focus  = measure_right_edge(layout, 0, 0, 0);

  SelectionDragState drag;
  // First drag: begin, update, commit — produces a committed selection.
  ASSERT_TRUE(drag.begin(ActiveTool::kSelection, anchor));
  ASSERT_TRUE(drag.update(fixture.project, layout, focus).has_value());
  const std::optional<Selection> committed = drag.commit();
  ASSERT_TRUE(committed.has_value());

  // Second drag: begin.
  ASSERT_TRUE(drag.begin(ActiveTool::kSelection, anchor));
  ASSERT_TRUE(drag.is_dragging());

  // Rejected begin() with a non-Selection tool cancels the drag and
  // preserves the first drag's committed selection.
  EXPECT_FALSE(drag.begin(ActiveTool::kNoteEntry, anchor));
  EXPECT_FALSE(drag.is_dragging());
  ASSERT_TRUE(drag.committed_selection().has_value());
  EXPECT_EQ(*drag.committed_selection(), *committed);
}

// ---- build_range_highlight_rects -------------------------------------------

TEST(HighlightRectsTest, EmptyOnNonRangeSelection) {
  Fixture              fixture(1);
  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const auto caret_set = InsertionCaretSet::create({{InsertionCaretItem{
      fixture.node_id, fixture.track_ids[0], fixture.stave_id(),
      *Voice::create(1), Rational(0)}}});
  ASSERT_TRUE(caret_set.has_value());
  const Selection caret = *caret_set;
  EXPECT_TRUE(
      build_range_highlight_rects(caret, fixture.project, layout).empty());
}

TEST(HighlightRectsTest, FullMeasureDragProducesOneRectPerSelectedStaff) {
  Fixture        fixture(1);
  const Duration quarter = *Duration::create(NoteValue::kQuarter, 0);
  for (int index = 0; index < 4; ++index) {
    ASSERT_TRUE(
        fixture.voice()
            .append(make_note(*SpelledPitch::create(Letter::kC, 4), quarter))
            .ok());
  }

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint anchor = measure_left_edge(layout, 0, 0, 0);
  const NotationPoint focus  = measure_right_edge(layout, 0, 0, 0);

  const auto selection =
      resolve_range_selection(fixture.project, layout, anchor, focus);
  ASSERT_TRUE(selection.has_value());

  const std::vector<NotationRect> rects =
      build_range_highlight_rects(*selection, fixture.project, layout);
  ASSERT_FALSE(rects.empty());

  // One rect per measure in the span.  The fixture has exactly one measure.
  EXPECT_EQ(rects.size(), 1u);

  // Exact NotationRect equality: for a full-measure drag across measure 0
  // of the default fixture (C major, 4/4, staff_space=10), the highlight
  // rect spans the rhythmic area of the measure horizontally and the
  // staff vertically.  The x starts after the leading area (clef, key,
  // time signature) and width equals the rhythmic width.
  {
    const auto& staff   = layout.systems[0].staves[0];
    const auto& measure = layout.systems[0].measures[0];
    // measure_leading_width for C major, 4/4, measure 0, staff_space=10:
    //   min(measure_width - 20, 80)
    constexpr double kStaffSpace = 10.0;
    const double     leading =
        std::min(measure.bounds.width - kStaffSpace * 2.0, kStaffSpace * 8.0);
    const double rhythmic_width =
        std::max(kStaffSpace, measure.bounds.width - leading - kStaffSpace);
    const NotationRect expected{measure.bounds.x + leading, staff.bounds.y,
                                rhythmic_width, staff.bounds.height};
    EXPECT_EQ(rects.front(), expected);
  }
}

TEST(HighlightRectsTest, NullProjectIsEmpty) {
  Fixture        fixture(1);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice()
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint anchor = measure_left_edge(layout, 0, 0, 0);
  const NotationPoint focus  = measure_right_edge(layout, 0, 0, 0);
  const auto          selection =
      resolve_range_selection(fixture.project, layout, anchor, focus);
  ASSERT_TRUE(selection.has_value());

  const Project empty_project{ProjectId::generate(), "Empty"};
  EXPECT_TRUE(
      build_range_highlight_rects(*selection, empty_project, layout).empty());
}

TEST(HighlightRectsTest, MultiTrackProducesRectsForEachSelectedStaff) {
  // Two tracks, each with one staff. Both are selected; each should produce
  // its own rect.
  Fixture        fixture({StaffLayout::single_staff(Clef::kTreble),
                          StaffLayout::single_staff(Clef::kBass)},
                         1);
  const Duration quarter = *Duration::create(NoteValue::kQuarter, 0);
  for (std::size_t track = 0; track < 2; ++track) {
    for (int i = 0; i < 4; ++i) {
      ASSERT_TRUE(
          fixture.voice(1, track)
              .append(make_note(*SpelledPitch::create(Letter::kC, 4), quarter))
              .ok());
    }
  }

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint anchor = measure_left_edge(layout, 0, 0, 0);
  const NotationPoint focus  = measure_right_edge(layout, 0, 1, 0);

  const auto selection =
      resolve_range_selection(fixture.project, layout, anchor, focus);
  ASSERT_TRUE(selection.has_value());
  const auto* set = std::get_if<ArbitraryRangeSet>(&*selection);
  ASSERT_NE(set, nullptr);
  EXPECT_GE(set->items().size(), 2u);

  const std::vector<NotationRect> rects =
      build_range_highlight_rects(*selection, fixture.project, layout);
  ASSERT_FALSE(rects.empty());

  // One rect per measure per staff (2 staves × 1 measure).
  EXPECT_EQ(rects.size(), 2u);
}

TEST(HighlightRectsTest, RepeatedStaveIdAcrossTracksIsDisambiguated) {
  // Two tracks, each with one staff. StaveIds are created independently per
  // track and may collide. The highlight projection must use (TrackId,
  // StaveId) keys so staves from different tracks are not conflated.
  Fixture        fixture({StaffLayout::single_staff(Clef::kTreble),
                          StaffLayout::single_staff(Clef::kBass)},
                         1);
  const Duration quarter = *Duration::create(NoteValue::kQuarter, 0);
  for (std::size_t track = 0; track < 2; ++track) {
    for (int i = 0; i < 4; ++i) {
      ASSERT_TRUE(
          fixture.voice(1, track)
              .append(make_note(*SpelledPitch::create(Letter::kC, 4), quarter))
              .ok());
    }
  }

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  // Select only the first track by anchoring and focusing within its
  // staff bounds.
  const NotationPoint anchor = measure_left_edge(layout, 0, 0, 0);
  const NotationPoint focus  = measure_right_edge(layout, 0, 0, 0);

  const auto selection =
      resolve_range_selection(fixture.project, layout, anchor, focus);
  ASSERT_TRUE(selection.has_value());
  const auto* set = std::get_if<ArbitraryRangeSet>(&*selection);
  ASSERT_NE(set, nullptr);

  const std::vector<NotationRect> rects =
      build_range_highlight_rects(*selection, fixture.project, layout);
  ASSERT_FALSE(rects.empty());

  // Only the first track should produce rects, even if StaveIds happen to
  // collide.
  EXPECT_EQ(rects.size(), 1u);
  EXPECT_DOUBLE_EQ(rects[0].y, layout.systems[0].staves[0].bounds.y);
}

TEST(HighlightRectsTest, MixedSpanItemsEachProjectedIndependently) {
  // Construct two items with different musical spans on the same staff.
  // build_range_highlight_rects must project each item's span independently.
  Fixture        fixture(2);  // 2 measures
  const Duration quarter = *Duration::create(NoteValue::kQuarter, 0);
  for (int i = 0; i < 8; ++i) {
    ASSERT_TRUE(
        fixture.voice()
            .append(make_note(*SpelledPitch::create(Letter::kC, 4), quarter))
            .ok());
  }

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  // Build a range set manually with two items: one spanning measure 0,
  // the other spanning measure 1.
  const Voice voice1 = *Voice::create(1);
  const auto  r1     = Rational::create(1, 4);
  const auto  r2     = Rational::create(1, 2);
  ASSERT_TRUE(r1.has_value());
  ASSERT_TRUE(r2.has_value());
  std::vector<ArbitraryRangeItem> items;
  items.push_back(ArbitraryRangeItem{fixture.node_id, fixture.track_ids[0],
                                     fixture.stave_id(), voice1,
                                     MusicalSpan{Rational(0), *r1}});
  items.push_back(ArbitraryRangeItem{fixture.node_id, fixture.track_ids[0],
                                     fixture.stave_id(), voice1,
                                     MusicalSpan{*r1, *r2}});
  const auto range_set = ArbitraryRangeSet::create(std::move(items));
  ASSERT_TRUE(range_set.has_value());
  const Selection sel = *range_set;

  const std::vector<NotationRect> rects =
      build_range_highlight_rects(sel, fixture.project, layout);
  ASSERT_FALSE(rects.empty());

  // Items [0, ¼) and [¼, ½) are touching on the same measure; the
  // interval-coalescing logic merges touching intervals into one rect.
  EXPECT_EQ(rects.size(), 1u);

  // All rects should be on the same staff.
  for (const NotationRect& r : rects) {
    EXPECT_DOUBLE_EQ(r.y, layout.systems[0].staves[0].bounds.y);
    EXPECT_GT(r.width, 0.0);
  }
}

// Two items with disjoint spans on the same staff and measure produce
// separate highlight rectangles — the coalescing logic must not fill the
// gap between them.
TEST(HighlightRectsTest, DisjointSpanProducesSeparateRects) {
  Fixture        fixture(2);  // 2 measures
  const Duration quarter = *Duration::create(NoteValue::kQuarter, 0);
  for (int i = 0; i < 8; ++i) {
    ASSERT_TRUE(
        fixture.voice()
            .append(make_note(*SpelledPitch::create(Letter::kC, 4), quarter))
            .ok());
  }

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  const Voice voice1 = *Voice::create(1);
  const auto  r1     = Rational::create(1, 8);
  const auto  r2     = Rational::create(3, 8);
  const auto  r3     = Rational::create(1, 2);
  ASSERT_TRUE(r1.has_value());
  ASSERT_TRUE(r2.has_value());
  ASSERT_TRUE(r3.has_value());

  // [0, ⅛) and [⅜, ½): a disjoint pair with a ¼ gap on the same measure.
  std::vector<ArbitraryRangeItem> items;
  items.push_back(ArbitraryRangeItem{fixture.node_id, fixture.track_ids[0],
                                     fixture.stave_id(), voice1,
                                     MusicalSpan{Rational(0), *r1}});
  items.push_back(ArbitraryRangeItem{fixture.node_id, fixture.track_ids[0],
                                     fixture.stave_id(), voice1,
                                     MusicalSpan{*r2, *r3}});
  const auto range_set = ArbitraryRangeSet::create(std::move(items));
  ASSERT_TRUE(range_set.has_value());
  const Selection sel = *range_set;

  const std::vector<NotationRect> rects =
      build_range_highlight_rects(sel, fixture.project, layout);
  // Both intervals fall within measure 0.  They are disjoint (gap from ⅛
  // to ⅜) so must produce two separate rects.
  ASSERT_EQ(rects.size(), 2u);

  // Both rects should be on the same staff, non-zero width, and sorted
  // left to right.
  EXPECT_DOUBLE_EQ(rects[0].y, layout.systems[0].staves[0].bounds.y);
  EXPECT_DOUBLE_EQ(rects[1].y, layout.systems[0].staves[0].bounds.y);
  EXPECT_GT(rects[0].width, 0.0);
  EXPECT_GT(rects[1].width, 0.0);
  EXPECT_LT(rects[0].x + rects[0].width, rects[1].x)
      << "disjoint rects must not overlap";
}

TEST(HighlightRectsTest, ItemFromDifferentNodeIsRejected) {
  Fixture        fixture(1);
  const Duration whole = *Duration::create(NoteValue::kWhole, 0);
  ASSERT_TRUE(
      fixture.voice()
          .append(make_note(*SpelledPitch::create(Letter::kC, 4), whole))
          .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  // Build a range set where the item's node does not match the layout's
  // node. The highlight projection must reject it.
  const Voice  voice1     = *Voice::create(1);
  const NodeId other_node = fixture.project.add_node("Other");
  std::vector<ArbitraryRangeItem> items;
  items.push_back(ArbitraryRangeItem{other_node, fixture.track_ids[0],
                                     fixture.stave_id(), voice1,
                                     MusicalSpan{Rational(0), Rational(1)}});
  const auto range_set = ArbitraryRangeSet::create(std::move(items));
  ASSERT_TRUE(range_set.has_value());

  EXPECT_TRUE(
      build_range_highlight_rects(*range_set, fixture.project, layout).empty());
}

// Two voices on the same staff, both selected in a single range drag,
// produce duplicate items in the ArbitraryRangeSet (one per voice).  The
// highlight projection must de-duplicate by (system, staff, measure) so
// the translucent highlight overlay is drawn once, not stacked twice.
TEST(HighlightRectsTest, TwoVoiceSameStaffProducesOneRect) {
  Fixture        fixture(1);
  const Duration quarter = *Duration::create(NoteValue::kQuarter, 0);

  // Fill both voices so the range resolver includes both.
  for (int i = 0; i < 4; ++i) {
    ASSERT_TRUE(
        fixture.voice(1)
            .append(make_note(*SpelledPitch::create(Letter::kC, 4), quarter))
            .ok());
    ASSERT_TRUE(
        fixture.voice(2)
            .append(make_note(*SpelledPitch::create(Letter::kE, 4), quarter))
            .ok());
  }

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint anchor = measure_left_edge(layout, 0, 0, 0);
  const NotationPoint focus  = measure_right_edge(layout, 0, 0, 0);

  const auto selection =
      resolve_range_selection(fixture.project, layout, anchor, focus);
  ASSERT_TRUE(selection.has_value());
  const auto* set = std::get_if<ArbitraryRangeSet>(&*selection);
  ASSERT_NE(set, nullptr);
  // Both voices appear in the range set.
  ASSERT_EQ(set->items().size(), 2u);

  const std::vector<NotationRect> rects =
      build_range_highlight_rects(*selection, fixture.project, layout);
  // One rect per (system, staff, measure) — de-duplicated even though
  // two voices produced two items spanning the exact same measure.
  ASSERT_EQ(rects.size(), 1u);
  EXPECT_DOUBLE_EQ(rects[0].y, layout.systems[0].staves[0].bounds.y);
  EXPECT_DOUBLE_EQ(rects[0].height, layout.systems[0].staves[0].bounds.height);
  EXPECT_GT(rects[0].width, 0.0);
}

// Multi-track with mixed spans: two tracks produce rects on their respective
// staves, and the per-staff de-duplication does not collapse across tracks.
TEST(HighlightRectsTest, MultiTrackMixedSpanPreservesPerStaffDeDuplication) {
  Fixture        fixture({StaffLayout::single_staff(Clef::kTreble),
                          StaffLayout::single_staff(Clef::kBass)},
                         1);
  const Duration quarter = *Duration::create(NoteValue::kQuarter, 0);
  for (std::size_t track = 0; track < 2; ++track) {
    for (int i = 0; i < 4; ++i) {
      ASSERT_TRUE(
          fixture.voice(1, track)
              .append(make_note(*SpelledPitch::create(Letter::kC, 4), quarter))
              .ok());
      ASSERT_TRUE(
          fixture.voice(2, track)
              .append(make_note(*SpelledPitch::create(Letter::kE, 4), quarter))
              .ok());
    }
  }

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationPoint anchor = measure_left_edge(layout, 0, 0, 0);
  const NotationPoint focus  = measure_right_edge(layout, 0, 1, 0);

  const auto selection =
      resolve_range_selection(fixture.project, layout, anchor, focus);
  ASSERT_TRUE(selection.has_value());
  const auto* set = std::get_if<ArbitraryRangeSet>(&*selection);
  ASSERT_NE(set, nullptr);
  // 2 voices × 2 tracks = 4 items in the range set.
  ASSERT_EQ(set->items().size(), 4u);

  const std::vector<NotationRect> rects =
      build_range_highlight_rects(*selection, fixture.project, layout);
  // One rect per staff (each staff gets its own, de-duplicated across its
  // two voices).  2 staves × 1 measure = 2 rects.
  ASSERT_EQ(rects.size(), 2u);

  // The two rects belong to different staves.
  std::vector<double> y_positions = {rects[0].y, rects[1].y};
  std::ranges::sort(y_positions);
  EXPECT_DOUBLE_EQ(y_positions[0], layout.systems[0].staves[0].bounds.y);
  EXPECT_DOUBLE_EQ(y_positions[1], layout.systems[0].staves[1].bounds.y);
}

}  // namespace
