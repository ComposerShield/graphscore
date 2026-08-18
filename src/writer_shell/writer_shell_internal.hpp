// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <graphscore/writer_shell/graphscore_writer_shell.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace graphscore {

// Shared implementation state. Platform objects are deliberately type-erased;
// each implementation translation unit casts them only after including the
// platform header.
struct WriterShell::Impl {
  void*       window            = nullptr;
  bool        initialised_video = false;
  std::string backend;
  void*       renderer = nullptr;

  InputHandler* input_handler     = nullptr;
  bool          text_input_active = false;
  KeyModifiers  ordered_modifiers;

  const ViewportTransform* viewport_transform                  = nullptr;
  bool                     test_force_pinch_conversion_failure = false;

  std::vector<NotationRect> highlight_rects;
  std::vector<NotationRect> paste_preview_rects;

  void*         notation_texture = nullptr;
  RasterSurface notation_surface;

  double dpi_scale_x    = 1.0;
  double dpi_scale_y    = 1.0;
  double test_dpi_scale = 0.0;

  bool          test_inject_texture_failure = false;
  bool          test_force_flush_failure    = false;
  std::uint64_t test_present_call_count     = 0;
  std::shared_ptr<WriterShell::TextureStatsHandle::Counters>
      test_texture_counters;

  Impl();
  Impl(const Impl&)            = delete;
  Impl& operator=(const Impl&) = delete;
  Impl(Impl&&)                 = delete;
  Impl& operator=(Impl&&)      = delete;
  ~Impl();
};

[[nodiscard]] const char* renderer_driver_name() noexcept;

[[nodiscard]] ShellResult validate_surface_dimensions(
    const RasterSurface& surface);
[[nodiscard]] std::optional<NotationRect> notation_rect_to_float_rect(
    const NotationRect& rect) noexcept;
[[nodiscard]] std::optional<NotationRect> map_rect_to_viewport(
    const NotationRect& rect, const ViewportTransform* transform) noexcept;

void dispatch_platform_event(InputHandler* handler, void* renderer,
                             KeyModifiers* ordered_modifiers, const void* event,
                             bool force_pinch_conversion_failure = false);
void initialise_platform_modifiers(KeyModifiers* ordered_modifiers);
void deliver_viewport_size(void* window, InputHandler* handler);
[[nodiscard]] ShellResult recompute_render_scale(void* window, void* renderer,
                                                 double& dpi_scale_x,
                                                 double& dpi_scale_y,
                                                 double  test_dpi_scale);
[[nodiscard]] ShellResult upload_notation_surface(
    void* renderer, const RasterSurface& surface, void*& texture,
    bool inject_failure_after_create, std::uint64_t& test_created_count,
    std::uint64_t& test_destroyed_count);

}  // namespace graphscore
