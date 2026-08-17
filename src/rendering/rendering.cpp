// SPDX-License-Identifier: Apache-2.0

#include <graphscore/rendering/graphscore_rendering.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <ranges>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(GRAPHSCORE_HAVE_RENDERING_BACKEND)
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include <hb-ot.h>
#include <thorvg.h>
#endif

namespace graphscore {

struct BravuraFont::Impl {
#if defined(GRAPHSCORE_HAVE_RENDERING_BACKEND)
  std::vector<std::uint8_t> font_data;
  mutable std::mutex        font_mutex;
  FT_Library                library = nullptr;
  FT_Face                   face    = nullptr;
  hb_blob_t*                blob    = nullptr;
  hb_face_t*                hb_face = nullptr;
  hb_font_t*                hb_font = nullptr;
#endif

  ~Impl() {
#if defined(GRAPHSCORE_HAVE_RENDERING_BACKEND)
    if (hb_font != nullptr) {
      hb_font_destroy(hb_font);
    }
    if (hb_face != nullptr) {
      hb_face_destroy(hb_face);
    }
    if (blob != nullptr) {
      hb_blob_destroy(blob);
    }
    if (face != nullptr) {
      FT_Done_Face(face);
    }
    if (library != nullptr) {
      FT_Done_FreeType(library);
    }
#endif
  }
};

BravuraFont::BravuraFont(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}

BravuraFont::BravuraFont(BravuraFont&&) noexcept            = default;
BravuraFont& BravuraFont::operator=(BravuraFont&&) noexcept = default;
BravuraFont::~BravuraFont()                                 = default;

namespace {

[[nodiscard]] RenderingError make_error(
    RenderingErrorCode code, std::string detail,
    std::optional<std::size_t> index = std::nullopt,
    std::optional<NotationId>  id    = std::nullopt) {
  return RenderingError{code, index, std::move(id), std::move(detail)};
}

#if defined(GRAPHSCORE_HAVE_RENDERING_BACKEND)

constexpr char32_t kReplacementCodePoint = U'\uFFFD';
constexpr double   kStaffSpacesPerEm     = 4.0;
constexpr double   kMaximumRasterValue   = 16'777'216.0;

[[nodiscard]] bool finite(double value) noexcept {
  return std::isfinite(value);
}

[[nodiscard]] bool finite_point(NotationPoint point) noexcept {
  return finite(point.x) && finite(point.y);
}

[[nodiscard]] bool finite_rect(const NotationRect& rect) noexcept {
  return finite(rect.x) && finite(rect.y) && finite(rect.width) &&
         finite(rect.height) && rect.width >= 0.0 && rect.height >= 0.0;
}

[[nodiscard]] FT_UInt direct_glyph(const BravuraFont::Impl& impl,
                                   char32_t code_point) noexcept {
  return FT_Get_Char_Index(impl.face, static_cast<FT_ULong>(code_point));
}

struct ResolvedGlyph {
  FT_UInt         glyph      = 0;
  GlyphResolution resolution = GlyphResolution::kNotdef;
};

[[nodiscard]] ResolvedGlyph resolve_glyph(const BravuraFont::Impl& impl,
                                          char32_t code_point) noexcept {
  const FT_UInt direct = direct_glyph(impl, code_point);
  if (direct != 0) {
    return {direct, GlyphResolution::kPresent};
  }
  const FT_UInt replacement = direct_glyph(impl, kReplacementCodePoint);
  if (replacement != 0) {
    return {replacement, GlyphResolution::kReplacementGlyph};
  }
  return {0, GlyphResolution::kNotdef};
}

[[nodiscard]] double font_scale(const BravuraFont::Impl& impl,
                                double                   staff_space) noexcept {
  if (!finite(staff_space) || staff_space <= 0.0 ||
      impl.face->units_per_EM == 0) {
    return 0.0;
  }
  return kStaffSpacesPerEm * staff_space /
         static_cast<double>(impl.face->units_per_EM);
}

[[nodiscard]] double shaped_advance(const BravuraFont::Impl& impl,
                                    const char32_t*          code_points,
                                    std::size_t              count) {
  hb_buffer_t* const buffer = hb_buffer_create();
  if (buffer == nullptr || hb_buffer_allocation_successful(buffer) == 0) {
    if (buffer != nullptr) {
      hb_buffer_destroy(buffer);
    }
    return 0.0;
  }
  std::array<hb_codepoint_t, 2> points{};
  for (std::size_t index = 0; index < count; ++index) {
    points[index] = static_cast<hb_codepoint_t>(code_points[index]);
  }
  hb_buffer_add_codepoints(buffer, points.data(), static_cast<int>(count), 0,
                           static_cast<int>(count));
  hb_buffer_guess_segment_properties(buffer);
  hb_shape(impl.hb_font, buffer, nullptr, 0);
  unsigned int                     count_out = 0;
  const hb_glyph_position_t* const positions =
      hb_buffer_get_glyph_positions(buffer, &count_out);
  double advance = 0.0;
  if (positions != nullptr) {
    for (unsigned int index = 0; index < count_out; ++index) {
      advance += static_cast<double>(positions[index].x_advance);
    }
  }
  hb_buffer_destroy(buffer);
  return advance;
}

[[nodiscard]] bool tvg_success(tvg::Result result) noexcept {
  return result == tvg::Result::Success;
}

void release_shape(tvg::Shape* shape) noexcept {
  if (shape != nullptr) {
    tvg::Paint::rel(shape);
  }
}

struct OutlineContext {
  tvg::Shape*   shape = nullptr;
  double        scale = 0.0;
  NotationPoint origin;
  NotationPoint current;
};

[[nodiscard]] double outline_x(const OutlineContext& context,
                               FT_Pos                value) noexcept {
  return context.origin.x + static_cast<double>(value) * context.scale;
}

[[nodiscard]] double outline_y(const OutlineContext& context,
                               FT_Pos                value) noexcept {
  return context.origin.y - static_cast<double>(value) * context.scale;
}

int outline_move(const FT_Vector* to, void* user) {
  auto& context   = *static_cast<OutlineContext*>(user);
  context.current = {outline_x(context, to->x), outline_y(context, to->y)};
  return tvg_success(
             context.shape->moveTo(static_cast<float>(context.current.x),
                                   static_cast<float>(context.current.y)))
             ? 0
             : 1;
}

int outline_line(const FT_Vector* to, void* user) {
  auto& context   = *static_cast<OutlineContext*>(user);
  context.current = {outline_x(context, to->x), outline_y(context, to->y)};
  return tvg_success(
             context.shape->lineTo(static_cast<float>(context.current.x),
                                   static_cast<float>(context.current.y)))
             ? 0
             : 1;
}

int outline_conic(const FT_Vector* control, const FT_Vector* to, void* user) {
  auto&               context = *static_cast<OutlineContext*>(user);
  const NotationPoint control_point{outline_x(context, control->x),
                                    outline_y(context, control->y)};
  const NotationPoint end{outline_x(context, to->x), outline_y(context, to->y)};
  const NotationPoint control1{
      context.current.x + (2.0 / 3.0) * (control_point.x - context.current.x),
      context.current.y + (2.0 / 3.0) * (control_point.y - context.current.y)};
  const NotationPoint control2{end.x + (2.0 / 3.0) * (control_point.x - end.x),
                               end.y + (2.0 / 3.0) * (control_point.y - end.y)};
  context.current = end;
  return tvg_success(context.shape->cubicTo(
             static_cast<float>(control1.x), static_cast<float>(control1.y),
             static_cast<float>(control2.x), static_cast<float>(control2.y),
             static_cast<float>(end.x), static_cast<float>(end.y)))
             ? 0
             : 1;
}

int outline_cubic(const FT_Vector* control1, const FT_Vector* control2,
                  const FT_Vector* to, void* user) {
  auto& context   = *static_cast<OutlineContext*>(user);
  context.current = {outline_x(context, to->x), outline_y(context, to->y)};
  return tvg_success(context.shape->cubicTo(
             static_cast<float>(outline_x(context, control1->x)),
             static_cast<float>(outline_y(context, control1->y)),
             static_cast<float>(outline_x(context, control2->x)),
             static_cast<float>(outline_y(context, control2->y)),
             static_cast<float>(context.current.x),
             static_cast<float>(context.current.y)))
             ? 0
             : 1;
}

[[nodiscard]] tvg::Matrix raster_matrix(const RasterOptions& options) noexcept {
  const double scale = options.pixels_per_unit;
  return tvg::Matrix{
      static_cast<float>(scale * options.transform.xx),
      static_cast<float>(scale * options.transform.xy),
      static_cast<float>(options.origin.x + scale * options.transform.tx),
      static_cast<float>(scale * options.transform.yx),
      static_cast<float>(scale * options.transform.yy),
      static_cast<float>(options.origin.y + scale * options.transform.ty),
      0.0F,
      0.0F,
      1.0F,
  };
}

[[nodiscard]] bool style_shape(tvg::Shape& shape, const RasterOptions& options,
                               bool fill, double stroke_width) noexcept {
  bool success = true;
  if (fill) {
    success = tvg_success(shape.fill(options.color.red, options.color.green,
                                     options.color.blue, options.color.alpha));
  }
  if (stroke_width > 0.0) {
    success = success &&
              tvg_success(shape.strokeWidth(static_cast<float>(stroke_width)));
    success = success && tvg_success(shape.strokeFill(
                             options.color.red, options.color.green,
                             options.color.blue, options.color.alpha));
  }
  success = success && tvg_success(shape.opacity(options.opacity));
  success = success && tvg_success(shape.transform(raster_matrix(options)));
  return success;
}

[[nodiscard]] NotationRect glyph_bounds(const FT_Outline& outline, double scale,
                                        NotationPoint origin) noexcept {
  FT_BBox bounds{};
  FT_Outline_Get_CBox(&outline, &bounds);
  return NotationRect{origin.x + static_cast<double>(bounds.xMin) * scale,
                      origin.y - static_cast<double>(bounds.yMax) * scale,
                      static_cast<double>(bounds.xMax - bounds.xMin) * scale,
                      static_cast<double>(bounds.yMax - bounds.yMin) * scale};
}

[[nodiscard]] bool glyph_detail_is_below_pixel_resolution(
    const NotationRect& bounds, const RasterOptions& options) noexcept {
  if (bounds.width <= 0.0 || bounds.height <= 0.0) {
    return false;
  }
  const double projected_width =
      options.pixels_per_unit *
      (std::abs(options.transform.xx) * bounds.width +
       std::abs(options.transform.xy) * bounds.height);
  const double projected_height =
      options.pixels_per_unit *
      (std::abs(options.transform.yx) * bounds.width +
       std::abs(options.transform.yy) * bounds.height);
  return finite(projected_width) && finite(projected_height) &&
         projected_width < 1.0 && projected_height < 1.0;
}

[[nodiscard]] tvg::Shape* make_clip(const NotationRect&  clip,
                                    const RasterOptions& options) {
  tvg::Shape* const shape = tvg::Shape::gen();
  if (shape == nullptr ||
      !tvg_success(shape->appendRect(
          static_cast<float>(clip.x), static_cast<float>(clip.y),
          static_cast<float>(clip.width), static_cast<float>(clip.height))) ||
      !tvg_success(shape->transform(raster_matrix(options)))) {
    release_shape(shape);
    return nullptr;
  }
  return shape;
}

[[nodiscard]] NotationRect intersect_rect(const NotationRect& left,
                                          const NotationRect& right) noexcept {
  const double x1 = std::max(left.x, right.x);
  const double y1 = std::max(left.y, right.y);
  const double x2 = std::min(left.x + left.width, right.x + right.width);
  const double y2 = std::min(left.y + left.height, right.y + right.height);
  return {x1, y1, std::max(0.0, x2 - x1), std::max(0.0, y2 - y1)};
}

[[nodiscard]] bool attach_clip(tvg::Shape&                      shape,
                               const std::vector<NotationRect>& clips,
                               const RasterOptions&             options) {
  if (clips.empty()) {
    return true;
  }
  tvg::Shape* clip = make_clip(clips.back(), options);
  if (clip == nullptr) {
    return false;
  }
  if (!tvg_success(shape.clip(clip))) {
    release_shape(clip);
    return false;
  }
  return true;
}

[[nodiscard]] bool append_path(tvg::Shape&        shape,
                               const PathCommand& command) noexcept {
  NotationPoint current;
  bool          have_current = false;
  for (const PathElement& element : command.elements) {
    bool success = false;
    switch (element.verb) {
      case PathVerb::kMove:
        success = tvg_success(shape.moveTo(static_cast<float>(element.end.x),
                                           static_cast<float>(element.end.y)));
        current = element.end;
        have_current = true;
        break;
      case PathVerb::kLine:
        success = have_current &&
                  tvg_success(shape.lineTo(static_cast<float>(element.end.x),
                                           static_cast<float>(element.end.y)));
        current = element.end;
        break;
      case PathVerb::kQuadratic: {
        if (!have_current) {
          return false;
        }
        const NotationPoint control1{
            current.x + (2.0 / 3.0) * (element.control1.x - current.x),
            current.y + (2.0 / 3.0) * (element.control1.y - current.y)};
        const NotationPoint control2{
            element.end.x + (2.0 / 3.0) * (element.control1.x - element.end.x),
            element.end.y + (2.0 / 3.0) * (element.control1.y - element.end.y)};
        success = tvg_success(shape.cubicTo(
            static_cast<float>(control1.x), static_cast<float>(control1.y),
            static_cast<float>(control2.x), static_cast<float>(control2.y),
            static_cast<float>(element.end.x),
            static_cast<float>(element.end.y)));
        current = element.end;
        break;
      }
      case PathVerb::kCubic:
        success = have_current && tvg_success(shape.cubicTo(
                                      static_cast<float>(element.control1.x),
                                      static_cast<float>(element.control1.y),
                                      static_cast<float>(element.control2.x),
                                      static_cast<float>(element.control2.y),
                                      static_cast<float>(element.end.x),
                                      static_cast<float>(element.end.y)));
        current = element.end;
        break;
      case PathVerb::kClose:
        success = have_current && tvg_success(shape.close());
        break;
    }
    if (!success) {
      return false;
    }
  }
  return have_current;
}

[[nodiscard]] bool bounded_point(NotationPoint point) noexcept {
  return finite_point(point) && std::abs(point.x) <= kMaximumRasterValue &&
         std::abs(point.y) <= kMaximumRasterValue;
}

[[nodiscard]] bool valid_command(const NotationCommand& command) {
  return std::visit(
      [](const auto& value) {
        using Command = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<Command, GlyphCommand>) {
          return bounded_point(value.origin) && finite(value.staff_space) &&
                 value.staff_space > 0.0;
        } else if constexpr (std::is_same_v<Command, LineCommand>) {
          return bounded_point(value.from) && bounded_point(value.to) &&
                 finite(value.width) && value.width > 0.0;
        } else if constexpr (std::is_same_v<Command, PathCommand>) {
          if (value.elements.empty() || !finite(value.stroke_width) ||
              value.stroke_width < 0.0 ||
              (!value.filled && value.stroke_width == 0.0)) {
            return false;
          }
          return std::ranges::all_of(value.elements, [](const PathElement& e) {
            return bounded_point(e.control1) && bounded_point(e.control2) &&
                   bounded_point(e.end);
          });
        } else {
          return finite_rect(value.bounds) &&
                 bounded_point({value.bounds.x, value.bounds.y}) &&
                 value.bounds.width <= kMaximumRasterValue &&
                 value.bounds.height <= kMaximumRasterValue;
        }
      },
      command);
}

[[nodiscard]] NotationId command_id(const NotationCommand& command) {
  return std::visit([](const auto& value) { return value.id; }, command);
}

class Sha256 {
 public:
  Sha256() { clear(); }

  void update(const std::uint8_t* data, std::size_t len) {
    for (std::size_t i = 0; i < len; ++i) {
      buffer_[buf_len_++] = data[i];
      if (buf_len_ == 64) {
        transform(buffer_);
        bit_len_ += 512;
        buf_len_ = 0;
      }
    }
  }

  void finalize(std::uint8_t hash[32]) {
    const std::uint64_t total_bits = bit_len_ + buf_len_ * 8;
    buffer_[buf_len_++]            = 0x80;
    if (buf_len_ > 56) {
      while (buf_len_ < 64) {
        buffer_[buf_len_++] = 0;
      }
      transform(buffer_);
      buf_len_ = 0;
    }
    while (buf_len_ < 56) {
      buffer_[buf_len_++] = 0;
    }
    for (int i = 0; i < 8; ++i) {
      buffer_[56 + i] = static_cast<std::uint8_t>(total_bits >> ((7 - i) * 8));
    }
    transform(buffer_);
    for (int word = 0; word < 8; ++word) {
      hash[word * 4 + 0] =
          static_cast<std::uint8_t>((state_[word] >> 24) & 0xFF);
      hash[word * 4 + 1] =
          static_cast<std::uint8_t>((state_[word] >> 16) & 0xFF);
      hash[word * 4 + 2] =
          static_cast<std::uint8_t>((state_[word] >> 8) & 0xFF);
      hash[word * 4 + 3] = static_cast<std::uint8_t>(state_[word] & 0xFF);
    }
  }

 private:
  void clear() {
    state_[0] = 0x6a09e667;
    state_[1] = 0xbb67ae85;
    state_[2] = 0x3c6ef372;
    state_[3] = 0xa54ff53a;
    state_[4] = 0x510e527f;
    state_[5] = 0x9b05688c;
    state_[6] = 0x1f83d9ab;
    state_[7] = 0x5be0cd19;
    bit_len_  = 0;
    buf_len_  = 0;
  }

  static std::uint32_t rotr(std::uint32_t x, std::uint32_t n) {
    return (x >> n) | (x << (32 - n));
  }

  void transform(const std::uint8_t block[64]) {
    std::uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
      const std::ptrdiff_t offset = static_cast<std::ptrdiff_t>(i) * 4;
      w[i] = (static_cast<std::uint32_t>(block[offset]) << 24) |
             (static_cast<std::uint32_t>(block[offset + 1]) << 16) |
             (static_cast<std::uint32_t>(block[offset + 2]) << 8) |
             static_cast<std::uint32_t>(block[offset + 3]);
    }
    for (int i = 16; i < 64; ++i) {
      const std::uint32_t s0 =
          rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const std::uint32_t s1 =
          rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    std::uint32_t a = state_[0];
    std::uint32_t b = state_[1];
    std::uint32_t c = state_[2];
    std::uint32_t d = state_[3];
    std::uint32_t e = state_[4];
    std::uint32_t f = state_[5];
    std::uint32_t g = state_[6];
    std::uint32_t h = state_[7];
    for (int i = 0; i < 64; ++i) {
      const std::uint32_t s1    = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
      const std::uint32_t ch    = (e & f) ^ ((~e) & g);
      const std::uint32_t temp1 = h + s1 + ch + kRound[i] + w[i];
      const std::uint32_t s0    = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
      const std::uint32_t maj   = (a & b) ^ (a & c) ^ (b & c);
      const std::uint32_t temp2 = s0 + maj;
      h                         = g;
      g                         = f;
      f                         = e;
      e                         = d + temp1;
      d                         = c;
      c                         = b;
      b                         = a;
      a                         = temp1 + temp2;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
  }

  std::uint32_t state_[8];
  std::uint64_t bit_len_;
  std::uint8_t  buffer_[64];
  std::size_t   buf_len_;

  static constexpr std::uint32_t kRound[64] = {
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
      0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
      0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
      0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
      0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
      0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
      0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
      0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
      0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
      0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
      0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
};

// ThorVG's SW canvas engine cannot be proven safe for parallel use
// (global Initializer state and per-canvas raster are both serialized).
// Hold a global mutex for the entire lifetime of every raster session so
// that init, shape/canvas work, draw/sync, and term form one uninterrupted
// critical section.  State lives in function-local statics, not file-scope
// globals, to satisfy cppcoreguidelines-avoid-non-const-global-variables.

struct TvgGuard {
  std::mutex mutex;
  bool       initialized = false;
};

[[nodiscard]] TvgGuard& tvg_guard() noexcept {
  static TvgGuard guard;
  return guard;
}

struct TvgSession {
  std::unique_lock<std::mutex> lock;
  bool                         valid = false;

  TvgSession() : lock(tvg_guard().mutex) {
    TvgGuard& guard = tvg_guard();
    if (!guard.initialized) {
      if (!tvg_success(tvg::Initializer::init(0))) {
        return;
      }
      guard.initialized = true;
    }
    valid = true;
  }

  ~TvgSession() {
    if (!valid) {
      return;
    }
    TvgGuard& guard = tvg_guard();
    (void)tvg::Initializer::term();
    guard.initialized = false;
  }

  TvgSession(const TvgSession&)            = delete;
  TvgSession& operator=(const TvgSession&) = delete;
};

#endif

}  // namespace

GlyphMetricsValue BravuraFont::glyph_metrics(char32_t code_point,
                                             double   staff_space) const {
#if defined(GRAPHSCORE_HAVE_RENDERING_BACKEND)
  const std::lock_guard<std::mutex> lock(impl_->font_mutex);
  const double                      scale = font_scale(*impl_, staff_space);
  if (scale == 0.0) {
    return {};
  }
  const ResolvedGlyph resolved = resolve_glyph(*impl_, code_point);
  if (resolved.resolution == GlyphResolution::kNotdef) {
    return GlyphMetricsValue{NotationRect{0.0, -2.0 * staff_space,
                                          1.5 * staff_space, 2.0 * staff_space},
                             1.5 * staff_space};
  }
  if (FT_Load_Glyph(
          impl_->face, resolved.glyph,
          FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP) != 0) {
    return {};
  }
  FT_BBox bounds{};
  if (impl_->face->glyph->format == FT_GLYPH_FORMAT_OUTLINE) {
    FT_Outline_Get_CBox(&impl_->face->glyph->outline, &bounds);
  }
  return GlyphMetricsValue{
      NotationRect{static_cast<double>(bounds.xMin) * scale,
                   -static_cast<double>(bounds.yMax) * scale,
                   static_cast<double>(bounds.xMax - bounds.xMin) * scale,
                   static_cast<double>(bounds.yMax - bounds.yMin) * scale},
      static_cast<double>(impl_->face->glyph->advance.x) * scale};
#else
  (void)code_point;
  (void)staff_space;
  return {};
#endif
}

double BravuraFont::kerning(char32_t left, char32_t right,
                            double staff_space) const {
#if defined(GRAPHSCORE_HAVE_RENDERING_BACKEND)
  const std::lock_guard<std::mutex> lock(impl_->font_mutex);
  const double                      scale = font_scale(*impl_, staff_space);
  if (scale == 0.0) {
    return 0.0;
  }
  const std::array<char32_t, 2> pair{left, right};
  const double pair_advance = shaped_advance(*impl_, pair.data(), pair.size());
  const double singles =
      shaped_advance(*impl_, &pair[0], 1) + shaped_advance(*impl_, &pair[1], 1);
  const double value = (pair_advance - singles) * scale;
  return finite(value) ? value : 0.0;
#else
  (void)left;
  (void)right;
  (void)staff_space;
  return 0.0;
#endif
}

GlyphResolution BravuraFont::glyph_resolution(char32_t code_point) const {
#if defined(GRAPHSCORE_HAVE_RENDERING_BACKEND)
  const std::lock_guard<std::mutex> lock(impl_->font_mutex);
  return resolve_glyph(*impl_, code_point).resolution;
#else
  (void)code_point;
  return GlyphResolution::kNotdef;
#endif
}

BravuraFontLoadResult load_bravura_font(const std::string& bravura_font_path) {
  BravuraFontLoadResult result;
#if defined(GRAPHSCORE_HAVE_RENDERING_BACKEND)
  // Read the entire font file into owned bytes so we can verify its SHA-256
  // before any third-party library opens it, and then construct FreeType and
  // HarfBuzz objects from those verified bytes — eliminating the check/use
  // race between file-open and identity verification.
  std::ifstream file(bravura_font_path, std::ios::binary | std::ios::ate);
  if (!file) {
    result.errors.push_back(make_error(RenderingErrorCode::kFontOpenFailed,
                                       "Bravura font could not be opened"));
    return result;
  }
  const auto file_size = static_cast<std::size_t>(file.tellg());
  if (file_size == 0) {
    result.errors.push_back(
        make_error(RenderingErrorCode::kInvalidFont, "Bravura font is empty"));
    return result;
  }
  file.seekg(0, std::ios::beg);
  std::vector<std::uint8_t> font_data(file_size);
  if (!file.read(reinterpret_cast<char*>(font_data.data()),
                 static_cast<std::streamsize>(file_size))) {
    result.errors.push_back(make_error(RenderingErrorCode::kFontOpenFailed,
                                       "Bravura font could not be read"));
    return result;
  }
  file.close();

  // Verify the pinned Bravura artifact SHA-256 (ADR 0002 §A4). A mismatched or
  // substituted font is rejected with a structured error — no silent fallback.
  {
    constexpr std::uint8_t kExpectedHash[32] = {
        0xdc, 0xa2, 0xd9, 0x0c, 0x88, 0x43, 0x7a, 0x70, 0x1b, 0x1c, 0x2e,
        0x71, 0xfa, 0x54, 0xe7, 0x6f, 0x9f, 0xa4, 0x1d, 0x7d, 0xee, 0xe9,
        0x35, 0xd7, 0x4d, 0xc8, 0x71, 0xea, 0x66, 0xec, 0xfd, 0xd2};
    Sha256       hasher;
    std::uint8_t actual_hash[32]{};
    hasher.update(font_data.data(), font_data.size());
    hasher.finalize(actual_hash);
    bool mismatch = false;
    for (int i = 0; i < 32; ++i) {
      if (actual_hash[i] != kExpectedHash[i]) {
        mismatch = true;
        break;
      }
    }
    if (mismatch) {
      result.errors.push_back(make_error(
          RenderingErrorCode::kInvalidFont,
          "font does not match the pinned Bravura artifact (ADR 0002 §A4); "
          "a different SMuFL font or a modified file cannot be used"));
      return result;
    }
  }

  auto impl       = std::make_unique<BravuraFont::Impl>();
  impl->font_data = std::move(font_data);

  if (FT_Init_FreeType(&impl->library) != 0 || impl->library == nullptr) {
    result.errors.push_back(make_error(RenderingErrorCode::kBackendFailure,
                                       "FreeType initialization failed"));
    return result;
  }
  if (FT_New_Memory_Face(
          impl->library, static_cast<const FT_Byte*>(impl->font_data.data()),
          static_cast<FT_Long>(impl->font_data.size()), 0, &impl->face) != 0 ||
      impl->face == nullptr) {
    result.errors.push_back(make_error(RenderingErrorCode::kFontOpenFailed,
                                       "Bravura font could not be opened"));
    return result;
  }
  if (impl->face->units_per_EM == 0 ||
      FT_Get_Char_Index(impl->face, 0xE050UL) == 0) {
    result.errors.push_back(make_error(
        RenderingErrorCode::kInvalidFont,
        "font fails SMuFL validation (U+E050 missing or zero upem)"));
    return result;
  }
  impl->blob =
      hb_blob_create(reinterpret_cast<const char*>(impl->font_data.data()),
                     static_cast<unsigned int>(impl->font_data.size()),
                     HB_MEMORY_MODE_READONLY, nullptr, nullptr);
  if (impl->blob == nullptr || hb_blob_get_length(impl->blob) == 0) {
    result.errors.push_back(make_error(RenderingErrorCode::kInvalidFont,
                                       "HarfBuzz could not read Bravura"));
    return result;
  }
  impl->hb_face = hb_face_create(impl->blob, 0);
  impl->hb_font = hb_font_create(impl->hb_face);
  if (impl->hb_face == nullptr || impl->hb_font == nullptr) {
    result.errors.push_back(make_error(RenderingErrorCode::kBackendFailure,
                                       "HarfBuzz font creation failed"));
    return result;
  }
  hb_ot_font_set_funcs(impl->hb_font);
  const int upem = static_cast<int>(impl->face->units_per_EM);
  hb_font_set_scale(impl->hb_font, upem, upem);
  hb_codepoint_t hb_glyph = 0;
  if (hb_font_get_nominal_glyph(impl->hb_font, 0xE050U, &hb_glyph) == 0 ||
      hb_glyph != FT_Get_Char_Index(impl->face, 0xE050UL)) {
    result.errors.push_back(
        make_error(RenderingErrorCode::kInvalidFont,
                   "FreeType and HarfBuzz disagree on Bravura's U+E050 glyph"));
    return result;
  }
  result.font = std::unique_ptr<BravuraFont>(new BravuraFont(std::move(impl)));
#else
  (void)bravura_font_path;
  result.errors.push_back(make_error(RenderingErrorCode::kBackendUnavailable,
                                     "writer rendering backend is disabled"));
#endif
  return result;
}

RasterResult rasterize_notation(const std::vector<NotationCommand>& commands,
                                const BravuraFont&                  font,
                                const RasterOptions&                options) {
  RasterResult result;
#if defined(GRAPHSCORE_HAVE_RENDERING_BACKEND)
  if (options.width == 0 || options.height == 0) {
    result.errors.push_back(make_error(RenderingErrorCode::kInvalidSurfaceSize,
                                       "surface dimensions must be nonzero"));
    return result;
  }
  const std::uint64_t pixels = static_cast<std::uint64_t>(options.width) *
                               static_cast<std::uint64_t>(options.height);
  if (options.width > RasterOptions::kMaximumDimension ||
      options.height > RasterOptions::kMaximumDimension ||
      pixels > RasterOptions::kMaximumPixels) {
    result.errors.push_back(make_error(RenderingErrorCode::kSurfaceTooLarge,
                                       "surface exceeds deterministic limits"));
    return result;
  }
  if (!finite(options.pixels_per_unit) || options.pixels_per_unit <= 0.0 ||
      options.pixels_per_unit > kMaximumRasterValue ||
      !finite_point(options.origin) ||
      std::abs(options.origin.x) > kMaximumRasterValue ||
      std::abs(options.origin.y) > kMaximumRasterValue) {
    result.errors.push_back(make_error(RenderingErrorCode::kInvalidScale,
                                       "scale and origin must be finite"));
    return result;
  }
  const std::array<double, 6> transform{
      options.transform.xx, options.transform.xy, options.transform.yx,
      options.transform.yy, options.transform.tx, options.transform.ty};
  if (!std::ranges::all_of(transform, [](double value) {
        return finite(value) && std::abs(value) <= kMaximumRasterValue;
      })) {
    result.errors.push_back(make_error(RenderingErrorCode::kInvalidTransform,
                                       "transform must be finite"));
    return result;
  }
  const std::array<double, 6> matrix_values{
      options.pixels_per_unit * options.transform.xx,
      options.pixels_per_unit * options.transform.xy,
      options.origin.x + options.pixels_per_unit * options.transform.tx,
      options.pixels_per_unit * options.transform.yx,
      options.pixels_per_unit * options.transform.yy,
      options.origin.y + options.pixels_per_unit * options.transform.ty};
  if (!std::ranges::all_of(matrix_values, [](double value) {
        return finite(value) && std::abs(value) <= kMaximumRasterValue;
      })) {
    result.errors.push_back(make_error(RenderingErrorCode::kInvalidTransform,
                                       "composed transform exceeds limits"));
    return result;
  }

  std::size_t clip_depth = 0;
  for (std::size_t index = 0; index < commands.size(); ++index) {
    if (!valid_command(commands[index])) {
      result.errors.push_back(make_error(RenderingErrorCode::kInvalidCommand,
                                         "command geometry is malformed", index,
                                         command_id(commands[index])));
      return result;
    }
    if (const auto* clip = std::get_if<ClipCommand>(&commands[index])) {
      if (clip->begin) {
        ++clip_depth;
      } else if (clip_depth == 0) {
        result.errors.push_back(make_error(RenderingErrorCode::kUnbalancedClip,
                                           "clip end has no matching begin",
                                           index, clip->id));
        return result;
      } else {
        --clip_depth;
      }
    }
  }
  if (clip_depth != 0) {
    result.errors.push_back(make_error(RenderingErrorCode::kUnbalancedClip,
                                       "clip stack is not closed"));
    return result;
  }

  const TvgSession tvg_session;
  if (!tvg_session.valid) {
    result.errors.push_back(make_error(RenderingErrorCode::kBackendFailure,
                                       "ThorVG initialization failed"));
    return result;
  }

  std::vector<std::uint32_t> native_pixels(static_cast<std::size_t>(pixels));
  std::unique_ptr<tvg::SwCanvas> canvas(tvg::SwCanvas::gen());
  if (canvas == nullptr ||
      !tvg_success(canvas->target(native_pixels.data(), options.width,
                                  options.width, options.height,
                                  tvg::ColorSpace::ABGR8888S))) {
    result.errors.push_back(make_error(RenderingErrorCode::kBackendFailure,
                                       "ThorVG surface setup failed"));
    return result;
  }

  std::vector<NotationRect> clips;
  for (std::size_t index = 0; index < commands.size(); ++index) {
    const NotationCommand& retained = commands[index];
    if (const auto* clip = std::get_if<ClipCommand>(&retained)) {
      if (clip->begin) {
        clips.push_back(clips.empty()
                            ? clip->bounds
                            : intersect_rect(clips.back(), clip->bounds));
      } else {
        clips.pop_back();
      }
      continue;
    }

    tvg::Shape* shape   = tvg::Shape::gen();
    bool        success = shape != nullptr;
    if (const auto* glyph = std::get_if<GlyphCommand>(&retained)) {
      ResolvedGlyph resolved;
      bool          ft_ok      = true;
      bool          outline_ok = true;
      bool          simplify   = false;
      NotationRect  exact_bounds;
      const double  scale = font_scale(*font.impl_, glyph->staff_space);
      {
        // Keep glyph resolution, FT_Load_Glyph, format check, and outline
        // decomposition in one uninterrupted per-font critical section.
        // FT_Face::glyph is mutable and shared across same-face operations;
        // releasing the mutex between load and decompose allows another
        // thread to overwrite the glyph slot.
        const std::lock_guard<std::mutex> font_lock(font.impl_->font_mutex);
        resolved = resolve_glyph(*font.impl_, glyph->code_point);
        if (resolved.resolution != GlyphResolution::kNotdef) {
          ft_ok = FT_Load_Glyph(font.impl_->face, resolved.glyph,
                                FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING |
                                    FT_LOAD_NO_BITMAP) == 0 &&
                  font.impl_->face->glyph->format == FT_GLYPH_FORMAT_OUTLINE;
          if (ft_ok) {
            exact_bounds = glyph_bounds(font.impl_->face->glyph->outline, scale,
                                        glyph->origin);
            simplify =
                glyph_detail_is_below_pixel_resolution(exact_bounds, options);
            if (!simplify) {
              OutlineContext         context{shape, scale, glyph->origin, {}};
              const FT_Outline_Funcs callbacks{outline_move,
                                               outline_line,
                                               outline_conic,
                                               outline_cubic,
                                               0,
                                               0};
              outline_ok =
                  FT_Outline_Decompose(&font.impl_->face->glyph->outline,
                                       &callbacks, &context) == 0;
            }
          }
        }
      }
      if (resolved.resolution != GlyphResolution::kPresent) {
        result.errors.push_back(
            make_error(RenderingErrorCode::kMissingGlyph,
                       resolved.resolution == GlyphResolution::kReplacementGlyph
                           ? "missing code point rendered with U+FFFD"
                           : "missing code point rendered with replacement box",
                       index, glyph->id));
      }
      if (resolved.resolution == GlyphResolution::kNotdef) {
        success =
            success &&
            tvg_success(shape->appendRect(
                static_cast<float>(glyph->origin.x),
                static_cast<float>(glyph->origin.y - 2.0 * glyph->staff_space),
                static_cast<float>(1.5 * glyph->staff_space),
                static_cast<float>(2.0 * glyph->staff_space))) &&
            style_shape(*shape, options, false, 0.12 * glyph->staff_space);
      } else {
        success = success && ft_ok && outline_ok;
        if (success) {
          if (simplify) {
            success = tvg_success(
                shape->appendRect(static_cast<float>(exact_bounds.x),
                                  static_cast<float>(exact_bounds.y),
                                  static_cast<float>(exact_bounds.width),
                                  static_cast<float>(exact_bounds.height)));
          }
          success = success && style_shape(*shape, options, true, 0.0);
        }
      }
    } else if (const auto* line = std::get_if<LineCommand>(&retained)) {
      success = success &&
                tvg_success(shape->moveTo(static_cast<float>(line->from.x),
                                          static_cast<float>(line->from.y))) &&
                tvg_success(shape->lineTo(static_cast<float>(line->to.x),
                                          static_cast<float>(line->to.y))) &&
                style_shape(*shape, options, false, line->width);
    } else if (const auto* path = std::get_if<PathCommand>(&retained)) {
      success = success && append_path(*shape, *path) &&
                style_shape(*shape, options, path->filled, path->stroke_width);
    }
    success = success && attach_clip(*shape, clips, options);
    if (!success || !tvg_success(canvas->add(shape))) {
      release_shape(shape);
      result.errors.push_back(make_error(RenderingErrorCode::kBackendFailure,
                                         "ThorVG rejected retained command",
                                         index, command_id(retained)));
      return result;
    }
  }

  if (!tvg_success(canvas->draw(true)) || !tvg_success(canvas->sync())) {
    result.errors.push_back(make_error(RenderingErrorCode::kBackendFailure,
                                       "ThorVG rasterization failed"));
    return result;
  }

  RasterSurface surface;
  surface.width  = options.width;
  surface.height = options.height;
  surface.rgba.resize(static_cast<std::size_t>(pixels) * 4U);
  for (std::size_t index = 0; index < native_pixels.size(); ++index) {
    const std::uint32_t pixel = native_pixels[index];
    surface.rgba[index * 4U]  = static_cast<std::uint8_t>(pixel & 0xFFU);
    surface.rgba[index * 4U + 1U] =
        static_cast<std::uint8_t>((pixel >> 8U) & 0xFFU);
    surface.rgba[index * 4U + 2U] =
        static_cast<std::uint8_t>((pixel >> 16U) & 0xFFU);
    surface.rgba[index * 4U + 3U] =
        static_cast<std::uint8_t>((pixel >> 24U) & 0xFFU);
  }
  result.surface = std::move(surface);
#else
  (void)commands;
  (void)font;
  (void)options;
  result.errors.push_back(make_error(RenderingErrorCode::kBackendUnavailable,
                                     "writer rendering backend is disabled"));
#endif
  return result;
}

bool rendering_backend_available() noexcept {
#if defined(GRAPHSCORE_HAVE_RENDERING_BACKEND)
  return true;
#else
  return false;
#endif
}

}  // namespace graphscore
