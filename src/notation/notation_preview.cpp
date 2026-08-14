// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/project.hpp>
#include <graphscore/notation/notation_palette.hpp>

#include "engraving.hpp"
#include "hit_resolution_internal.hpp"
#include "layout_builder.hpp"
#include "measure_math.hpp"
#include "notation_geometry.hpp"
#include "notation_ids.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace graphscore {

// A system plus the staff `point` attributes to within it: shared by
// preview_note_entry and resolve_selection_at, both of which need exactly
// the same point -> staff attribution.
[[nodiscard]] std::optional<ResolvedStaffSite> resolve_staff_at(
    const NotationLayout& layout, NotationPoint point) {
  const SystemLayout* system = nullptr;
  for (const SystemLayout& candidate : layout.systems) {
    if (candidate.bounds.contains(point)) {
      system = &candidate;
      break;
    }
  }
  if (system == nullptr) {
    return std::nullopt;
  }

  // The nearest staff by vertical center, not strict containment: a click
  // in the ledger-line/marking lane above or below a staff's own five lines
  // must still resolve to that staff. But SystemLayout::bounds reserves a
  // large per-system marking budget (system_top_padding plus a
  // staff_slot_height per stave, up to 256 staff-spaces for a single-staff
  // system) so system containment alone is not a bounded proximity check --
  // a click far below every staff, still inside that oversized system
  // extent, must not silently attribute to the nearest one. kLedgerLaneSpaces
  // matches system_top_padding's own six-staff-space marking budget: the
  // window a click may fall in above/below a staff's own five lines and
  // still resolve to it.
  constexpr double         kLedgerLaneSpaces = 6.0;
  const StaffSystemLayout* staff             = nullptr;
  double staff_distance = std::numeric_limits<double>::infinity();
  for (const StaffSystemLayout& candidate : system->staves) {
    const double candidate_space = candidate.bounds.height / 4.0;
    const double allowed =
        candidate.bounds.height * 0.5 + kLedgerLaneSpaces * candidate_space;
    const double center   = candidate.bounds.y + candidate.bounds.height * 0.5;
    const double distance = std::abs(point.y - center);
    if (distance <= allowed && distance < staff_distance) {
      staff_distance = distance;
      staff          = &candidate;
    }
  }
  if (staff == nullptr) {
    return std::nullopt;
  }
  return ResolvedStaffSite{system, staff};
}

[[nodiscard]] const MeasureLayout* resolve_measure_at(
    const SystemLayout& system, NotationPoint point) {
  for (const MeasureLayout& candidate : system.measures) {
    if (point.x >= candidate.bounds.x &&
        point.x <= candidate.bounds.x + candidate.bounds.width) {
      return &candidate;
    }
  }
  return nullptr;
}

// The staff/measure/nearest-legal-onset `point` resolves to for `voice`:
// the resolution preview_note_entry previews and resolve_selection_at's
// insertion-caret arm snaps to, factored out so the two can never disagree
// about it.
[[nodiscard]] std::optional<ResolvedInsertionSite> resolve_insertion_site(
    const Project& project, const NotationLayout& layout, Voice voice,
    NotationPoint point) {
  const std::optional<ResolvedStaffSite> site = resolve_staff_at(layout, point);
  if (!site.has_value()) {
    return std::nullopt;
  }
  const MeasureLayout* measure = resolve_measure_at(*site->system, point);
  if (measure == nullptr) {
    return std::nullopt;
  }

  const Node* node = project.find_node(layout.node_id);
  if (node == nullptr) {
    return std::nullopt;
  }
  const NodeTimeline* timeline = node->timeline();
  if (timeline == nullptr) {
    return std::nullopt;
  }
  const MeasureMap& measures = timeline->measures();
  if (measure->ordinal >= measures.measure_count()) {
    return std::nullopt;
  }
  const TrackLane* lane = node->lane(site->staff->track_id);
  if (lane == nullptr) {
    return std::nullopt;
  }
  const StaveVoices* voices = lane->stave(site->staff->stave_id);
  if (voices == nullptr) {
    return std::nullopt;
  }
  const VoiceContent& content = voices->voice(voice);

  const Rational measure_start  = measures.measure_start(measure->ordinal);
  const Rational measure_length = measures.measure_length(measure->ordinal);
  const double   staff_space    = site->staff->bounds.height / 4.0;
  const double   reference_time =
      time_at_x(measures, measure->ordinal, point.x, measure->bounds.x,
                measure->bounds.width, staff_space);

  // The durations to scan for onsets: `voice`'s own events, unless it is
  // entirely empty, in which case the voice-stream workflow resolves
  // against the durations of the same measure-aligned rest fill a click
  // would materialize (decompose_measure_aligned_rests, voice_content.hpp).
  // A voice that merely has no event boundary within the resolved measure
  // (but is not itself empty) is not covered by this substitution and
  // still yields std::nullopt below via an unmatched scan.
  std::vector<Rational> event_durations;
  if (content.events().empty()) {
    // Reads only the hypothetical fill's shape, so the duration-only core
    // (decompose_measure_aligned_rest_durations, voice_content.hpp) is used
    // directly rather than decompose_measure_aligned_rests: every pointer
    // move through an empty voice would otherwise mint and immediately
    // discard a fresh Rest id per term.
    const std::optional<std::vector<Duration>> hypothetical_fill =
        decompose_measure_aligned_rest_durations(*timeline);
    if (!hypothetical_fill.has_value()) {
      return std::nullopt;
    }
    event_durations.reserve(hypothetical_fill->size());
    for (const Duration& duration : *hypothetical_fill) {
      event_durations.push_back(duration.resolved());
    }
  } else {
    event_durations.reserve(content.events().size());
    for (const VoiceEvent& event : content.events()) {
      event_durations.push_back(event_duration(event).resolved());
    }
  }

  // Snaps to the start of an existing rhythmic event (including a
  // normalized rest, real or hypothetical) in `voice`, scoped to the
  // resolved measure -- never a metric grid derived from an armed duration.
  // Onsets are visited in order, and only a strictly smaller distance
  // replaces the current best, so on an exact tie the earlier onset wins
  // deterministically.
  bool     found_onset   = false;
  double   best_distance = std::numeric_limits<double>::infinity();
  Rational resolved_onset;
  Rational onset;
  for (const Rational& event_dur : event_durations) {
    if (onset >= measure_start && onset < measure_start + measure_length) {
      const double distance = std::abs(onset.to_double() - reference_time);
      if (distance < best_distance) {
        best_distance  = distance;
        resolved_onset = onset;
        found_onset    = true;
      }
    }
    onset = onset + event_dur;
  }
  if (!found_onset) {
    return std::nullopt;
  }

  return ResolvedInsertionSite{site->staff,    measure, timeline,
                               resolved_onset, lane,    &content};
}

std::optional<NotationPreview> preview_note_entry(
    const Project& project, const NotationLayout& layout,
    const NotePaletteState& palette, NotationPoint point) {
  if (!finite_point(point)) {
    return std::nullopt;
  }
  const std::optional<ResolvedInsertionSite> site =
      resolve_insertion_site(project, layout, palette.voice(), point);
  if (!site.has_value()) {
    return std::nullopt;
  }
  const StaffSystemLayout* staff          = site->staff;
  const MeasureLayout*     measure        = site->measure;
  const NodeTimeline*      timeline       = site->timeline;
  const Rational           resolved_onset = site->resolved_onset;
  const double             staff_space    = staff->bounds.height / 4.0;

  const ClefLane* clef_lane = timeline->clef_lane(staff->stave_id);
  const Clef      clef =
      clef_lane == nullptr ? Clef::kTreble : clef_lane->clef_at(resolved_onset);

  const double glyph_x =
      position_x(timeline->measures(), measure->ordinal, measure->bounds.width,
                 resolved_onset, measure->bounds.x, staff_space);

  NotationPreview preview;
  preview.track_id        = staff->track_id;
  preview.stave_id        = staff->stave_id;
  preview.voice           = palette.voice();
  preview.entry_kind      = palette.entry_kind();
  preview.candidate_onset = resolved_onset;

  double glyph_y = 0.0;
  if (palette.entry_kind() == NotePaletteEntryKind::kNote) {
    const std::optional<SpelledPitch> candidate_pitch =
        spelled_pitch_at(point.y, clef, staff->bounds.y, staff_space);
    if (!candidate_pitch.has_value()) {
      return std::nullopt;
    }
    preview.candidate_pitch = candidate_pitch;
    glyph_y = pitch_y(*candidate_pitch, clef, staff->bounds.y, staff_space);
  } else {
    glyph_y = event_y(palette.voice(), staff->bounds.y, staff_space);
  }
  if (!bounded_point({glyph_x, glyph_y})) {
    return std::nullopt;
  }

  const NoteValue base_value = palette.resolved_duration().base();
  const bool      is_note = palette.entry_kind() == NotePaletteEntryKind::kNote;
  const char32_t  code_point = smufl_codepoint(
      is_note ? notehead_glyph(base_value) : rest_glyph(base_value));
  preview.commands.emplace_back(
      GlyphCommand{make_id("preview", is_note ? "notehead" : "rest"),
                   code_point,
                   {glyph_x, glyph_y},
                   staff_space});
  for (std::uint8_t dot = 0; dot < palette.dots(); ++dot) {
    preview.commands.emplace_back(
        GlyphCommand{make_id("preview", "dot/" + std::to_string(dot)),
                     smufl_codepoint(SmuflGlyph::kAugmentationDot),
                     {glyph_x + staff_space * (1.2 + dot * 0.65),
                      glyph_y - staff_space * 0.25},
                     staff_space});
  }
  return preview;
}

}  // namespace graphscore
