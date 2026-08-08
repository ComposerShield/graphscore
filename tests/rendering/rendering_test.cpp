// SPDX-License-Identifier: Apache-2.0

#include <graphscore/rendering/graphscore_rendering.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>  // NOLINT(build/c++17): GraphScore requires C++23.
#include <fstream>
#include <limits>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using graphscore::BravuraFont;
using graphscore::ClipCommand;
using graphscore::GlyphCommand;
using graphscore::LineCommand;
using graphscore::NotationCommand;
using graphscore::NotationId;
using graphscore::NotationPoint;
using graphscore::NotationRect;
using graphscore::PathCommand;
using graphscore::PathElement;
using graphscore::PathVerb;
using graphscore::RasterOptions;
using graphscore::RasterResult;
using graphscore::RenderingErrorCode;

[[nodiscard]] std::unique_ptr<BravuraFont> load_font() {
  if (!graphscore::rendering_backend_available()) {
    return nullptr;
  }
#if defined(GRAPHSCORE_BRAVURA_FONT_PATH)
  auto loaded = graphscore::load_bravura_font(GRAPHSCORE_BRAVURA_FONT_PATH);
  EXPECT_TRUE(loaded) << (loaded.errors.empty() ? "" : loaded.errors[0].detail);
  return std::move(loaded.font);
#else
  ADD_FAILURE() << "writer backend enabled without configured Bravura path";
  return nullptr;
#endif
}

[[nodiscard]] std::uint8_t alpha_at(const graphscore::RasterSurface& surface,
                                    std::uint32_t x, std::uint32_t y) {
  return surface
      .rgba[(static_cast<std::size_t>(y) * surface.width + x) * 4U + 3U];
}

[[nodiscard]] std::size_t covered_pixels(
    const graphscore::RasterSurface& surface) {
  std::size_t count = 0;
  for (std::size_t index = 3; index < surface.rgba.size(); index += 4) {
    count += surface.rgba[index] != 0 ? 1U : 0U;
  }
  return count;
}

[[nodiscard]] std::uint64_t hash_surface(
    const graphscore::RasterSurface& surface) {
  std::uint64_t hash = 1469598103934665603ULL;
  for (std::uint8_t byte : surface.rgba) {
    hash ^= byte;
    hash *= 1099511628211ULL;
  }
  return hash;
}

[[nodiscard]] PathCommand filled_rect(std::string id, double x, double y,
                                      double width, double height) {
  return PathCommand{NotationId{std::move(id)},
                     {
                         {PathVerb::kMove, {}, {}, {x, y}},
                         {PathVerb::kLine, {}, {}, {x + width, y}},
                         {PathVerb::kLine, {}, {}, {x + width, y + height}},
                         {PathVerb::kLine, {}, {}, {x, y + height}},
                         {PathVerb::kClose, {}, {}, {}},
                     },
                     0.0,
                     true};
}

// Unique temporary directory with RAII cleanup.  The directory is created
// atomically under the platform-standard temp directory using randomised
// candidate names with bounded retry, so concurrent test processes never
// collide.  remove_all in the destructor cleans up the directory and every
// file inside it even when a fatal assertion unwinds the stack — no leaked
// artifact, and the original Bravura file is never modified.
class TempDir {
 public:
  TempDir() : dir_path_(make_unique_dir()) {}

  ~TempDir() {
    if (!dir_path_.empty()) {
      std::error_code ec;
      std::filesystem::remove_all(dir_path_, ec);
    }
  }

  TempDir(const TempDir&)            = delete;
  TempDir& operator=(const TempDir&) = delete;

  [[nodiscard]] const std::filesystem::path& path() const noexcept {
    return dir_path_;
  }

 private:
  static std::filesystem::path make_unique_dir() {
    const auto                base = std::filesystem::temp_directory_path();
    static std::random_device rd;
    static std::mt19937_64    gen(rd());
    static std::uniform_int_distribution<std::uint64_t> dist;
    static std::atomic<std::uint64_t>                   counter{0};

    constexpr int kMaxRetries = 100;
    for (int attempt = 0; attempt < kMaxRetries; ++attempt) {
      auto candidate =
          base /
          ("graphscore_rs_" + std::to_string(dist(gen)) + "_" +
           std::to_string(counter.fetch_add(1, std::memory_order_relaxed)) +
           ".d");
      std::error_code ec;
      if (std::filesystem::create_directory(candidate, ec)) {
        return candidate;
      }
      // Already exists or a transient error — try next candidate.
    }
    throw std::runtime_error(
        "TempDir: could not create unique temporary directory after " +
        std::to_string(kMaxRetries) + " attempts");
  }

  std::filesystem::path dir_path_;
};

TEST(RenderingAvailabilityTest, IsExplicitWithWriterOnOrOff) {
  if (!graphscore::rendering_backend_available()) {
    const auto loaded = graphscore::load_bravura_font("not-used.otf");
    ASSERT_FALSE(loaded);
    ASSERT_EQ(loaded.errors.size(), 1U);
    EXPECT_EQ(loaded.errors[0].code, RenderingErrorCode::kBackendUnavailable);
    return;
  }
  EXPECT_NE(load_font(), nullptr);
}

TEST(BravuraFontTest, LoadsConcreteFiniteStaffSpaceMetricsAndShaping) {
  const auto font = load_font();
  if (font == nullptr) {
    GTEST_SKIP() << "writer rendering backend unavailable";
  }

  const auto gclef = font->glyph_metrics(U'\uE050', 10.0);
  EXPECT_TRUE(std::isfinite(gclef.bounds.x));
  EXPECT_TRUE(std::isfinite(gclef.bounds.y));
  EXPECT_GT(gclef.bounds.width, 0.0);
  EXPECT_GT(gclef.bounds.height, 0.0);
  EXPECT_GT(gclef.advance, 0.0);
  EXPECT_EQ(font->glyph_resolution(U'\uE050'),
            graphscore::GlyphResolution::kPresent);
  EXPECT_NE(font->glyph_resolution(U'\U0010FFFF'),
            graphscore::GlyphResolution::kPresent);
  EXPECT_TRUE(std::isfinite(font->kerning(U'\uE050', U'\uE0A4', 10.0)));

  const auto doubled = font->glyph_metrics(U'\uE050', 20.0);
  EXPECT_DOUBLE_EQ(doubled.advance, gclef.advance * 2.0);
  EXPECT_DOUBLE_EQ(doubled.bounds.height, gclef.bounds.height * 2.0);
}

TEST(BravuraFontTest, RejectsMissingOrNonFontResourceWithoutFallback) {
  if (!graphscore::rendering_backend_available()) {
    GTEST_SKIP() << "writer rendering backend unavailable";
  }
  const auto loaded = graphscore::load_bravura_font("/not/a/font/Bravura.otf");
  ASSERT_FALSE(loaded);
  ASSERT_EQ(loaded.errors.size(), 1U);
  EXPECT_EQ(loaded.errors[0].code, RenderingErrorCode::kFontOpenFailed);
}

TEST(RasterTest, ConsumesEveryCommandVariantWithNestedClipAndOrdering) {
  const auto font = load_font();
  if (font == nullptr) {
    GTEST_SKIP() << "writer rendering backend unavailable";
  }
  const std::vector<NotationCommand> commands = {
      filled_rect("before-clip", 1.0, 1.0, 8.0, 8.0),
      ClipCommand{NotationId{"clip-one"}, {10.0, 0.0, 20.0, 30.0}, true},
      ClipCommand{NotationId{"clip-two"}, {15.0, 0.0, 5.0, 30.0}, true},
      LineCommand{NotationId{"line"}, {0.0, 10.0}, {30.0, 10.0}, 2.0},
      PathCommand{
          NotationId{"path"},
          {{PathVerb::kMove, {}, {}, {10.0, 15.0}},
           {PathVerb::kQuadratic, {15.0, 10.0}, {}, {20.0, 15.0}},
           {PathVerb::kCubic, {20.0, 18.0}, {18.0, 20.0}, {15.0, 20.0}}},
          1.5,
          false},
      GlyphCommand{NotationId{"glyph"}, U'\uE0A4', {17.0, 25.0}, 4.0},
      ClipCommand{NotationId{"clip-two-end"}, {}, false},
      ClipCommand{NotationId{"clip-one-end"}, {}, false},
  };
  RasterOptions options{40, 35};
  options.color   = {20, 100, 220, 200};
  options.opacity = 128;

  const RasterResult raster =
      graphscore::rasterize_notation(commands, *font, options);
  ASSERT_TRUE(raster);
  ASSERT_TRUE(raster.errors.empty());
  EXPECT_GT(alpha_at(*raster.surface, 2, 2), 0U);
  EXPECT_EQ(alpha_at(*raster.surface, 5, 10), 0U);
  EXPECT_GT(alpha_at(*raster.surface, 17, 10), 0U);
  EXPECT_EQ(alpha_at(*raster.surface, 25, 10), 0U);
  EXPECT_GT(covered_pixels(*raster.surface), 60U);

  const auto colored = std::ranges::find_if(
      raster.surface->rgba, [](std::uint8_t value) { return value != 0; });
  EXPECT_NE(colored, raster.surface->rgba.end());
}

TEST(RasterTest, AppliesScaleOriginTransformColorAndOpacity) {
  const auto font = load_font();
  if (font == nullptr) {
    GTEST_SKIP() << "writer rendering backend unavailable";
  }
  RasterOptions options{50, 40};
  options.pixels_per_unit   = 2.0;
  options.origin            = {3.0, 4.0};
  options.transform.tx      = 2.0;
  options.transform.ty      = 1.0;
  options.color             = {200, 40, 10, 255};
  options.opacity           = 96;
  const RasterResult raster = graphscore::rasterize_notation(
      {filled_rect("rect", 1.0, 1.0, 4.0, 3.0)}, *font, options);
  ASSERT_TRUE(raster);
  EXPECT_EQ(alpha_at(*raster.surface, 4, 4), 0U);
  // ThorVG antialiasing is a backend boundary, so channel assertions allow a
  // two-byte tolerance. Semantic geometry is exact in graphscore_notation;
  // fully clipped and clear pixels remain exact here.
  EXPECT_NEAR(alpha_at(*raster.surface, 10, 9), 96U, 2U);
  const std::size_t offset =
      (static_cast<std::size_t>(9) * raster.surface->width + 10U) * 4U;
  EXPECT_NEAR(raster.surface->rgba[offset], 200U, 2U);
  EXPECT_NEAR(raster.surface->rgba[offset + 1U], 40U, 2U);
  EXPECT_NEAR(raster.surface->rgba[offset + 2U], 10U, 2U);
}

TEST(RasterTest, MissingCodePointHasStructuredWarningAndVisibleFallback) {
  const auto font = load_font();
  if (font == nullptr) {
    GTEST_SKIP() << "writer rendering backend unavailable";
  }
  RasterOptions      options{40, 40};
  const RasterResult raster = graphscore::rasterize_notation(
      {GlyphCommand{NotationId{"missing"}, U'\U0010FFFF', {10.0, 25.0}, 8.0}},
      *font, options);
  ASSERT_TRUE(raster);
  ASSERT_EQ(raster.errors.size(), 1U);
  EXPECT_EQ(raster.errors[0].code, RenderingErrorCode::kMissingGlyph);
  EXPECT_EQ(raster.errors[0].command_index, 0U);
  EXPECT_GT(covered_pixels(*raster.surface), 0U);
}

TEST(RasterTest, RepeatedRasterHasIdenticalOwnedBytesAndHash) {
  const auto font = load_font();
  if (font == nullptr) {
    GTEST_SKIP() << "writer rendering backend unavailable";
  }
  const std::vector<NotationCommand> commands = {
      LineCommand{NotationId{"line"}, {2.0, 4.0}, {30.0, 20.0}, 2.0},
      GlyphCommand{NotationId{"clef"}, U'\uE050', {12.0, 30.0}, 5.0},
  };
  const RasterOptions options{48, 40};
  const RasterResult  first =
      graphscore::rasterize_notation(commands, *font, options);
  const RasterResult second =
      graphscore::rasterize_notation(commands, *font, options);
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  EXPECT_EQ(*first.surface, *second.surface);
  EXPECT_EQ(hash_surface(*first.surface), hash_surface(*second.surface));
  EXPECT_NE(hash_surface(*first.surface), 0U);
}

TEST(RasterValidationTest, RejectsInvalidSizeOverflowAndNonFiniteInput) {
  const auto font = load_font();
  if (font == nullptr) {
    GTEST_SKIP() << "writer rendering backend unavailable";
  }
  RasterOptions options{};
  auto          result = graphscore::rasterize_notation({}, *font, options);
  ASSERT_FALSE(result);
  EXPECT_EQ(result.errors[0].code, RenderingErrorCode::kInvalidSurfaceSize);

  options = {RasterOptions::kMaximumDimension,
             RasterOptions::kMaximumDimension};
  result  = graphscore::rasterize_notation({}, *font, options);
  ASSERT_FALSE(result);
  EXPECT_EQ(result.errors[0].code, RenderingErrorCode::kSurfaceTooLarge);

  options                 = {16, 16};
  options.pixels_per_unit = std::numeric_limits<double>::infinity();
  result                  = graphscore::rasterize_notation({}, *font, options);
  ASSERT_FALSE(result);
  EXPECT_EQ(result.errors[0].code, RenderingErrorCode::kInvalidScale);

  options.pixels_per_unit = std::numeric_limits<double>::max();
  result                  = graphscore::rasterize_notation({}, *font, options);
  ASSERT_FALSE(result);
  EXPECT_EQ(result.errors[0].code, RenderingErrorCode::kInvalidScale);

  options              = {16, 16};
  options.transform.xx = std::numeric_limits<double>::quiet_NaN();
  result               = graphscore::rasterize_notation({}, *font, options);
  ASSERT_FALSE(result);
  EXPECT_EQ(result.errors[0].code, RenderingErrorCode::kInvalidTransform);

  options = {16, 16};
  result  = graphscore::rasterize_notation(
      {LineCommand{NotationId{"bad"}, {0.0, 0.0}, {1.0, 1.0}, 0.0}}, *font,
      options);
  ASSERT_FALSE(result);
  EXPECT_EQ(result.errors[0].code, RenderingErrorCode::kInvalidCommand);

  result = graphscore::rasterize_notation(
      {ClipCommand{NotationId{"end"}, {}, false}}, *font, options);
  ASSERT_FALSE(result);
  EXPECT_EQ(result.errors[0].code, RenderingErrorCode::kUnbalancedClip);
}

TEST(RasterIntegrationTest, RendersConcreteToolkitNeutralNotationLayout) {
  const auto font = load_font();
  if (font == nullptr) {
    GTEST_SKIP() << "writer rendering backend unavailable";
  }
  graphscore::Project project{graphscore::ProjectId::generate(), "Raster"};
  const auto          track_id =
      project.add_track("Track", graphscore::StaffLayout::single_staff(),
                        *graphscore::MidiChannel::create(0));
  ASSERT_TRUE(track_id.has_value());
  const auto  node_id = project.add_node("Node");
  auto* const node    = project.find_node(node_id);
  ASSERT_NE(node, nullptr);
  const auto stave = project.active_tracks()[0].layout().staves()[0];
  node->lane(*track_id)->ensure_stave(stave.id);
  const graphscore::Measure measure{*graphscore::TimeSignature::create(4, 4),
                                    graphscore::KeySignature{}};
  auto timeline = graphscore::NodeTimeline::create({measure}, {stave});
  ASSERT_TRUE(timeline.has_value());
  node->set_timeline(std::move(*timeline));
  const auto duration =
      *graphscore::Duration::create(graphscore::NoteValue::kQuarter, 0);
  const auto pitch =
      *graphscore::SpelledPitch::create(graphscore::Letter::kC, 4);
  for (int index = 0; index < 4; ++index) {
    ASSERT_TRUE(node->lane(*track_id)
                    ->stave(stave.id)
                    ->voice(*graphscore::Voice::create(1))
                    .append(graphscore::make_note(pitch, duration))
                    .ok());
  }

  graphscore::NotationLayoutOptions layout_options;
  layout_options.system_width = 300.0;
  layout_options.staff_space  = 6.0;
  const auto layout =
      graphscore::layout_notation(project, node_id, *font, layout_options);
  ASSERT_TRUE(layout);
  ASSERT_FALSE(layout.layout->commands.empty());
  RasterOptions      options{360, 180};
  const RasterResult raster =
      graphscore::rasterize_notation(layout.layout->commands, *font, options);
  ASSERT_TRUE(raster);
  EXPECT_TRUE(raster.errors.empty());
  EXPECT_GT(covered_pixels(*raster.surface), 100U);
}

TEST(BravuraFontTest, ArtifactIdentity) {
  if (!graphscore::rendering_backend_available()) {
    GTEST_SKIP() << "writer rendering backend unavailable";
  }
#if defined(GRAPHSCORE_BRAVURA_FONT_PATH)
  // The pinned Bravura artifact must pass its own SHA-256 identity check.
  auto loaded = graphscore::load_bravura_font(GRAPHSCORE_BRAVURA_FONT_PATH);
  ASSERT_TRUE(loaded) << (loaded.errors.empty() ? "" : loaded.errors[0].detail);
  EXPECT_EQ(loaded.font->glyph_resolution(U'\uE050'),
            graphscore::GlyphResolution::kPresent);
#else
  ADD_FAILURE() << "writer backend enabled without configured Bravura path";
#endif
}

TEST(BravuraFontTest, RejectsSubstitute) {
  if (!graphscore::rendering_backend_available()) {
    GTEST_SKIP() << "writer rendering backend unavailable";
  }
#if defined(GRAPHSCORE_BRAVURA_FONT_PATH)
  // Read the verified font, flip one byte, write a unique temporary copy
  // inside an atomically-created randomised directory under the
  // platform-standard temp directory, and assert that load_bravura_font
  // rejects it.  TempDir RAII ensures cleanup via remove_all even when
  // a fatal assertion unwinds the stack — no fixed path, no leaked artifact,
  // no cross-process collision, and the original Bravura file is never
  // modified.
  std::ifstream src(GRAPHSCORE_BRAVURA_FONT_PATH,
                    std::ios::binary | std::ios::ate);
  ASSERT_TRUE(src) << "cannot read Bravura font for substitute test";
  const auto size = static_cast<std::size_t>(src.tellg());
  ASSERT_GT(size, 0U);
  src.seekg(0, std::ios::beg);
  std::vector<std::uint8_t> modified(size);
  ASSERT_TRUE(src.read(reinterpret_cast<char*>(modified.data()),
                       static_cast<std::streamsize>(size)));
  src.close();

  // Corrupt one byte deep enough to be past any header that FreeType might
  // validate before we reach the hash check — the hash must still fail.
  modified[std::min<std::size_t>(size - 1, 4096)] ^= 0xFF;

  TempDir           tmpdir;
  const std::string temp_path = (tmpdir.path() / "substitute.otf").string();
  {
    std::ofstream dst(temp_path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(dst) << "cannot write substitute font to " << temp_path;
    dst.write(reinterpret_cast<const char*>(modified.data()),
              static_cast<std::streamsize>(modified.size()));
    dst.flush();
    ASSERT_TRUE(dst) << "write/flush of substitute font failed";
    dst.close();
    ASSERT_FALSE(dst.is_open()) << "substitute file did not close cleanly";
  }

  const auto loaded = graphscore::load_bravura_font(temp_path);

  ASSERT_FALSE(loaded);
  ASSERT_FALSE(loaded.errors.empty());
  EXPECT_EQ(loaded.errors[0].code, RenderingErrorCode::kInvalidFont);
  EXPECT_NE(loaded.errors[0].detail.find("pinned Bravura artifact"),
            std::string::npos);
#else
  ADD_FAILURE() << "writer backend enabled without configured Bravura path";
#endif
}

TEST(BravuraFontTest, ConcurrentLayoutMetricsRaster) {
  const auto font = load_font();
  if (font == nullptr) {
    GTEST_SKIP() << "writer rendering backend unavailable";
  }

  constexpr int      kThreads    = 6;
  constexpr int      kIterations = 80;
  constexpr char32_t kGlyphs[]   = {U'\uE050', U'\uE0A4', U'\uE0A0', U'\uE062'};
  std::atomic<int>   errors{0};
  std::atomic<int>   empty_surfaces{0};

  std::vector<std::thread> threads;
  threads.reserve(kThreads);
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&, t]() {
      for (int i = 0; i < kIterations; ++i) {
        const char32_t cp = kGlyphs[(static_cast<std::size_t>(t) +
                                     static_cast<std::size_t>(i)) %
                                    4U];

        // Concurrent glyph_metrics — different code points, same font.
        const auto metrics = font->glyph_metrics(cp, 10.0);
        if (metrics.bounds.width <= 0.0 || !std::isfinite(metrics.advance)) {
          ++errors;
        }

        // Concurrent kerning — different pairs.
        const auto kern = font->kerning(cp, kGlyphs[(i + 1) % 4], 10.0);
        if (!std::isfinite(kern)) {
          ++errors;
        }

        // Concurrent glyph_resolution.
        if (font->glyph_resolution(cp) !=
            graphscore::GlyphResolution::kPresent) {
          ++errors;
        }

        // Concurrent rasterize_notation — different glyphs, varying
        // options.  Surface dimensions are large enough to contain every
        // glyph at every pixel_per_unit so the empty-surface assertion
        // detects real rendering failures, not out-of-bounds geometry.
        graphscore::RasterOptions opts{96U, 96U};
        opts.pixels_per_unit = 1.0 + ((t + i) % 3) * 0.5;
        opts.origin = {static_cast<double>(t % 4), static_cast<double>(i % 4)};
        const auto raster = graphscore::rasterize_notation(
            {graphscore::GlyphCommand{graphscore::NotationId{"g"},
                                      cp,
                                      {12.0 + (t % 3), 20.0 + (i % 5)},
                                      5.0}},
            *font, opts);
        if (!raster || !raster.errors.empty()) {
          ++errors;
        } else if (raster.surface->rgba.empty() ||
                   covered_pixels(*raster.surface) == 0) {
          ++empty_surfaces;
        }
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }
  EXPECT_EQ(errors.load(), 0);
  EXPECT_EQ(empty_surfaces.load(), 0);
}

}  // namespace
