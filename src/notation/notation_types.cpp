// SPDX-License-Identifier: Apache-2.0

#include <graphscore/notation/notation_palette.hpp>
#include <graphscore/notation/notation_types.hpp>

#include "notation_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <variant>

namespace graphscore {

char32_t smufl_codepoint(SmuflGlyph glyph) noexcept {
  switch (glyph) {
    case SmuflGlyph::kGClef:
      return U'\uE050';
    case SmuflGlyph::kCClef:
      return U'\uE05C';
    case SmuflGlyph::kFClef:
      return U'\uE062';
    case SmuflGlyph::kNoteheadWhole:
      return U'\uE0A2';
    case SmuflGlyph::kNoteheadHalf:
      return U'\uE0A3';
    case SmuflGlyph::kNoteheadBlack:
      return U'\uE0A4';
    case SmuflGlyph::kRestWhole:
      return U'\uE4E3';
    case SmuflGlyph::kRestHalf:
      return U'\uE4E4';
    case SmuflGlyph::kRestQuarter:
      return U'\uE4E5';
    case SmuflGlyph::kRestEighth:
      return U'\uE4E6';
    case SmuflGlyph::kRest16th:
      return U'\uE4E7';
    case SmuflGlyph::kRest32nd:
      return U'\uE4E8';
    case SmuflGlyph::kRest64th:
      return U'\uE4E9';
    case SmuflGlyph::kAugmentationDot:
      return U'\uE1E7';
    case SmuflGlyph::kAccidentalDoubleFlat:
      return U'\uE264';
    case SmuflGlyph::kAccidentalFlat:
      return U'\uE260';
    case SmuflGlyph::kAccidentalNatural:
      return U'\uE261';
    case SmuflGlyph::kAccidentalSharp:
      return U'\uE262';
    case SmuflGlyph::kAccidentalDoubleSharp:
      return U'\uE263';
    case SmuflGlyph::kFlag8thUp:
      return U'\uE240';
    case SmuflGlyph::kFlag16thUp:
      return U'\uE242';
    case SmuflGlyph::kFlag32ndUp:
      return U'\uE244';
    case SmuflGlyph::kFlag64thUp:
      return U'\uE246';
    case SmuflGlyph::kFlag8thDown:
      return U'\uE241';
    case SmuflGlyph::kFlag16thDown:
      return U'\uE243';
    case SmuflGlyph::kFlag32ndDown:
      return U'\uE245';
    case SmuflGlyph::kFlag64thDown:
      return U'\uE247';
    case SmuflGlyph::kDynamicP:
      return U'\uE520';
    case SmuflGlyph::kDynamicM:
      return U'\uE521';
    case SmuflGlyph::kDynamicF:
      return U'\uE522';
    case SmuflGlyph::kArticAccentAbove:
      return U'\uE4A0';
    case SmuflGlyph::kArticMarcatoAbove:
      return U'\uE4AC';
    case SmuflGlyph::kArticStaccatoAbove:
      return U'\uE4A2';
    case SmuflGlyph::kArticStaccatissimoAbove:
      return U'\uE4A6';
    case SmuflGlyph::kArticTenutoAbove:
      return U'\uE4A4';
    case SmuflGlyph::kPedalDown:
      return U'\uE650';
    case SmuflGlyph::kPedalUp:
      return U'\uE655';
    case SmuflGlyph::kTimeDigit0:
      return U'\uE080';
    case SmuflGlyph::kTupletDigit0:
      return U'\uE880';
  }
  return U'\uFFFD';
}

bool NotationRect::contains(NotationPoint point) const noexcept {
  return std::isfinite(point.x) && std::isfinite(point.y) && point.x >= x &&
         point.x <= x + width && point.y >= y && point.y <= y + height;
}

std::optional<HitResult> NotationLayout::hit_test(NotationPoint point) const {
  const HitRegion* best = nullptr;
  const auto       area = [](const HitRegion& region) {
    return region.bounds.width * region.bounds.height;
  };
  for (const HitRegion& region : hit_regions) {
    if (!finite_rect(region.bounds) || !region.bounds.contains(point)) {
      continue;
    }
    if (best == nullptr || region.priority > best->priority ||
        (region.priority == best->priority && area(region) < area(*best)) ||
        (region.priority == best->priority && area(region) == area(*best) &&
         region.semantic_id < best->semantic_id) ||
        (region.priority == best->priority && area(region) == area(*best) &&
         region.semantic_id == best->semantic_id && region.id < best->id)) {
      best = &region;
    }
  }
  if (best == nullptr) {
    return std::nullopt;
  }
  return HitResult{best->id, best->semantic_id, best->role};
}

bool NotationLayout::geometry_is_finite() const {
  if (!finite_rect(bounds) ||
      !std::all_of(
          systems.begin(), systems.end(),
          [](const SystemLayout& system) {
            return finite_rect(system.bounds) &&
                   std::all_of(system.measures.begin(), system.measures.end(),
                               [](const MeasureLayout& measure) {
                                 return finite_rect(measure.bounds);
                               }) &&
                   std::all_of(system.staves.begin(), system.staves.end(),
                               [](const StaffSystemLayout& staff) {
                                 return finite_rect(staff.bounds) &&
                                        std::all_of(
                                            staff.measure_bounds.begin(),
                                            staff.measure_bounds.end(),
                                            finite_rect);
                               });
          }) ||
      !std::all_of(
          hit_regions.begin(), hit_regions.end(),
          [](const HitRegion& region) { return finite_rect(region.bounds); })) {
    return false;
  }
  return std::all_of(commands.begin(), commands.end(), finite_command);
}

bool NotationPreview::geometry_is_finite() const {
  return std::all_of(commands.begin(), commands.end(), finite_command);
}

bool NotationLayoutOptions::valid() const noexcept {
  const double values[] = {
      system_width,          left_margin,        right_margin, top_margin,
      bottom_margin,         staff_space,        stave_gap,    system_gap,
      minimum_measure_width, whole_note_spacing,
  };
  if (!std::all_of(std::begin(values), std::end(values), [](double value) {
        return std::isfinite(value) &&
               value <= NotationLayoutOptions::kMaximumCoordinate;
      })) {
    return false;
  }
  return system_width > 0.0 && left_margin >= 0.0 && right_margin >= 0.0 &&
         top_margin >= 0.0 && bottom_margin >= 0.0 && staff_space > 0.0 &&
         stave_gap >= 0.0 && system_gap >= 0.0 && minimum_measure_width > 0.0 &&
         whole_note_spacing > 0.0 && left_margin + right_margin < system_width;
}

}  // namespace graphscore
