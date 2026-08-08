// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <graphscore/notation/graphscore_notation.hpp>

namespace graphscore {

struct BravuraFontLoadResult;
struct RasterOptions;
struct RasterResult;

enum class RenderingErrorCode : std::uint8_t {
  kBackendUnavailable,
  kFontOpenFailed,
  kInvalidFont,
  kMissingGlyph,
  kInvalidSurfaceSize,
  kSurfaceTooLarge,
  kInvalidScale,
  kInvalidTransform,
  kInvalidCommand,
  kUnbalancedClip,
  kBackendFailure,
};

struct RenderingError {
  RenderingErrorCode         code = RenderingErrorCode::kBackendFailure;
  std::optional<std::size_t> command_index;
  std::optional<NotationId>  command_id;
  std::string                detail;

  [[nodiscard]] bool operator==(const RenderingError&) const = default;
};

enum class GlyphResolution : std::uint8_t {
  kPresent,
  kReplacementGlyph,
  kNotdef,
};

class BravuraFont final : public GlyphMetrics {
 public:
  struct Impl;

  BravuraFont(BravuraFont&&) noexcept;
  BravuraFont& operator=(BravuraFont&&) noexcept;
  ~BravuraFont() override;

  BravuraFont(const BravuraFont&)            = delete;
  BravuraFont& operator=(const BravuraFont&) = delete;

  [[nodiscard]] GlyphMetricsValue glyph_metrics(
      char32_t code_point, double staff_space) const override;
  [[nodiscard]] double          kerning(char32_t left, char32_t right,
                                        double staff_space) const override;
  [[nodiscard]] GlyphResolution glyph_resolution(char32_t code_point) const;

 private:
  explicit BravuraFont(std::unique_ptr<Impl> impl) noexcept;

  std::unique_ptr<Impl> impl_;

  friend struct BravuraFontLoadResult;
  friend BravuraFontLoadResult load_bravura_font(const std::string&);
  friend RasterResult rasterize_notation(const std::vector<NotationCommand>&,
                                         const BravuraFont&,
                                         const RasterOptions&);
};

struct BravuraFontLoadResult {
  std::unique_ptr<BravuraFont> font;
  std::vector<RenderingError>  errors;

  [[nodiscard]] explicit operator bool() const noexcept {
    return font != nullptr;
  }
};

// The path is supplied by writer assembly. Production installs always use
// share/graphscore/fonts/Bravura.otf; rendering never searches system fonts.
[[nodiscard]] BravuraFontLoadResult load_bravura_font(
    const std::string& bravura_font_path);

struct RgbaColor {
  std::uint8_t red   = 0;
  std::uint8_t green = 0;
  std::uint8_t blue  = 0;
  std::uint8_t alpha = 255;

  [[nodiscard]] bool operator==(const RgbaColor&) const = default;
};

struct RenderingTransform {
  double xx = 1.0;
  double xy = 0.0;
  double yx = 0.0;
  double yy = 1.0;
  double tx = 0.0;
  double ty = 0.0;

  [[nodiscard]] bool operator==(const RenderingTransform&) const = default;
};

struct RasterOptions {
  static constexpr std::uint32_t kMaximumDimension = 16'384;
  static constexpr std::uint64_t kMaximumPixels    = 64ULL * 1024ULL * 1024ULL;

  constexpr RasterOptions() = default;

  constexpr RasterOptions(std::uint32_t surface_width,
                          std::uint32_t surface_height) noexcept
      : width(surface_width), height(surface_height) {}

  std::uint32_t      width           = 0;
  std::uint32_t      height          = 0;
  double             pixels_per_unit = 1.0;
  NotationPoint      origin;
  RenderingTransform transform;
  RgbaColor          color;
  std::uint8_t       opacity = 255;
};

// Pixels are tightly packed, row-major, unpremultiplied RGBA8. The transparent
// clear color is {0,0,0,0}. Scale is applied after transform, then origin is
// added in pixels: pixel = origin + pixels_per_unit * transform(point).
struct RasterSurface {
  std::uint32_t             width  = 0;
  std::uint32_t             height = 0;
  std::vector<std::uint8_t> rgba;

  [[nodiscard]] bool operator==(const RasterSurface&) const = default;
};

struct RasterResult {
  std::optional<RasterSurface> surface;
  std::vector<RenderingError>  errors;

  [[nodiscard]] explicit operator bool() const noexcept {
    return surface.has_value();
  }
};

// Rasterizes in retained-list order through ThorVG's CPU backend. Clip
// begin/end commands form a deterministic stack; each draw receives the
// intersection of all active rectangular clips. Missing glyphs use U+FFFD when
// Bravura maps it; otherwise a deterministic visible replacement box is used.
// Both paths append kMissingGlyph and never search for another font.
[[nodiscard]] RasterResult rasterize_notation(
    const std::vector<NotationCommand>& commands, const BravuraFont& font,
    const RasterOptions& options);

[[nodiscard]] bool rendering_backend_available() noexcept;

}  // namespace graphscore
