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

using graphscore::ArbitraryRangeItem;
using graphscore::ArbitraryRangeSet;
using graphscore::Articulation;
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
using graphscore::Project;
using graphscore::ProjectId;
using graphscore::Rational;
using graphscore::resolve_range_selection;
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
  EXPECT_EQ(column->priority, 4);
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
           tied[0].id.to_string() + "/tie/segment/system-0/hit",
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
        region.role == HitRole::kStaff || region.role == HitRole::kVoice;
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
  // Today: 8 containers (system, two measures, staff, four voices), the one
  // column, and 14 engraved-object regions -- 7 at kHitPriorityGlyph (two
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
// keeps resolving exactly as it does without this feature -- through the
// voice container region, to an insertion caret.
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
  EXPECT_EQ(hit->role, HitRole::kVoice);

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

// Known limitation, pinned so a future fix trips a test rather than passing
// silently: add_span_segment gives a tie a region four staff-spaces tall,
// which blankets a stemless chord's whole column. A tie span segment
// outranks the column, so the gap click that would otherwise select the
// chord yields the tie's MarkingSet instead -- the affordance does not
// reach a tied whole-note chord at all. The oversized tie region is
// pre-existing engraver geometry, carried forward as its own hit-priority
// item rather than reworked here.
TEST(SelectionResolverTest,
     ATiedWholeNoteChordsGapClickIsShadowedByTheTieRegion) {
  Fixture                      fixture(2);
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

  const NotationPoint point =
      notehead_gap_point(layout, tied[0].id, tied[1].id);
  EXPECT_TRUE(column->bounds.contains(point));

  const auto hit = layout.hit_test(point);
  ASSERT_TRUE(hit.has_value());
  EXPECT_EQ(hit->role, HitRole::kMarking);

  const auto selection =
      resolve_selection_at(fixture.project, layout, note_state(), point);
  ASSERT_TRUE(selection.has_value());
  const auto* marking_set = std::get_if<MarkingSet>(&*selection);
  ASSERT_NE(marking_set, nullptr);
  ASSERT_EQ(marking_set->items().size(), 1u);
  EXPECT_EQ(marking_set->items().front().kind, MarkingKind::kTie);
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

}  // namespace
