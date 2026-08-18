// SPDX-License-Identifier: Apache-2.0

#include "selection_test_support.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <string>
#include <variant>
#include <vector>

#include <graphscore/notation/graphscore_notation.hpp>

namespace {

using graphscore::Accidental;
using graphscore::LineCommand;

[[nodiscard]] NotationPoint selectable_accidental_point(
    const NotationLayout& layout, const NotationEntityId& note_id) {
  const std::string prefix = note_id.to_string() + "/accidental/";
  const auto        found =
      std::ranges::find_if(layout.hit_regions, [&](const HitRegion& region) {
        return region.id.value.starts_with(prefix);
      });
  EXPECT_NE(found, layout.hit_regions.end());
  if (found == layout.hit_regions.end()) {
    return {};
  }
  constexpr int kSubdivisions = 20;
  for (int row = 1; row < kSubdivisions; ++row) {
    for (int column = 1; column < kSubdivisions; ++column) {
      const NotationPoint point{
          found->bounds.x +
              found->bounds.width * static_cast<double>(column) / kSubdivisions,
          found->bounds.y +
              found->bounds.height * static_cast<double>(row) / kSubdivisions,
      };
      const auto hit = layout.hit_test(point);
      if (hit.has_value() && hit->id == found->id) {
        return point;
      }
    }
  }
  ADD_FAILURE() << "accidental has no selectable area: " << note_id.to_string();
  return {};
}

[[nodiscard]] const LineCommand* find_line(const NotationLayout& layout,
                                           const std::string&    id) {
  const auto found =
      std::ranges::find_if(layout.commands, [&](const auto& item) {
        const auto* line = std::get_if<LineCommand>(&item);
        return line != nullptr && line->id.value == id;
      });
  return found == layout.commands.end() ? nullptr
                                        : std::get_if<LineCommand>(&*found);
}

void expect_notehead_selection(const Fixture&          fixture,
                               const NotationLayout&   layout,
                               NotationPoint           point,
                               const NotationEntityId& expected,
                               std::uint8_t            armed_voice = 1) {
  const auto selection = resolve_selection_at(fixture.project, layout,
                                              note_state(armed_voice), point);
  ASSERT_TRUE(selection.has_value());
  const auto* noteheads = std::get_if<NoteheadSet>(&*selection);
  ASSERT_NE(noteheads, nullptr);
  ASSERT_EQ(noteheads->items().size(), 1u);
  EXPECT_EQ(noteheads->items().front().entity, expected);
  EXPECT_TRUE(validate_selection(fixture.project, *selection).empty());
}

TEST(SelectionResolverTest,
     EveryAccidentalInADenseChordSelectsItsOwningNotehead) {
  Fixture fixture(1);
  ASSERT_TRUE(fixture.project.find_node(fixture.node_id)
                  ->timeline()
                  ->set_measure_key_signature(0, *KeySignature::create(1))
                  .ok());

  const std::array pitches{
      *SpelledPitch::create(Letter::kC, 4, Accidental::kDoubleFlat),
      *SpelledPitch::create(Letter::kD, 4, Accidental::kFlat),
      *SpelledPitch::create(Letter::kF, 4, Accidental::kNatural),
      *SpelledPitch::create(Letter::kG, 4, Accidental::kSharp),
      *SpelledPitch::create(Letter::kA, 4, Accidental::kDoubleSharp),
  };
  std::vector<ChordNote> notes;
  for (const SpelledPitch pitch : pitches) {
    notes.push_back({NotationEntityId::generate(), pitch, false});
  }
  const Chord chord =
      make_chord(*Duration::create(NoteValue::kQuarter, 0), notes);
  ASSERT_TRUE(fixture.voice().append(chord).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  for (const ChordNote& note : notes) {
    const NotationPoint point = selectable_accidental_point(layout, note.id);
    const auto          hit   = layout.hit_test(point);
    ASSERT_TRUE(hit.has_value());
    EXPECT_TRUE(
        hit->id.value.starts_with(note.id.to_string() + "/accidental/"));
    EXPECT_EQ(hit->semantic_id.value, note.id.to_string());
    expect_notehead_selection(fixture, layout, point, note.id);
  }
}

TEST(SelectionResolverTest,
     BeamedEventsKeepStemTargetsWithoutMakingTheBeamBodyAnEventTarget) {
  Fixture        fixture(1);
  const Duration eighth = *Duration::create(NoteValue::kEighth, 0);
  const Note first  = make_note(*SpelledPitch::create(Letter::kE, 4), eighth);
  const Note second = make_note(*SpelledPitch::create(Letter::kG, 4), eighth);
  ASSERT_TRUE(fixture.voice().append(first).ok());
  ASSERT_TRUE(fixture.voice().append(second).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const std::string beam_id =
      first.id.to_string() + "/beam/to/" + second.id.to_string() + "/level/0";
  const LineCommand* beam = find_line(layout, beam_id);
  ASSERT_NE(beam, nullptr);

  expect_notehead_selection(fixture, layout, stem_click_point(layout, first.id),
                            first.id);
  expect_notehead_selection(fixture, layout,
                            stem_click_point(layout, second.id), second.id);

  EXPECT_TRUE(
      std::ranges::none_of(layout.hit_regions, [&](const HitRegion& region) {
        return region.id.value.starts_with(beam_id);
      }));
  const NotationPoint midpoint{(beam->from.x + beam->to.x) * 0.5,
                               (beam->from.y + beam->to.y) * 0.5};
  const auto          hit = layout.hit_test(midpoint);
  if (hit.has_value()) {
    EXPECT_NE(hit->role, HitRole::kEvent);
    EXPECT_NE(hit->role, HitRole::kNotehead);
    EXPECT_NE(hit->role, HitRole::kMarking);
  }
}

TEST(SelectionResolverTest,
     OverlappingBeamedVoicesPreserveAccidentalStemAndSlurTargets) {
  Fixture        fixture(1);
  const Duration eighth = *Duration::create(NoteValue::kEighth, 0);

  const std::vector<ChordNote> upper_notes = {
      {NotationEntityId::generate(),
       *SpelledPitch::create(Letter::kE, 4, Accidental::kFlat), false},
      {NotationEntityId::generate(),
       *SpelledPitch::create(Letter::kF, 4, Accidental::kSharp), false},
  };
  const Chord upper_first  = make_chord(eighth, upper_notes);
  const Chord upper_second = make_chord(eighth, two_chord_notes());
  ASSERT_TRUE(fixture.voice(1).append(upper_first).ok());
  ASSERT_TRUE(fixture.voice(1).append(upper_second).ok());
  const Slur slur = make_slur(upper_first.id, upper_second.id);
  ASSERT_TRUE(fixture.voice(1).add_slur(slur).ok());

  const std::vector<ChordNote> lower_notes = {
      {NotationEntityId::generate(),
       *SpelledPitch::create(Letter::kD, 4, Accidental::kDoubleFlat), false},
      {NotationEntityId::generate(),
       *SpelledPitch::create(Letter::kG, 4, Accidental::kDoubleSharp), false},
  };
  const Chord lower_first  = make_chord(eighth, lower_notes);
  const Chord lower_second = make_chord(eighth, two_chord_notes());
  ASSERT_TRUE(fixture.voice(2).append(lower_first).ok());
  ASSERT_TRUE(fixture.voice(2).append(lower_second).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const std::string upper_beam_id = upper_first.id.to_string() + "/beam/to/" +
                                    upper_second.id.to_string() + "/level/0";
  const std::string lower_beam_id = lower_first.id.to_string() + "/beam/to/" +
                                    lower_second.id.to_string() + "/level/0";
  ASSERT_NE(find_line(layout, upper_beam_id), nullptr);
  ASSERT_NE(find_line(layout, lower_beam_id), nullptr);

  expect_notehead_selection(
      fixture, layout, selectable_accidental_point(layout, lower_notes[1].id),
      lower_notes[1].id, 1);

  const auto stem_selection =
      resolve_selection_at(fixture.project, layout, note_state(2),
                           stem_click_point(layout, lower_first.id));
  ASSERT_TRUE(stem_selection.has_value());
  const auto* chords = std::get_if<ChordSet>(&*stem_selection);
  ASSERT_NE(chords, nullptr);
  ASSERT_EQ(chords->items().size(), 1u);
  EXPECT_EQ(chords->items().front().entity, lower_first.id);
  EXPECT_EQ(chords->items().front().voice, *Voice::create(2));
  EXPECT_TRUE(validate_selection(fixture.project, *stem_selection).empty());

  const NotationPoint slur_point = hit_region_center(
      layout, slur.id.to_string() + "/slur/segment/system-0/hit");
  const auto slur_selection =
      resolve_selection_at(fixture.project, layout, note_state(2), slur_point);
  ASSERT_TRUE(slur_selection.has_value());
  const auto* markings = std::get_if<MarkingSet>(&*slur_selection);
  ASSERT_NE(markings, nullptr);
  ASSERT_EQ(markings->items().size(), 1u);
  EXPECT_EQ(markings->items().front().kind, MarkingKind::kSlur);
  EXPECT_EQ(markings->items().front().anchor, slur.id);
  EXPECT_TRUE(validate_selection(fixture.project, *slur_selection).empty());
}

}  // namespace
