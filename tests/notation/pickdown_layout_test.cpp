// SPDX-License-Identifier: Apache-2.0

#include <graphscore/notation/graphscore_notation.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

using graphscore::Clef;
using graphscore::Duration;
using graphscore::GlyphCommand;
using graphscore::GlyphMetrics;
using graphscore::GlyphMetricsValue;
using graphscore::KeySignature;
using graphscore::layout_notation;
using graphscore::Letter;
using graphscore::LineCommand;
using graphscore::make_note;
using graphscore::Measure;
using graphscore::MidiChannel;
using graphscore::Node;
using graphscore::NodeId;
using graphscore::NodeTimeline;
using graphscore::NotationEntityId;
using graphscore::NotationInvalidation;
using graphscore::NotationInvalidationKind;
using graphscore::NotationLayout;
using graphscore::NotationLayoutCache;
using graphscore::NotationLayoutOptions;
using graphscore::NotationPoint;
using graphscore::NotationRect;
using graphscore::NoteValue;
using graphscore::Project;
using graphscore::ProjectId;
using graphscore::Rational;
using graphscore::SpelledPitch;
using graphscore::StaffLayout;
using graphscore::StaveDefinition;
using graphscore::StaveId;
using graphscore::TimeSignature;
using graphscore::TrackId;
using graphscore::Voice;

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

// Custom metrics whose glyph box is deliberately asymmetric and wider than the
// FixedMetrics box (-0.25 .. +1.25 staff spaces), so containment must derive
// from the injected metrics rather than any hardcoded bounding-box assumption.
class AsymmetricMetrics final : public GlyphMetrics {
 public:
  [[nodiscard]] GlyphMetricsValue glyph_metrics(
      char32_t /*code_point*/, double staff_space) const override {
    return GlyphMetricsValue{
        NotationRect{-staff_space * 0.9, -staff_space * 0.5, staff_space * 2.8,
                     staff_space * 2.0},
        staff_space * 2.8};
  }

  [[nodiscard]] double kerning(char32_t /*left*/, char32_t /*right*/,
                               double /*staff_space*/) const override {
    return 0.0;
  }
};

// Codepoint-sensitive metrics: only the dynamic glyphs get a left extent far
// wider than the main region's fixed half-space clamp and than the one-space
// minimum content inset, so a boundary-anchored dynamic's left budgeting must
// derive from the injected metrics rather than the notehead's own box.
class WideLeftDynamicMetrics final : public GlyphMetrics {
 public:
  [[nodiscard]] GlyphMetricsValue glyph_metrics(
      char32_t code_point, double staff_space) const override {
    const bool dynamic =
        code_point ==
            graphscore::smufl_codepoint(graphscore::SmuflGlyph::kDynamicP) ||
        code_point ==
            graphscore::smufl_codepoint(graphscore::SmuflGlyph::kDynamicM) ||
        code_point ==
            graphscore::smufl_codepoint(graphscore::SmuflGlyph::kDynamicF);
    return GlyphMetricsValue{
        NotationRect{dynamic ? -staff_space * 1.6 : -staff_space * 0.25,
                     -staff_space * 0.5,
                     dynamic ? staff_space * 0.6 : staff_space * 1.5,
                     staff_space * 2.0},
        staff_space * 1.5};
  }

  [[nodiscard]] double kerning(char32_t /*left*/, char32_t /*right*/,
                               double /*staff_space*/) const override {
    return 0.0;
  }
};

struct PickdownFixture {
  Project project{ProjectId::generate(), "PickdownLayout"};
  NodeId  node_id;
  TrackId track_id;
  StaveId stave_id;
};

[[nodiscard]] PickdownFixture build(std::size_t   measure_count = 1,
                                    std::uint8_t  numerator     = 4,
                                    std::uint16_t denominator   = 4) {
  PickdownFixture fixture;
  const auto      midi_channel = MidiChannel::create(0);
  EXPECT_TRUE(midi_channel.has_value());
  const auto track_added = fixture.project.add_track(
      "Track", StaffLayout::single_staff(Clef::kTreble), *midi_channel);
  EXPECT_TRUE(track_added.has_value());
  fixture.track_id = *track_added;
  fixture.node_id  = fixture.project.add_node("Node");
  auto* lane =
      fixture.project.find_node(fixture.node_id)->lane(fixture.track_id);
  fixture.stave_id = fixture.project.active_tracks()[0].layout().staves()[0].id;
  lane->ensure_stave(fixture.stave_id);

  const auto signature = TimeSignature::create(numerator, denominator);
  EXPECT_TRUE(signature.has_value());
  std::vector<Measure> measures(measure_count,
                                Measure{*signature, KeySignature{}});
  auto                 timeline = NodeTimeline::create(
      std::move(measures), {StaveDefinition{fixture.stave_id, Clef::kTreble}});
  EXPECT_TRUE(timeline.has_value());
  fixture.project.find_node(fixture.node_id)
      ->set_timeline(std::move(*timeline));
  return fixture;
}

[[nodiscard]] const LineCommand* find_line(const NotationLayout& layout,
                                           const std::string&    suffix) {
  for (const auto& command : layout.commands) {
    const auto* line = std::get_if<LineCommand>(&command);
    if (line != nullptr && line->id.value.size() >= suffix.size() &&
        line->id.value.compare(line->id.value.size() - suffix.size(),
                               suffix.size(), suffix) == 0) {
      return line;
    }
  }
  return nullptr;
}

[[nodiscard]] const GlyphCommand* find_glyph(const NotationLayout& layout,
                                             const std::string&    suffix) {
  for (const auto& command : layout.commands) {
    const auto* glyph = std::get_if<GlyphCommand>(&command);
    if (glyph != nullptr && glyph->id.value.size() >= suffix.size() &&
        glyph->id.value.compare(glyph->id.value.size() - suffix.size(),
                                suffix.size(), suffix) == 0) {
      return glyph;
    }
  }
  return nullptr;
}

[[nodiscard]] NotationLayout require_layout(
    const graphscore::NotationLayoutResult& result) {
  EXPECT_TRUE(result);
  return *result.layout;
}

// ---- M5-phase-31 equivalent-family fixtures --------------------------------

[[nodiscard]] graphscore::VoiceContent& content_of(
    PickdownFixture& fixture, std::uint8_t voice_index = 1) {
  return fixture.project.find_node(fixture.node_id)
      ->lane(fixture.track_id)
      ->stave(fixture.stave_id)
      ->voice(*Voice::create(voice_index));
}

void set_pickdown(PickdownFixture& fixture, Rational duration) {
  Node* node = const_cast<Node*>(fixture.project.find_node(fixture.node_id));
  ASSERT_TRUE(node->timeline()->set_pickdown(duration).ok());
}

void fill_main_quarters(PickdownFixture& fixture, std::uint8_t voice_index = 1,
                        int count = 4) {
  const Duration     quarter = *Duration::create(NoteValue::kQuarter, 0);
  const SpelledPitch c4      = *SpelledPitch::create(Letter::kC, 4);
  for (int index = 0; index < count; ++index) {
    ASSERT_TRUE(
        content_of(fixture, voice_index).append(make_note(c4, quarter)).ok());
  }
}

void normalize_voice(PickdownFixture& fixture, std::uint8_t voice_index = 1) {
  const Node* node = fixture.project.find_node(fixture.node_id);
  ASSERT_TRUE(content_of(fixture, voice_index)
                  .normalize(node->timeline()->node_end())
                  .ok());
}

[[nodiscard]] bool has_command(const NotationLayout& layout,
                               const std::string&    needle) {
  return std::ranges::any_of(
      layout.commands, [&](const graphscore::NotationCommand& command) {
        return std::visit(
            [&](const auto& concrete) {
              return concrete.id.value.find(needle) != std::string::npos;
            },
            command);
      });
}

[[nodiscard]] const graphscore::HitRegion* find_hit(
    const NotationLayout& layout, const std::string& suffix) {
  const auto found = std::ranges::find_if(
      layout.hit_regions, [&](const graphscore::HitRegion& hit) {
        return hit.id.value.size() >= suffix.size() &&
               hit.id.value.compare(hit.id.value.size() - suffix.size(),
                                    suffix.size(), suffix) == 0;
      });
  return found == layout.hit_regions.end() ? nullptr : &*found;
}

TEST(PickdownLayoutTest, NoPickdownLeavesNoBoundaryGeometry) {
  PickdownFixture      fixture = build();
  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  EXPECT_EQ(find_line(layout, "pickdown-boundary/first"), nullptr);
  EXPECT_EQ(find_line(layout, "pickdown-end-barline"), nullptr);
  EXPECT_NE(find_line(layout, "measure/0/end-barline"), nullptr);
}

TEST(PickdownLayoutTest, PickdownDrawsDistinctBoundaryAndEndBarline) {
  PickdownFixture fixture  = build();
  const auto      duration = Rational::create(1, 4);
  ASSERT_TRUE(duration.has_value());
  Node* node = const_cast<Node*>(fixture.project.find_node(fixture.node_id));
  ASSERT_TRUE(node->timeline()->set_pickdown(*duration).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  ASSERT_EQ(layout.systems.size(), 1u);
  ASSERT_EQ(layout.systems[0].measures.size(), 1u);

  const LineCommand* first  = find_line(layout, "pickdown-boundary/first");
  const LineCommand* second = find_line(layout, "pickdown-boundary/second");
  const LineCommand* end    = find_line(layout, "pickdown-end-barline");
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);
  ASSERT_NE(end, nullptr);

  // The ordinary final barline is replaced, not reused, at the boundary.
  EXPECT_EQ(find_line(layout, "measure/0/end-barline"), nullptr);

  // The double barline is distinct: two lines ordered before the end barline.
  EXPECT_GT(second->from.x, first->from.x);
  EXPECT_GT(end->from.x, second->from.x);
  EXPECT_EQ(first->from.x, first->to.x);
  EXPECT_EQ(second->from.y, first->from.y);

  // Deterministic width: a 1/4 pickdown under the final 4/4 measure occupies
  // exactly one quarter of the measure's own width.
  const double measure_width = layout.systems[0].measures[0].bounds.width;
  const double region_width  = end->from.x - first->from.x;
  EXPECT_NEAR(region_width, measure_width * 0.25, 1e-9);

  // The staff's horizontal extent includes the pickdown region.
  const double staff_width = layout.systems[0].staves[0].bounds.width;
  EXPECT_NEAR(staff_width, measure_width + region_width, 1e-9);
}

TEST(PickdownLayoutTest, PickdownDoesNotCreateAFakeMeasureMapMeasure) {
  PickdownFixture fixture = build();
  Node* node = const_cast<Node*>(fixture.project.find_node(fixture.node_id));
  EXPECT_EQ(node->timeline()->measures().measure_count(), 1u);
  ASSERT_TRUE(node->timeline()->set_pickdown(*Rational::create(1, 4)).ok());
  EXPECT_EQ(node->timeline()->measures().measure_count(), 1u);

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  EXPECT_EQ(layout.systems[0].measures.size(), 1u);
}

TEST(PickdownLayoutTest, PickdownNoteRendersInsideTheRegion) {
  PickdownFixture fixture = build();
  const auto      voice   = *Voice::create(1);
  const auto      quarter = *Duration::create(NoteValue::kQuarter, 0);
  const auto      c4      = *SpelledPitch::create(Letter::kC, 4);
  auto&           content = fixture.project.find_node(fixture.node_id)
                      ->lane(fixture.track_id)
                      ->stave(fixture.stave_id)
                      ->voice(voice);
  for (std::size_t index = 0; index < 4; ++index) {
    ASSERT_TRUE(content.append(make_note(c4, quarter)).ok());
  }
  Node* node = const_cast<Node*>(fixture.project.find_node(fixture.node_id));
  ASSERT_TRUE(node->timeline()->set_pickdown(*Rational::create(1, 4)).ok());
  const auto             g4        = *SpelledPitch::create(Letter::kG, 4);
  const graphscore::Note tail_note = make_note(g4, quarter);
  const NotationEntityId tail_id   = tail_note.id;
  ASSERT_TRUE(content.append(std::move(tail_note)).ok());
  ASSERT_TRUE(content.normalize(node->timeline()->node_end()).ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  const LineCommand* first = find_line(layout, "pickdown-boundary/first");
  const LineCommand* end   = find_line(layout, "pickdown-end-barline");
  ASSERT_NE(first, nullptr);
  ASSERT_NE(end, nullptr);

  const GlyphCommand* notehead =
      find_glyph(layout, tail_id.to_string() + "/notehead");
  ASSERT_NE(notehead, nullptr);
  EXPECT_GE(notehead->origin.x, first->from.x);
  EXPECT_LT(notehead->origin.x, end->from.x);
}

TEST(PickdownLayoutTest, LayoutIsDeterministic) {
  PickdownFixture fixture = build();
  Node* node = const_cast<Node*>(fixture.project.find_node(fixture.node_id));
  ASSERT_TRUE(node->timeline()->set_pickdown(*Rational::create(1, 4)).ok());

  const FixedMetrics   metrics;
  const NotationLayout first = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationLayout second = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  EXPECT_EQ(first.commands, second.commands);
  EXPECT_EQ(first.systems, second.systems);
  EXPECT_EQ(first.hit_regions, second.hit_regions);
}

// A pickdown duration change is a kMeasureStructure invalidation at the final
// measure, so the incremental cache rebuilds only the final system's geometry
// and reuses every earlier system (M5-phase-31 "correct incremental-cache
// invalidation when duration changes").
TEST(PickdownLayoutTest, DurationChangeInvalidatesOnlyTheFinalSystem) {
  PickdownFixture fixture = build(4);
  Node* node = const_cast<Node*>(fixture.project.find_node(fixture.node_id));

  NotationLayoutOptions options;
  options.system_width          = 120.0;
  options.left_margin           = 20.0;
  options.right_margin          = 20.0;
  options.minimum_measure_width = 120.0;
  options.whole_note_spacing    = 120.0;

  const FixedMetrics  metrics;
  NotationLayoutCache cache;
  const auto          initial =
      cache.update(fixture.project, fixture.node_id, metrics, options, {});
  ASSERT_TRUE(initial);

  ASSERT_TRUE(node->timeline()->set_pickdown(*Rational::create(1, 4)).ok());
  const auto updated =
      cache.update(fixture.project, fixture.node_id, metrics, options,
                   {NotationInvalidation{
                       NotationInvalidationKind::kMeasureStructure, 3, 3}});
  ASSERT_TRUE(updated);
  ASSERT_TRUE(updated.layout.has_value());

  ASSERT_EQ(updated.work.rebuilt_systems.size(), 1u);
  EXPECT_EQ(updated.work.rebuilt_systems[0], 3u);
  ASSERT_EQ(updated.work.reused_systems.size(), 3u);
  EXPECT_NE(find_line(*updated.layout, "pickdown-boundary/first"), nullptr);
  // The retained earlier systems carry no pickdown geometry of their own.
  for (std::size_t system = 0; system < 3; ++system) {
    const std::string boundary_id = fixture.stave_id.to_string() + "/system/" +
                                    std::to_string(system) +
                                    "/pickdown-boundary/first";
    EXPECT_EQ(find_line(*updated.layout, boundary_id), nullptr);
  }
}

TEST(PickdownLayoutTest, EngravesRhythmAndMarkingFamiliesInTheRegion) {
  PickdownFixture fixture = build();
  fill_main_quarters(fixture);
  set_pickdown(fixture, *Rational::create(3, 4));
  const Duration     eighth  = *Duration::create(NoteValue::kEighth, 0);
  const Duration     quarter = *Duration::create(NoteValue::kQuarter, 0);
  const SpelledPitch e4      = *SpelledPitch::create(Letter::kE, 4);
  // Two eighths: an automatic beam inside the pickdown.
  ASSERT_TRUE(content_of(fixture).append(make_note(e4, eighth)).ok());
  ASSERT_TRUE(content_of(fixture).append(make_note(e4, eighth)).ok());
  // One quarter carrying an articulation, a dynamic, and a grace group.
  const graphscore::Note marked =
      make_note(e4, quarter, false, {graphscore::Articulation::kAccent});
  ASSERT_TRUE(content_of(fixture).append(marked).ok());
  ASSERT_TRUE(content_of(fixture)
                  .add_dynamic(graphscore::make_dynamic_marking(
                      marked.id, graphscore::Dynamic::kFf))
                  .ok());
  const graphscore::GraceNote grace{
      graphscore::NotationEntityId::generate(), e4,
      *Duration::create(NoteValue::kSixteenth, 0),
      graphscore::GraceNoteType::kAcciaccatura, true};
  ASSERT_TRUE(
      content_of(fixture)
          .add_grace_group(graphscore::make_grace_group(marked.id, {grace}))
          .ok());
  // A 3:2 eighth triplet, one quarter long.
  const auto     ratio   = *graphscore::TupletRatio::create(3, 2);
  const Duration triplet = *Duration::create(NoteValue::kEighth, 0, ratio);
  for (int index = 0; index < 3; ++index) {
    ASSERT_TRUE(content_of(fixture).append(make_note(e4, triplet)).ok());
  }
  normalize_voice(fixture);

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  EXPECT_TRUE(has_command(layout, "/beam/to/"));
  EXPECT_TRUE(has_command(layout, "/tuplet/bracket"));
  EXPECT_TRUE(has_command(layout, "/articulation/"));
  EXPECT_TRUE(has_command(layout, "/grace-notehead"));
  EXPECT_TRUE(has_command(layout, "/glyph/"));
}

TEST(PickdownLayoutTest, EngravesBoundaryCrossingSpansInTheRegion) {
  PickdownFixture fixture = build();
  const Duration  quarter = *Duration::create(NoteValue::kQuarter, 0);
  const auto      c4      = *SpelledPitch::create(Letter::kC, 4);
  const auto      g4      = *SpelledPitch::create(Letter::kG, 4);
  std::vector<graphscore::Note> notes;
  for (int index = 0; index < 3; ++index) {
    notes.push_back(make_note(c4, quarter));
    ASSERT_TRUE(content_of(fixture).append(notes.back()).ok());
  }
  // The fourth main quarter ties across the boundary into the pickdown note.
  const graphscore::Note tie_source =
      make_note(g4, quarter, /*tied_to_next=*/true);
  notes.push_back(tie_source);
  ASSERT_TRUE(content_of(fixture).append(tie_source).ok());
  set_pickdown(fixture, *Rational::create(1, 4));
  const graphscore::Note tail = make_note(g4, quarter);
  ASSERT_TRUE(content_of(fixture).append(tail).ok());
  normalize_voice(fixture);

  ASSERT_TRUE(content_of(fixture)
                  .add_slur(graphscore::make_slur(notes.front().id, tail.id))
                  .ok());
  ASSERT_TRUE(content_of(fixture)
                  .add_hairpin(graphscore::make_hairpin(
                      notes.front().id, tail.id,
                      graphscore::HairpinDirection::kCrescendo))
                  .ok());
  auto* lane =
      fixture.project.find_node(fixture.node_id)->lane(fixture.track_id);
  ASSERT_TRUE(lane->add_pedal_span(fixture.stave_id,
                                   graphscore::make_pedal_span(
                                       graphscore::Rational(0),
                                       *graphscore::Rational::create(5, 4)))
                  .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  EXPECT_TRUE(has_command(layout, "/tie/segment/pickdown"));
  EXPECT_TRUE(has_command(layout, "/slur/segment/pickdown"));
  EXPECT_TRUE(has_command(layout, "/hairpin/segment/pickdown"));
  EXPECT_TRUE(has_command(layout, "/pedal/segment/pickdown"));
  // The pickdown pedal segment spans the tail region, not the main measures.
  const LineCommand* pedal_line =
      find_line(layout, "/pedal/segment/pickdown/line");
  ASSERT_NE(pedal_line, nullptr);
  const LineCommand* boundary = find_line(layout, "pickdown-boundary/first");
  ASSERT_NE(boundary, nullptr);
  EXPECT_GE(pedal_line->from.x, boundary->from.x);
}

TEST(PickdownLayoutTest, EmitsStemlessChordColumnInTheRegion) {
  // A 5/4 final measure admits a whole-note pickdown, so a whole-note chord
  // at the boundary is stemless and must expose a notehead-column hit region.
  PickdownFixture fixture = build(1, 5, 4);
  fill_main_quarters(fixture, 1, 5);
  set_pickdown(fixture, *Rational::create(1, 1));
  const Duration          whole = *Duration::create(NoteValue::kWhole, 0);
  const auto              c4    = *SpelledPitch::create(Letter::kC, 4);
  const auto              e4    = *SpelledPitch::create(Letter::kE, 4);
  const graphscore::Chord chord = graphscore::make_chord(
      whole, {{graphscore::NotationEntityId::generate(), c4, false},
              {graphscore::NotationEntityId::generate(), e4, false}});
  ASSERT_TRUE(content_of(fixture).append(chord).ok());
  normalize_voice(fixture);

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const auto* column = find_hit(layout, "/notehead-column/hit");
  ASSERT_NE(column, nullptr);
  EXPECT_EQ(column->role, graphscore::HitRole::kEvent);
  // The column sits inside the pickdown region.
  const LineCommand* first = find_line(layout, "pickdown-boundary/first");
  const LineCommand* end   = find_line(layout, "pickdown-end-barline");
  ASSERT_NE(first, nullptr);
  ASSERT_NE(end, nullptr);
  EXPECT_GE(column->bounds.x, first->from.x);
  EXPECT_LE(column->bounds.x + column->bounds.width, end->from.x);
}

TEST(PickdownLayoutTest, CountsTailEventsInVoiceLayout) {
  PickdownFixture fixture = build();
  fill_main_quarters(fixture);
  set_pickdown(fixture, *Rational::create(1, 4));
  const Duration     quarter = *Duration::create(NoteValue::kQuarter, 0);
  const SpelledPitch c4      = *SpelledPitch::create(Letter::kC, 4);
  ASSERT_TRUE(content_of(fixture).append(make_note(c4, quarter)).ok());
  normalize_voice(fixture);

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  ASSERT_EQ(layout.systems.size(), 1u);
  ASSERT_EQ(layout.systems[0].staves.size(), 1u);
  // Voice 1 carries 4 main quarters + 1 pickdown note = 5 events.
  EXPECT_EQ(layout.systems[0].staves[0].voices[0].event_count, 5u);
}

TEST(PickdownLayoutTest, TinyPickdownOrdersBoundaryAndContainsGeometry) {
  PickdownFixture fixture = build();
  fill_main_quarters(fixture);
  set_pickdown(fixture, *Rational::create(1, 64));
  const Duration sixtyfourth  = *Duration::create(NoteValue::kSixtyFourth, 0);
  const SpelledPitch     c4   = *SpelledPitch::create(Letter::kC, 4);
  const graphscore::Note tail = make_note(c4, sixtyfourth);
  const NotationEntityId tail_id = tail.id;
  ASSERT_TRUE(content_of(fixture).append(tail).ok());
  normalize_voice(fixture);

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const LineCommand* first  = find_line(layout, "pickdown-boundary/first");
  const LineCommand* second = find_line(layout, "pickdown-boundary/second");
  const LineCommand* end    = find_line(layout, "pickdown-end-barline");
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);
  ASSERT_NE(end, nullptr);
  // first <= second < node_end even for a valid 1/64 pickdown.
  EXPECT_LE(first->from.x, second->from.x);
  EXPECT_LT(second->from.x, end->from.x);
  // The boundary-onset notehead is inset and contained within [first, end).
  const GlyphCommand* notehead =
      find_glyph(layout, tail_id.to_string() + "/notehead");
  ASSERT_NE(notehead, nullptr);
  EXPECT_GT(notehead->origin.x, first->from.x);
  EXPECT_LT(notehead->origin.x, end->from.x);
  const auto* hit = find_hit(layout, tail_id.to_string() + "/notehead/hit");
  ASSERT_NE(hit, nullptr);
  EXPECT_GE(hit->bounds.x, first->from.x);
  EXPECT_LE(hit->bounds.x + hit->bounds.width, end->from.x);
}

TEST(PickdownLayoutTest, MultiVoiceCollisionDisplacesPickdownNoteheads) {
  PickdownFixture fixture = build();
  fill_main_quarters(fixture, 1);
  fill_main_quarters(fixture, 2);
  set_pickdown(fixture, *Rational::create(1, 4));
  const Duration         quarter = *Duration::create(NoteValue::kQuarter, 0);
  const SpelledPitch     c4      = *SpelledPitch::create(Letter::kC, 4);
  const graphscore::Note voice1  = make_note(c4, quarter);
  const graphscore::Note voice2  = make_note(c4, quarter);
  ASSERT_TRUE(content_of(fixture, 1).append(voice1).ok());
  ASSERT_TRUE(content_of(fixture, 2).append(voice2).ok());
  normalize_voice(fixture, 1);
  normalize_voice(fixture, 2);

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const GlyphCommand* head1 =
      find_glyph(layout, voice1.id.to_string() + "/notehead");
  const GlyphCommand* head2 =
      find_glyph(layout, voice2.id.to_string() + "/notehead");
  ASSERT_NE(head1, nullptr);
  ASSERT_NE(head2, nullptr);
  EXPECT_NE(head1->origin.x, head2->origin.x);
}

// Wide options keep several measures on one shared (final) system, so a
// non-final edit rebuilds the very system that also draws the pickdown.
[[nodiscard]] NotationLayoutOptions wide_system_options() {
  NotationLayoutOptions options;
  options.system_width          = 2000.0;
  options.left_margin           = 20.0;
  options.right_margin          = 20.0;
  options.minimum_measure_width = 120.0;
  options.whole_note_spacing    = 180.0;
  return options;
}

[[nodiscard]] const GlyphCommand* find_glyph_containing(
    const NotationLayout& layout, const std::string& needle) {
  for (const auto& command : layout.commands) {
    const auto* glyph = std::get_if<GlyphCommand>(&command);
    if (glyph != nullptr && glyph->id.value.find(needle) != std::string::npos) {
      return glyph;
    }
  }
  return nullptr;
}

[[nodiscard]] const graphscore::PathCommand* find_path(
    const NotationLayout& layout, const std::string& suffix) {
  for (const auto& command : layout.commands) {
    const auto* path = std::get_if<graphscore::PathCommand>(&command);
    if (path != nullptr && path->id.value.size() >= suffix.size() &&
        path->id.value.compare(path->id.value.size() - suffix.size(),
                               suffix.size(), suffix) == 0) {
      return path;
    }
  }
  return nullptr;
}

// Builds a four-measure node whose final system carries a pickdown note plus
// a dynamic anchored to it, then hands back the retained tail identities.
struct IncrementalPickdownFixture {
  PickdownFixture  fixture = build(4);
  NotationEntityId tail_id;
  NotationEntityId dynamic_id;
};

[[nodiscard]] IncrementalPickdownFixture build_incremental_fixture() {
  IncrementalPickdownFixture out;
  const Duration     quarter = *Duration::create(NoteValue::kQuarter, 0);
  const SpelledPitch c4      = *SpelledPitch::create(Letter::kC, 4);
  for (int index = 0; index < 16; ++index) {
    EXPECT_TRUE(content_of(out.fixture).append(make_note(c4, quarter)).ok());
  }
  set_pickdown(out.fixture, *Rational::create(1, 4));
  const graphscore::Note tail = make_note(c4, quarter);
  out.tail_id                 = tail.id;
  EXPECT_TRUE(content_of(out.fixture).append(tail).ok());
  const graphscore::DynamicMarking dynamic =
      graphscore::make_dynamic_marking(out.tail_id, graphscore::Dynamic::kF);
  out.dynamic_id = dynamic.id;
  EXPECT_TRUE(content_of(out.fixture).add_dynamic(dynamic).ok());
  normalize_voice(out.fixture);
  return out;
}

// A non-final measure edit on a shared final system must not drop the
// pickdown bucket: the tail note, its dynamic, event_count, and ids must all
// match a fresh layout after the incremental refresh.
TEST(PickdownLayoutTest, NonFinalEditOnSharedFinalSystemPreservesPickdown) {
  IncrementalPickdownFixture  data   = build_incremental_fixture();
  const Duration              eighth = *Duration::create(NoteValue::kEighth, 0);
  const SpelledPitch          c4     = *SpelledPitch::create(Letter::kC, 4);
  const NotationLayoutOptions options = wide_system_options();
  const FixedMetrics          metrics;
  NotationLayoutCache         cache;
  ASSERT_TRUE(cache.update(data.fixture.project, data.fixture.node_id, metrics,
                           options, {}));

  // Contract the first measure's opening quarter to an eighth: the event
  // count in measure 0 grows by one (an eighth rest is inserted), shifting
  // every later event index including the pickdown tail.
  ASSERT_TRUE(
      content_of(data.fixture)
          .replace_event(Rational(0), make_note(c4, eighth),
                         data.fixture.project.find_node(data.fixture.node_id)
                             ->timeline()
                             ->node_end())
          .ok());

  const auto updated =
      cache.update(data.fixture.project, data.fixture.node_id, metrics, options,
                   {{NotationInvalidationKind::kLocalContent, 0, 0}});
  ASSERT_TRUE(updated);
  ASSERT_TRUE(updated.layout.has_value());

  // The pickdown note and its dynamic survive the non-final edit.
  EXPECT_NE(find_glyph(*updated.layout, data.tail_id.to_string() + "/notehead"),
            nullptr);
  EXPECT_TRUE(
      has_command(*updated.layout, data.dynamic_id.to_string() + "/glyph/"));
  // 16 main quarters -> 17 main events (measure 0 gained an eighth rest) plus
  // the retained pickdown note.
  EXPECT_EQ(updated.layout->systems[0].staves[0].voices[0].event_count, 18u);

  const auto fresh = layout_notation(data.fixture.project, data.fixture.node_id,
                                     metrics, options);
  ASSERT_TRUE(fresh);
  EXPECT_EQ(*updated.layout, *fresh.layout);
}

// A non-final edit on an earlier system (one measure per system) leaves the
// retained final system's pickdown content intact, and the index stays
// consistent with a fresh layout afterward.
TEST(PickdownLayoutTest, NonFinalEditOnEarlierSystemPreservesRetainedPickdown) {
  IncrementalPickdownFixture data   = build_incremental_fixture();
  const Duration             eighth = *Duration::create(NoteValue::kEighth, 0);
  const SpelledPitch         c4     = *SpelledPitch::create(Letter::kC, 4);
  NotationLayoutOptions      options;
  options.system_width          = 160.0;
  options.left_margin           = 20.0;
  options.right_margin          = 20.0;
  options.minimum_measure_width = 120.0;
  options.whole_note_spacing    = 120.0;
  const FixedMetrics  metrics;
  NotationLayoutCache cache;
  ASSERT_TRUE(cache.update(data.fixture.project, data.fixture.node_id, metrics,
                           options, {}));

  ASSERT_TRUE(
      content_of(data.fixture)
          .replace_event(Rational(0), make_note(c4, eighth),
                         data.fixture.project.find_node(data.fixture.node_id)
                             ->timeline()
                             ->node_end())
          .ok());

  const auto updated =
      cache.update(data.fixture.project, data.fixture.node_id, metrics, options,
                   {{NotationInvalidationKind::kLocalContent, 0, 0}});
  ASSERT_TRUE(updated);
  ASSERT_TRUE(updated.layout.has_value());
  EXPECT_EQ(updated.work.rebuilt_systems, (std::vector<std::size_t>{0}));
  EXPECT_EQ(updated.work.reused_systems, (std::vector<std::size_t>{1, 2, 3}));

  // The retained final system still carries the pickdown note and dynamic.
  EXPECT_NE(find_glyph(*updated.layout, data.tail_id.to_string() + "/notehead"),
            nullptr);
  EXPECT_TRUE(
      has_command(*updated.layout, data.dynamic_id.to_string() + "/glyph/"));

  const auto fresh = layout_notation(data.fixture.project, data.fixture.node_id,
                                     metrics, options);
  ASSERT_TRUE(fresh);
  EXPECT_EQ(*updated.layout, *fresh.layout);
}

// A clef change inside [boundary, node_end) emits a glyph in the pickdown
// region and steers the following note's placement through clef_at().
TEST(PickdownLayoutTest, PickdownClefChangeEmitsGlyphAndSteersFollowingNote) {
  PickdownFixture fixture = build();
  fill_main_quarters(fixture);
  set_pickdown(fixture, *Rational::create(1, 4));
  const Duration         eighth    = *Duration::create(NoteValue::kEighth, 0);
  const SpelledPitch     c4        = *SpelledPitch::create(Letter::kC, 4);
  const graphscore::Note first     = make_note(c4, eighth);
  const graphscore::Note second    = make_note(c4, eighth);
  const NotationEntityId first_id  = first.id;
  const NotationEntityId second_id = second.id;
  ASSERT_TRUE(content_of(fixture).append(first).ok());
  ASSERT_TRUE(content_of(fixture).append(second).ok());
  normalize_voice(fixture);

  Node* const node =
      const_cast<Node*>(fixture.project.find_node(fixture.node_id));
  const Rational change_pos =
      node->timeline()->boundary_position() + *Rational::create(1, 8);
  ASSERT_TRUE(node->timeline()
                  ->add_clef_change(fixture.stave_id, change_pos, Clef::kBass)
                  .ok());

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  const GlyphCommand* clef =
      find_glyph_containing(layout, "/clef-change/pickdown/");
  ASSERT_NE(clef, nullptr);
  const LineCommand* first_line = find_line(layout, "pickdown-boundary/first");
  const LineCommand* end_line   = find_line(layout, "pickdown-end-barline");
  ASSERT_NE(first_line, nullptr);
  ASSERT_NE(end_line, nullptr);
  EXPECT_GE(clef->origin.x, first_line->from.x);
  EXPECT_LT(clef->origin.x, end_line->from.x);

  // The same pitch (C4) must render at different staff steps before and after
  // the change: treble clef (middle line B4, diatonic 41) puts C4 (diatonic
  // 35) at top + 5.0*space; bass clef (middle line D3, diatonic 29) puts it
  // at top - 1.0*space.
  const GlyphCommand* first_head =
      find_glyph(layout, first_id.to_string() + "/notehead");
  const GlyphCommand* second_head =
      find_glyph(layout, second_id.to_string() + "/notehead");
  ASSERT_NE(first_head, nullptr);
  ASSERT_NE(second_head, nullptr);
  const double staff_top = layout.systems[0].staves[0].bounds.y;
  const double space     = layout.systems[0].staves[0].bounds.height / 4.0;
  EXPECT_NEAR(first_head->origin.y, staff_top + 5.0 * space, 1e-6);
  EXPECT_NEAR(second_head->origin.y, staff_top - 1.0 * space, 1e-6);
}

// Boundary-adjacent pickdown content (accidental, grace, stems, flags, dots,
// dynamics, chord seconds, multivoice collision, and spans) must stay inside
// the transition-to-node-end area, with the boundary strokes distinct.
TEST(PickdownLayoutTest, PickdownContentBoundsContainBoundaryGeometry) {
  PickdownFixture fixture = build();
  fill_main_quarters(fixture, 1);
  fill_main_quarters(fixture, 2);
  fill_main_quarters(fixture, 3);
  set_pickdown(fixture, *Rational::create(3, 4));

  const Duration quarter       = *Duration::create(NoteValue::kQuarter, 0);
  const Duration eighth        = *Duration::create(NoteValue::kEighth, 0);
  const Duration dotted_eighth = *Duration::create(NoteValue::kEighth, 1);
  const Duration sixteenth     = *Duration::create(NoteValue::kSixteenth, 0);
  const auto     c4            = *SpelledPitch::create(Letter::kC, 4);
  const auto     d4            = *SpelledPitch::create(Letter::kD, 4);
  const auto     fs4 =
      *SpelledPitch::create(Letter::kF, 4, graphscore::Accidental::kSharp);

  // Voice 1: a boundary sharp (accidental), its grace group, and a dynamic;
  // then a dotted eighth; then a displaced second chord.
  const graphscore::Note boundary    = make_note(fs4, quarter);
  const NotationEntityId boundary_id = boundary.id;
  ASSERT_TRUE(content_of(fixture, 1).append(boundary).ok());
  const graphscore::GraceNote grace{
      graphscore::NotationEntityId::generate(),
      *SpelledPitch::create(Letter::kF, 4, graphscore::Accidental::kSharp),
      sixteenth, graphscore::GraceNoteType::kAcciaccatura, true};
  const NotationEntityId grace_id = grace.id;
  ASSERT_TRUE(
      content_of(fixture, 1)
          .add_grace_group(graphscore::make_grace_group(boundary_id, {grace}))
          .ok());
  const graphscore::DynamicMarking dynamic =
      graphscore::make_dynamic_marking(boundary_id, graphscore::Dynamic::kFf);
  const NotationEntityId dynamic_id = dynamic.id;
  ASSERT_TRUE(content_of(fixture, 1).add_dynamic(dynamic).ok());
  const graphscore::Note dotted    = make_note(d4, dotted_eighth);
  const NotationEntityId dotted_id = dotted.id;
  ASSERT_TRUE(content_of(fixture, 1).append(dotted).ok());
  const NotationEntityId  chord_low  = graphscore::NotationEntityId::generate();
  const NotationEntityId  chord_high = graphscore::NotationEntityId::generate();
  const graphscore::Chord chord      = graphscore::make_chord(
      eighth, {{chord_low, c4, false}, {chord_high, d4, false}});
  const NotationEntityId chord_id = chord.id;
  ASSERT_TRUE(content_of(fixture, 1).append(chord).ok());

  // Voice 2: a down-stem eighth at the boundary (down stem + down flag).
  const graphscore::Note down    = make_note(c4, eighth);
  const NotationEntityId down_id = down.id;
  ASSERT_TRUE(content_of(fixture, 2).append(down).ok());
  // Voice 3: an up-stem eighth at the same boundary onset (voice collision).
  const graphscore::Note collide    = make_note(c4, eighth);
  const NotationEntityId collide_id = collide.id;
  ASSERT_TRUE(content_of(fixture, 3).append(collide).ok());

  // A hairpin and a slur inside the pickdown, plus a pedal span crossing in.
  const graphscore::Hairpin hairpin = graphscore::make_hairpin(
      boundary_id, dotted_id, graphscore::HairpinDirection::kCrescendo);
  const NotationEntityId hairpin_id = hairpin.id;
  ASSERT_TRUE(content_of(fixture, 1).add_hairpin(hairpin).ok());
  const graphscore::Slur slur    = graphscore::make_slur(boundary_id, chord_id);
  const NotationEntityId slur_id = slur.id;
  ASSERT_TRUE(content_of(fixture, 1).add_slur(slur).ok());
  auto* lane =
      fixture.project.find_node(fixture.node_id)->lane(fixture.track_id);
  ASSERT_TRUE(lane->add_pedal_span(fixture.stave_id,
                                   graphscore::make_pedal_span(
                                       Rational(0), *Rational::create(7, 4)))
                  .ok());

  normalize_voice(fixture, 1);
  normalize_voice(fixture, 2);
  normalize_voice(fixture, 3);

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  const LineCommand* first  = find_line(layout, "pickdown-boundary/first");
  const LineCommand* second = find_line(layout, "pickdown-boundary/second");
  const LineCommand* end    = find_line(layout, "pickdown-end-barline");
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);
  ASSERT_NE(end, nullptr);
  const double left  = first->from.x;
  const double right = end->from.x;
  EXPECT_GT(second->from.x, left);
  EXPECT_GT(right, second->from.x);

  const auto glyph_within = [&](const GlyphCommand* glyph) {
    ASSERT_NE(glyph, nullptr);
    EXPECT_GE(glyph->origin.x - 0.25 * glyph->staff_space, left)
        << glyph->id.value;
    EXPECT_LE(glyph->origin.x + 1.25 * glyph->staff_space, right)
        << glyph->id.value;
  };
  const auto line_within = [&](const LineCommand* line) {
    ASSERT_NE(line, nullptr);
    EXPECT_GE(std::min(line->from.x, line->to.x), left) << line->id.value;
    EXPECT_LE(std::max(line->from.x, line->to.x), right) << line->id.value;
  };
  const auto path_within = [&](const graphscore::PathCommand* path) {
    ASSERT_NE(path, nullptr);
    for (const auto& element : path->elements) {
      EXPECT_GE(element.end.x, left) << path->id.value;
      EXPECT_LE(element.end.x, right) << path->id.value;
      if (element.verb != graphscore::PathVerb::kMove) {
        for (const double x : {element.control1.x, element.control2.x}) {
          EXPECT_GE(x, left) << path->id.value;
          EXPECT_LE(x, right) << path->id.value;
        }
      }
    }
  };
  const auto hit_within = [&](const graphscore::HitRegion* hit) {
    ASSERT_NE(hit, nullptr);
    EXPECT_GE(hit->bounds.x, left) << hit->id.value;
    EXPECT_LE(hit->bounds.x + hit->bounds.width, right) << hit->id.value;
  };

  // Accidental.
  glyph_within(
      find_glyph(layout, boundary_id.to_string() + "/accidental/column-0"));
  hit_within(
      find_hit(layout, boundary_id.to_string() + "/accidental/column-0/hit"));
  // Grace notehead, accidental, and stem.
  glyph_within(find_glyph(layout, grace_id.to_string() + "/grace-notehead"));
  hit_within(find_hit(layout, grace_id.to_string() + "/grace-notehead/hit"));
  glyph_within(find_glyph(layout, grace_id.to_string() + "/grace-accidental"));
  line_within(find_line(layout, grace_id.to_string() + "/grace-stem"));
  // Down-stem and its flag (voice 2), and the up-stem flag (voice 3).
  line_within(find_line(layout, down_id.to_string() + "/stem"));
  hit_within(find_hit(layout, down_id.to_string() + "/stem/hit"));
  glyph_within(find_glyph(layout, down_id.to_string() + "/flag"));
  glyph_within(find_glyph(layout, collide_id.to_string() + "/flag"));
  // Dotted note's augmentation dot and the dynamic's rightmost glyph.
  glyph_within(find_glyph(layout, dotted_id.to_string() + "/dot/0"));
  glyph_within(find_glyph(layout, dynamic_id.to_string() + "/glyph/1"));
  // Displaced chord noteheads and the collided multivoice noteheads.
  glyph_within(find_glyph(layout, chord_low.to_string() + "/notehead"));
  glyph_within(find_glyph(layout, chord_high.to_string() + "/notehead"));
  glyph_within(find_glyph(layout, down_id.to_string() + "/notehead"));
  glyph_within(find_glyph(layout, collide_id.to_string() + "/notehead"));
  // Hairpin wedge lines and the slur curve stay inside the region.
  line_within(find_line(
      layout, hairpin_id.to_string() + "/hairpin/segment/pickdown/upper"));
  line_within(find_line(
      layout, hairpin_id.to_string() + "/hairpin/segment/pickdown/lower"));
  path_within(
      find_path(layout, slur_id.to_string() + "/slur/segment/pickdown/curve"));
  // Pedal line and end glyph.
  line_within(find_line(layout, "/pedal/segment/pickdown/line"));
  glyph_within(find_glyph(layout, "/pedal/segment/pickdown/up"));
}

// Custom asymmetric metrics must keep transition and node-end content inside
// the region, with containment derived from the injected metrics rather than a
// hardcoded FixedMetrics bounding box.
TEST(PickdownLayoutTest, CustomMetricsContainTransitionAndNodeEndGlyphs) {
  PickdownFixture fixture = build();
  fill_main_quarters(fixture);
  set_pickdown(fixture, *Rational::create(1, 4));
  const Duration eighth = *Duration::create(NoteValue::kEighth, 0);
  const auto     fs4 =
      *SpelledPitch::create(Letter::kF, 4, graphscore::Accidental::kSharp);
  const auto c4 = *SpelledPitch::create(Letter::kC, 4);
  // Transition: a boundary sharp, whose wide accidental hangs left of onset.
  const graphscore::Note boundary    = make_note(fs4, eighth);
  const NotationEntityId boundary_id = boundary.id;
  ASSERT_TRUE(content_of(fixture).append(boundary).ok());
  // Node-end: a second eighth ending at node_end, whose wide notehead hangs
  // right toward the node-end barline; a two-glyph dynamic on it pushes its
  // rightmost glyph toward node_end.
  const graphscore::Note tail    = make_note(c4, eighth);
  const NotationEntityId tail_id = tail.id;
  ASSERT_TRUE(content_of(fixture).append(tail).ok());
  const graphscore::DynamicMarking dynamic =
      graphscore::make_dynamic_marking(tail_id, graphscore::Dynamic::kFf);
  const NotationEntityId dynamic_id = dynamic.id;
  ASSERT_TRUE(content_of(fixture).add_dynamic(dynamic).ok());
  normalize_voice(fixture);

  const AsymmetricMetrics metrics;
  const NotationLayout    layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  const LineCommand* first = find_line(layout, "pickdown-boundary/first");
  const LineCommand* end   = find_line(layout, "pickdown-end-barline");
  ASSERT_NE(first, nullptr);
  ASSERT_NE(end, nullptr);
  const double left  = first->from.x;
  const double right = end->from.x;

  const auto glyph_bounds_within = [&](const GlyphCommand* glyph) {
    ASSERT_NE(glyph, nullptr);
    const GlyphMetricsValue value =
        metrics.glyph_metrics(glyph->code_point, glyph->staff_space);
    const double glyph_left = glyph->origin.x + value.bounds.x;
    const double glyph_right =
        glyph->origin.x + value.bounds.x + value.bounds.width;
    EXPECT_GE(glyph_left, left) << glyph->id.value;
    EXPECT_LE(glyph_right, right) << glyph->id.value;
  };

  // Transition glyphs (wide accidental and notehead hang left of the onset).
  glyph_bounds_within(
      find_glyph(layout, boundary_id.to_string() + "/notehead"));
  glyph_bounds_within(
      find_glyph(layout, boundary_id.to_string() + "/accidental/column-0"));
  // Node-end glyphs (notehead and the dynamic's rightmost glyph).
  glyph_bounds_within(find_glyph(layout, tail_id.to_string() + "/notehead"));
  glyph_bounds_within(find_glyph(layout, dynamic_id.to_string() + "/glyph/1"));
}

// A boundary-anchored dynamic whose first glyph's left extent is far wider
// than the main region's fixed half-space clamp, plus boundary-anchored span
// endpoints, must all stay inside the transition-to-node-end area under
// asymmetric metrics: containment is derived from the injected metrics, not
// from a hardcoded bounding box.
TEST(PickdownLayoutTest, BoundaryAnchoredDynamicAndSpansHonorMetricInset) {
  PickdownFixture fixture = build();
  fill_main_quarters(fixture);
  set_pickdown(fixture, *Rational::create(1, 2));
  const Duration quarter = *Duration::create(NoteValue::kQuarter, 0);
  const auto     c4      = *SpelledPitch::create(Letter::kC, 4);

  // A boundary-onset note carrying a wide-left dynamic (its first glyph hangs
  // left of the note's own onset x, wider than the minimum content inset).
  const graphscore::Note boundary    = make_note(c4, quarter);
  const NotationEntityId boundary_id = boundary.id;
  ASSERT_TRUE(content_of(fixture).append(boundary).ok());
  const graphscore::DynamicMarking dynamic =
      graphscore::make_dynamic_marking(boundary_id, graphscore::Dynamic::kF);
  const NotationEntityId dynamic_id = dynamic.id;
  ASSERT_TRUE(content_of(fixture).add_dynamic(dynamic).ok());

  // A second pickdown note ending at node_end; a hairpin and a slur both
  // anchor their left endpoint at the boundary.
  const graphscore::Note tail    = make_note(c4, quarter);
  const NotationEntityId tail_id = tail.id;
  ASSERT_TRUE(content_of(fixture).append(tail).ok());
  const graphscore::Hairpin hairpin = graphscore::make_hairpin(
      boundary_id, tail_id, graphscore::HairpinDirection::kCrescendo);
  const NotationEntityId hairpin_id = hairpin.id;
  ASSERT_TRUE(content_of(fixture).add_hairpin(hairpin).ok());
  const graphscore::Slur slur    = graphscore::make_slur(boundary_id, tail_id);
  const NotationEntityId slur_id = slur.id;
  ASSERT_TRUE(content_of(fixture).add_slur(slur).ok());
  normalize_voice(fixture);

  const WideLeftDynamicMetrics metrics;
  const NotationLayout         layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  const LineCommand* first  = find_line(layout, "pickdown-boundary/first");
  const LineCommand* second = find_line(layout, "pickdown-boundary/second");
  const LineCommand* end    = find_line(layout, "pickdown-end-barline");
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);
  ASSERT_NE(end, nullptr);
  const double left  = first->from.x;
  const double right = end->from.x;
  // Transition strokes stay distinct.
  EXPECT_GT(second->from.x, left);
  EXPECT_GT(right, second->from.x);

  const auto glyph_bounds_within = [&](const GlyphCommand* glyph) {
    ASSERT_NE(glyph, nullptr);
    const GlyphMetricsValue value =
        metrics.glyph_metrics(glyph->code_point, glyph->staff_space);
    EXPECT_GE(glyph->origin.x + value.bounds.x, left) << glyph->id.value;
    EXPECT_LE(glyph->origin.x + value.bounds.x + value.bounds.width, right)
        << glyph->id.value;
  };

  // The wide-left dynamic's glyph stays inside the transition boundary.
  glyph_bounds_within(find_glyph(layout, dynamic_id.to_string() + "/glyph/0"));

  // Boundary-anchored span endpoints stay inside and remain musically ordered.
  const LineCommand* hairpin_upper = find_line(
      layout, hairpin_id.to_string() + "/hairpin/segment/pickdown/upper");
  ASSERT_NE(hairpin_upper, nullptr);
  EXPECT_GE(hairpin_upper->from.x, left);
  EXPECT_LE(hairpin_upper->to.x, right);
  EXPECT_LT(hairpin_upper->from.x, hairpin_upper->to.x);
  const graphscore::PathCommand* slur_curve =
      find_path(layout, slur_id.to_string() + "/slur/segment/pickdown/curve");
  ASSERT_NE(slur_curve, nullptr);
  for (const auto& element : slur_curve->elements) {
    EXPECT_GE(element.end.x, left) << slur_id.to_string();
    EXPECT_LE(element.end.x, right) << slur_id.to_string();
  }
}

// Simultaneous close-pitched accidentals across voices must share the
// engraver's accidental-column allocation, so a later column's glyph and hit
// stay inside the region instead of being budgeted as column zero only.
TEST(PickdownLayoutTest, CrossVoiceAccidentalsShareColumnsAtBoundary) {
  PickdownFixture fixture = build();
  fill_main_quarters(fixture, 1);
  fill_main_quarters(fixture, 2);
  fill_main_quarters(fixture, 3);
  fill_main_quarters(fixture, 4);
  set_pickdown(fixture, *Rational::create(1, 4));

  const Duration quarter = *Duration::create(NoteValue::kQuarter, 0);
  // C, D, E, F sharps are pairwise within the engraver's 1.6 staff-space
  // accidental-column overlap threshold, so they need columns 0..3.
  const auto c4 =
      *SpelledPitch::create(Letter::kC, 4, graphscore::Accidental::kSharp);
  const auto d4 =
      *SpelledPitch::create(Letter::kD, 4, graphscore::Accidental::kSharp);
  const auto e4 =
      *SpelledPitch::create(Letter::kE, 4, graphscore::Accidental::kSharp);
  const auto f4 =
      *SpelledPitch::create(Letter::kF, 4, graphscore::Accidental::kSharp);

  const graphscore::Note voice1 = make_note(c4, quarter);
  const graphscore::Note voice2 = make_note(d4, quarter);
  const graphscore::Note voice3 = make_note(e4, quarter);
  const graphscore::Note voice4 = make_note(f4, quarter);
  ASSERT_TRUE(content_of(fixture, 1).append(voice1).ok());
  ASSERT_TRUE(content_of(fixture, 2).append(voice2).ok());
  ASSERT_TRUE(content_of(fixture, 3).append(voice3).ok());
  ASSERT_TRUE(content_of(fixture, 4).append(voice4).ok());
  normalize_voice(fixture, 1);
  normalize_voice(fixture, 2);
  normalize_voice(fixture, 3);
  normalize_voice(fixture, 4);

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  const LineCommand* first = find_line(layout, "pickdown-boundary/first");
  const LineCommand* end   = find_line(layout, "pickdown-end-barline");
  ASSERT_NE(first, nullptr);
  ASSERT_NE(end, nullptr);
  const double left  = first->from.x;
  const double right = end->from.x;

  const auto hit_within = [&](const graphscore::HitRegion* hit) {
    ASSERT_NE(hit, nullptr);
    EXPECT_GE(hit->bounds.x, left) << hit->id.value;
    EXPECT_LE(hit->bounds.x + hit->bounds.width, right) << hit->id.value;
  };

  // The shared allocation must reach column 3 (F#), and its glyph and hit
  // stay inside the region.
  const GlyphCommand* column3 =
      find_glyph(layout, voice4.id.to_string() + "/accidental/column-3");
  ASSERT_NE(column3, nullptr);
  EXPECT_GE(column3->origin.x - 0.25 * column3->staff_space, left);
  hit_within(
      find_hit(layout, voice4.id.to_string() + "/accidental/column-3/hit"));

  // Every voice's notehead and accidental hit stay inside the region.
  const std::vector<graphscore::Note> notes = {voice1, voice2, voice3, voice4};
  for (std::size_t column = 0; column < notes.size(); ++column) {
    hit_within(
        find_hit(layout, notes[column].id.to_string() + "/notehead/hit"));
    hit_within(find_hit(layout, notes[column].id.to_string() +
                                    "/accidental/column-" +
                                    std::to_string(column) + "/hit"));
  }
}

// Boundary lines carry a stroke width: a line's drawn extent is its centerline
// plus half its width on each side, and that must stay inside the represented
// region.
TEST(PickdownLayoutTest, BoundaryLinesIncludeStrokeExtents) {
  PickdownFixture fixture = build();
  fill_main_quarters(fixture, 1);
  fill_main_quarters(fixture, 2);
  set_pickdown(fixture, *Rational::create(1, 4));
  const Duration eighth = *Duration::create(NoteValue::kEighth, 0);
  const auto     b5     = *SpelledPitch::create(Letter::kB, 5);
  const auto     c4     = *SpelledPitch::create(Letter::kC, 4);
  // Voice 1: a high note whose ledger line hangs both sides of the notehead.
  const graphscore::Note high    = make_note(b5, eighth);
  const NotationEntityId high_id = high.id;
  ASSERT_TRUE(content_of(fixture, 1).append(high).ok());
  // Voice 2: a down-stem eighth whose stem hangs left of the notehead.
  const graphscore::Note down    = make_note(c4, eighth);
  const NotationEntityId down_id = down.id;
  ASSERT_TRUE(content_of(fixture, 2).append(down).ok());
  normalize_voice(fixture, 1);
  normalize_voice(fixture, 2);

  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  const LineCommand* first = find_line(layout, "pickdown-boundary/first");
  const LineCommand* end   = find_line(layout, "pickdown-end-barline");
  ASSERT_NE(first, nullptr);
  ASSERT_NE(end, nullptr);
  const double left  = first->from.x;
  const double right = end->from.x;

  const auto line_stroke_within = [&](const LineCommand* line) {
    ASSERT_NE(line, nullptr);
    const double half = line->width * 0.5;
    EXPECT_GE(std::min(line->from.x, line->to.x) - half, left)
        << line->id.value;
    EXPECT_LE(std::max(line->from.x, line->to.x) + half, right)
        << line->id.value;
  };

  line_stroke_within(find_line(layout, down_id.to_string() + "/stem"));
  line_stroke_within(
      find_line(layout, high_id.to_string() + "/ledger/above/0"));
}

// Only the second glyph of a multi-glyph dynamic (`mf` -> M F) carries an
// extreme left extent; the first glyph stays narrow. A boundary-anchored `mf`
// places the F one staff-space right of the M, so F's wide left edge hangs a
// full staff-space left of the dynamic's own onset and must be budgeted from
// the second glyph's emitted geometry, not the first glyph's box.
class LateWideLeftDynamicMetrics final : public GlyphMetrics {
 public:
  [[nodiscard]] GlyphMetricsValue glyph_metrics(
      char32_t code_point, double staff_space) const override {
    const bool f = code_point == graphscore::smufl_codepoint(
                                     graphscore::SmuflGlyph::kDynamicF);
    return GlyphMetricsValue{
        NotationRect{f ? -staff_space * 2.0 : -staff_space * 0.25,
                     -staff_space * 0.5, staff_space * 0.6, staff_space * 2.0},
        staff_space * 0.6};
  }

  [[nodiscard]] double kerning(char32_t /*left*/, char32_t /*right*/,
                               double /*staff_space*/) const override {
    return 0.0;
  }
};

TEST(PickdownLayoutTest, LaterDynamicGlyphExtremeLeftExtentStaysInside) {
  PickdownFixture fixture = build();
  fill_main_quarters(fixture);
  set_pickdown(fixture, *Rational::create(1, 4));
  const Duration quarter = *Duration::create(NoteValue::kQuarter, 0);
  const auto     c4      = *SpelledPitch::create(Letter::kC, 4);

  // A boundary-onset note carrying an `mf` dynamic: glyph 0 (M) is narrow, but
  // glyph 1 (F) is emitted one staff-space right and hangs two staff-spaces
  // left, one full staff-space left of the dynamic's own onset.
  const graphscore::Note boundary    = make_note(c4, quarter);
  const NotationEntityId boundary_id = boundary.id;
  ASSERT_TRUE(content_of(fixture).append(boundary).ok());
  const graphscore::DynamicMarking dynamic =
      graphscore::make_dynamic_marking(boundary_id, graphscore::Dynamic::kMf);
  const NotationEntityId dynamic_id = dynamic.id;
  ASSERT_TRUE(content_of(fixture).add_dynamic(dynamic).ok());
  normalize_voice(fixture);

  const LateWideLeftDynamicMetrics metrics;
  const NotationLayout             layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  const LineCommand* first  = find_line(layout, "pickdown-boundary/first");
  const LineCommand* second = find_line(layout, "pickdown-boundary/second");
  const LineCommand* end    = find_line(layout, "pickdown-end-barline");
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);
  ASSERT_NE(end, nullptr);
  // Non-pedal content must clear the second transition stroke; the first
  // stroke is the boundary itself.
  const double left  = second->from.x;
  const double right = end->from.x;

  const auto glyph_bounds_within = [&](const GlyphCommand* glyph) {
    ASSERT_NE(glyph, nullptr);
    const GlyphMetricsValue value =
        metrics.glyph_metrics(glyph->code_point, glyph->staff_space);
    EXPECT_GE(glyph->origin.x + value.bounds.x, left) << glyph->id.value;
    EXPECT_LE(glyph->origin.x + value.bounds.x + value.bounds.width, right)
        << glyph->id.value;
  };

  // The second (F) glyph, not just the first, must stay inside the transition.
  glyph_bounds_within(find_glyph(layout, dynamic_id.to_string() + "/glyph/0"));
  glyph_bounds_within(find_glyph(layout, dynamic_id.to_string() + "/glyph/1"));
}

// Codepoint-sensitive metrics where tuplet digits/colon, the grace flag/dot,
// and the pedal down/up glyphs all carry extreme boxes, so containment must
// derive from each emitted glyph's own translated metric bounds.
class FamilyExtremeMetrics final : public GlyphMetrics {
 public:
  [[nodiscard]] GlyphMetricsValue glyph_metrics(
      char32_t code_point, double staff_space) const override {
    const char32_t digit0 =
        graphscore::smufl_codepoint(graphscore::SmuflGlyph::kTupletDigit0);
    const bool tuplet_glyph =
        (code_point >= digit0 && code_point <= digit0 + 9) ||
        code_point ==
            graphscore::smufl_codepoint(graphscore::SmuflGlyph::kTupletColon);
    const bool flag16 = code_point == graphscore::smufl_codepoint(
                                          graphscore::SmuflGlyph::kFlag16thUp);
    const bool dot =
        code_point ==
        graphscore::smufl_codepoint(graphscore::SmuflGlyph::kAugmentationDot);
    const bool pedal_down =
        code_point ==
        graphscore::smufl_codepoint(graphscore::SmuflGlyph::kPedalDown);
    const bool pedal_up = code_point == graphscore::smufl_codepoint(
                                            graphscore::SmuflGlyph::kPedalUp);

    double x     = -staff_space * 0.25;
    double width = staff_space * 1.5;
    if (tuplet_glyph) {
      x     = -staff_space * 1.4;  // hangs far left of its origin
      width = staff_space * 1.6;
    } else if (flag16) {
      x     = -staff_space * 1.6;
      width = staff_space * 1.8;
    } else if (dot) {
      x     = -staff_space * 1.0;
      width = staff_space * 2.0;
    } else if (pedal_down) {
      x     = -staff_space * 1.8;  // complete width mostly left of origin
      width = staff_space * 1.0;
    } else if (pedal_up) {
      x     = -staff_space * 0.2;  // complete width mostly right of origin
      width = staff_space * 2.2;
    }
    return GlyphMetricsValue{
        NotationRect{x, -staff_space * 0.5, width, staff_space * 2.0},
        staff_space * 1.5};
  }

  [[nodiscard]] double kerning(char32_t /*left*/, char32_t /*right*/,
                               double /*staff_space*/) const override {
    return 0.0;
  }
};

TEST(PickdownLayoutTest, OmittedFamilyExtremesStayInside) {
  PickdownFixture fixture = build();
  fill_main_quarters(fixture);
  set_pickdown(fixture, *Rational::create(1, 4));

  // A 3:4 sixteenth tuplet (label "3:4", so digit and colon glyphs) fills the
  // quarter-note pickdown; its first note at the boundary carries a dotted
  // sixteenth grace note (flag and dot glyphs).
  const auto     ratio   = *graphscore::TupletRatio::create(3, 4);
  const Duration triplet = *Duration::create(NoteValue::kSixteenth, 0, ratio);
  const SpelledPitch     c4           = *SpelledPitch::create(Letter::kC, 4);
  const graphscore::Note tuplet_first = make_note(c4, triplet);
  const NotationEntityId first_id     = tuplet_first.id;
  ASSERT_TRUE(content_of(fixture).append(tuplet_first).ok());
  const graphscore::GraceNote grace{
      graphscore::NotationEntityId::generate(), c4,
      *Duration::create(NoteValue::kSixteenth, 1),
      graphscore::GraceNoteType::kAcciaccatura, true};
  const NotationEntityId grace_id = grace.id;
  ASSERT_TRUE(
      content_of(fixture)
          .add_grace_group(graphscore::make_grace_group(first_id, {grace}))
          .ok());
  for (int index = 1; index < 3; ++index) {
    ASSERT_TRUE(content_of(fixture).append(make_note(c4, triplet)).ok());
  }
  // A pedal span from the boundary to node-end puts the down glyph at the
  // boundary and the up glyph at node-end.
  auto* lane =
      fixture.project.find_node(fixture.node_id)->lane(fixture.track_id);
  ASSERT_TRUE(lane->add_pedal_span(fixture.stave_id,
                                   graphscore::make_pedal_span(
                                       Rational(1), *Rational::create(5, 4)))
                  .ok());
  normalize_voice(fixture);

  const FamilyExtremeMetrics metrics;
  const NotationLayout       layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  const LineCommand* first  = find_line(layout, "pickdown-boundary/first");
  const LineCommand* second = find_line(layout, "pickdown-boundary/second");
  const LineCommand* end    = find_line(layout, "pickdown-end-barline");
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);
  ASSERT_NE(end, nullptr);
  const double content_left  = second->from.x;  // clears the second stroke
  const double boundary_left = first->from.x;   // pedal may touch the first
  const double right         = end->from.x;

  const auto glyph_bounds_within = [&](const GlyphCommand* glyph,
                                       double              left_bound) {
    ASSERT_NE(glyph, nullptr);
    const GlyphMetricsValue value =
        metrics.glyph_metrics(glyph->code_point, glyph->staff_space);
    EXPECT_GE(glyph->origin.x + value.bounds.x, left_bound) << glyph->id.value;
    EXPECT_LE(glyph->origin.x + value.bounds.x + value.bounds.width, right)
        << glyph->id.value;
  };

  // Tuplet digit and colon glyphs.
  glyph_bounds_within(find_glyph(layout, "/tuplet/digit/0"), content_left);
  glyph_bounds_within(find_glyph(layout, "/tuplet/digit/1"), content_left);
  glyph_bounds_within(find_glyph(layout, "/tuplet/digit/2"), content_left);
  // Grace flag and dot.
  glyph_bounds_within(find_glyph(layout, grace_id.to_string() + "/grace-flag"),
                      content_left);
  glyph_bounds_within(find_glyph(layout, grace_id.to_string() + "/grace-dot/0"),
                      content_left);
  // Pedal down/up complete widths: both edges stay inside the region, with the
  // down glyph permitted to reach the first boundary stroke.
  glyph_bounds_within(find_glyph(layout, "/pedal/segment/pickdown/down"),
                      boundary_left);
  glyph_bounds_within(find_glyph(layout, "/pedal/segment/pickdown/up"),
                      boundary_left);
}

// A later pickdown onset (fraction 0.75) whose dynamic carries a wide left
// sidebearing makes the measured content inset grow past the one-space
// minimum, but by an amount small enough that the region width's conservative
// minimum (2*I + one staff-space) stays below the pure proportional width W0.
// If the width is left at W0 the mapped content span shrinks from W0-2S to
// W0-2I and the 0.75 onset shifts left by (I-S)/2, dragging the dynamic's
// translated bounds past the transition boundary. The fix widens the region by
// 2*(I - S) first, so the content span is preserved and the dynamic stays
// inside the transition-to-node-end area.
class LatePickdownDynamicMetrics final : public GlyphMetrics {
 public:
  [[nodiscard]] GlyphMetricsValue glyph_metrics(
      char32_t code_point, double staff_space) const override {
    const bool f = code_point == graphscore::smufl_codepoint(
                                     graphscore::SmuflGlyph::kDynamicF);
    return GlyphMetricsValue{
        NotationRect{
            f ? -staff_space * 4.5 : -staff_space * 0.25, -staff_space * 0.5,
            f ? staff_space * 5.0 : staff_space * 1.5, staff_space * 2.0},
        staff_space * 1.5};
  }

  [[nodiscard]] double kerning(char32_t /*left*/, char32_t /*right*/,
                               double /*staff_space*/) const override {
    return 0.0;
  }
};

TEST(PickdownLayoutTest, LaterPickdownOnsetStaysContainedWhenInsetGrows) {
  PickdownFixture fixture = build();
  fill_main_quarters(fixture);
  set_pickdown(fixture, *Rational::create(1, 4));

  const Duration     sixteenth = *Duration::create(NoteValue::kSixteenth, 0);
  const SpelledPitch c4        = *SpelledPitch::create(Letter::kC, 4);
  // Four sixteenths exactly fill the quarter-note pickdown, so the final onset
  // lands at fraction 0.75 of the content span.
  ASSERT_TRUE(content_of(fixture).append(make_note(c4, sixteenth)).ok());
  ASSERT_TRUE(content_of(fixture).append(make_note(c4, sixteenth)).ok());
  ASSERT_TRUE(content_of(fixture).append(make_note(c4, sixteenth)).ok());
  const graphscore::Note last    = make_note(c4, sixteenth);
  const NotationEntityId last_id = last.id;
  ASSERT_TRUE(content_of(fixture).append(last).ok());
  const graphscore::DynamicMarking dynamic =
      graphscore::make_dynamic_marking(last_id, graphscore::Dynamic::kF);
  const NotationEntityId dynamic_id = dynamic.id;
  ASSERT_TRUE(content_of(fixture).add_dynamic(dynamic).ok());
  normalize_voice(fixture);

  const LatePickdownDynamicMetrics metrics;
  const NotationLayout             layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  const LineCommand* first  = find_line(layout, "pickdown-boundary/first");
  const LineCommand* second = find_line(layout, "pickdown-boundary/second");
  const LineCommand* end    = find_line(layout, "pickdown-end-barline");
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);
  ASSERT_NE(end, nullptr);

  // The measured inset grew past the one-space minimum, so the region widens
  // beyond its pure proportional width (measure_width * 0.25) to preserve the
  // trial content span. This is the fix's observable effect and would be false
  // if the width were still capped at W0.
  const double measure_width = layout.systems[0].measures[0].bounds.width;
  const double region_width  = end->from.x - first->from.x;
  EXPECT_GT(region_width, measure_width * 0.25);

  // The later onset's dynamic must keep its translated metric bounds inside the
  // transition-to-node-end area: left of the second transition stroke, right of
  // the node-end barline. Under the un-widened inset growth the left edge lands
  // left of the first stroke.
  const GlyphCommand* dynamic_glyph =
      find_glyph(layout, dynamic_id.to_string() + "/glyph/0");
  ASSERT_NE(dynamic_glyph, nullptr);
  const GlyphMetricsValue value = metrics.glyph_metrics(
      dynamic_glyph->code_point, dynamic_glyph->staff_space);
  EXPECT_GE(dynamic_glyph->origin.x + value.bounds.x, second->from.x)
      << dynamic_glyph->id.value;
  EXPECT_LE(dynamic_glyph->origin.x + value.bounds.x + value.bounds.width,
            end->from.x)
      << dynamic_glyph->id.value;
}

}  // namespace
