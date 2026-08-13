// SPDX-License-Identifier: Apache-2.0

#include <graphscore/notation/graphscore_notation.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

using graphscore::BeamOverride;
using graphscore::Clef;
using graphscore::ClipCommand;
using graphscore::Duration;
using graphscore::Dynamic;
using graphscore::event_id;
using graphscore::GlyphCommand;
using graphscore::GlyphMetrics;
using graphscore::GlyphMetricsValue;
using graphscore::GraceNote;
using graphscore::GraceNoteType;
using graphscore::HairpinDirection;
using graphscore::HitRegion;
using graphscore::HitRole;
using graphscore::KeySignature;
using graphscore::layout_notation;
using graphscore::Letter;
using graphscore::LineCommand;
using graphscore::make_beam_override;
using graphscore::make_dynamic_marking;
using graphscore::make_grace_group;
using graphscore::make_hairpin;
using graphscore::make_note;
using graphscore::make_pedal_span;
using graphscore::make_slur;
using graphscore::Measure;
using graphscore::MidiChannel;
using graphscore::NodeId;
using graphscore::NodeTimeline;
using graphscore::NotationId;
using graphscore::NotationInvalidation;
using graphscore::NotationInvalidationKind;
using graphscore::NotationLayout;
using graphscore::NotationLayoutCache;
using graphscore::NotationLayoutError;
using graphscore::NotationLayoutOptions;
using graphscore::NotationPoint;
using graphscore::NotationRect;
using graphscore::NoteValue;
using graphscore::PathCommand;
using graphscore::PathElement;
using graphscore::PathVerb;
using graphscore::PedalSpan;
using graphscore::Project;
using graphscore::ProjectId;
using graphscore::Rational;
using graphscore::SmuflGlyph;
using graphscore::SpelledPitch;
using graphscore::StaffLayout;
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

class InvalidMetrics final : public GlyphMetrics {
 public:
  [[nodiscard]] GlyphMetricsValue glyph_metrics(
      char32_t /*code_point*/, double /*staff_space*/) const override {
    return GlyphMetricsValue{
        NotationRect{0.0, 0.0, std::numeric_limits<double>::infinity(), 1.0},
        1.0};
  }

  [[nodiscard]] double kerning(char32_t /*left*/, char32_t /*right*/,
                               double /*staff_space*/) const override {
    return 0.0;
  }
};

class OverflowMetrics final : public GlyphMetrics {
 public:
  [[nodiscard]] GlyphMetricsValue glyph_metrics(
      char32_t /*code_point*/, double /*staff_space*/) const override {
    return GlyphMetricsValue{
        NotationRect{std::numeric_limits<double>::max(), 0.0,
                     std::numeric_limits<double>::max(), 1.0},
        1.0};
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
  Project              project{ProjectId::generate(), "Notation"};
  NodeId               node_id;
  std::vector<TrackId> track_ids;

  Fixture(std::vector<StaffLayout> layouts, std::size_t measure_count) {
    std::vector<graphscore::StaveDefinition> staves;
    std::uint8_t                             channel = 0;
    for (StaffLayout& layout : layouts) {
      for (const auto& stave : layout.staves()) {
        staves.push_back(stave);
      }
      const auto track_id = project.add_track("Track", std::move(layout),
                                              *MidiChannel::create(channel));
      EXPECT_TRUE(track_id.has_value());
      track_ids.push_back(*track_id);
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

  void append_quarter_notes(std::size_t track, std::size_t stave,
                            std::uint8_t voice_index, std::size_t count) {
    const auto& definition =
        project.active_tracks()[track].layout().staves()[stave];
    auto* voices = project.find_node(node_id)
                       ->lane(track_ids[track])
                       ->stave(definition.id);
    const Voice        voice    = *Voice::create(voice_index);
    const Duration     duration = *Duration::create(NoteValue::kQuarter, 0);
    const SpelledPitch pitch    = *SpelledPitch::create(Letter::kC, 4);
    for (std::size_t index = 0; index < count; ++index) {
      ASSERT_TRUE(voices->voice(voice).append(make_note(pitch, duration)).ok());
    }
  }

  void append_quarter_rest(std::size_t track, std::size_t stave,
                           std::uint8_t voice_index) {
    const auto& definition =
        project.active_tracks()[track].layout().staves()[stave];
    auto* voices = project.find_node(node_id)
                       ->lane(track_ids[track])
                       ->stave(definition.id);
    const Duration duration = *Duration::create(NoteValue::kQuarter, 0);
    ASSERT_TRUE(voices->voice(*Voice::create(voice_index))
                    .append(graphscore::make_rest(duration))
                    .ok());
  }

  [[nodiscard]] graphscore::VoiceContent& voice(std::uint8_t voice_index = 1) {
    const auto& definition = project.active_tracks()[0].layout().staves()[0];
    return project.find_node(node_id)
        ->lane(track_ids[0])
        ->stave(definition.id)
        ->voice(*Voice::create(voice_index));
  }

  [[nodiscard]] graphscore::StaveId stave_id() const {
    return project.active_tracks()[0].layout().staves()[0].id;
  }
};

[[nodiscard]] NotationLayout require_layout(
    const graphscore::NotationLayoutResult& result) {
  EXPECT_TRUE(result);
  return *result.layout;
}

[[nodiscard]] NotationLayoutOptions one_measure_system_options() {
  NotationLayoutOptions options;
  options.system_width          = 160.0;
  options.left_margin           = 20.0;
  options.right_margin          = 20.0;
  options.minimum_measure_width = 120.0;
  options.whole_note_spacing    = 120.0;
  return options;
}

[[nodiscard]] SpelledPitch pitch(Letter letter) {
  return *SpelledPitch::create(letter, 4);
}

[[nodiscard]] Duration eighth() {
  return *Duration::create(NoteValue::kEighth, 0);
}

[[nodiscard]] std::vector<graphscore::NotationCommand> system_commands(
    const NotationLayout& layout, std::size_t first_measure) {
  const std::string root = layout.node_id.to_string() + "/system/" +
                           std::to_string(first_measure) + "/clip/";
  std::vector<graphscore::NotationCommand> commands;
  bool                                     copying = false;
  for (const auto& command : layout.commands) {
    const auto* clip = std::get_if<ClipCommand>(&command);
    if (clip != nullptr && clip->id.value == root + "begin") {
      copying = true;
    }
    if (copying) {
      commands.push_back(command);
    }
    if (clip != nullptr && clip->id.value == root + "end") {
      break;
    }
  }
  return commands;
}

[[nodiscard]] std::vector<HitRegion> system_hits(const NotationLayout& layout,
                                                 std::size_t system_index) {
  const NotationRect     bounds = layout.systems[system_index].bounds;
  std::vector<HitRegion> hits;
  std::ranges::copy_if(
      layout.hit_regions, std::back_inserter(hits), [&](const HitRegion& hit) {
        return hit.bounds.y >= bounds.y &&
               hit.bounds.y + hit.bounds.height <= bounds.y + bounds.height;
      });
  return hits;
}

TEST(NotationLayoutTest, AlignsEveryActiveStaffToOneCommonTimeline) {
  Fixture fixture({StaffLayout::single_staff(), StaffLayout::grand_staff()}, 3);
  const FixedMetrics    metrics;
  const NotationLayout& layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  ASSERT_EQ(layout.systems.size(), 1u);
  ASSERT_EQ(layout.systems[0].staves.size(), 3u);
  for (const auto& staff : layout.systems[0].staves) {
    ASSERT_EQ(staff.measure_bounds.size(), 3u);
    for (std::size_t index = 0; index < 3; ++index) {
      EXPECT_EQ(staff.measure_bounds[index].x,
                layout.systems[0].measures[index].bounds.x);
      EXPECT_EQ(staff.measure_bounds[index].width,
                layout.systems[0].measures[index].bounds.width);
    }
  }
  EXPECT_EQ(layout.systems[0].staves[0].track_id, fixture.track_ids[0]);
  EXPECT_EQ(layout.systems[0].staves[1].track_id, fixture.track_ids[1]);
}

TEST(NotationLayoutTest, PreservesArbitraryFixedStaveOrder) {
  const std::vector<graphscore::StaveDefinition> definitions = {
      {graphscore::StaveId::generate(), Clef::kTreble},
      {graphscore::StaveId::generate(), Clef::kAlto},
      {graphscore::StaveId::generate(), Clef::kBass},
  };
  Fixture              fixture({*StaffLayout::create(definitions)}, 1);
  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  ASSERT_EQ(layout.systems[0].staves.size(), definitions.size());
  for (std::size_t index = 0; index < definitions.size(); ++index) {
    EXPECT_EQ(layout.systems[0].staves[index].stave_id, definitions[index].id);
  }
}

TEST(NotationLayoutTest, ExcludesArchivedTracksAndPreservesProjectOrder) {
  Fixture fixture({StaffLayout::single_staff(), StaffLayout::single_staff(),
                   StaffLayout::single_staff()},
                  1);
  ASSERT_TRUE(fixture.project.archive_track(fixture.track_ids[1]).ok());
  const FixedMetrics    metrics;
  const NotationLayout& layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  ASSERT_EQ(layout.systems[0].staves.size(), 2u);
  EXPECT_EQ(layout.systems[0].staves[0].track_id, fixture.track_ids[0]);
  EXPECT_EQ(layout.systems[0].staves[1].track_id, fixture.track_ids[2]);

  ASSERT_TRUE(fixture.project.restore_track(fixture.track_ids[1]).ok());
  const NotationLayout restored = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  ASSERT_EQ(restored.systems[0].staves.size(), 3u);
  EXPECT_EQ(restored.systems[0].staves[2].track_id, fixture.track_ids[1]);
}

TEST(NotationLayoutTest, EnumeratesAllFourVoiceSlotsAndTheirContent) {
  Fixture fixture({StaffLayout::single_staff()}, 1);
  for (std::uint8_t voice = Voice::kMin; voice <= Voice::kMax; ++voice) {
    fixture.append_quarter_notes(0, 0, voice, voice);
  }
  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const auto& voices = layout.systems[0].staves[0].voices;
  ASSERT_EQ(voices.size(), 4u);
  for (std::size_t index = 0; index < voices.size(); ++index) {
    EXPECT_EQ(voices[index].voice.index(), index + 1);
    EXPECT_EQ(voices[index].event_count, index + 1);
  }
}

TEST(NotationLayoutTest, BreaksSystemsGreedilyAndKeepsExactFit) {
  Fixture               fixture({StaffLayout::single_staff()}, 5);
  const FixedMetrics    metrics;
  NotationLayoutOptions options;
  options.system_width          = 280.0;
  options.left_margin           = 20.0;
  options.right_margin          = 20.0;
  options.minimum_measure_width = 120.0;
  options.whole_note_spacing    = 120.0;
  const NotationLayout& layout  = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics, options));

  ASSERT_EQ(layout.systems.size(), 3u);
  EXPECT_EQ(layout.systems[0].measures.size(), 2u);
  EXPECT_EQ(layout.systems[1].first_measure, 2u);
  EXPECT_EQ(layout.systems[2].first_measure, 4u);
  EXPECT_EQ(layout.systems[0].measures[1].bounds.x +
                layout.systems[0].measures[1].bounds.width,
            260.0);
}

TEST(NotationLayoutTest, DefaultSpacingPolicyIsStableAndValid) {
  const NotationLayoutOptions options;
  EXPECT_EQ(options.system_width, 960.0);
  EXPECT_EQ(options.left_margin, 24.0);
  EXPECT_EQ(options.right_margin, 24.0);
  EXPECT_EQ(options.top_margin, 24.0);
  EXPECT_EQ(options.bottom_margin, 24.0);
  EXPECT_EQ(options.staff_space, 10.0);
  EXPECT_EQ(options.stave_gap, 50.0);
  EXPECT_EQ(options.system_gap, 70.0);
  EXPECT_EQ(options.minimum_measure_width, 120.0);
  EXPECT_EQ(options.whole_note_spacing, 180.0);
  EXPECT_TRUE(options.valid());
}

TEST(NotationLayoutTest, ConfiguredSpacingChangesBreaksAtDocumentedTie) {
  Fixture               fixture({StaffLayout::single_staff()}, 3);
  const FixedMetrics    metrics;
  NotationLayoutOptions exact_fit;
  exact_fit.system_width          = 280.0;
  exact_fit.left_margin           = 20.0;
  exact_fit.right_margin          = 20.0;
  exact_fit.minimum_measure_width = 120.0;
  exact_fit.whole_note_spacing    = 120.0;
  const NotationLayout exact      = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics, exact_fit));

  NotationLayoutOptions narrower = exact_fit;
  narrower.system_width          = 279.0;
  const NotationLayout split     = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics, narrower));
  ASSERT_EQ(exact.systems.size(), 2u);
  EXPECT_EQ(exact.systems[0].measures.size(), 2u);
  ASSERT_EQ(split.systems.size(), 3u);
  EXPECT_EQ(split.systems[0].measures.size(), 1u);
}

TEST(NotationLayoutTest, VoiceCountsAndRhythmCommandsAreSystemLocal) {
  Fixture fixture({StaffLayout::single_staff()}, 2);
  fixture.append_quarter_notes(0, 0, 1, 5);
  fixture.append_quarter_rest(0, 0, 2);
  const FixedMetrics    metrics;
  NotationLayoutOptions options;
  options.system_width          = 160.0;
  options.left_margin           = 20.0;
  options.right_margin          = 20.0;
  options.minimum_measure_width = 120.0;
  options.whole_note_spacing    = 120.0;
  const NotationLayout layout   = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics, options));

  ASSERT_EQ(layout.systems.size(), 2u);
  EXPECT_EQ(layout.systems[0].staves[0].voices[0].event_count, 4u);
  EXPECT_EQ(layout.systems[1].staves[0].voices[0].event_count, 1u);
  EXPECT_EQ(layout.systems[0].staves[0].voices[1].event_count, 1u);
  EXPECT_EQ(layout.systems[1].staves[0].voices[1].event_count, 0u);
}

TEST(NotationLayoutTest, EmitsRetainedStaffBarlineSignatureAndRhythmGeometry) {
  Fixture fixture({StaffLayout::single_staff(Clef::kBass)}, 1);
  fixture.append_quarter_notes(0, 0, 1, 1);
  const FixedMetrics    metrics;
  const NotationLayout& layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  const auto line_count = std::count_if(
      layout.commands.begin(), layout.commands.end(), [](const auto& command) {
        return std::holds_alternative<LineCommand>(command);
      });
  const auto glyph_count = std::count_if(
      layout.commands.begin(), layout.commands.end(), [](const auto& command) {
        return std::holds_alternative<GlyphCommand>(command);
      });
  const auto clip_count = std::count_if(
      layout.commands.begin(), layout.commands.end(), [](const auto& command) {
        return std::holds_alternative<ClipCommand>(command);
      });
  EXPECT_GE(line_count, 7);
  EXPECT_GE(glyph_count, 4);
  EXPECT_EQ(clip_count, 2);
  const auto& staff = layout.systems[0].staves[0];
  EXPECT_EQ(staff.bounds.height, 40.0);
  EXPECT_EQ(staff.measure_bounds[0].x, 24.0);
  EXPECT_EQ(staff.measure_bounds[0].height, 40.0);
  EXPECT_TRUE(layout.geometry_is_finite());
  EXPECT_TRUE(std::all_of(
      layout.commands.begin(), layout.commands.end(), [](const auto& command) {
        return std::visit(
            [](const auto& concrete) { return !concrete.id.value.empty(); },
            command);
      }));
}

TEST(NotationLayoutTest, EmitsConfiguredClefKeyAndTimeSmuflCodepoints) {
  Fixture       fixture({StaffLayout::single_staff(Clef::kBass)}, 1);
  NodeTimeline* timeline =
      fixture.project.find_node(fixture.node_id)->timeline();
  ASSERT_TRUE(
      timeline->set_measure_key_signature(0, *KeySignature::create(2)).ok());
  ASSERT_TRUE(
      timeline->set_measure_time_signature(0, *TimeSignature::create(3, 8))
          .ok());
  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  std::vector<char32_t> codepoints;
  for (const auto& command : layout.commands) {
    if (const auto* glyph = std::get_if<GlyphCommand>(&command)) {
      codepoints.push_back(glyph->code_point);
    }
  }
  EXPECT_EQ(std::count(codepoints.begin(), codepoints.end(), U'\uE062'), 1);
  EXPECT_EQ(std::count(codepoints.begin(), codepoints.end(), U'\uE262'), 2);
  EXPECT_EQ(std::count(codepoints.begin(), codepoints.end(), U'\uE083'), 1);
  EXPECT_EQ(std::count(codepoints.begin(), codepoints.end(), U'\uE088'), 1);
}

TEST(NotationLayoutTest, OwnsRetainedPathGeometryWithoutToolkitTypes) {
  NotationLayout layout;
  PathCommand    path{NotationId{"domain-id/slur/path"},
                      {{PathVerb::kMove, {}, {}, {1.0, 2.0}},
                       {PathVerb::kCubic, {2.0, 1.0}, {3.0, 1.0}, {4.0, 2.0}}},
                   0.5,
                   false};
  layout.commands.emplace_back(path);
  EXPECT_EQ(std::get<PathCommand>(layout.commands[0]), path);
  EXPECT_TRUE(layout.geometry_is_finite());
  std::get<PathCommand>(layout.commands[0]).elements[0].end.x =
      std::numeric_limits<double>::quiet_NaN();
  EXPECT_FALSE(layout.geometry_is_finite());
}

TEST(NotationLayoutTest, StableSemanticIdsSurviveUnrelatedContentChanges) {
  Fixture fixture({StaffLayout::single_staff(), StaffLayout::single_staff()},
                  2);
  const FixedMetrics   metrics;
  const NotationLayout before = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  fixture.append_quarter_notes(1, 0, 4, 1);
  const NotationLayout after = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  EXPECT_EQ(before.systems[0].id, after.systems[0].id);
  EXPECT_EQ(before.systems[0].measures[0].id, after.systems[0].measures[0].id);
  EXPECT_EQ(before.systems[0].staves[0].id, after.systems[0].staves[0].id);
  EXPECT_EQ(before.systems[0].staves[0].voices[0].id,
            after.systems[0].staves[0].voices[0].id);

  const std::string stable_root =
      before.systems[0].staves[0].stave_id.to_string();
  const auto command_ids = [&stable_root](const NotationLayout& layout) {
    std::vector<NotationId> ids;
    for (const auto& command : layout.commands) {
      const NotationId& id = std::visit(
          [](const auto& concrete) -> const NotationId& { return concrete.id; },
          command);
      if (id.value.starts_with(stable_root)) {
        ids.push_back(id);
      }
    }
    return ids;
  };
  const auto hit_ids = [&stable_root](const NotationLayout& layout) {
    std::vector<NotationId> ids;
    for (const HitRegion& region : layout.hit_regions) {
      if (region.id.value.starts_with(stable_root)) {
        ids.push_back(region.id);
      }
    }
    return ids;
  };
  EXPECT_EQ(command_ids(before), command_ids(after));
  EXPECT_EQ(hit_ids(before), hit_ids(after));
}

TEST(NotationLayoutTest, HitTestingUsesDocumentedPriorityAreaAndIdTies) {
  NotationLayout layout;
  layout.hit_regions = {
      HitRegion{NotationId{"z"}, NotationId{"z"}, HitRole::kStaff,
                NotationRect{0.0, 0.0, 10.0, 10.0}, 2, std::nullopt,
                std::nullopt},
      HitRegion{NotationId{"b"}, NotationId{"b"}, HitRole::kEvent,
                NotationRect{0.0, 0.0, 5.0, 5.0}, 4, std::nullopt,
                std::nullopt},
      HitRegion{NotationId{"a2"}, NotationId{"a"}, HitRole::kEvent,
                NotationRect{0.0, 0.0, 5.0, 5.0}, 4, std::nullopt,
                std::nullopt},
      HitRegion{NotationId{"a1"}, NotationId{"a"}, HitRole::kEvent,
                NotationRect{0.0, 0.0, 5.0, 5.0}, 4, std::nullopt,
                std::nullopt},
  };
  const auto hit = layout.hit_test(NotationPoint{2.0, 2.0});
  ASSERT_TRUE(hit.has_value());
  EXPECT_EQ(hit->id.value, "a1");
  std::reverse(layout.hit_regions.begin(), layout.hit_regions.end());
  EXPECT_EQ(layout.hit_test(NotationPoint{2.0, 2.0}), hit);
  EXPECT_FALSE(layout
                   .hit_test(NotationPoint{
                       std::numeric_limits<double>::quiet_NaN(), 2.0})
                   .has_value());
}

TEST(NotationLayoutTest, RejectsMissingInputNonFiniteOptionsAndMetrics) {
  Fixture            fixture({StaffLayout::single_staff()}, 1);
  const FixedMetrics metrics;
  EXPECT_EQ(layout_notation(fixture.project, NodeId::generate(), metrics).error,
            NotationLayoutError::kNodeNotFound);

  Project      no_timeline(ProjectId::generate(), "No timeline");
  const NodeId no_timeline_node = no_timeline.add_node();
  EXPECT_EQ(layout_notation(no_timeline, no_timeline_node, metrics).error,
            NotationLayoutError::kTimelineMissing);

  NotationLayoutOptions options;
  options.system_width = std::numeric_limits<double>::infinity();
  EXPECT_EQ(
      layout_notation(fixture.project, fixture.node_id, metrics, options).error,
      NotationLayoutError::kInvalidOptions);
  options             = {};
  options.staff_space = std::numeric_limits<double>::quiet_NaN();
  EXPECT_EQ(
      layout_notation(fixture.project, fixture.node_id, metrics, options).error,
      NotationLayoutError::kInvalidOptions);
  options                    = {};
  options.whole_note_spacing = NotationLayoutOptions::kMaximumCoordinate + 1.0;
  EXPECT_EQ(
      layout_notation(fixture.project, fixture.node_id, metrics, options).error,
      NotationLayoutError::kInvalidOptions);

  const InvalidMetrics invalid_metrics;
  EXPECT_EQ(
      layout_notation(fixture.project, fixture.node_id, invalid_metrics).error,
      NotationLayoutError::kInvalidMetrics);

  const OverflowMetrics overflow_metrics;
  EXPECT_EQ(
      layout_notation(fixture.project, fixture.node_id, overflow_metrics).error,
      NotationLayoutError::kInvalidMetrics);
}

TEST(NotationLayoutTest, RejectsEveryInvalidSpacingRelationship) {
  NotationLayoutOptions options;
  options.system_width = 0.0;
  EXPECT_FALSE(options.valid());
  options             = {};
  options.left_margin = -1.0;
  EXPECT_FALSE(options.valid());
  options              = {};
  options.right_margin = options.system_width;
  EXPECT_FALSE(options.valid());
  options             = {};
  options.staff_space = 0.0;
  EXPECT_FALSE(options.valid());
  options           = {};
  options.stave_gap = -1.0;
  EXPECT_FALSE(options.valid());
  options            = {};
  options.system_gap = -1.0;
  EXPECT_FALSE(options.valid());
  options                       = {};
  options.minimum_measure_width = 0.0;
  EXPECT_FALSE(options.valid());
  options                    = {};
  options.whole_note_spacing = 0.0;
  EXPECT_FALSE(options.valid());
}

TEST(NotationLayoutTest, RejectsNonFiniteValueInEveryOption) {
  using OptionMember = double NotationLayoutOptions::*;

  constexpr OptionMember members[] = {
      &NotationLayoutOptions::system_width,
      &NotationLayoutOptions::left_margin,
      &NotationLayoutOptions::right_margin,
      &NotationLayoutOptions::top_margin,
      &NotationLayoutOptions::bottom_margin,
      &NotationLayoutOptions::staff_space,
      &NotationLayoutOptions::stave_gap,
      &NotationLayoutOptions::system_gap,
      &NotationLayoutOptions::minimum_measure_width,
      &NotationLayoutOptions::whole_note_spacing,
  };
  for (const OptionMember member : members) {
    NotationLayoutOptions options;
    options.*member = std::numeric_limits<double>::quiet_NaN();
    EXPECT_FALSE(options.valid());
    options.*member = std::numeric_limits<double>::infinity();
    EXPECT_FALSE(options.valid());
  }
}

TEST(NotationLayoutTest, DoesNotMutateDomainSemantics) {
  Fixture fixture({StaffLayout::grand_staff()}, 2);
  fixture.append_quarter_notes(0, 0, 1, 2);
  const graphscore::Node before = *fixture.project.find_node(fixture.node_id);
  const FixedMetrics     metrics;
  ASSERT_TRUE(layout_notation(fixture.project, fixture.node_id, metrics));
  EXPECT_EQ(*fixture.project.find_node(fixture.node_id), before);
}

TEST(NotationLayoutTest, RepeatedLayoutIsExactlyReproducible) {
  Fixture fixture({StaffLayout::grand_staff()}, 3);
  fixture.append_quarter_notes(0, 0, 1, 3);
  fixture.append_quarter_notes(0, 1, 4, 2);
  const FixedMetrics   metrics;
  const NotationLayout first = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const NotationLayout second = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  EXPECT_EQ(first, second);
}

TEST(NotationLayoutTest, CentralizesExactSmuflBravuraVocabularyAndFallback) {
  const std::vector<std::pair<SmuflGlyph, char32_t>> expected = {
      {SmuflGlyph::kGClef, U'\uE050'},
      {SmuflGlyph::kCClef, U'\uE05C'},
      {SmuflGlyph::kFClef, U'\uE062'},
      {SmuflGlyph::kNoteheadWhole, U'\uE0A2'},
      {SmuflGlyph::kNoteheadHalf, U'\uE0A3'},
      {SmuflGlyph::kNoteheadBlack, U'\uE0A4'},
      {SmuflGlyph::kRestWhole, U'\uE4E3'},
      {SmuflGlyph::kRestHalf, U'\uE4E4'},
      {SmuflGlyph::kRestQuarter, U'\uE4E5'},
      {SmuflGlyph::kRestEighth, U'\uE4E6'},
      {SmuflGlyph::kRest16th, U'\uE4E7'},
      {SmuflGlyph::kRest32nd, U'\uE4E8'},
      {SmuflGlyph::kRest64th, U'\uE4E9'},
      {SmuflGlyph::kAugmentationDot, U'\uE1E7'},
      {SmuflGlyph::kAccidentalDoubleFlat, U'\uE264'},
      {SmuflGlyph::kAccidentalFlat, U'\uE260'},
      {SmuflGlyph::kAccidentalNatural, U'\uE261'},
      {SmuflGlyph::kAccidentalSharp, U'\uE262'},
      {SmuflGlyph::kAccidentalDoubleSharp, U'\uE263'},
      {SmuflGlyph::kFlag8thUp, U'\uE240'},
      {SmuflGlyph::kFlag64thDown, U'\uE247'},
      {SmuflGlyph::kDynamicP, U'\uE520'},
      {SmuflGlyph::kDynamicM, U'\uE521'},
      {SmuflGlyph::kDynamicF, U'\uE522'},
      {SmuflGlyph::kArticAccentAbove, U'\uE4A0'},
      {SmuflGlyph::kArticMarcatoAbove, U'\uE4AC'},
      {SmuflGlyph::kArticStaccatoAbove, U'\uE4A2'},
      {SmuflGlyph::kArticStaccatissimoAbove, U'\uE4A6'},
      {SmuflGlyph::kArticTenutoAbove, U'\uE4A4'},
      {SmuflGlyph::kPedalDown, U'\uE650'},
      {SmuflGlyph::kPedalUp, U'\uE655'},
      {SmuflGlyph::kTimeDigit0, U'\uE080'},
      {SmuflGlyph::kTupletDigit0, U'\uE880'},
  };
  for (const auto& [glyph, codepoint] : expected) {
    EXPECT_EQ(graphscore::smufl_codepoint(glyph), codepoint);
  }
  // Test-only malformed-input boundary: intentionally construct an unknown
  // enum value to verify the public fallback behavior.
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
  EXPECT_EQ(graphscore::smufl_codepoint(static_cast<SmuflGlyph>(255)),
            U'\uFFFD');
}

TEST(NotationLayoutTest, SelectsEveryClefNoteheadAndRestDurationGlyph) {
  const std::vector<graphscore::StaveDefinition> definitions = {
      {graphscore::StaveId::generate(), Clef::kTreble},
      {graphscore::StaveId::generate(), Clef::kBass},
      {graphscore::StaveId::generate(), Clef::kAlto},
      {graphscore::StaveId::generate(), Clef::kTenor},
  };
  Fixture fixture({*StaffLayout::create(definitions)}, 2);
  for (std::uint8_t value = static_cast<std::uint8_t>(NoteValue::kWhole);
       value <= static_cast<std::uint8_t>(NoteValue::kSixtyFourth); ++value) {
    const std::uint8_t dots =
        value == static_cast<std::uint8_t>(NoteValue::kSixtyFourth) ? 2 : 0;
    ASSERT_TRUE(fixture.voice()
                    .append(graphscore::make_rest(
                        *Duration::create(static_cast<NoteValue>(value), dots)))
                    .ok());
  }
  ASSERT_TRUE(fixture.voice(2)
                  .append(make_note(*SpelledPitch::create(Letter::kC, 4),
                                    *Duration::create(NoteValue::kWhole, 0)))
                  .ok());
  ASSERT_TRUE(fixture.voice(3)
                  .append(make_note(*SpelledPitch::create(Letter::kD, 4),
                                    *Duration::create(NoteValue::kHalf, 0)))
                  .ok());
  ASSERT_TRUE(fixture.project.find_node(fixture.node_id)
                  ->timeline()
                  ->add_clef_change(definitions[0].id,
                                    *graphscore::Rational::create(1, 2),
                                    Clef::kTenor)
                  .ok());
  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  std::vector<char32_t> codepoints;
  for (const auto& command : layout.commands) {
    if (const auto* glyph = std::get_if<GlyphCommand>(&command)) {
      codepoints.push_back(glyph->code_point);
    }
  }
  EXPECT_EQ(std::count(codepoints.begin(), codepoints.end(), U'\uE050'), 1);
  EXPECT_EQ(std::count(codepoints.begin(), codepoints.end(), U'\uE062'), 1);
  EXPECT_EQ(std::count(codepoints.begin(), codepoints.end(), U'\uE05C'), 3);
  for (char32_t rest = U'\uE4E3'; rest <= U'\uE4E9'; ++rest) {
    EXPECT_NE(std::ranges::find(codepoints, rest), codepoints.end());
  }
  EXPECT_NE(std::ranges::find(codepoints, U'\uE0A2'), codepoints.end());
  EXPECT_NE(std::ranges::find(codepoints, U'\uE0A3'), codepoints.end());
  EXPECT_GE(std::count(codepoints.begin(), codepoints.end(), U'\uE1E7'), 2);
}

TEST(NotationLayoutTest,
     EngravesDenseChordDotsAccidentalsStemsAndArticulations) {
  Fixture        fixture({StaffLayout::single_staff()}, 1);
  const Duration duration = *Duration::create(NoteValue::kSixteenth, 2);
  const std::vector<graphscore::Accidental> accidentals = {
      graphscore::Accidental::kDoubleFlat, graphscore::Accidental::kFlat,
      graphscore::Accidental::kNatural, graphscore::Accidental::kSharp,
      graphscore::Accidental::kDoubleSharp};
  std::vector<graphscore::ChordNote> notes;
  for (std::size_t index = 0; index < accidentals.size(); ++index) {
    notes.push_back(
        {graphscore::NotationEntityId::generate(),
         *SpelledPitch::create(
             static_cast<Letter>(static_cast<std::uint8_t>(Letter::kC) + index),
             4, accidentals[index]),
         false});
  }
  const graphscore::Chord chord = graphscore::make_chord(
      duration, notes,
      {graphscore::Articulation::kAccent, graphscore::Articulation::kMarcato},
      graphscore::StemDirection::kDown);
  ASSERT_TRUE(fixture.voice().append(chord).ok());
  const std::vector<graphscore::Articulation> duration_articulations = {
      graphscore::Articulation::kStaccato,
      graphscore::Articulation::kStaccatissimo,
      graphscore::Articulation::kTenuto};
  for (graphscore::Articulation articulation : duration_articulations) {
    ASSERT_TRUE(fixture.voice()
                    .append(make_note(*SpelledPitch::create(Letter::kC, 4),
                                      *Duration::create(NoteValue::kEighth, 0),
                                      false, {articulation}))
                    .ok());
  }
  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  std::vector<char32_t> codepoints;
  for (const auto& command : layout.commands) {
    if (const auto* glyph = std::get_if<GlyphCommand>(&command)) {
      codepoints.push_back(glyph->code_point);
    }
  }
  for (graphscore::Accidental accidental : accidentals) {
    const auto expected = static_cast<SmuflGlyph>(
        static_cast<std::uint8_t>(SmuflGlyph::kAccidentalDoubleFlat) +
        static_cast<std::uint8_t>(static_cast<int>(accidental) + 2));
    EXPECT_NE(
        std::ranges::find(codepoints, graphscore::smufl_codepoint(expected)),
        codepoints.end());
  }
  EXPECT_EQ(std::count(codepoints.begin(), codepoints.end(), U'\uE1E7'), 10);
  for (char32_t articulation :
       {U'\uE4A0', U'\uE4AC', U'\uE4A2', U'\uE4A6', U'\uE4A4'}) {
    EXPECT_NE(std::ranges::find(codepoints, articulation), codepoints.end());
  }
  for (const auto& note : notes) {
    const auto head =
        std::ranges::find_if(layout.commands, [&](const auto& command) {
          return std::visit(
              [&](const auto& concrete) {
                return concrete.id.value == note.id.to_string() + "/notehead";
              },
              command);
        });
    ASSERT_NE(head, layout.commands.end());
    const auto& glyph = std::get<GlyphCommand>(*head);
    const auto  hit   = layout.hit_test(glyph.origin);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->semantic_id.value, note.id.to_string());
    EXPECT_EQ(hit->role, HitRole::kNotehead);
  }
  EXPECT_TRUE(std::ranges::any_of(layout.commands, [&](const auto& command) {
    const auto* line = std::get_if<LineCommand>(&command);
    return line != nullptr &&
           line->id.value == chord.id.to_string() + "/stem" &&
           line->to.y > line->from.y;
  }));
}

TEST(NotationLayoutTest, AppliesFourVoiceCollisionRestBeamAndStemPolicies) {
  Fixture        fixture({StaffLayout::single_staff()}, 1);
  const Duration eighth = *Duration::create(NoteValue::kEighth, 0);
  const auto     c      = *SpelledPitch::create(Letter::kC, 4);
  const auto     d      = *SpelledPitch::create(Letter::kD, 4);
  std::vector<graphscore::NotationEntityId> first_ids;
  std::vector<graphscore::NotationEntityId> second_ids;
  for (std::uint8_t voice = 1; voice <= 4; ++voice) {
    graphscore::Note first = make_note(voice % 2 == 0 ? c : d, eighth);
    first_ids.push_back(first.id);
    ASSERT_TRUE(fixture.voice(voice).append(first).ok());
    if (voice == 3) {
      const auto rest = graphscore::make_rest(eighth);
      second_ids.push_back(rest.id);
      ASSERT_TRUE(fixture.voice(voice).append(rest).ok());
    } else {
      const auto second = make_note(c, eighth);
      second_ids.push_back(second.id);
      ASSERT_TRUE(fixture.voice(voice).append(second).ok());
    }
  }
  ASSERT_TRUE(fixture.voice(1)
                  .add_beam_override(graphscore::make_beam_override(
                      graphscore::BeamOverride::Kind::kBreak,
                      {first_ids[0],
                       graphscore::event_id(fixture.voice(1).events()[1])}))
                  .ok());
  const auto joined = make_note(d, eighth);
  ASSERT_TRUE(fixture.voice(2).append(joined).ok());
  ASSERT_TRUE(fixture.voice(2)
                  .add_beam_override(graphscore::make_beam_override(
                      graphscore::BeamOverride::Kind::kJoin,
                      {second_ids[1], joined.id}))
                  .ok());
  const auto overridden =
      make_note(d, *Duration::create(NoteValue::kQuarter, 0), false, {},
                graphscore::StemDirection::kUp);
  ASSERT_TRUE(fixture.voice(4).append(overridden).ok());
  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));

  for (std::size_t index = 0; index < first_ids.size(); ++index) {
    const bool expected_up = index == 0 || index == 2;
    EXPECT_TRUE(std::ranges::any_of(layout.commands, [&](const auto& command) {
      const auto* line = std::get_if<LineCommand>(&command);
      return line != nullptr &&
             line->id.value == first_ids[index].to_string() + "/stem" &&
             ((line->to.y < line->from.y) == expected_up);
    }));
  }
  EXPECT_FALSE(std::ranges::any_of(layout.commands, [&](const auto& command) {
    return std::visit(
        [&](const auto& concrete) {
          return concrete.id.value.starts_with(first_ids[0].to_string() +
                                               "/beam/");
        },
        command);
  }));
  EXPECT_TRUE(std::ranges::any_of(layout.commands, [](const auto& command) {
    const auto* line = std::get_if<LineCommand>(&command);
    return line != nullptr && line->id.value.contains("/beam/to/") &&
           std::abs(line->to.y - line->from.y) <= 10.0;
  }));
  EXPECT_TRUE(std::ranges::any_of(layout.commands, [&](const auto& command) {
    const auto* line = std::get_if<LineCommand>(&command);
    return line != nullptr &&
           line->id.value.starts_with(second_ids[1].to_string() + "/beam/to/" +
                                      joined.id.to_string());
  }));
  EXPECT_TRUE(std::ranges::any_of(layout.commands, [&](const auto& command) {
    const auto* line = std::get_if<LineCommand>(&command);
    return line != nullptr &&
           line->id.value == overridden.id.to_string() + "/stem" &&
           line->to.y < line->from.y;
  }));
  EXPECT_TRUE(std::ranges::any_of(layout.commands, [&](const auto& command) {
    const auto* glyph = std::get_if<GlyphCommand>(&command);
    return glyph != nullptr && glyph->id.value.starts_with(
                                   second_ids[2].to_string() + "/voice/3/rest");
  }));
}

TEST(NotationLayoutTest, EngravesTupletsGraceDynamicsAndAllSpanFamilies) {
  Fixture        fixture({StaffLayout::single_staff()}, 2);
  const auto     ratio   = *graphscore::TupletRatio::create(3, 2);
  const Duration triplet = *Duration::create(NoteValue::kEighth, 0, ratio);
  const auto     pitch   = *SpelledPitch::create(Letter::kE, 4);
  std::vector<graphscore::Note> notes;
  for (int index = 0; index < 6; ++index) {
    notes.push_back(make_note(pitch, triplet, index == 0));
    ASSERT_TRUE(fixture.voice().append(notes.back()).ok());
  }
  ASSERT_TRUE(fixture.voice()
                  .add_dynamic(graphscore::make_dynamic_marking(
                      notes[0].id, graphscore::Dynamic::kFff))
                  .ok());
  ASSERT_TRUE(fixture.voice()
                  .add_hairpin(graphscore::make_hairpin(
                      notes[0].id, notes[5].id,
                      graphscore::HairpinDirection::kDiminuendo))
                  .ok());
  ASSERT_TRUE(fixture.voice()
                  .add_slur(graphscore::make_slur(notes[0].id, notes[5].id))
                  .ok());
  const graphscore::GraceNote grace{graphscore::NotationEntityId::generate(),
                                    *SpelledPitch::create(Letter::kF, 5),
                                    *Duration::create(NoteValue::kSixteenth, 0),
                                    graphscore::GraceNoteType::kAcciaccatura,
                                    true};
  ASSERT_TRUE(
      fixture.voice()
          .add_grace_group(graphscore::make_grace_group(notes[2].id, {grace}))
          .ok());
  auto* lane =
      fixture.project.find_node(fixture.node_id)->lane(fixture.track_ids[0]);
  ASSERT_TRUE(
      lane->add_pedal_span(fixture.stave_id(),
                           graphscore::make_pedal_span(graphscore::Rational(0),
                                                       graphscore::Rational(1)))
          .ok());
  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const std::vector<std::string> roles = {
      "/tuplet/bracket",   "/grace-notehead", "/slash",
      "/hairpin/segment/", "/slur/segment/",  "/tie/segment/",
      "/pedal/segment/"};
  for (const std::string& role : roles) {
    EXPECT_TRUE(std::ranges::any_of(layout.commands, [&](const auto& command) {
      return std::visit(
          [&](const auto& concrete) {
            return concrete.id.value.contains(role);
          },
          command);
    })) << role;
  }
  EXPECT_GE(
      std::count_if(layout.commands.begin(), layout.commands.end(),
                    [](const auto& command) {
                      const auto* glyph = std::get_if<GlyphCommand>(&command);
                      return glyph != nullptr && glyph->code_point == U'\uE522';
                    }),
      3);
}

TEST(NotationLayoutTest,
     SplitsCrossSystemSpansIntoStableClippedSemanticSegments) {
  Fixture                       fixture({StaffLayout::single_staff()}, 4);
  std::vector<graphscore::Note> notes;
  const Duration quarter = *Duration::create(NoteValue::kQuarter, 0);
  for (int index = 0; index < 16; ++index) {
    notes.push_back(make_note(*SpelledPitch::create(Letter::kC, 4), quarter));
    ASSERT_TRUE(fixture.voice().append(notes.back()).ok());
  }
  const auto slur = graphscore::make_slur(notes.front().id, notes.back().id);
  ASSERT_TRUE(fixture.voice().add_slur(slur).ok());
  ASSERT_TRUE(fixture.voice()
                  .add_hairpin(graphscore::make_hairpin(
                      notes.front().id, notes.back().id,
                      graphscore::HairpinDirection::kCrescendo))
                  .ok());
  NotationLayoutOptions options;
  options.system_width          = 160.0;
  options.left_margin           = 20.0;
  options.right_margin          = 20.0;
  options.minimum_measure_width = 120.0;
  options.whole_note_spacing    = 120.0;
  const FixedMetrics   metrics;
  const NotationLayout first = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics, options));
  const NotationLayout replay = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics, options));
  EXPECT_EQ(first, replay);
  ASSERT_EQ(first.systems.size(), 4u);
  for (std::size_t system = 0; system < first.systems.size(); ++system) {
    const std::string segment =
        slur.id.to_string() + "/slur/segment/system-" + std::to_string(system);
    EXPECT_TRUE(std::ranges::any_of(first.commands, [&](const auto& command) {
      return std::visit(
          [&](const auto& concrete) {
            return concrete.id.value.starts_with(segment);
          },
          command);
    }));
    EXPECT_TRUE(
        std::ranges::any_of(first.hit_regions, [&](const HitRegion& hit) {
          return hit.id.value.starts_with(segment) &&
                 hit.semantic_id.value == slur.id.to_string();
        }));
  }
}

TEST(NotationLayoutTest,
     OmitsMalformedReferencesWithOwnedDeterministicDiagnostics) {
  Fixture    fixture({StaffLayout::single_staff()}, 1);
  const auto note = make_note(*SpelledPitch::create(Letter::kC, 4),
                              *Duration::create(NoteValue::kQuarter, 0));
  ASSERT_TRUE(fixture.voice().append(note).ok());
  const auto missing = graphscore::NotationEntityId::generate();
  const auto dynamic =
      graphscore::make_dynamic_marking(missing, graphscore::Dynamic::kF);
  const auto slur = graphscore::make_slur(note.id, missing);
  ASSERT_TRUE(fixture.voice().add_dynamic(dynamic).ok());
  ASSERT_TRUE(fixture.voice().add_slur(slur).ok());
  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  ASSERT_EQ(layout.diagnostics.size(), 2u);
  EXPECT_TRUE(
      std::ranges::all_of(layout.diagnostics, [](const auto& diagnostic) {
        return diagnostic.policy.starts_with("omitted-invalid-reference:");
      }));
  EXPECT_FALSE(std::ranges::any_of(layout.commands, [&](const auto& command) {
    return std::visit(
        [&](const auto& concrete) {
          return concrete.id.value.starts_with(dynamic.id.to_string()) ||
                 concrete.id.value.starts_with(slur.id.to_string());
        },
        command);
  }));
  const NotationLayout replay = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  EXPECT_EQ(layout.diagnostics, replay.diagnostics);
}

TEST(NotationIncrementalTest,
     LocalEditRebuildsOneSystemAndRetainsUnaffectedRecordsAndIds) {
  Fixture fixture({StaffLayout::single_staff()}, 8);
  fixture.append_quarter_notes(0, 0, 1, 32);
  const FixedMetrics          metrics;
  const NotationLayoutOptions options = one_measure_system_options();
  NotationLayoutCache         cache;
  const auto                  initial =
      cache.update(fixture.project, fixture.node_id, metrics, options, {});
  ASSERT_TRUE(initial);
  ASSERT_EQ(initial.layout->systems.size(), 8U);
  const auto unchanged_commands = system_commands(*initial.layout, 0);
  const auto unchanged_hits     = system_hits(*initial.layout, 0);

  const auto event = graphscore::event_id(fixture.voice().events()[21]);
  ASSERT_TRUE(fixture.voice()
                  .add_dynamic(graphscore::make_dynamic_marking(
                      event, graphscore::Dynamic::kMf))
                  .ok());
  const auto updated =
      cache.update(fixture.project, fixture.node_id, metrics, options,
                   {{NotationInvalidationKind::kLocalContent, 5, 5}});
  ASSERT_TRUE(updated);
  EXPECT_EQ(updated.work.visited_measures, (std::vector<std::size_t>{5}));
  EXPECT_EQ(updated.work.rebuilt_measures, (std::vector<std::size_t>{5}));
  EXPECT_EQ(updated.work.rebuilt_systems, (std::vector<std::size_t>{5}));
  EXPECT_EQ(updated.work.reused_systems.size(), 7U);
  EXPECT_EQ(system_commands(*updated.layout, 0), unchanged_commands);
  EXPECT_EQ(system_hits(*updated.layout, 0), unchanged_hits);
  const auto fresh =
      layout_notation(fixture.project, fixture.node_id, metrics, options);
  ASSERT_TRUE(fresh);
  EXPECT_EQ(*updated.layout, *fresh.layout);

  const auto unchanged =
      cache.update(fixture.project, fixture.node_id, metrics, options, {});
  ASSERT_TRUE(unchanged);
  EXPECT_TRUE(unchanged.work.visited_measures.empty());
  EXPECT_TRUE(unchanged.work.rebuilt_systems.empty());
  EXPECT_EQ(unchanged.work.reused_systems.size(), 8U);
  EXPECT_EQ(*unchanged.layout, *updated.layout);
}

TEST(NotationIncrementalTest, CrossMeasureSpanRebuildsEveryTouchedSystem) {
  Fixture fixture({StaffLayout::single_staff()}, 6);
  fixture.append_quarter_notes(0, 0, 1, 24);
  const FixedMetrics          metrics;
  const NotationLayoutOptions options = one_measure_system_options();
  NotationLayoutCache         cache;
  ASSERT_TRUE(
      cache.update(fixture.project, fixture.node_id, metrics, options, {}));
  const auto& events = fixture.voice().events();
  ASSERT_TRUE(
      fixture.voice()
          .add_slur(graphscore::make_slur(graphscore::event_id(events[5]),
                                          graphscore::event_id(events[18])))
          .ok());
  const auto updated =
      cache.update(fixture.project, fixture.node_id, metrics, options,
                   {{NotationInvalidationKind::kCrossMeasureSpan, 1, 4}});
  ASSERT_TRUE(updated);
  EXPECT_EQ(updated.work.rebuilt_systems,
            (std::vector<std::size_t>{1, 2, 3, 4}));
  EXPECT_EQ(updated.work.reused_systems, (std::vector<std::size_t>{0, 5}));
  const auto fresh =
      layout_notation(fixture.project, fixture.node_id, metrics, options);
  ASSERT_TRUE(fresh);
  EXPECT_EQ(*updated.layout, *fresh.layout);
}

TEST(NotationIncrementalTest, ContextAndMeasureStructureInvalidateSuffixes) {
  Fixture                     fixture({StaffLayout::single_staff()}, 6);
  const FixedMetrics          metrics;
  const NotationLayoutOptions options = one_measure_system_options();
  NotationLayoutCache         cache;
  ASSERT_TRUE(
      cache.update(fixture.project, fixture.node_id, metrics, options, {}));
  ASSERT_TRUE(fixture.project.find_node(fixture.node_id)
                  ->timeline()
                  ->set_measure_key_signature(2, *KeySignature::create(-3))
                  .ok());
  NodeTimeline* const timeline =
      fixture.project.find_node(fixture.node_id)->timeline();
  ASSERT_TRUE(timeline
                  ->add_clef_change(fixture.stave_id(),
                                    timeline->measures().measure_start(2),
                                    Clef::kBass)
                  .ok());
  const auto context =
      cache.update(fixture.project, fixture.node_id, metrics, options,
                   {{NotationInvalidationKind::kContext, 2, 2}});
  ASSERT_TRUE(context);
  EXPECT_EQ(context.work.rebuilt_systems,
            (std::vector<std::size_t>{2, 3, 4, 5}));
  EXPECT_EQ(context.work.reused_systems, (std::vector<std::size_t>{0, 1}));

  ASSERT_TRUE(fixture.project.find_node(fixture.node_id)
                  ->timeline()
                  ->insert_measure(3)
                  .ok());
  const auto structure =
      cache.update(fixture.project, fixture.node_id, metrics, options,
                   {{NotationInvalidationKind::kMeasureStructure, 3, 3}});
  ASSERT_TRUE(structure);
  EXPECT_EQ(structure.work.reused_systems, (std::vector<std::size_t>{0, 1, 2}));
  EXPECT_EQ(structure.work.rebuilt_systems,
            (std::vector<std::size_t>{3, 4, 5, 6}));
  const auto fresh =
      layout_notation(fixture.project, fixture.node_id, metrics, options);
  ASSERT_TRUE(fresh);
  EXPECT_EQ(*structure.layout, *fresh.layout);
}

TEST(NotationIncrementalTest,
     OptionsTrackStaffAndMetricsChangesForceCompleteReset) {
  Fixture fixture({StaffLayout::single_staff(), StaffLayout::single_staff()},
                  4);
  const FixedMetrics          metrics;
  const NotationLayoutOptions options = one_measure_system_options();
  NotationLayoutCache         cache;
  ASSERT_TRUE(
      cache.update(fixture.project, fixture.node_id, metrics, options, {}));
  NotationLayoutOptions wider = options;
  wider.system_width          = 280.0;
  const auto option_change =
      cache.update(fixture.project, fixture.node_id, metrics, wider,
                   {{NotationInvalidationKind::kLayoutOptionsOrMetrics, 0, 0}});
  ASSERT_TRUE(option_change);
  EXPECT_TRUE(option_change.work.reused_systems.empty());
  EXPECT_EQ(option_change.work.rebuilt_measures.size(), 4U);

  ASSERT_TRUE(fixture.project.archive_track(fixture.track_ids[1]).ok());
  const auto track_change =
      cache.update(fixture.project, fixture.node_id, metrics, wider,
                   {{NotationInvalidationKind::kTrackStaffArchive, 0, 0}});
  ASSERT_TRUE(track_change);
  EXPECT_TRUE(track_change.work.reused_systems.empty());
  EXPECT_EQ(track_change.work.rebuilt_measures.size(), 4U);

  const auto metric_reset =
      cache.update(fixture.project, fixture.node_id, metrics, wider,
                   {{NotationInvalidationKind::kLayoutOptionsOrMetrics, 0, 0}});
  ASSERT_TRUE(metric_reset);
  EXPECT_TRUE(metric_reset.work.reused_systems.empty());

  const auto explicit_reset =
      cache.update(fixture.project, fixture.node_id, metrics, wider,
                   {{NotationInvalidationKind::kFullReset, 0, 0}});
  ASSERT_TRUE(explicit_reset);
  EXPECT_TRUE(explicit_reset.work.reused_systems.empty());
  cache.reset();
  const auto reset_method =
      cache.update(fixture.project, fixture.node_id, metrics, wider, {});
  ASSERT_TRUE(reset_method);
  EXPECT_TRUE(reset_method.work.reused_systems.empty());
}

TEST(NotationIncrementalTest, MalformedInvalidationFailsWithoutChangingCache) {
  Fixture                     fixture({StaffLayout::single_staff()}, 3);
  const FixedMetrics          metrics;
  const NotationLayoutOptions options = one_measure_system_options();
  NotationLayoutCache         cache;
  const auto                  initial =
      cache.update(fixture.project, fixture.node_id, metrics, options, {});
  ASSERT_TRUE(initial);
  const auto malformed =
      cache.update(fixture.project, fixture.node_id, metrics, options,
                   {{NotationInvalidationKind::kLocalContent, 2, 8}});
  EXPECT_FALSE(malformed);
  EXPECT_EQ(malformed.error, NotationLayoutError::kInvalidInvalidation);
  const auto replay =
      cache.update(fixture.project, fixture.node_id, metrics, options, {});
  ASSERT_TRUE(replay);
  EXPECT_EQ(*replay.layout, *initial.layout);
  EXPECT_EQ(replay.work.reused_systems.size(), 3U);
}

TEST(NotationIncrementalTest,
     Representative64MeasureFixtureRebuildsOnlyEditedEngravingFragment) {
  Fixture fixture({StaffLayout::single_staff()}, 64);
  fixture.append_quarter_notes(0, 0, 1, 256);
  for (std::size_t measure = 8; measure < 64; ++measure) {
    ASSERT_TRUE(
        fixture.voice()
            .add_dynamic(graphscore::make_dynamic_marking(
                graphscore::event_id(fixture.voice().events()[measure * 4]),
                graphscore::Dynamic::kP))
            .ok());
  }
  const FixedMetrics          metrics;
  const NotationLayoutOptions options = one_measure_system_options();
  NotationLayoutCache         cache;
  const auto                  initial =
      cache.update(fixture.project, fixture.node_id, metrics, options, {});
  ASSERT_TRUE(initial);
  const auto trailing_commands = system_commands(*initial.layout, 63);
  const auto trailing_hits     = system_hits(*initial.layout, 63);
  ASSERT_TRUE(fixture.voice()
                  .add_dynamic(graphscore::make_dynamic_marking(
                      graphscore::event_id(fixture.voice().events()[4]),
                      graphscore::Dynamic::kF))
                  .ok());
  const auto updated =
      cache.update(fixture.project, fixture.node_id, metrics, options,
                   {{NotationInvalidationKind::kLocalContent, 1, 1}});
  ASSERT_TRUE(updated);
  EXPECT_EQ(updated.work.visited_measures, (std::vector<std::size_t>{1}));
  EXPECT_EQ(updated.work.rebuilt_measures, (std::vector<std::size_t>{1}));
  EXPECT_EQ(updated.work.rebuilt_systems, (std::vector<std::size_t>{1}));
  EXPECT_EQ(updated.work.reused_systems.size(), 63U);
  EXPECT_EQ(system_commands(*updated.layout, 63), trailing_commands);
  EXPECT_EQ(system_hits(*updated.layout, 63), trailing_hits);
  const auto fresh =
      layout_notation(fixture.project, fixture.node_id, metrics, options);
  ASSERT_TRUE(fresh);
  EXPECT_EQ(*updated.layout, *fresh.layout);
}

TEST(NotationIncrementalTest, CountsPrerequisiteEventAndReferenceVisits) {
  Fixture fixture({StaffLayout::single_staff()}, 64);
  fixture.append_quarter_notes(0, 0, 1, 256);
  for (std::size_t measure = 0; measure < 63; ++measure) {
    ASSERT_TRUE(
        fixture.voice()
            .add_dynamic(graphscore::make_dynamic_marking(
                graphscore::event_id(fixture.voice().events()[measure * 4]),
                graphscore::Dynamic::kP))
            .ok());
  }
  const FixedMetrics          metrics;
  const NotationLayoutOptions options = one_measure_system_options();
  NotationLayoutCache         cache;
  ASSERT_TRUE(
      cache.update(fixture.project, fixture.node_id, metrics, options, {}));
  const auto event = graphscore::event_id(fixture.voice().events()[253]);
  ASSERT_TRUE(fixture.voice()
                  .add_dynamic(graphscore::make_dynamic_marking(
                      event, graphscore::Dynamic::kF))
                  .ok());
  const auto updated =
      cache.update(fixture.project, fixture.node_id, metrics, options,
                   {{NotationInvalidationKind::kLocalContent, 63, 63}});
  ASSERT_TRUE(updated);
  EXPECT_EQ(updated.work.event_visits, 4U);
  EXPECT_EQ(updated.work.reference_visits, 1U);
  EXPECT_EQ(updated.work.visited_measures, (std::vector<std::size_t>{63}));
}

TEST(NotationLayoutTest, ContentSpacingPreventsSixtyFourthAndSignatureOverlap) {
  Fixture        fixture({StaffLayout::single_staff()}, 1);
  const Duration duration = *Duration::create(NoteValue::kSixtyFourth, 0);
  for (int index = 0; index < 64; ++index) {
    ASSERT_TRUE(
        fixture.voice()
            .append(make_note(*SpelledPitch::create(Letter::kC, 4), duration))
            .ok());
  }
  ASSERT_TRUE(fixture.project.find_node(fixture.node_id)
                  ->timeline()
                  ->set_measure_key_signature(0, *KeySignature::create(7))
                  .ok());
  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  std::vector<double> heads;
  double              signature_right = 0.0;
  for (const auto& command : layout.commands) {
    const auto* glyph = std::get_if<GlyphCommand>(&command);
    if (glyph == nullptr) {
      continue;
    }
    if (glyph->id.value.ends_with("/notehead")) {
      heads.push_back(glyph->origin.x);
    } else if (glyph->id.value.contains("/measure/0/") &&
               (glyph->id.value.contains("/clef") ||
                glyph->id.value.contains("/key/") ||
                glyph->id.value.contains("/time/"))) {
      signature_right = std::max(signature_right, glyph->origin.x + 12.5);
    }
  }
  ASSERT_EQ(heads.size(), 64U);
  EXPECT_GT(heads.front() - 2.5, signature_right);
  for (std::size_t index = 1; index < heads.size(); ++index) {
    EXPECT_GE(heads[index] - heads[index - 1], 15.0);
  }
}

TEST(NotationLayoutTest, AccidentalStateUsesKeyAndSharedVoiceOrder) {
  Fixture fixture({StaffLayout::single_staff()}, 1);
  ASSERT_TRUE(fixture.project.find_node(fixture.node_id)
                  ->timeline()
                  ->set_measure_key_signature(0, *KeySignature::create(1))
                  .ok());
  const Duration quarter = *Duration::create(NoteValue::kQuarter, 0);
  const auto     f_sharp =
      *SpelledPitch::create(Letter::kF, 4, graphscore::Accidental::kSharp);
  const auto f_natural =
      *SpelledPitch::create(Letter::kF, 4, graphscore::Accidental::kNatural);
  ASSERT_TRUE(fixture.voice(1).append(make_note(f_sharp, quarter)).ok());
  ASSERT_TRUE(fixture.voice(1).append(make_note(f_natural, quarter)).ok());
  ASSERT_TRUE(fixture.voice(1).append(make_note(f_natural, quarter)).ok());
  ASSERT_TRUE(fixture.voice(2).append(make_note(f_natural, quarter)).ok());
  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const auto note_accidentals = std::count_if(
      layout.commands.begin(), layout.commands.end(), [](const auto& command) {
        const auto* glyph = std::get_if<GlyphCommand>(&command);
        return glyph != nullptr && glyph->id.value.contains("/accidental/");
      });
  EXPECT_EQ(note_accidentals, 1);
  EXPECT_TRUE(std::ranges::any_of(layout.commands, [](const auto& command) {
    const auto* glyph = std::get_if<GlyphCommand>(&command);
    return glyph != nullptr && glyph->id.value.contains("/accidental/") &&
           glyph->code_point == U'\uE261';
  }));
}

TEST(NotationLayoutTest, CollisionPassMovesOnlyCollisionsAndAlternatesSeconds) {
  Fixture        fixture({StaffLayout::single_staff()}, 1);
  const Duration quarter = *Duration::create(NoteValue::kQuarter, 0);
  const auto high = make_note(*SpelledPitch::create(Letter::kC, 6), quarter);
  const auto low  = make_note(*SpelledPitch::create(Letter::kC, 3), quarter);
  ASSERT_TRUE(fixture.voice(1).append(high).ok());
  ASSERT_TRUE(fixture.voice(2).append(low).ok());
  std::vector<graphscore::ChordNote> seconds;
  for (Letter letter : {Letter::kC, Letter::kD, Letter::kE, Letter::kF}) {
    seconds.push_back({graphscore::NotationEntityId::generate(),
                       *SpelledPitch::create(letter, 4), false});
  }
  ASSERT_TRUE(
      fixture.voice(3).append(graphscore::make_chord(quarter, seconds)).ok());
  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  const auto head_x = [&](graphscore::NotationEntityId id) {
    const auto found =
        std::ranges::find_if(layout.commands, [&](const auto& command) {
          const auto* glyph = std::get_if<GlyphCommand>(&command);
          return glyph != nullptr &&
                 glyph->id.value == id.to_string() + "/notehead";
        });
    EXPECT_NE(found, layout.commands.end());
    return std::get<GlyphCommand>(*found).origin.x;
  };
  EXPECT_EQ(head_x(high.id), head_x(low.id));
  const double first = head_x(seconds[0].id);
  EXPECT_NE(head_x(seconds[1].id), first);
  EXPECT_EQ(head_x(seconds[2].id), first);
  EXPECT_NE(head_x(seconds[3].id), first);
}

TEST(NotationLayoutTest, CMajorShowsOnlyAlterationAndCancellation) {
  Fixture        fixture({StaffLayout::single_staff()}, 1);
  const Duration quarter = *Duration::create(NoteValue::kQuarter, 0);
  const auto     natural =
      *SpelledPitch::create(Letter::kC, 4, graphscore::Accidental::kNatural);
  const auto sharp =
      *SpelledPitch::create(Letter::kC, 4, graphscore::Accidental::kSharp);
  ASSERT_TRUE(fixture.voice().append(make_note(natural, quarter)).ok());
  ASSERT_TRUE(fixture.voice().append(make_note(sharp, quarter)).ok());
  ASSERT_TRUE(fixture.voice().append(make_note(sharp, quarter)).ok());
  ASSERT_TRUE(fixture.voice().append(make_note(natural, quarter)).ok());
  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  std::vector<char32_t> note_accidentals;
  for (const auto& command : layout.commands) {
    const auto* glyph = std::get_if<GlyphCommand>(&command);
    if (glyph != nullptr && glyph->id.value.contains("/accidental/")) {
      note_accidentals.push_back(glyph->code_point);
    }
  }
  EXPECT_EQ(note_accidentals, (std::vector<char32_t>{U'\uE262', U'\uE261'}));
}

TEST(NotationLayoutTest, EmitsSignaturesOnlyAtStartsAndChangesWithKeyLanes) {
  const std::vector<graphscore::StaveDefinition> definitions = {
      {graphscore::StaveId::generate(), Clef::kTreble},
      {graphscore::StaveId::generate(), Clef::kBass},
      {graphscore::StaveId::generate(), Clef::kAlto},
      {graphscore::StaveId::generate(), Clef::kTenor},
  };
  Fixture fixture({*StaffLayout::create(definitions)}, 3);
  auto*   timeline = fixture.project.find_node(fixture.node_id)->timeline();
  ASSERT_TRUE(
      timeline->set_measure_key_signature(0, *KeySignature::create(3)).ok());
  ASSERT_TRUE(
      timeline->set_measure_key_signature(1, *KeySignature::create(3)).ok());
  ASSERT_TRUE(
      timeline->set_measure_key_signature(2, *KeySignature::create(-2)).ok());
  ASSERT_TRUE(
      timeline->set_measure_time_signature(2, *TimeSignature::create(3, 4))
          .ok());
  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  for (const auto& definition : definitions) {
    const std::string root = definition.id.to_string();
    EXPECT_FALSE(std::ranges::any_of(layout.commands, [&](const auto& command) {
      const auto* glyph = std::get_if<GlyphCommand>(&command);
      return glyph != nullptr && glyph->id.value.starts_with(root) &&
             glyph->id.value.contains("measure/1/");
    }));
  }
  EXPECT_EQ(std::count_if(layout.commands.begin(), layout.commands.end(),
                          [](const auto& command) {
                            const auto* glyph =
                                std::get_if<GlyphCommand>(&command);
                            return glyph != nullptr &&
                                   glyph->id.value.contains("/key-cancel/") &&
                                   glyph->code_point == U'\uE261';
                          }),
            12);
  std::vector<double> key_y;
  for (const auto& command : layout.commands) {
    const auto* glyph = std::get_if<GlyphCommand>(&command);
    if (glyph != nullptr && glyph->id.value.contains("measure/0/key/0")) {
      key_y.push_back(glyph->origin.y);
    }
  }
  ASSERT_EQ(key_y.size(), 4U);
  std::ranges::sort(key_y);
  EXPECT_GT(std::ranges::unique(key_y).begin() - key_y.begin(), 1);
}

TEST(NotationLayoutTest, MarkingLanesUseOnlyLocalGeometricOverlaps) {
  Fixture fixture({StaffLayout::single_staff()}, 4);
  fixture.append_quarter_notes(0, 0, 1, 16);
  const auto& events = fixture.voice().events();
  for (const auto& event : events) {
    ASSERT_TRUE(fixture.voice()
                    .add_dynamic(graphscore::make_dynamic_marking(
                        graphscore::event_id(event), graphscore::Dynamic::kF))
                    .ok());
  }
  for (std::size_t index = 0; index < 8; ++index) {
    ASSERT_TRUE(fixture.voice()
                    .add_hairpin(graphscore::make_hairpin(
                        graphscore::event_id(events[index]),
                        graphscore::event_id(events[index + 8]),
                        graphscore::HairpinDirection::kCrescendo))
                    .ok());
  }
  auto* lane =
      fixture.project.find_node(fixture.node_id)->lane(fixture.track_ids[0]);
  for (int index = 0; index < 8; ++index) {
    ASSERT_TRUE(lane->add_pedal_span(
                        fixture.stave_id(),
                        graphscore::make_pedal_span(graphscore::Rational(0),
                                                    graphscore::Rational(4)))
                    .ok());
  }
  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  EXPECT_TRUE(layout.geometry_is_finite());
  for (const auto& command : layout.commands) {
    const auto* glyph = std::get_if<GlyphCommand>(&command);
    if (glyph != nullptr && (glyph->id.value.contains("/glyph/") ||
                             glyph->id.value.contains("/pedal/segment/"))) {
      EXPECT_TRUE(std::ranges::any_of(
          layout.systems, [&](const graphscore::SystemLayout& system) {
            return glyph->origin.y >= system.bounds.y &&
                   glyph->origin.y <= system.bounds.y + system.bounds.height;
          }));
    }
  }
}

TEST(NotationLayoutTest, CompoundMeterAndBoundarySegmentsAreConventional) {
  Fixture fixture({StaffLayout::single_staff()}, 2);
  auto*   timeline = fixture.project.find_node(fixture.node_id)->timeline();
  ASSERT_TRUE(
      timeline->set_measure_time_signature(0, *TimeSignature::create(6, 8))
          .ok());
  ASSERT_TRUE(
      timeline->set_measure_time_signature(1, *TimeSignature::create(6, 8))
          .ok());
  const Duration eighth = *Duration::create(NoteValue::kEighth, 0);
  std::vector<graphscore::Note> notes;
  for (int index = 0; index < 12; ++index) {
    notes.push_back(
        make_note(*SpelledPitch::create(Letter::kC, 4), eighth, index == 5));
    ASSERT_TRUE(fixture.voice().append(notes.back()).ok());
  }
  const auto slur = graphscore::make_slur(notes.front().id, notes[6].id);
  ASSERT_TRUE(fixture.voice().add_slur(slur).ok());
  ASSERT_TRUE(fixture.voice()
                  .add_beam_override(graphscore::make_beam_override(
                      graphscore::BeamOverride::Kind::kJoin,
                      {notes[5].id, notes[6].id}))
                  .ok());
  const FixedMetrics          metrics;
  const NotationLayoutOptions options = one_measure_system_options();
  const NotationLayout        layout  = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics, options));
  const auto beam_exists = [&](std::size_t left, std::size_t right) {
    const std::string id =
        notes[left].id.to_string() + "/beam/to/" + notes[right].id.to_string();
    return std::ranges::any_of(layout.commands, [&](const auto& command) {
      const auto* line = std::get_if<LineCommand>(&command);
      return line != nullptr && line->id.value.starts_with(id);
    });
  };
  EXPECT_TRUE(beam_exists(1, 2));
  EXPECT_FALSE(beam_exists(2, 3));
  EXPECT_TRUE(beam_exists(5, 6));
  for (std::size_t system : {0U, 1U}) {
    const std::string suffix = "/segment/system-" + std::to_string(system);
    EXPECT_TRUE(std::ranges::any_of(layout.commands, [&](const auto& command) {
      return std::visit(
          [&](const auto& concrete) {
            return concrete.id.value.starts_with(slur.id.to_string()) &&
                   concrete.id.value.contains(suffix);
          },
          command);
    }));
    EXPECT_TRUE(std::ranges::any_of(layout.commands, [&](const auto& command) {
      return std::visit(
          [&](const auto& concrete) {
            return concrete.id.value.starts_with(notes[5].id.to_string()) &&
                   concrete.id.value.contains("/tie/segment/") &&
                   concrete.id.value.contains(suffix);
          },
          command);
    }));
  }
}

TEST(NotationLayoutTest, GraceNotationIsScaledAndComplete) {
  Fixture    fixture({StaffLayout::single_staff()}, 1);
  const auto principal = make_note(*SpelledPitch::create(Letter::kC, 4),
                                   *Duration::create(NoteValue::kQuarter, 0));
  ASSERT_TRUE(fixture.voice().append(principal).ok());
  const graphscore::GraceNote grace{
      graphscore::NotationEntityId::generate(),
      *SpelledPitch::create(Letter::kF, 7, graphscore::Accidental::kSharp),
      *Duration::create(NoteValue::kSixteenth, 1),
      graphscore::GraceNoteType::kAcciaccatura, true};
  ASSERT_TRUE(
      fixture.voice()
          .add_grace_group(graphscore::make_grace_group(principal.id, {grace}))
          .ok());
  const FixedMetrics   metrics;
  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  for (const std::string& role :
       {"grace-notehead", "grace-stem", "grace-flag", "grace-accidental",
        "grace-dot/0", "grace-ledger/above/0", "slash"}) {
    EXPECT_TRUE(std::ranges::any_of(layout.commands, [&](const auto& command) {
      return std::visit(
          [&](const auto& concrete) {
            return concrete.id.value.starts_with(grace.id.to_string()) &&
                   concrete.id.value.contains(role);
          },
          command);
    })) << role;
  }
  EXPECT_TRUE(std::ranges::any_of(layout.commands, [&](const auto& command) {
    const auto* glyph = std::get_if<GlyphCommand>(&command);
    return glyph != nullptr &&
           glyph->id.value == grace.id.to_string() + "/grace-notehead" &&
           glyph->staff_space == 6.5;
  }));
}

TEST(NotationLayoutTest, RejectsDerivedCoordinatesOutsideRendererContract) {
  std::vector<StaffLayout> layouts(8, StaffLayout::single_staff());
  Fixture                  fixture(std::move(layouts), 2);
  const FixedMetrics       metrics;
  NotationLayoutOptions    huge;
  huge.staff_space = NotationLayoutOptions::kMaximumCoordinate;
  EXPECT_EQ(
      layout_notation(fixture.project, fixture.node_id, metrics, huge).error,
      NotationLayoutError::kInvalidGeometry);

  const NotationLayout layout = require_layout(
      layout_notation(fixture.project, fixture.node_id, metrics));
  EXPECT_LE(layout.bounds.x + layout.bounds.width,
            NotationLayoutOptions::kMaximumCoordinate);
  EXPECT_LE(layout.bounds.y + layout.bounds.height,
            NotationLayoutOptions::kMaximumCoordinate);
  EXPECT_TRUE(std::ranges::all_of(layout.commands, [](const auto& command) {
    return std::visit(
        [](const auto& concrete) {
          if constexpr (std::is_same_v<std::decay_t<decltype(concrete)>,
                                       GlyphCommand>) {
            return std::abs(concrete.origin.x) <=
                       NotationLayoutOptions::kMaximumCoordinate &&
                   std::abs(concrete.origin.y) <=
                       NotationLayoutOptions::kMaximumCoordinate;
          }
          return true;
        },
        command);
  }));
}

class CountingMetrics final : public GlyphMetrics {
 public:
  mutable std::size_t glyph_calls   = 0;
  mutable std::size_t kerning_calls = 0;

  [[nodiscard]] GlyphMetricsValue glyph_metrics(
      char32_t /*code_point*/, double staff_space) const override {
    ++glyph_calls;
    return GlyphMetricsValue{
        NotationRect{-staff_space * 0.25, -staff_space * 0.5, staff_space * 1.5,
                     staff_space * 2.0},
        staff_space * 1.5};
  }

  [[nodiscard]] double kerning(char32_t /*left*/, char32_t /*right*/,
                               double /*staff_space*/) const override {
    ++kerning_calls;
    return 0.0;
  }

  void reset() noexcept {
    glyph_calls   = 0;
    kerning_calls = 0;
  }
};

// Locality regression for engraving work only. A counting metrics double
// verifies that unaffected fragments do not request glyph metrics. Domain and
// cache bookkeeping plus returned-layout assembly may still scan/copy retained
// content and are intentionally outside this metric.
TEST(NotationIncrementalTest,
     LocalEditNearBeginningDoesNotTriggerTrailingWidthRecomputation) {
  Fixture fixture({StaffLayout::single_staff(), StaffLayout::single_staff()},
                  16);
  fixture.append_quarter_notes(0, 0, 1, 64);
  const NotationLayoutOptions options = one_measure_system_options();
  CountingMetrics             counting;
  NotationLayoutCache         cache;

  // Full build: every measure's width is computed.
  ASSERT_TRUE(
      cache.update(fixture.project, fixture.node_id, counting, options, {}));
  const std::size_t full_build_metrics = counting.glyph_calls;
  counting.reset();

  // Local edit in measure 1 (near the beginning): add a dynamic.
  const auto& events = fixture.voice().events();
  ASSERT_GE(events.size(), 10U);
  ASSERT_TRUE(
      fixture.voice()
          .add_dynamic(graphscore::make_dynamic_marking(
              graphscore::event_id(events[4]), graphscore::Dynamic::kMf))
          .ok());
  const auto updated =
      cache.update(fixture.project, fixture.node_id, counting, options,
                   {{NotationInvalidationKind::kLocalContent, 1, 1}});
  ASSERT_TRUE(updated);

  // Only widths for measures 1–2 are recomputed (the +1 accounts for
  // key-signature cancellation comparison).  The remaining 14 measures
  // must not trigger any glyph-metrics query.
  EXPECT_LT(counting.glyph_calls, full_build_metrics / 9);

  // Engraving fragment counters reflect the local rebuild.
  EXPECT_EQ(updated.work.visited_measures, (std::vector<std::size_t>{1}));
  EXPECT_EQ(updated.work.rebuilt_systems, (std::vector<std::size_t>{1}));
  EXPECT_EQ(updated.work.reused_systems.size(), 15U);

  // Incremental must equal fresh.
  const auto fresh =
      layout_notation(fixture.project, fixture.node_id, counting, options);
  ASSERT_TRUE(fresh);
  EXPECT_EQ(*updated.layout, *fresh.layout);
}

// Item 2: same-count dynamic replacement.  Removing one dynamic and adding a
// different one keeps the vector cardinality unchanged; the cache must detect
// the content change, emit the new glyphs, drop the stale glyphs, refresh
// diagnostics, and match a fresh layout.
TEST(NotationIncrementalTest,
     SameCountDynamicReplacementReflectsNewContentAndDropsStale) {
  Fixture fixture({StaffLayout::single_staff()}, 4);
  fixture.append_quarter_notes(0, 0, 1, 16);
  const auto& events  = fixture.voice().events();
  const auto  old_dyn = graphscore::make_dynamic_marking(
      graphscore::event_id(events[0]), graphscore::Dynamic::kP);
  ASSERT_TRUE(fixture.voice().add_dynamic(old_dyn).ok());
  const auto old_dyn2 = graphscore::make_dynamic_marking(
      graphscore::event_id(events[8]), graphscore::Dynamic::kF);
  ASSERT_TRUE(fixture.voice().add_dynamic(old_dyn2).ok());

  const FixedMetrics          metrics;
  const NotationLayoutOptions options = one_measure_system_options();
  NotationLayoutCache         cache;
  ASSERT_TRUE(
      cache.update(fixture.project, fixture.node_id, metrics, options, {}));

  // Same-count replacement: remove first, add different.
  ASSERT_TRUE(fixture.voice().remove_dynamic(old_dyn.id).ok());
  const auto new_dyn = graphscore::make_dynamic_marking(
      graphscore::event_id(events[4]), graphscore::Dynamic::kFff);
  ASSERT_TRUE(fixture.voice().add_dynamic(new_dyn).ok());

  const auto updated =
      cache.update(fixture.project, fixture.node_id, metrics, options,
                   {{NotationInvalidationKind::kLocalContent, 0, 1}});
  ASSERT_TRUE(updated);

  // The new dynamic must be rendered; the old one must not.
  const auto id_contains = [&](const std::string& fragment) {
    return std::ranges::any_of(updated.layout->commands, [&](const auto& cmd) {
      return std::visit(
          [&](const auto& concrete) {
            return concrete.id.value.contains(fragment);
          },
          cmd);
    });
  };
  EXPECT_TRUE(id_contains(new_dyn.id.to_string()))
      << "new dynamic not rendered";
  EXPECT_FALSE(id_contains(old_dyn.id.to_string()))
      << "stale old dynamic still present";

  // Diagnostics must be up-to-date with the new content.
  EXPECT_TRUE(std::ranges::none_of(
      updated.layout->diagnostics,
      [&](const auto& d) { return d.entity_id == new_dyn.id; }));

  // Incremental must equal fresh layout.
  const auto fresh =
      layout_notation(fixture.project, fixture.node_id, metrics, options);
  ASSERT_TRUE(fresh);
  EXPECT_EQ(*updated.layout, *fresh.layout);

  // The updated content is correct, and the scoped engraving counters record
  // the affected systems and reference work. Retained metadata scans and index
  // maintenance are outside these engraving-fragment metrics.
  EXPECT_GE(updated.work.reference_visits, 2U);
  EXPECT_EQ(updated.work.rebuilt_systems, (std::vector<std::size_t>{0, 1}));
}

// Item 3: same-count pedal replacement.  Pedal spans are stave-scoped (not
// voice-scoped), so they exercise refresh_references independently of the
// voice-level reference refresh.  Replacing one pedal with another while
// keeping the total count equal must drop the old clip/glyph segments and
// emit the new ones.
TEST(NotationIncrementalTest,
     SameCountPedalReplacementReflectsChangedPedalContent) {
  Fixture fixture({StaffLayout::single_staff()}, 4);
  fixture.append_quarter_notes(0, 0, 1, 16);
  auto* lane =
      fixture.project.find_node(fixture.node_id)->lane(fixture.track_ids[0]);
  const auto old_pedal = graphscore::make_pedal_span(graphscore::Rational(0),
                                                     graphscore::Rational(1));
  ASSERT_TRUE(lane->add_pedal_span(fixture.stave_id(), old_pedal).ok());

  const FixedMetrics          metrics;
  const NotationLayoutOptions options = one_measure_system_options();
  NotationLayoutCache         cache;
  ASSERT_TRUE(
      cache.update(fixture.project, fixture.node_id, metrics, options, {}));

  // Same-count replacement: remove the original, add a different one.
  ASSERT_TRUE(lane->remove_pedal_span(fixture.stave_id(), old_pedal.id).ok());
  const auto new_pedal = graphscore::make_pedal_span(graphscore::Rational(0),
                                                     graphscore::Rational(3));
  ASSERT_TRUE(lane->add_pedal_span(fixture.stave_id(), new_pedal).ok());

  const auto updated =
      cache.update(fixture.project, fixture.node_id, metrics, options,
                   {{NotationInvalidationKind::kCrossMeasureSpan, 0, 3}});
  ASSERT_TRUE(updated);

  // The new pedal's segments must appear; the old one's must not.
  const auto id_contains = [&](const std::string& fragment) {
    return std::ranges::any_of(updated.layout->commands, [&](const auto& cmd) {
      return std::visit(
          [&](const auto& concrete) {
            return concrete.id.value.contains(fragment);
          },
          cmd);
    });
  };
  EXPECT_TRUE(id_contains(new_pedal.id.to_string() + "/pedal/"))
      << "new pedal segments not rendered";
  EXPECT_FALSE(id_contains(old_pedal.id.to_string() + "/pedal/"))
      << "stale old pedal segments still present";

  // Incremental must equal fresh layout.
  const auto fresh =
      layout_notation(fixture.project, fixture.node_id, metrics, options);
  ASSERT_TRUE(fresh);
  EXPECT_EQ(*updated.layout, *fresh.layout);

  // Work evidence: the systems touched by the new pedal (0-3) were rebuilt;
  // pedal reference visits are tracked.
  EXPECT_GE(updated.work.reference_visits, 1U);
  EXPECT_EQ(updated.work.rebuilt_systems.size(), 4U);
}

TEST(NotationIncrementalTest,
     LocalPedalEditRebuildsOnlyMusicallyAffectedEngravingSystem) {
  Fixture fixture({StaffLayout::single_staff()}, 8);
  fixture.append_quarter_notes(0, 0, 1, 32);
  auto* lane =
      fixture.project.find_node(fixture.node_id)->lane(fixture.track_ids[0]);
  const auto local_pedal = make_pedal_span(Rational(2), Rational(3));
  ASSERT_TRUE(lane->add_pedal_span(fixture.stave_id(), local_pedal).ok());
  for (int measure = 4; measure < 8; ++measure) {
    ASSERT_TRUE(
        lane->add_pedal_span(
                fixture.stave_id(),
                make_pedal_span(Rational(measure),
                                Rational(measure) + *Rational::create(1, 2)))
            .ok());
  }
  const FixedMetrics          metrics;
  const NotationLayoutOptions options = one_measure_system_options();
  NotationLayoutCache         cache;
  const auto                  initial =
      cache.update(fixture.project, fixture.node_id, metrics, options, {});
  ASSERT_TRUE(initial);
  const auto trailing_commands = system_commands(*initial.layout, 7);
  const auto trailing_hits     = system_hits(*initial.layout, 7);

  ASSERT_TRUE(lane->remove_pedal_span(fixture.stave_id(), local_pedal.id).ok());
  const auto replacement =
      make_pedal_span(*Rational::create(9, 4), *Rational::create(11, 4));
  ASSERT_TRUE(lane->add_pedal_span(fixture.stave_id(), replacement).ok());
  const auto updated =
      cache.update(fixture.project, fixture.node_id, metrics, options,
                   {{NotationInvalidationKind::kCrossMeasureSpan, 2, 2}});
  ASSERT_TRUE(updated);
  EXPECT_EQ(updated.work.rebuilt_measures, (std::vector<std::size_t>{2}));
  EXPECT_EQ(updated.work.rebuilt_systems, (std::vector<std::size_t>{2}));
  EXPECT_EQ(updated.work.reused_systems.size(), 7U);
  EXPECT_EQ(system_commands(*updated.layout, 7), trailing_commands);
  EXPECT_EQ(system_hits(*updated.layout, 7), trailing_hits);
  const auto fresh =
      layout_notation(fixture.project, fixture.node_id, metrics, options);
  ASSERT_TRUE(fresh);
  EXPECT_EQ(*updated.layout, *fresh.layout);
}

// ---- Item 4: notation-cache remove-one/add-two tests per family ----

// Helper: a cache-friendly fixture holding enough measures for multi-system
// coverage plus an existing early reference.
struct IncrementalReferenceFixture {
  static constexpr std::size_t kMeasures = 6;
  Fixture                     fixture{{StaffLayout::single_staff()}, kMeasures};
  const NotationLayoutOptions options = one_measure_system_options();
  const FixedMetrics          metrics;
  NotationLayoutCache         cache;

  explicit IncrementalReferenceFixture(bool complete_project = false) {
    // Fill each measure with 4 quarter notes.
    fixture.append_quarter_notes(0, 0, 1, kMeasures * 4);
    if (complete_project) {
      const Rational node_end =
          fixture.project.find_node(fixture.node_id)->timeline()->node_end();
      for (std::uint8_t voice = Voice::kMin; voice <= Voice::kMax; ++voice) {
        EXPECT_TRUE(fixture.voice(voice).normalize(node_end).ok());
      }
    }
    EXPECT_TRUE(
        cache.update(fixture.project, fixture.node_id, metrics, options, {}));
  }

  [[nodiscard]] graphscore::VoiceContent& voice() { return fixture.voice(); }

  [[nodiscard]] const std::vector<graphscore::VoiceEvent>& events() {
    return voice().events();
  }

  [[nodiscard]] graphscore::NotationEntityId early_event() {
    return graphscore::event_id(events()[0]);
  }

  [[nodiscard]] graphscore::NotationEntityId later_event() {
    return graphscore::event_id(events()[4]);
  }

  [[nodiscard]] graphscore::StaveId stave_id() { return fixture.stave_id(); }

  [[nodiscard]] graphscore::TrackLane* lane() {
    return fixture.project.find_node(fixture.node_id)
        ->lane(fixture.track_ids[0]);
  }

  template <typename Result>
  void assert_incremental_equals_fresh(const Result& updated) {
    const auto fresh =
        layout_notation(fixture.project, fixture.node_id, metrics, options);
    ASSERT_TRUE(fresh);
    EXPECT_EQ(*updated.layout, *fresh.layout);
  }

  void assert_id_present(const NotationLayout& layout,
                         const std::string&    fragment) {
    EXPECT_TRUE(std::ranges::any_of(layout.commands,
                                    [&](const auto& cmd) {
                                      return std::visit(
                                          [&](const auto& concrete) {
                                            return concrete.id.value.contains(
                                                fragment);
                                          },
                                          cmd);
                                    }))
        << "expected id fragment not found: " << fragment;
  }

  void assert_id_absent(const NotationLayout& layout,
                        const std::string&    fragment) {
    EXPECT_FALSE(std::ranges::any_of(layout.commands,
                                     [&](const auto& cmd) {
                                       return std::visit(
                                           [&](const auto& concrete) {
                                             return concrete.id.value.contains(
                                                 fragment);
                                           },
                                           cmd);
                                     }))
        << "unexpected id fragment still present: " << fragment;
  }
};

TEST(NotationIncrementalReferenceDeltaTest, RemoveOneAddTwoDynamics) {
  IncrementalReferenceFixture f;
  const auto                  old_dyn =
      make_dynamic_marking(f.early_event(), graphscore::Dynamic::kP);
  ASSERT_TRUE(f.voice().add_dynamic(old_dyn).ok());
  ASSERT_TRUE(
      f.cache.update(f.fixture.project, f.fixture.node_id, f.metrics, f.options,
                     {{NotationInvalidationKind::kLocalContent, 0, 0}}));

  // Remove old, add two new dynamics.
  ASSERT_TRUE(f.voice().remove_dynamic(old_dyn.id).ok());
  const auto new1 =
      make_dynamic_marking(f.early_event(), graphscore::Dynamic::kFf);
  const auto new2 =
      make_dynamic_marking(f.later_event(), graphscore::Dynamic::kMf);
  ASSERT_TRUE(f.voice().add_dynamic(new1).ok());
  ASSERT_TRUE(f.voice().add_dynamic(new2).ok());

  const auto updated =
      f.cache.update(f.fixture.project, f.fixture.node_id, f.metrics, f.options,
                     {{NotationInvalidationKind::kLocalContent, 0, 1}});
  ASSERT_TRUE(updated);

  f.assert_id_absent(*updated.layout, old_dyn.id.to_string());
  f.assert_id_present(*updated.layout, new1.id.to_string());
  f.assert_id_present(*updated.layout, new2.id.to_string());
  f.assert_incremental_equals_fresh(updated);
  EXPECT_LE(updated.work.reference_visits, 4U);
  EXPECT_EQ(updated.work.rebuilt_systems.size(), 2U);
}

TEST(NotationIncrementalReferenceDeltaTest, RemoveOneAddTwoHairpins) {
  IncrementalReferenceFixture f;
  const auto                  e2 = graphscore::event_id(f.events()[1]);
  const auto                  old_hp =
      make_hairpin(f.early_event(), e2, HairpinDirection::kCrescendo);
  ASSERT_TRUE(f.voice().add_hairpin(old_hp).ok());
  ASSERT_TRUE(
      f.cache.update(f.fixture.project, f.fixture.node_id, f.metrics, f.options,
                     {{NotationInvalidationKind::kCrossMeasureSpan, 0, 0}}));

  ASSERT_TRUE(f.voice().remove_hairpin(old_hp.id).ok());
  const auto new1 =
      make_hairpin(f.early_event(), e2, HairpinDirection::kDiminuendo);
  const auto e4   = graphscore::event_id(f.events()[4]);
  const auto new2 = make_hairpin(e2, e4, HairpinDirection::kCrescendo);
  ASSERT_TRUE(f.voice().add_hairpin(new1).ok());
  ASSERT_TRUE(f.voice().add_hairpin(new2).ok());

  const auto updated =
      f.cache.update(f.fixture.project, f.fixture.node_id, f.metrics, f.options,
                     {{NotationInvalidationKind::kCrossMeasureSpan, 0, 1}});
  ASSERT_TRUE(updated);

  f.assert_id_absent(*updated.layout, old_hp.id.to_string());
  f.assert_id_present(*updated.layout, new1.id.to_string());
  f.assert_id_present(*updated.layout, new2.id.to_string());
  f.assert_incremental_equals_fresh(updated);
  EXPECT_LE(updated.work.reference_visits, 5U);
}

TEST(NotationIncrementalReferenceDeltaTest, RemoveOneAddTwoSlurs) {
  IncrementalReferenceFixture f;
  const auto                  e2    = graphscore::event_id(f.events()[1]);
  const auto                  old_s = make_slur(f.early_event(), e2);
  ASSERT_TRUE(f.voice().add_slur(old_s).ok());
  ASSERT_TRUE(
      f.cache.update(f.fixture.project, f.fixture.node_id, f.metrics, f.options,
                     {{NotationInvalidationKind::kCrossMeasureSpan, 0, 0}}));

  ASSERT_TRUE(f.voice().remove_slur(old_s.id).ok());
  const auto new1 = make_slur(f.early_event(), e2);
  const auto new2 = make_slur(f.early_event(), f.later_event());
  ASSERT_TRUE(f.voice().add_slur(new1).ok());
  ASSERT_TRUE(f.voice().add_slur(new2).ok());

  const auto updated =
      f.cache.update(f.fixture.project, f.fixture.node_id, f.metrics, f.options,
                     {{NotationInvalidationKind::kCrossMeasureSpan, 0, 1}});
  ASSERT_TRUE(updated);

  f.assert_id_absent(*updated.layout, old_s.id.to_string());
  f.assert_id_present(*updated.layout, new1.id.to_string());
  f.assert_id_present(*updated.layout, new2.id.to_string());
  f.assert_incremental_equals_fresh(updated);
  EXPECT_LE(updated.work.reference_visits, 5U);
}

TEST(NotationIncrementalReferenceDeltaTest, RemoveOneAddTwoBeamOverrides) {
  IncrementalReferenceFixture f(true);
  const Duration              e_dur = *Duration::create(NoteValue::kEighth, 0);
  const auto                  pitch = *SpelledPitch::create(Letter::kC, 4);
  ASSERT_TRUE(
      f.voice()
          .replace_event(Rational(0), make_note(pitch, e_dur), Rational(6))
          .ok());
  ASSERT_TRUE(f.voice()
                  .replace_event(*Rational::create(1, 8),
                                 make_note(pitch, e_dur), Rational(6))
                  .ok());
  ASSERT_TRUE(f.voice()
                  .replace_event(*Rational::create(1, 4),
                                 make_note(pitch, e_dur), Rational(6))
                  .ok());
  ASSERT_TRUE(
      f.cache.update(f.fixture.project, f.fixture.node_id, f.metrics, f.options,
                     {{NotationInvalidationKind::kLocalContent, 0, 0}}));
  const auto& evs   = f.events();
  const auto  e1_id = graphscore::event_id(evs[0]);
  const auto  e2_id = graphscore::event_id(evs[1]);
  const auto  e3_id = graphscore::event_id(evs[2]);
  const auto  old_b =
      make_beam_override(BeamOverride::Kind::kJoin, {e1_id, e2_id});
  ASSERT_TRUE(f.voice().add_beam_override(old_b).ok());
  EXPECT_TRUE(graphscore::validate_voice_references(f.voice()).empty());
  ASSERT_TRUE(graphscore::ValidationService{}
                  .validate_complete(f.fixture.project)
                  .diagnostics.empty());
  ASSERT_TRUE(
      f.cache.update(f.fixture.project, f.fixture.node_id, f.metrics, f.options,
                     {{NotationInvalidationKind::kLocalContent, 0, 0}}));

  const auto beam_exists = [](const NotationLayout&        layout,
                              graphscore::NotationEntityId left,
                              graphscore::NotationEntityId right) {
    const std::string id = left.to_string() + "/beam/to/" + right.to_string();
    return std::ranges::any_of(layout.commands, [&](const auto& command) {
      const auto* line = std::get_if<LineCommand>(&command);
      return line != nullptr && line->id.value.starts_with(id);
    });
  };
  const auto before = layout_notation(f.fixture.project, f.fixture.node_id,
                                      f.metrics, f.options);
  ASSERT_TRUE(before);
  EXPECT_TRUE(beam_exists(*before.layout, e1_id, e2_id));

  ASSERT_TRUE(f.voice().remove_beam_override(old_b.id).ok());
  const auto new1 =
      make_beam_override(BeamOverride::Kind::kBreak, {e1_id, e2_id});
  const auto new2 =
      make_beam_override(BeamOverride::Kind::kJoin, {e2_id, e3_id});
  ASSERT_TRUE(f.voice().add_beam_override(new1).ok());
  ASSERT_TRUE(f.voice().add_beam_override(new2).ok());
  EXPECT_TRUE(graphscore::validate_voice_references(f.voice()).empty());
  ASSERT_TRUE(graphscore::ValidationService{}
                  .validate_complete(f.fixture.project)
                  .diagnostics.empty());

  const auto updated =
      f.cache.update(f.fixture.project, f.fixture.node_id, f.metrics, f.options,
                     {{NotationInvalidationKind::kLocalContent, 0, 0}});
  ASSERT_TRUE(updated);

  EXPECT_FALSE(beam_exists(*updated.layout, e1_id, e2_id));
  EXPECT_TRUE(beam_exists(*updated.layout, e2_id, e3_id));
  f.assert_incremental_equals_fresh(updated);
  EXPECT_EQ(updated.work.rebuilt_measures, (std::vector<std::size_t>{0}));
  EXPECT_EQ(updated.work.rebuilt_systems, (std::vector<std::size_t>{0}));
  EXPECT_LE(updated.work.reference_visits, 6U);
}

TEST(NotationIncrementalReferenceDeltaTest,
     ReplaceBeamOverridePreservesOrderPrecedence) {
  // Overlapping join/break overrides: the join (added first) covers e1-e2-e3
  // and the break (added second) covers e2-e3, so the break wins on e2-e3.
  // Replacing the join in place (run reduced to e2-e3) must keep its order
  // key ahead of the break — a remove/re-add would reinsert it after the
  // break and flip e2-e3 from broken to joined.
  IncrementalReferenceFixture f(true);
  const Duration              e_dur = *Duration::create(NoteValue::kEighth, 0);
  const auto                  pitch = *SpelledPitch::create(Letter::kC, 4);
  ASSERT_TRUE(
      f.voice()
          .replace_event(Rational(0), make_note(pitch, e_dur), Rational(6))
          .ok());
  ASSERT_TRUE(f.voice()
                  .replace_event(*Rational::create(1, 8),
                                 make_note(pitch, e_dur), Rational(6))
                  .ok());
  ASSERT_TRUE(f.voice()
                  .replace_event(*Rational::create(1, 4),
                                 make_note(pitch, e_dur), Rational(6))
                  .ok());
  ASSERT_TRUE(
      f.cache.update(f.fixture.project, f.fixture.node_id, f.metrics, f.options,
                     {{NotationInvalidationKind::kLocalContent, 0, 0}}));
  const auto& evs   = f.events();
  const auto  e1_id = graphscore::event_id(evs[0]);
  const auto  e2_id = graphscore::event_id(evs[1]);
  const auto  e3_id = graphscore::event_id(evs[2]);

  const auto join =
      make_beam_override(BeamOverride::Kind::kJoin, {e1_id, e2_id, e3_id});
  const auto brk =
      make_beam_override(BeamOverride::Kind::kBreak, {e2_id, e3_id});
  ASSERT_TRUE(f.voice().add_beam_override(join).ok());
  ASSERT_TRUE(f.voice().add_beam_override(brk).ok());
  EXPECT_TRUE(graphscore::validate_voice_references(f.voice()).empty());
  ASSERT_TRUE(
      f.cache.update(f.fixture.project, f.fixture.node_id, f.metrics, f.options,
                     {{NotationInvalidationKind::kLocalContent, 0, 0}}));

  const auto beam_exists = [](const NotationLayout&        layout,
                              graphscore::NotationEntityId left,
                              graphscore::NotationEntityId right) {
    const std::string id = left.to_string() + "/beam/to/" + right.to_string();
    return std::ranges::any_of(layout.commands, [&](const auto& command) {
      const auto* line = std::get_if<LineCommand>(&command);
      return line != nullptr && line->id.value.starts_with(id);
    });
  };

  // Break (added second) wins on the shared pair e2-e3.
  {
    const auto before = layout_notation(f.fixture.project, f.fixture.node_id,
                                        f.metrics, f.options);
    ASSERT_TRUE(before);
    EXPECT_TRUE(beam_exists(*before.layout, e1_id, e2_id));
    EXPECT_FALSE(beam_exists(*before.layout, e2_id, e3_id));
  }

  // Replace the join in place: same id, reduced run {e2, e3}.
  const BeamOverride replacement{
      join.id, BeamOverride::Kind::kJoin, {e2_id, e3_id}};
  ASSERT_TRUE(f.voice().replace_beam_override(join.id, replacement).ok());

  const auto updated =
      f.cache.update(f.fixture.project, f.fixture.node_id, f.metrics, f.options,
                     {{NotationInvalidationKind::kLocalContent, 0, 0}});
  ASSERT_TRUE(updated);

  // The break still wins on the shared pair e2-e3 (e1-e2 stays beamed by
  // automatic beaming, so it is not a discriminator here).
  EXPECT_TRUE(beam_exists(*updated.layout, e1_id, e2_id));
  EXPECT_FALSE(beam_exists(*updated.layout, e2_id, e3_id));
  f.assert_incremental_equals_fresh(updated);
  EXPECT_LE(updated.work.reference_visits, 3U);
}

TEST(NotationIncrementalReferenceDeltaTest, RemoveOneAddTwoGraceGroups) {
  IncrementalReferenceFixture f;
  const GraceNote             old_note{.pitch    = pitch(Letter::kD),
                                       .duration = eighth(),
                                       .type = GraceNoteType::kAppoggiatura};
  const auto old_g       = make_grace_group(f.early_event(), {old_note});
  const auto old_note_id = old_g.notes.front().id;
  ASSERT_TRUE(f.voice().add_grace_group(old_g).ok());
  ASSERT_TRUE(
      f.cache.update(f.fixture.project, f.fixture.node_id, f.metrics, f.options,
                     {{NotationInvalidationKind::kLocalContent, 0, 0}}));

  ASSERT_TRUE(f.voice().remove_grace_group(old_g.id).ok());
  const auto new1 = make_grace_group(
      f.early_event(), {GraceNote{.pitch    = pitch(Letter::kE),
                                  .duration = eighth(),
                                  .type     = GraceNoteType::kAcciaccatura}});
  const auto new2 = make_grace_group(
      f.later_event(), {GraceNote{.pitch    = pitch(Letter::kF),
                                  .duration = eighth(),
                                  .type     = GraceNoteType::kAppoggiatura}});
  const auto new1_note_id = new1.notes.front().id;
  const auto new2_note_id = new2.notes.front().id;
  ASSERT_TRUE(f.voice().add_grace_group(new1).ok());
  ASSERT_TRUE(f.voice().add_grace_group(new2).ok());

  const auto updated =
      f.cache.update(f.fixture.project, f.fixture.node_id, f.metrics, f.options,
                     {{NotationInvalidationKind::kLocalContent, 0, 1}});
  ASSERT_TRUE(updated);

  // Grace glyphs are rendered under the internal note ID, not the group ID.
  f.assert_id_absent(*updated.layout, old_note_id.to_string());
  f.assert_id_present(*updated.layout, new1_note_id.to_string());
  f.assert_id_present(*updated.layout, new2_note_id.to_string());
  f.assert_incremental_equals_fresh(updated);
  EXPECT_LE(updated.work.reference_visits, 5U);
}

TEST(NotationIncrementalReferenceDeltaTest, UpdateGraceGroupPitchInPlace) {
  // A pitch-only grace-notehead update through the narrow domain primitive
  // (VoiceContent::set_notehead_pitch) emits a kUpdate grace-group delta. The
  // retained cache must replace the group record in place, so the grace
  // notehead re-renders at the new pitch and the incremental layout still
  // equals a fresh layout.
  IncrementalReferenceFixture f;
  const GraceNote             note{.pitch    = pitch(Letter::kD),
                                   .duration = eighth(),
                                   .type     = GraceNoteType::kAcciaccatura};
  const auto                  group = make_grace_group(f.early_event(), {note});
  const auto                  note_id = group.notes.front().id;
  ASSERT_TRUE(f.voice().add_grace_group(group).ok());
  ASSERT_TRUE(
      f.cache.update(f.fixture.project, f.fixture.node_id, f.metrics, f.options,
                     {{NotationInvalidationKind::kLocalContent, 0, 0}}));

  ASSERT_TRUE(f.voice().set_notehead_pitch(note_id, pitch(Letter::kE)).ok());

  const auto updated =
      f.cache.update(f.fixture.project, f.fixture.node_id, f.metrics, f.options,
                     {{NotationInvalidationKind::kLocalContent, 0, 0}});
  ASSERT_TRUE(updated);

  f.assert_id_present(*updated.layout, note_id.to_string());
  f.assert_incremental_equals_fresh(updated);
  EXPECT_LE(updated.work.reference_visits, 3U);
}

TEST(NotationIncrementalPedalDeltaTest, RemoveOneAddTwoPedals) {
  IncrementalReferenceFixture f;
  const auto                  old_p = make_pedal_span(Rational(0), Rational(1));
  const auto                  stave_id = f.stave_id();
  ASSERT_TRUE(f.lane()->add_pedal_span(stave_id, old_p).ok());
  ASSERT_TRUE(
      f.cache.update(f.fixture.project, f.fixture.node_id, f.metrics, f.options,
                     {{NotationInvalidationKind::kCrossMeasureSpan, 0, 0}}));

  ASSERT_TRUE(f.lane()->remove_pedal_span(stave_id, old_p.id).ok());
  const auto new1 = make_pedal_span(Rational(0), Rational(1));
  const auto new2 = make_pedal_span(Rational(0), Rational(3));
  ASSERT_TRUE(f.lane()->add_pedal_span(stave_id, new1).ok());
  ASSERT_TRUE(f.lane()->add_pedal_span(stave_id, new2).ok());

  const auto updated =
      f.cache.update(f.fixture.project, f.fixture.node_id, f.metrics, f.options,
                     {{NotationInvalidationKind::kCrossMeasureSpan, 0, 3}});
  ASSERT_TRUE(updated);

  f.assert_id_absent(*updated.layout, old_p.id.to_string());
  f.assert_id_present(*updated.layout, new1.id.to_string());
  f.assert_id_present(*updated.layout, new2.id.to_string());
  f.assert_incremental_equals_fresh(updated);
  EXPECT_LE(updated.work.reference_visits, 4U);
}

// ---- Item 5: event-measure reassignment for unchanged references ----

// When the measure map changes (e.g. time-signature adjustment that shifts
// measure boundaries), unchanged references must appear in the correct new
// measure buckets without stale entries. Retained metadata scans and index
// maintenance are outside the engraving-fragment counters asserted below.

struct MeasureReassignmentFixture {
  static constexpr std::size_t kMeasures = 4;
  Fixture                     fixture{{StaffLayout::single_staff()}, kMeasures};
  const NotationLayoutOptions options = one_measure_system_options();
  const FixedMetrics          metrics;
  NotationLayoutCache         cache;

  MeasureReassignmentFixture() {
    // 4 measures × 4 quarter notes each = 16 notes.
    fixture.append_quarter_notes(0, 0, 1, kMeasures * 4);
    EXPECT_TRUE(
        cache.update(fixture.project, fixture.node_id, metrics, options, {}));
  }

  [[nodiscard]] graphscore::VoiceContent& voice() { return fixture.voice(); }

  [[nodiscard]] const std::vector<graphscore::VoiceEvent>& events() {
    return voice().events();
  }

  // Converts the timeline from 4 measures of 4/4 to 2 measures of 2/4
  // followed by 2 measures of 6/4, preserving total node length.  This
  // moves measure boundaries so that some events shift measures.
  // Because the measure count is unchanged, kContext (not kMeasureStructure)
  // is the correct invalidation kind — it rebuilds only the affected suffix.
  void change_to_2_2_6_6() {
    auto* timeline = fixture.project.find_node(fixture.node_id)->timeline();
    ASSERT_TRUE(
        timeline->set_measure_time_signature(0, *TimeSignature::create(2, 4))
            .ok());
    ASSERT_TRUE(
        timeline->set_measure_time_signature(1, *TimeSignature::create(2, 4))
            .ok());
    ASSERT_TRUE(
        timeline->set_measure_time_signature(2, *TimeSignature::create(6, 4))
            .ok());
    ASSERT_TRUE(
        timeline->set_measure_time_signature(3, *TimeSignature::create(6, 4))
            .ok());
    EXPECT_EQ(timeline->node_end(), Rational(4));
  }

  // Run cache update with kContext for this fixture's time-signature change.
  [[nodiscard]] graphscore::IncrementalNotationLayoutResult update_context() {
    return cache.update(fixture.project, fixture.node_id, metrics, options,
                        {{NotationInvalidationKind::kContext, 0, 0}});
  }

  void assert_ref_id_in_system(const NotationLayout& layout,
                               const std::string&    fragment,
                               std::size_t           system_first_measure) {
    const std::string root = layout.node_id.to_string() + "/system/" +
                             std::to_string(system_first_measure) + "/clip/";
    bool found = false;
    for (const auto& command : layout.commands) {
      if (std::visit(
              [&](const auto& concrete) -> bool {
                return concrete.id.value.starts_with(root) ||
                       concrete.id.value.contains(fragment);
              },
              command)) {
        found = true;
        break;
      }
    }
    EXPECT_TRUE(found) << "ref " << fragment << " not found in system "
                       << system_first_measure;
  }

  void assert_ref_id_in_layout(const NotationLayout& layout,
                               const std::string&    fragment) {
    EXPECT_TRUE(std::ranges::any_of(layout.commands,
                                    [&](const auto& cmd) {
                                      return std::visit(
                                          [&](const auto& concrete) {
                                            return concrete.id.value.contains(
                                                fragment);
                                          },
                                          cmd);
                                    }))
        << "ref " << fragment << " not found in layout";
  }

  void assert_ref_id_absent_from_layout(const NotationLayout& layout,
                                        const std::string&    fragment) {
    EXPECT_FALSE(std::ranges::any_of(layout.commands,
                                     [&](const auto& cmd) {
                                       return std::visit(
                                           [&](const auto& concrete) {
                                             return concrete.id.value.contains(
                                                 fragment);
                                           },
                                           cmd);
                                     }))
        << "ref " << fragment << " should be absent from layout";
  }
};

TEST(NotationIncrementalMeasureReassignmentTest,
     DynamicsFollowEventToNewMeasure) {
  MeasureReassignmentFixture f;
  // Place a dynamic on the event at onset 2/4 (measure 0, index 2 in 4/4).
  const auto evt = graphscore::event_id(f.events()[2]);
  const auto dyn = make_dynamic_marking(evt, graphscore::Dynamic::kF);
  ASSERT_TRUE(f.voice().add_dynamic(dyn).ok());
  // Use kLocalContent to incorporate the dynamic into the cache.
  ASSERT_TRUE(
      f.cache.update(f.fixture.project, f.fixture.node_id, f.metrics, f.options,
                     {{NotationInvalidationKind::kLocalContent, 0, 0}}));

  f.change_to_2_2_6_6();
  // The event at index 2 (onset 2/4) now falls in measure 1 (2/4+2/4).
  // The dynamic must move to the correct system bucket.
  const auto updated = f.update_context();
  ASSERT_TRUE(updated);
  // Dynamic glyphs must be present in the layout.
  f.assert_ref_id_in_layout(*updated.layout, dyn.id.to_string());
  const auto fresh = layout_notation(f.fixture.project, f.fixture.node_id,
                                     f.metrics, f.options);
  ASSERT_TRUE(fresh);
  EXPECT_EQ(*updated.layout, *fresh.layout);
  // Work evidence: suffix rebuild means some systems were rebuilt.
  EXPECT_GT(updated.work.rebuilt_systems.size(), 0U);
}

TEST(NotationIncrementalMeasureReassignmentTest,
     SlurEndpointsFollowEventsAcrossSignatureChange) {
  MeasureReassignmentFixture f;
  const auto                 e0 = graphscore::event_id(f.events()[0]);
  const auto e6 = graphscore::event_id(f.events()[6]);  // onset 6/4
  const auto sl = make_slur(e0, e6);
  ASSERT_TRUE(f.voice().add_slur(sl).ok());
  ASSERT_TRUE(f.cache.update(f.fixture.project, f.fixture.node_id, f.metrics,
                             f.options, {}));

  f.change_to_2_2_6_6();
  const auto updated = f.update_context();
  ASSERT_TRUE(updated);
  f.assert_ref_id_in_layout(*updated.layout, sl.id.to_string());
  const auto fresh = layout_notation(f.fixture.project, f.fixture.node_id,
                                     f.metrics, f.options);
  ASSERT_TRUE(fresh);
  EXPECT_EQ(*updated.layout, *fresh.layout);
}

TEST(NotationIncrementalMeasureReassignmentTest,
     HairpinEndpointsFollowEventsAcrossSignatureChange) {
  MeasureReassignmentFixture f;
  const auto                 e0 = graphscore::event_id(f.events()[0]);
  const auto e6 = graphscore::event_id(f.events()[6]);  // onset 6/4
  const auto hp = make_hairpin(e0, e6, HairpinDirection::kCrescendo);
  ASSERT_TRUE(f.voice().add_hairpin(hp).ok());
  ASSERT_TRUE(f.cache.update(f.fixture.project, f.fixture.node_id, f.metrics,
                             f.options, {}));

  f.change_to_2_2_6_6();
  // The hairpin spans from event 0 to event 6.  After signature change,
  // event 0 stays in measure 0 but event 6 moves from measure 1 to measure 2.
  // Both endpoints must be found and the hairpin must be rendered in the
  // correct systems.
  const auto updated = f.update_context();
  ASSERT_TRUE(updated);
  f.assert_ref_id_in_layout(*updated.layout, hp.id.to_string());
  const auto fresh = layout_notation(f.fixture.project, f.fixture.node_id,
                                     f.metrics, f.options);
  ASSERT_TRUE(fresh);
  EXPECT_EQ(*updated.layout, *fresh.layout);
}

TEST(NotationIncrementalMeasureReassignmentTest,
     BeamOverrideMembersFollowEventsAcrossSignatureChange) {
  MeasureReassignmentFixture f;
  const auto                 e0 = graphscore::event_id(f.events()[0]);
  const auto                 e1 = graphscore::event_id(f.events()[1]);
  const auto bo = make_beam_override(BeamOverride::Kind::kJoin, {e0, e1});
  ASSERT_TRUE(f.voice().add_beam_override(bo).ok());
  ASSERT_TRUE(f.cache.update(f.fixture.project, f.fixture.node_id, f.metrics,
                             f.options, {}));

  f.change_to_2_2_6_6();
  const auto updated = f.update_context();
  ASSERT_TRUE(updated);
  const auto fresh = layout_notation(f.fixture.project, f.fixture.node_id,
                                     f.metrics, f.options);
  ASSERT_TRUE(fresh);
  EXPECT_EQ(*updated.layout, *fresh.layout);
}

TEST(NotationIncrementalMeasureReassignmentTest,
     GraceGroupPrincipalFollowsEventAcrossSignatureChange) {
  MeasureReassignmentFixture f;
  const auto                 e3 = graphscore::event_id(f.events()[3]);
  const auto                 gr =
      make_grace_group(e3, {GraceNote{.pitch    = pitch(Letter::kD),
                                      .duration = eighth(),
                                      .type = GraceNoteType::kAppoggiatura}});
  ASSERT_TRUE(f.voice().add_grace_group(gr).ok());
  ASSERT_TRUE(f.cache.update(f.fixture.project, f.fixture.node_id, f.metrics,
                             f.options, {}));

  f.change_to_2_2_6_6();
  const auto updated = f.update_context();
  ASSERT_TRUE(updated);
  const auto fresh = layout_notation(f.fixture.project, f.fixture.node_id,
                                     f.metrics, f.options);
  ASSERT_TRUE(fresh);
  EXPECT_EQ(*updated.layout, *fresh.layout);
}

TEST(NotationIncrementalMeasureReassignmentTest,
     PedalSpanPositionalFollowsNewMeasureBoundaries) {
  MeasureReassignmentFixture f;
  const auto                 stave_id = f.fixture.stave_id();
  const auto                 old_p = make_pedal_span(Rational(0), Rational(1));
  ASSERT_TRUE(f.fixture.project.find_node(f.fixture.node_id)
                  ->lane(f.fixture.track_ids[0])
                  ->add_pedal_span(stave_id, old_p)
                  .ok());
  ASSERT_TRUE(f.cache.update(f.fixture.project, f.fixture.node_id, f.metrics,
                             f.options, {}));

  f.change_to_2_2_6_6();
  const auto updated = f.update_context();
  ASSERT_TRUE(updated);
  const auto fresh = layout_notation(f.fixture.project, f.fixture.node_id,
                                     f.metrics, f.options);
  ASSERT_TRUE(fresh);
  EXPECT_EQ(*updated.layout, *fresh.layout);
}

// ---- Item 6: stale-cursor fallback ----

TEST(NotationIncrementalStaleCursorTest,
     ExceedRingCapacityTriggersFullRebuildThenLocalEngravingRebuild) {
  Fixture fixture({StaffLayout::single_staff()}, 8);
  fixture.append_quarter_notes(0, 0, 1, 32);
  const FixedMetrics          metrics;
  const NotationLayoutOptions options = one_measure_system_options();
  NotationLayoutCache         cache;
  ASSERT_TRUE(
      cache.update(fixture.project, fixture.node_id, metrics, options, {}));

  const auto& events = fixture.voice().events();

  // Perform more than 16 (ring capacity) successful reference mutations
  // before the next cache update. Each add_dynamic pushes a delta onto
  // the ring buffer; after 17 mutations, the earliest cursor falls off.
  for (int i = 0; i < 17; ++i) {
    ASSERT_TRUE(
        fixture.voice()
            .add_dynamic(make_dynamic_marking(
                graphscore::event_id(
                    events[static_cast<std::size_t>(i) % events.size()]),
                static_cast<graphscore::Dynamic>(
                    static_cast<int>(graphscore::Dynamic::kPpp) + (i % 8))))
            .ok());
  }

  // This update sees a stale cursor and must perform a full rebuild.
  const auto full =
      cache.update(fixture.project, fixture.node_id, metrics, options,
                   {{NotationInvalidationKind::kFullReset, 0, 0}});
  ASSERT_TRUE(full);
  EXPECT_TRUE(full.work.reused_systems.empty());
  // Full rebuild visits every event and reference.
  EXPECT_GE(full.work.event_visits, 8U);
  EXPECT_GE(full.work.reference_visits, 1U);

  // Now perform an ordinary delta: add one more dynamic in a local measure.
  const auto evt = graphscore::event_id(events[28]);
  ASSERT_TRUE(
      fixture.voice()
          .add_dynamic(make_dynamic_marking(evt, graphscore::Dynamic::kMf))
          .ok());
  const auto local =
      cache.update(fixture.project, fixture.node_id, metrics, options,
                   {{NotationInvalidationKind::kLocalContent, 7, 7}});
  ASSERT_TRUE(local);
  EXPECT_EQ(local.work.visited_measures, (std::vector<std::size_t>{7}));
  EXPECT_EQ(local.work.rebuilt_systems, (std::vector<std::size_t>{7}));
  EXPECT_EQ(local.work.reused_systems.size(), 7U);

  const auto fresh =
      layout_notation(fixture.project, fixture.node_id, metrics, options);
  ASSERT_TRUE(fresh);
  EXPECT_EQ(*local.layout, *fresh.layout);
}

}  // namespace
