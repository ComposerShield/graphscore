// SPDX-License-Identifier: Apache-2.0

#include <graphscore/notation/graphscore_notation.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace {

class GoldenMetrics final : public graphscore::GlyphMetrics {
 public:
  [[nodiscard]] graphscore::GlyphMetricsValue glyph_metrics(
      char32_t code_point, double staff_space) const override {
    const double width = code_point >= U'0' && code_point <= U'9' ? 0.75 : 1.5;
    return {{-staff_space * 0.25, -staff_space * 0.5, staff_space * width,
             staff_space * 2.0},
            staff_space * width};
  }

  [[nodiscard]] double kerning(char32_t left, char32_t right,
                               double staff_space) const override {
    return left == right ? staff_space * 0.125 : 0.0;
  }
};

class CompactGoldenMetrics final : public graphscore::GlyphMetrics {
 public:
  [[nodiscard]] graphscore::GlyphMetricsValue glyph_metrics(
      char32_t code_point, double staff_space) const override {
    const double width =
        code_point >= U'0' && code_point <= U'9' ? 0.625 : 1.25;
    return {{-staff_space * 0.125, -staff_space * 0.625, staff_space * width,
             staff_space * 1.75},
            staff_space * width};
  }

  [[nodiscard]] double kerning(char32_t left, char32_t right,
                               double staff_space) const override {
    return left != right ? staff_space * 0.0625 : 0.0;
  }
};

class LayoutFingerprint {
 public:
  void add(std::uint64_t value) {
    for (std::size_t shift = 0; shift < 64; shift += 8) {
      add_byte(static_cast<std::uint8_t>(value >> shift));
    }
  }

  void add(double value) {
    // Arm64 and x86-64 can differ by an IEEE-754 ULP for the same expression.
    // Encode geometry at a fixed precision far below any rendered pixel.
    constexpr double kGoldenUnitsPerPixel = 1'000'000'000.0;
    add(static_cast<std::uint64_t>(std::llround(value * kGoldenUnitsPerPixel)));
  }

  void add(bool value) { add_byte(value ? 1U : 0U); }

  template <typename Value>
    requires(std::is_integral_v<Value> && !std::is_same_v<Value, bool>)
  void add(Value value) {
    add(static_cast<std::uint64_t>(value));
  }

  void add(std::string_view value) {
    add(value.size());
    for (const char character : value) {
      add_byte(static_cast<std::uint8_t>(character));
    }
  }

  template <typename Value>
    requires std::is_enum_v<Value>
  void add(Value value) {
    add(static_cast<std::uint64_t>(value));
  }

  [[nodiscard]] std::uint64_t value() const noexcept { return value_; }

 private:
  void add_byte(std::uint8_t byte) {
    value_ ^= byte;
    value_ *= 1099511628211ULL;
  }

  std::uint64_t value_ = 1469598103934665603ULL;
};

void add(LayoutFingerprint& fingerprint, const graphscore::NotationId& id) {
  fingerprint.add(id.value);
}

template <typename Tag>
void add(LayoutFingerprint& fingerprint, const graphscore::StrongId<Tag>& id) {
  fingerprint.add(id.to_string());
}

void add(LayoutFingerprint&               fingerprint,
         const graphscore::NotationPoint& point) {
  fingerprint.add(point.x);
  fingerprint.add(point.y);
}

void add(LayoutFingerprint& fingerprint, const graphscore::NotationRect& rect) {
  fingerprint.add(rect.x);
  fingerprint.add(rect.y);
  fingerprint.add(rect.width);
  fingerprint.add(rect.height);
}

template <typename Value, typename AddValue>
void add_values(LayoutFingerprint&        fingerprint,
                const std::vector<Value>& values, AddValue add_value) {
  fingerprint.add(values.size());
  for (const Value& value : values) {
    add_value(fingerprint, value);
  }
}

void add(LayoutFingerprint&                 fingerprint,
         const graphscore::NotationCommand& command) {
  fingerprint.add(command.index());
  std::visit(
      [&fingerprint](const auto& concrete) {
        add(fingerprint, concrete.id);
        using Command = std::decay_t<decltype(concrete)>;
        if constexpr (std::is_same_v<Command, graphscore::GlyphCommand>) {
          fingerprint.add(static_cast<std::uint64_t>(concrete.code_point));
          add(fingerprint, concrete.origin);
          fingerprint.add(concrete.staff_space);
        } else if constexpr (std::is_same_v<Command, graphscore::LineCommand>) {
          add(fingerprint, concrete.from);
          add(fingerprint, concrete.to);
          fingerprint.add(concrete.width);
        } else if constexpr (std::is_same_v<Command, graphscore::PathCommand>) {
          add_values(fingerprint, concrete.elements,
                     [](LayoutFingerprint&             target,
                        const graphscore::PathElement& element) {
                       target.add(element.verb);
                       add(target, element.control1);
                       add(target, element.control2);
                       add(target, element.end);
                     });
          fingerprint.add(concrete.stroke_width);
          fingerprint.add(concrete.filled);
        } else {
          add(fingerprint, concrete.bounds);
          fingerprint.add(concrete.begin);
        }
      },
      command);
}

void add(LayoutFingerprint& fingerprint, const graphscore::HitRegion& hit) {
  add(fingerprint, hit.id);
  add(fingerprint, hit.semantic_id);
  fingerprint.add(hit.role);
  add(fingerprint, hit.bounds);
  fingerprint.add(static_cast<std::uint64_t>(hit.priority));
  const auto add_owner =
      [&fingerprint](const std::optional<graphscore::NotationId>& id) {
        fingerprint.add(id.has_value());
        if (id.has_value()) {
          add(fingerprint, *id);
        }
      };
  add_owner(hit.owner_system_id);
  add_owner(hit.owner_staff_id);
}

[[nodiscard]] std::uint64_t fingerprint(
    const graphscore::NotationLayout& layout) {
  LayoutFingerprint result;
  add(result, layout.node_id);
  add(result, layout.bounds);
  add_values(
      result, layout.systems,
      [](LayoutFingerprint& target, const graphscore::SystemLayout& system) {
        target.add(system.first_measure);
        add(target, system.id);
        add(target, system.bounds);
        add_values(target, system.measures,
                   [](LayoutFingerprint&               measure_target,
                      const graphscore::MeasureLayout& measure) {
                     measure_target.add(measure.ordinal);
                     add(measure_target, measure.id);
                     add(measure_target, measure.bounds);
                   });
        add_values(target, system.staves,
                   [](LayoutFingerprint&                   staff_target,
                      const graphscore::StaffSystemLayout& staff) {
                     add(staff_target, staff.track_id);
                     add(staff_target, staff.stave_id);
                     add(staff_target, staff.id);
                     add(staff_target, staff.bounds);
                     add_values(staff_target, staff.measure_bounds,
                                [](LayoutFingerprint& bounds_target,
                                   const graphscore::NotationRect& rect) {
                                  add(bounds_target, rect);
                                });
                     add_values(staff_target, staff.voices,
                                [](LayoutFingerprint&             voice_target,
                                   const graphscore::VoiceLayout& voice) {
                                  voice_target.add(voice.voice.index());
                                  add(voice_target, voice.id);
                                  voice_target.add(voice.event_count);
                                });
                   });
      });
  add_values(
      result, layout.commands,
      [](LayoutFingerprint&                 target,
         const graphscore::NotationCommand& command) { add(target, command); });
  add_values(result, layout.hit_regions,
             [](LayoutFingerprint& target, const graphscore::HitRegion& hit) {
               add(target, hit);
             });
  add_values(result, layout.diagnostics,
             [](LayoutFingerprint&                            target,
                const graphscore::NotationLayout::Diagnostic& diagnostic) {
               add(target, diagnostic.entity_id);
               target.add(diagnostic.policy);
             });
  return result.value();
}

void expect_nonnegative(const graphscore::NotationRect& bounds) {
  EXPECT_GE(bounds.width, 0.0);
  EXPECT_GE(bounds.height, 0.0);
}

void expect_contains(const graphscore::NotationRect& outer,
                     const graphscore::NotationRect& inner) {
  EXPECT_GE(inner.x, outer.x);
  EXPECT_GE(inner.y, outer.y);
  EXPECT_LE(inner.x + inner.width, outer.x + outer.width);
  EXPECT_LE(inner.y + inner.height, outer.y + outer.height);
}

void expect_semantic_geometry(const graphscore::NotationLayout& layout) {
  ASSERT_TRUE(layout.geometry_is_finite());
  expect_nonnegative(layout.bounds);
  ASSERT_FALSE(layout.systems.empty());

  std::size_t next_measure  = 0;
  double      next_system_y = layout.bounds.y;
  for (const auto& system : layout.systems) {
    EXPECT_FALSE(system.id.value.empty());
    expect_nonnegative(system.bounds);
    expect_contains(layout.bounds, system.bounds);
    EXPECT_GE(system.bounds.y, next_system_y);
    EXPECT_EQ(system.first_measure, next_measure);
    ASSERT_FALSE(system.measures.empty());

    double next_measure_x = system.bounds.x;
    for (const auto& measure : system.measures) {
      EXPECT_FALSE(measure.id.value.empty());
      EXPECT_EQ(measure.ordinal, next_measure);
      expect_nonnegative(measure.bounds);
      expect_contains(system.bounds, measure.bounds);
      EXPECT_GE(measure.bounds.x, next_measure_x);
      next_measure_x = measure.bounds.x + measure.bounds.width;
      ++next_measure;
    }

    ASSERT_FALSE(system.staves.empty());
    for (const auto& staff : system.staves) {
      EXPECT_FALSE(staff.id.value.empty());
      expect_nonnegative(staff.bounds);
      expect_contains(system.bounds, staff.bounds);
      ASSERT_EQ(staff.measure_bounds.size(), system.measures.size());
      for (std::size_t index = 0; index < staff.measure_bounds.size();
           ++index) {
        const auto& staff_measure = staff.measure_bounds[index];
        const auto& measure       = system.measures[index].bounds;
        expect_nonnegative(staff_measure);
        EXPECT_EQ(staff_measure.x, measure.x);
        EXPECT_EQ(staff_measure.width, measure.width);
        EXPECT_EQ(staff_measure.y, staff.bounds.y);
        EXPECT_EQ(staff_measure.height, staff.bounds.height);
      }
      ASSERT_EQ(staff.voices.size(), graphscore::Voice::kMax);
      for (std::size_t index = 0; index < staff.voices.size(); ++index) {
        EXPECT_FALSE(staff.voices[index].id.value.empty());
        EXPECT_EQ(staff.voices[index].voice.index(), index + 1);
      }
    }
    next_system_y = system.bounds.y + system.bounds.height;
  }

  for (const auto& command : layout.commands) {
    std::visit(
        [](const auto& concrete) {
          EXPECT_FALSE(concrete.id.value.empty());
          using Command = std::decay_t<decltype(concrete)>;
          if constexpr (std::is_same_v<Command, graphscore::GlyphCommand>) {
            EXPECT_GT(concrete.staff_space, 0.0);
          }
          if constexpr (std::is_same_v<Command, graphscore::LineCommand>) {
            EXPECT_GT(concrete.width, 0.0);
          }
          if constexpr (std::is_same_v<Command, graphscore::PathCommand>) {
            EXPECT_FALSE(concrete.elements.empty());
            EXPECT_GE(concrete.stroke_width, 0.0);
          }
          if constexpr (std::is_same_v<Command, graphscore::ClipCommand>) {
            expect_nonnegative(concrete.bounds);
          }
        },
        command);
  }

  for (const auto& hit : layout.hit_regions) {
    EXPECT_FALSE(hit.id.value.empty());
    EXPECT_FALSE(hit.semantic_id.value.empty());
    expect_nonnegative(hit.bounds);
    if (hit.owner_system_id.has_value()) {
      EXPECT_TRUE(std::ranges::any_of(layout.systems, [&](const auto& system) {
        return system.id == *hit.owner_system_id;
      }));
    }
    if (hit.owner_staff_id.has_value()) {
      EXPECT_TRUE(std::ranges::any_of(layout.systems, [&](const auto& system) {
        return std::ranges::any_of(system.staves, [&](const auto& staff) {
          return staff.id == *hit.owner_staff_id;
        });
      }));
    }
  }
}

template <typename Id>
[[nodiscard]] Id fixed_id(std::uint8_t suffix) {
  std::array<std::uint8_t, graphscore::Uuid::kSize> bytes{};
  bytes.back() = suffix;
  return Id{graphscore::Uuid{bytes}};
}

struct GoldenFixture {
  graphscore::Project project;
  graphscore::NodeId  node_id;
};

[[nodiscard]] GoldenFixture make_golden_fixture() {
  const auto treble_id = fixed_id<graphscore::StaveId>(2);
  const auto bass_id   = fixed_id<graphscore::StaveId>(3);
  const auto layout =
      *graphscore::StaffLayout::create({{treble_id, graphscore::Clef::kTreble},
                                        {bass_id, graphscore::Clef::kBass}});
  graphscore::Project project{fixed_id<graphscore::ProjectId>(1), "Golden"};
  const auto          track_id = fixed_id<graphscore::TrackId>(4);
  EXPECT_TRUE(project
                  .add_track_with_id(track_id, "Piano", layout,
                                     *graphscore::MidiChannel::create(0))
                  .ok());
  const auto node_id = fixed_id<graphscore::NodeId>(5);
  EXPECT_TRUE(project.add_node_with_id(node_id, "Fixture").ok());
  project.find_node(node_id)->lane(track_id)->ensure_stave(treble_id);
  project.find_node(node_id)->lane(track_id)->ensure_stave(bass_id);

  const std::vector<graphscore::Measure> measures = {
      {*graphscore::TimeSignature::create(4, 4), graphscore::KeySignature{}},
      {*graphscore::TimeSignature::create(3, 4),
       *graphscore::KeySignature::create(2)},
      {*graphscore::TimeSignature::create(6, 8),
       *graphscore::KeySignature::create(-3)},
  };
  auto timeline = graphscore::NodeTimeline::create(
      measures, project.active_tracks()[0].layout().staves());
  EXPECT_TRUE(timeline.has_value());
  project.find_node(node_id)->set_timeline(std::move(*timeline));

  auto& treble =
      project.find_node(node_id)->lane(track_id)->stave(treble_id)->voice(
          *graphscore::Voice::create(1));
  const auto quarter =
      *graphscore::Duration::create(graphscore::NoteValue::kQuarter, 0);
  for (std::uint8_t index = 0; index < 7; ++index) {
    auto note = graphscore::make_note(
        *graphscore::SpelledPitch::create(graphscore::Letter::kC, 4), quarter);
    note.id = fixed_id<graphscore::NotationEntityId>(
        static_cast<std::uint8_t>(20U + index));
    EXPECT_TRUE(treble.append(note).ok());
  }

  auto& bass =
      project.find_node(node_id)->lane(track_id)->stave(bass_id)->voice(
          *graphscore::Voice::create(2));
  auto rest = graphscore::make_rest(
      *graphscore::Duration::create(graphscore::NoteValue::kWhole, 0));
  rest.id = fixed_id<graphscore::NotationEntityId>(40);
  EXPECT_TRUE(bass.append(rest).ok());
  return {std::move(project), node_id};
}

TEST(NotationGoldenTest, SemanticLayoutMatchesExactPlatformIndependentGolden) {
  const GoldenMetrics               metrics;
  auto                              first_fixture  = make_golden_fixture();
  auto                              second_fixture = make_golden_fixture();
  graphscore::NotationLayoutOptions options;
  options.system_width          = 220.0;
  options.left_margin           = 20.0;
  options.right_margin          = 20.0;
  options.minimum_measure_width = 180.0;
  options.whole_note_spacing    = 120.0;

  const auto first = graphscore::layout_notation(
      first_fixture.project, first_fixture.node_id, metrics, options);
  const auto repeated = graphscore::layout_notation(
      first_fixture.project, first_fixture.node_id, metrics, options);
  const auto reconstructed = graphscore::layout_notation(
      second_fixture.project, second_fixture.node_id, metrics, options);
  ASSERT_TRUE(first);
  ASSERT_TRUE(repeated);
  ASSERT_TRUE(reconstructed);
  EXPECT_EQ(*first.layout, *repeated.layout);
  EXPECT_EQ(*first.layout, *reconstructed.layout);
  EXPECT_TRUE(first.layout->geometry_is_finite());

  // The semantic golden uses a sub-pixel fixed-point encoding. Any visible
  // per-platform antialiasing tolerance belongs only to raster tests.
  EXPECT_EQ(fingerprint(*first.layout), 0xDDE29C713AF47458ULL);
}

TEST(NotationGoldenTest,
     SemanticGeometryMatchesCompactMetricsPlatformIndependentGolden) {
  const CompactGoldenMetrics        metrics;
  auto                              fixture = make_golden_fixture();
  graphscore::NotationLayoutOptions options;
  options.system_width          = 300.0;
  options.left_margin           = 18.0;
  options.right_margin          = 18.0;
  options.top_margin            = 16.0;
  options.bottom_margin         = 16.0;
  options.staff_space           = 8.0;
  options.stave_gap             = 36.0;
  options.system_gap            = 44.0;
  options.minimum_measure_width = 126.0;
  options.whole_note_spacing    = 132.0;

  const auto layout = graphscore::layout_notation(
      fixture.project, fixture.node_id, metrics, options);
  ASSERT_TRUE(layout);
  expect_semantic_geometry(*layout.layout);
  EXPECT_EQ(layout.layout->systems.size(), 3U);
  EXPECT_EQ(fingerprint(*layout.layout), 0xDAB3EEC41F4144E1ULL);
}

}  // namespace
