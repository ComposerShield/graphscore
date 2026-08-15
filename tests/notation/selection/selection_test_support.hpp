// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <graphscore/notation/graphscore_notation.hpp>

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
using graphscore::ConnectorId;
using graphscore::ConnectorItem;
using graphscore::ConnectorSet;
using graphscore::Duration;
using graphscore::Dynamic;
using graphscore::DynamicMarking;
using graphscore::extend_measure_selection;
using graphscore::extend_range_selection;
using graphscore::extend_range_selection_staff_scope;
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
using graphscore::MeasureScope;
using graphscore::MidiChannel;
using graphscore::MusicalSpan;
using graphscore::Node;
using graphscore::NodeId;
using graphscore::NodeItem;
using graphscore::NodeSet;
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
using graphscore::RangeEdge;
using graphscore::RangeSelectionSpec;
using graphscore::Rational;
using graphscore::resolve_measure_selection_at;
using graphscore::resolve_range_selection;
using graphscore::resolve_range_selection_spec;
using graphscore::resolve_selection_at;
using graphscore::Rest;
using graphscore::RestItem;
using graphscore::RestSet;
using graphscore::score_ordered_staves;
using graphscore::Selection;
using graphscore::selection_after_staff_step;
using graphscore::SelectionDragState;
using graphscore::Slur;
using graphscore::SpelledPitch;
using graphscore::StaffLayout;
using graphscore::StaffStepDirection;
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
                              std::uint16_t denominator = 4);

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
    const graphscore::NotationLayoutResult& result);

[[nodiscard]] NotePaletteState note_state(std::uint8_t voice_index = 1);

// Finds a GlyphCommand by exact id suffix (e.g. "<id>/notehead",
// "<id>/articulation/0") and returns its origin -- ground truth read out of
// the real layout, never a reproduction of notation_engraving.cpp's own
// placement formulas.
[[nodiscard]] NotationPoint glyph_origin(const NotationLayout& layout,
                                         const std::string&    target);

[[nodiscard]] NotationPoint notehead_origin(const NotationLayout&   layout,
                                            const NotationEntityId& id);

[[nodiscard]] NotationPoint rest_origin(const NotationLayout& layout,
                                        const Rest&           rest,
                                        std::uint8_t          voice_index = 1);

[[nodiscard]] NotationPoint staff_center(const NotationLayout& layout,
                                         std::size_t           staff_index = 0,
                                         std::size_t measure_index         = 0);

// Finds the "<entity>/stem/hit" HitRegion and returns a point inside it
// that is guaranteed clear of the notehead's own (higher-priority, but
// narrower) hit region: the far end of the stem, away from the notehead.
[[nodiscard]] NotationPoint stem_click_point(const NotationLayout&   layout,
                                             const NotationEntityId& entity);

// Finds a HitRegion by exact id, or nullptr when the layout emits none --
// the ground truth for both "this region exists with these properties" and
// "this region is deliberately absent" assertions.
[[nodiscard]] const HitRegion* find_hit_region(const NotationLayout& layout,
                                               const std::string&    target);

// Finds a HitRegion by exact id (e.g. a span-family marking's own
// "<id>/<role>/segment/system-N/hit" region, which -- unlike a glyph's own
// hit region -- has no single GlyphCommand origin to read a click point
// from) and returns the center of its bounds.
[[nodiscard]] NotationPoint hit_region_center(const NotationLayout& layout,
                                              const std::string&    target);

// The "<entity>/notehead-column/hit" HitRegion id -- see
// selection_hit_notehead_column_test.cpp for the region this names.
[[nodiscard]] std::string column_hit_id(const NotationEntityId& entity);

// A point in the vertical gap between two noteheads: inside neither
// notehead's own hit region, but inside the column region that spans both.
// Read out of the real layout, never a reproduction of notation_engraving.cpp's
// own placement formulas.
[[nodiscard]] NotationPoint notehead_gap_point(const NotationLayout&   layout,
                                               const NotationEntityId& lower,
                                               const NotationEntityId& upper);

// A two-note chord a third apart, both noteheads on staff lines (E4 bottom
// line, G4 second line) so the gap between them lies inside the staff's own
// bounds -- the click there must beat the container hit regions, not merely
// land outside them.
[[nodiscard]] std::vector<ChordNote> two_chord_notes(
    graphscore::Accidental lower_accidental = graphscore::Accidental::kNatural);

// A measure's own left/right edges are exact time_at_x fixed points
// (fraction 0 and fraction 1, per time_at_x's own clamp), so a drag across
// them yields the exact measure_start/measure_start+measure_length span
// without reproducing any engraving placement formula.
[[nodiscard]] NotationPoint measure_left_edge(const NotationLayout& layout,
                                              std::size_t system_index,
                                              std::size_t staff_index,
                                              std::size_t measure_index);

[[nodiscard]] NotationPoint measure_right_edge(const NotationLayout& layout,
                                               std::size_t system_index,
                                               std::size_t staff_index,
                                               std::size_t measure_index);
