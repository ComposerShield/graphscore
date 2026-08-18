// SPDX-License-Identifier: Apache-2.0

#include "writer_shell_internal.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#ifdef GRAPHSCORE_HAVE_SDL3
#include <SDL3/SDL.h>  // NOLINT(build/include_order)
#endif

namespace graphscore {

ShellResult validate_surface_dimensions(const RasterSurface& surface) {
  if (surface.width == 0U && surface.height == 0U) {
    if (!surface.rgba.empty()) {
      return ShellResult{ShellError::kRenderingSetupFailed,
                         "notation surface is (0,0) but rgba buffer is not "
                         "empty"};
    }
    return {};
  }

  const std::uint32_t width  = surface.width;
  const std::uint32_t height = surface.height;
  if (width == 0U || height == 0U || width > 16384U || height > 16384U) {
    return ShellResult{ShellError::kRenderingSetupFailed,
                       "notation surface dimensions out of range"};
  }

  constexpr std::uint64_t kMaxDimension = 16384ULL;
  const std::uint64_t     pixel_count =
      static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
  if (pixel_count > kMaxDimension * kMaxDimension) {
    return ShellResult{ShellError::kRenderingSetupFailed,
                       "notation surface pixel count overflow"};
  }
  const std::uint64_t required_bytes = pixel_count * 4ULL;
  if (required_bytes > static_cast<std::uint64_t>(SIZE_MAX)) {
    return ShellResult{ShellError::kRenderingSetupFailed,
                       "notation surface byte count exceeds size_t"};
  }
  if (surface.rgba.size() != static_cast<std::size_t>(required_bytes)) {
    return ShellResult{ShellError::kRenderingSetupFailed,
                       "notation surface rgba buffer size mismatch"};
  }
  return {};
}

std::optional<NotationRect> map_rect_to_viewport(
    const NotationRect& rect, const ViewportTransform* transform) noexcept {
  if (transform == nullptr) {
    if (!std::isfinite(rect.x) || !std::isfinite(rect.y) ||
        !std::isfinite(rect.width) || !std::isfinite(rect.height) ||
        rect.width <= 0.0 || rect.height <= 0.0) {
      return std::nullopt;
    }
    return rect;
  }
  const auto top_left = transform->to_viewport(GraphPosition{rect.x, rect.y});
  const auto bottom_right = transform->to_viewport(
      GraphPosition{rect.x + rect.width, rect.y + rect.height});
  if (!top_left || !bottom_right) {
    return std::nullopt;
  }
  const double width  = bottom_right->x - top_left->x;
  const double height = bottom_right->y - top_left->y;
  if (!std::isfinite(top_left->x) || !std::isfinite(top_left->y) ||
      !std::isfinite(width) || !std::isfinite(height) || width <= 0.0 ||
      height <= 0.0) {
    return std::nullopt;
  }
  return NotationRect{top_left->x, top_left->y, width, height};
}

std::optional<NotationRect> notation_rect_to_float_rect(
    const NotationRect& rect) noexcept {
  const float x      = static_cast<float>(rect.x);
  const float y      = static_cast<float>(rect.y);
  const float width  = static_cast<float>(rect.width);
  const float height = static_cast<float>(rect.height);
  if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) ||
      !std::isfinite(height) || width <= 0.0F || height <= 0.0F) {
    return std::nullopt;
  }
  // SDL draws from float corners. A positive double width can disappear at a
  // large float origin, and a finite width can overflow the far edge.
  const float right  = x + width;
  const float bottom = y + height;
  if (!std::isfinite(right) || !std::isfinite(bottom) || !(right > x) ||
      !(bottom > y)) {
    return std::nullopt;
  }
  return NotationRect{static_cast<double>(x), static_cast<double>(y),
                      static_cast<double>(width), static_cast<double>(height)};
}

#ifdef GRAPHSCORE_HAVE_SDL3

namespace {

[[nodiscard]] std::optional<SDL_FRect> notation_rect_to_sdl_frect(
    const NotationRect& rect) noexcept {
  const auto rounded = notation_rect_to_float_rect(rect);
  if (!rounded.has_value()) {
    return std::nullopt;
  }
  return SDL_FRect{
      static_cast<float>(rounded->x), static_cast<float>(rounded->y),
      static_cast<float>(rounded->width), static_cast<float>(rounded->height)};
}

}  // namespace

ShellResult recompute_render_scale(void* window_handle, void* renderer_handle,
                                   double& dpi_scale_x, double& dpi_scale_y,
                                   double test_dpi_scale) {
  auto* window   = static_cast<SDL_Window*>(window_handle);
  auto* renderer = static_cast<SDL_Renderer*>(renderer_handle);
  int   pixel_w  = 0;
  int   pixel_h  = 0;
  if (!SDL_GetWindowSizeInPixels(window, &pixel_w, &pixel_h)) {
    return ShellResult{ShellError::kRenderingSetupFailed,
                       std::string("SDL_GetWindowSizeInPixels failed: ")
                           .append(SDL_GetError())};
  }
  int logical_w = 0;
  int logical_h = 0;
  if (!SDL_GetWindowSize(window, &logical_w, &logical_h)) {
    return ShellResult{
        ShellError::kRenderingSetupFailed,
        std::string("SDL_GetWindowSize failed: ").append(SDL_GetError())};
  }
  if (logical_w > 0 && logical_h > 0) {
    dpi_scale_x = static_cast<double>(pixel_w) / static_cast<double>(logical_w);
    dpi_scale_y = static_cast<double>(pixel_h) / static_cast<double>(logical_h);
  }

  const double scale_x = test_dpi_scale != 0.0 ? test_dpi_scale : dpi_scale_x;
  const double scale_y = test_dpi_scale != 0.0 ? test_dpi_scale : dpi_scale_y;
  if (!SDL_SetRenderScale(renderer, static_cast<float>(scale_x),
                          static_cast<float>(scale_y))) {
    return ShellResult{
        ShellError::kRenderingSetupFailed,
        std::string("SDL_SetRenderScale failed: ").append(SDL_GetError())};
  }
  return {};
}

ShellResult upload_notation_surface(void*                renderer_handle,
                                    const RasterSurface& surface,
                                    void*&               texture_handle,
                                    bool           inject_failure_after_create,
                                    std::uint64_t& test_created_count,
                                    std::uint64_t& test_destroyed_count) {
  auto* renderer = static_cast<SDL_Renderer*>(renderer_handle);
  auto* texture  = static_cast<SDL_Texture*>(texture_handle);
  if (surface.width == 0U && surface.height == 0U) {
    if (texture != nullptr) {
      SDL_DestroyTexture(texture);
      ++test_destroyed_count;
      texture_handle = nullptr;
    }
    return {};
  }

  const std::uint32_t width       = surface.width;
  const std::uint32_t height      = surface.height;
  SDL_Texture*        new_texture = SDL_CreateTexture(
      renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
      static_cast<int>(width), static_cast<int>(height));
  if (new_texture == nullptr) {
    return ShellResult{
        ShellError::kRenderingSetupFailed,
        std::string("SDL_CreateTexture failed: ").append(SDL_GetError())};
  }
  ++test_created_count;
  if (inject_failure_after_create) {
    SDL_DestroyTexture(new_texture);
    ++test_destroyed_count;
    return ShellResult{ShellError::kRenderingSetupFailed,
                       "test-injected texture upload failure"};
  }
  if (!SDL_SetTextureBlendMode(new_texture, SDL_BLENDMODE_BLEND)) {
    SDL_DestroyTexture(new_texture);
    ++test_destroyed_count;
    return ShellResult{
        ShellError::kRenderingSetupFailed,
        std::string("SDL_SetTextureBlendMode failed: ").append(SDL_GetError())};
  }
  if (!SDL_UpdateTexture(new_texture, /*rect=*/nullptr, surface.rgba.data(),
                         static_cast<int>(width * 4U))) {
    SDL_DestroyTexture(new_texture);
    ++test_destroyed_count;
    return ShellResult{
        ShellError::kRenderingSetupFailed,
        std::string("SDL_UpdateTexture failed: ").append(SDL_GetError())};
  }

  // Transactional replacement: no old state is touched until the candidate
  // texture is configured and uploaded successfully.
  if (texture != nullptr) {
    SDL_DestroyTexture(texture);
    ++test_destroyed_count;
  }
  texture_handle = new_texture;
  return {};
}

ShellResult WriterShell::render_frame(bool present) {
  auto* renderer = static_cast<SDL_Renderer*>(impl_->renderer);
  if (renderer == nullptr) {
    return {};
  }
  const auto fail = [](const char* operation) {
    return ShellResult{
        ShellError::kRenderingSetupFailed,
        std::string(operation).append(" failed: ").append(SDL_GetError())};
  };

  if (!SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255)) {
    return fail("SDL_SetRenderDrawColor (clear color)");
  }
  if (!SDL_RenderClear(renderer)) {
    return fail("SDL_RenderClear");
  }

  auto* notation_texture = static_cast<SDL_Texture*>(impl_->notation_texture);
  if (notation_texture != nullptr) {
    const auto notation_destination = map_rect_to_viewport(
        NotationRect{0.0, 0.0,
                     static_cast<double>(impl_->notation_surface.width),
                     static_cast<double>(impl_->notation_surface.height)},
        impl_->viewport_transform);
    if (!notation_destination.has_value()) {
      return ShellResult{ShellError::kRenderingSetupFailed,
                         "notation destination geometry is not a valid "
                         "viewport rectangle"};
    }
    const auto destination = notation_rect_to_sdl_frect(*notation_destination);
    if (!destination.has_value()) {
      return ShellResult{ShellError::kRenderingSetupFailed,
                         "notation destination geometry is not representable "
                         "as a positive SDL_FRect"};
    }
    // Zoom changes destination geometry only; the complete source texture is
    // always retained so notation elements are never semantically cropped.
    if (!SDL_RenderTexture(renderer, notation_texture, /*srcrect=*/nullptr,
                           &*destination)) {
      return fail("SDL_RenderTexture");
    }
  }

  if (!SDL_SetRenderDrawColor(renderer, 225, 165, 45, 70)) {
    return fail("SDL_SetRenderDrawColor (paste preview color)");
  }
  for (const NotationRect& rect : impl_->paste_preview_rects) {
    const auto mapped = map_rect_to_viewport(rect, impl_->viewport_transform);
    if (!mapped.has_value()) {
      continue;
    }
    const auto destination = notation_rect_to_sdl_frect(*mapped);
    if (destination.has_value() &&
        !SDL_RenderFillRect(renderer, &*destination)) {
      return fail("SDL_RenderFillRect (paste preview)");
    }
  }

  if (!SDL_SetRenderDrawColor(renderer, 80, 130, 210, 90)) {
    return fail("SDL_SetRenderDrawColor (highlight color)");
  }
  for (const NotationRect& rect : impl_->highlight_rects) {
    const auto mapped = map_rect_to_viewport(rect, impl_->viewport_transform);
    if (!mapped.has_value()) {
      continue;
    }
    const auto destination = notation_rect_to_sdl_frect(*mapped);
    if (destination.has_value() &&
        !SDL_RenderFillRect(renderer, &*destination)) {
      return fail("SDL_RenderFillRect");
    }
  }

  if (!present) {
    return {};
  }

  // At the pinned SDL SHA, the observable failure gate is the queued command
  // flush: SDL_RenderPresent itself returns true even for backend failures.
  if (impl_->test_force_flush_failure) {
    impl_->test_force_flush_failure = false;
    return ShellResult{ShellError::kRenderingSetupFailed,
                       "test-injected render flush failure"};
  }
  if (!SDL_FlushRenderer(renderer)) {
    return fail("SDL_FlushRenderer");
  }
  ++impl_->test_present_call_count;
  if (!SDL_RenderPresent(renderer)) {
    return fail("SDL_RenderPresent");
  }
  return {};
}

#else

ShellResult WriterShell::render_frame(bool /*present*/) {
  return ShellResult{ShellError::kBackendNotCompiledIn, "no renderer"};
}

#endif

void WriterShell::set_highlight_rects(std::vector<NotationRect> rects) {
  impl_->highlight_rects = std::move(rects);
}

void WriterShell::set_paste_preview_rects(std::vector<NotationRect> rects) {
  impl_->paste_preview_rects = std::move(rects);
}

ShellResult WriterShell::set_notation_surface(RasterSurface surface) {
  ShellResult dimensions = validate_surface_dimensions(surface);
  if (!dimensions.ok()) {
    return dimensions;
  }
#ifdef GRAPHSCORE_HAVE_SDL3
  if (impl_->renderer != nullptr) {
    const bool inject_failure          = impl_->test_inject_texture_failure;
    impl_->test_inject_texture_failure = false;
    ShellResult upload                 = upload_notation_surface(
        impl_->renderer, surface, impl_->notation_texture, inject_failure,
        impl_->test_texture_counters->created,
        impl_->test_texture_counters->destroyed);
    if (!upload.ok()) {
      return upload;
    }
  }
#endif
  impl_->notation_surface = std::move(surface);
  return {};
}

std::optional<NotationRect> WriterShell::test_notation_destination() const {
  if (impl_->notation_surface.width == 0U &&
      impl_->notation_surface.height == 0U) {
    return std::nullopt;
  }
  return map_rect_to_viewport(
      NotationRect{0.0, 0.0, static_cast<double>(impl_->notation_surface.width),
                   static_cast<double>(impl_->notation_surface.height)},
      impl_->viewport_transform);
}

std::optional<NotationRect> WriterShell::test_float_rect(NotationRect rect) {
  return notation_rect_to_float_rect(rect);
}

void WriterShell::set_test_dpi_scale(double scale) {
  impl_->test_dpi_scale = scale;
#ifdef GRAPHSCORE_HAVE_SDL3
  if (impl_->renderer != nullptr) {
    ShellResult const result = recompute_render_scale(
        impl_->window, impl_->renderer, impl_->dpi_scale_x, impl_->dpi_scale_y,
        impl_->test_dpi_scale);
    (void)result;
  }
#endif
}

double WriterShell::test_dpi_scale_x() const noexcept {
#ifdef GRAPHSCORE_HAVE_SDL3
  return impl_->dpi_scale_x;
#else
  return 1.0;
#endif
}

double WriterShell::test_dpi_scale_y() const noexcept {
#ifdef GRAPHSCORE_HAVE_SDL3
  return impl_->dpi_scale_y;
#else
  return 1.0;
#endif
}

std::vector<NotationRect> WriterShell::test_snapshot_highlight_rects() const {
  return impl_->highlight_rects;
}

std::vector<NotationRect> WriterShell::test_snapshot_paste_preview_rects()
    const {
  return impl_->paste_preview_rects;
}

std::optional<RasterSurface> WriterShell::test_snapshot_notation_surface()
    const {
  return impl_->notation_surface;
}

std::optional<std::array<std::uint8_t, 4>>
WriterShell::test_read_notation_pixel(std::uint32_t x, std::uint32_t y) {
#ifdef GRAPHSCORE_HAVE_SDL3
  auto* renderer = static_cast<SDL_Renderer*>(impl_->renderer);
  auto* texture  = static_cast<SDL_Texture*>(impl_->notation_texture);
  if (texture == nullptr || renderer == nullptr ||
      x >= impl_->notation_surface.width ||
      y >= impl_->notation_surface.height) {
    return std::nullopt;
  }

  SDL_Texture* saved_target = SDL_GetRenderTarget(renderer);
  (void)SDL_SetRenderTarget(renderer, nullptr);
  SDL_BlendMode saved_blend = SDL_BLENDMODE_NONE;
  (void)SDL_GetTextureBlendMode(texture, &saved_blend);
  (void)SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);
  float saved_scale_x = 1.0F;
  float saved_scale_y = 1.0F;
  (void)SDL_GetRenderScale(renderer, &saved_scale_x, &saved_scale_y);
  (void)SDL_SetRenderScale(renderer, 1.0F, 1.0F);

  const auto restore = [&] {
    (void)SDL_SetRenderScale(renderer, saved_scale_x, saved_scale_y);
    (void)SDL_SetTextureBlendMode(texture, saved_blend);
    (void)SDL_SetRenderTarget(renderer, saved_target);
  };
  (void)SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
  (void)SDL_RenderClear(renderer);
  const SDL_FRect destination{
      0.0F, 0.0F, static_cast<float>(impl_->notation_surface.width),
      static_cast<float>(impl_->notation_surface.height)};
  if (!SDL_RenderTexture(renderer, texture, /*srcrect=*/nullptr,
                         &destination)) {
    restore();
    return std::nullopt;
  }

  const SDL_Rect read_rect{static_cast<int>(x), static_cast<int>(y), 1, 1};
  SDL_Surface*   surface = SDL_RenderReadPixels(renderer, &read_rect);
  if (surface == nullptr) {
    restore();
    return std::nullopt;
  }
  std::array<std::uint8_t, 4> pixel{};
  const bool read = SDL_ReadSurfacePixel(surface, 0, 0, &pixel[0], &pixel[1],
                                         &pixel[2], &pixel[3]);
  SDL_DestroySurface(surface);
  restore();
  if (!read) {
    return std::nullopt;
  }
  return pixel;
#else
  (void)x;
  (void)y;
  return std::nullopt;
#endif
}

ShellResult WriterShell::test_render_frame() {
#ifdef GRAPHSCORE_HAVE_SDL3
  if (impl_->renderer == nullptr) {
    return ShellResult{ShellError::kRendererUnavailable, "no renderer"};
  }
  return render_frame(/*present=*/false);
#else
  return ShellResult{ShellError::kBackendNotCompiledIn, "no renderer"};
#endif
}

ShellResult WriterShell::test_present_frame() {
#ifdef GRAPHSCORE_HAVE_SDL3
  if (impl_->renderer == nullptr) {
    return ShellResult{ShellError::kRendererUnavailable, "no renderer"};
  }
  return render_frame(/*present=*/true);
#else
  return ShellResult{ShellError::kBackendNotCompiledIn, "no renderer"};
#endif
}

std::optional<std::array<std::uint8_t, 4>>
WriterShell::test_read_backbuffer_pixel(std::uint32_t x, std::uint32_t y) {
#ifdef GRAPHSCORE_HAVE_SDL3
  auto* renderer = static_cast<SDL_Renderer*>(impl_->renderer);
  if (renderer == nullptr) {
    return std::nullopt;
  }
  SDL_Texture* saved_target = SDL_GetRenderTarget(renderer);
  (void)SDL_SetRenderTarget(renderer, nullptr);
  const SDL_Rect read_rect{static_cast<int>(x), static_cast<int>(y), 1, 1};
  SDL_Surface*   surface = SDL_RenderReadPixels(renderer, &read_rect);
  (void)SDL_SetRenderTarget(renderer, saved_target);
  if (surface == nullptr) {
    return std::nullopt;
  }
  std::array<std::uint8_t, 4> pixel{};
  const bool read = SDL_ReadSurfacePixel(surface, 0, 0, &pixel[0], &pixel[1],
                                         &pixel[2], &pixel[3]);
  SDL_DestroySurface(surface);
  if (!read) {
    return std::nullopt;
  }
  return pixel;
#else
  (void)x;
  (void)y;
  return std::nullopt;
#endif
}

void WriterShell::set_test_force_texture_failure(bool force) {
#ifdef GRAPHSCORE_HAVE_SDL3
  impl_->test_inject_texture_failure = force;
#else
  (void)force;
#endif
}

void WriterShell::set_test_force_render_flush_failure(bool force) {
#ifdef GRAPHSCORE_HAVE_SDL3
  impl_->test_force_flush_failure = force;
#else
  (void)force;
#endif
}

std::uint64_t WriterShell::test_present_call_count() const noexcept {
#ifdef GRAPHSCORE_HAVE_SDL3
  return impl_->test_present_call_count;
#else
  return 0;
#endif
}

WriterShell::NotationTextureStats WriterShell::test_notation_texture_stats()
    const {
  NotationTextureStats stats;
#ifdef GRAPHSCORE_HAVE_SDL3
  if (impl_->test_texture_counters) {
    stats.created   = impl_->test_texture_counters->created;
    stats.destroyed = impl_->test_texture_counters->destroyed;
  }
#endif
  return stats;
}

WriterShell::TextureStatsHandle WriterShell::test_acquire_texture_stats_handle()
    const {
  TextureStatsHandle handle;
#ifdef GRAPHSCORE_HAVE_SDL3
  handle.counters_ = impl_->test_texture_counters;
#endif
  return handle;
}

}  // namespace graphscore
