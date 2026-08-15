// SPDX-License-Identifier: Apache-2.0

#include "selection_test_support.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include <graphscore/notation/graphscore_notation.hpp>

Measure measure(std::uint8_t numerator, std::uint16_t denominator) {
  return Measure{*TimeSignature::create(numerator, denominator),
                 KeySignature{}};
}

NotationLayout require_layout(const graphscore::NotationLayoutResult& result) {
  EXPECT_TRUE(result);
  return *result.layout;
}

NotePaletteState note_state(std::uint8_t voice_index) {
  return *NotePaletteState::create(NoteValue::kQuarter, 0,
                                   NotePaletteEntryKind::kNote,
                                   *Voice::create(voice_index));
}

// Finds a GlyphCommand by exact id suffix (e.g. "<id>/notehead",
// "<id>/articulation/0") and returns its origin -- ground truth read out of
// the real layout, never a reproduction of notation_engraving.cpp's own
// placement formulas.
NotationPoint glyph_origin(const NotationLayout& layout,
                           const std::string&    target) {
  const auto found =
      std::ranges::find_if(layout.commands, [&](const auto& command) {
        const auto* glyph = std::get_if<GlyphCommand>(&command);
        return glyph != nullptr && glyph->id.value == target;
      });
  EXPECT_NE(found, layout.commands.end());
  return std::get<GlyphCommand>(*found).origin;
}

NotationPoint notehead_origin(const NotationLayout&   layout,
                              const NotationEntityId& id) {
  return glyph_origin(layout, id.to_string() + "/notehead");
}

NotationPoint rest_origin(const NotationLayout& layout, const Rest& rest,
                          std::uint8_t voice_index) {
  return glyph_origin(layout, rest.id.to_string() + "/voice/" +
                                  std::to_string(voice_index) + "/rest");
}

NotationPoint staff_center(const NotationLayout& layout,
                           std::size_t staff_index, std::size_t measure_index) {
  const auto& staff = layout.systems[0].staves[staff_index];
  return NotationPoint{staff.measure_bounds[measure_index].x +
                           staff.measure_bounds[measure_index].width * 0.5,
                       staff.bounds.y + staff.bounds.height * 0.5};
}

// Finds the "<entity>/stem/hit" HitRegion and returns a point inside it
// that is guaranteed clear of the notehead's own (higher-priority, but
// narrower) hit region: the far end of the stem, away from the notehead.
NotationPoint stem_click_point(const NotationLayout&   layout,
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
const HitRegion* find_hit_region(const NotationLayout& layout,
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
NotationPoint hit_region_center(const NotationLayout& layout,
                                const std::string&    target) {
  const HitRegion* found = find_hit_region(layout, target);
  EXPECT_NE(found, nullptr);
  if (found == nullptr) {
    return NotationPoint{};
  }
  return NotationPoint{found->bounds.x + found->bounds.width * 0.5,
                       found->bounds.y + found->bounds.height * 0.5};
}

std::string column_hit_id(const NotationEntityId& entity) {
  return entity.to_string() + "/notehead-column/hit";
}

// A point in the vertical gap between two noteheads: inside neither
// notehead's own hit region, but inside the column region that spans both.
// Read out of the real layout, never a reproduction of notation_engraving.cpp's
// own placement formulas.
NotationPoint notehead_gap_point(const NotationLayout&   layout,
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
std::vector<ChordNote> two_chord_notes(
    graphscore::Accidental lower_accidental) {
  return {
      {NotationEntityId::generate(),
       *SpelledPitch::create(Letter::kE, 4, lower_accidental), false},
      {NotationEntityId::generate(), *SpelledPitch::create(Letter::kG, 4),
       false},
  };
}

NotationPoint measure_left_edge(const NotationLayout& layout,
                                std::size_t           system_index,
                                std::size_t           staff_index,
                                std::size_t           measure_index) {
  const auto& staff   = layout.systems[system_index].staves[staff_index];
  const auto& measure = layout.systems[system_index].measures[measure_index];
  return NotationPoint{measure.bounds.x,
                       staff.bounds.y + staff.bounds.height * 0.5};
}

NotationPoint measure_right_edge(const NotationLayout& layout,
                                 std::size_t           system_index,
                                 std::size_t           staff_index,
                                 std::size_t           measure_index) {
  const auto& staff   = layout.systems[system_index].staves[staff_index];
  const auto& measure = layout.systems[system_index].measures[measure_index];
  return NotationPoint{measure.bounds.x + measure.bounds.width,
                       staff.bounds.y + staff.bounds.height * 0.5};
}
