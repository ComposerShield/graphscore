// SPDX-License-Identifier: Apache-2.0

#include "writer_shell_internal.hpp"

#include <memory>
#include <string>
#include <string_view>

#ifdef GRAPHSCORE_HAVE_SDL3
#include <SDL3/SDL.h>  // NOLINT(build/include_order)
#endif

namespace graphscore {

const char* renderer_driver_name() noexcept {
#if defined(__APPLE__)
  return "metal";
#elif defined(_WIN32)
  return "direct3d11";
#else
  return "opengl";
#endif
}

#ifdef GRAPHSCORE_HAVE_SDL3

WriterShell::Impl::Impl()
    : test_texture_counters(
          std::make_shared<WriterShell::TextureStatsHandle::Counters>()) {}

WriterShell::Impl::~Impl() {
  if (window != nullptr && text_input_active) {
    (void)SDL_StopTextInput(static_cast<SDL_Window*>(window));
    text_input_active = false;
  }
  if (notation_texture != nullptr) {
    SDL_DestroyTexture(static_cast<SDL_Texture*>(notation_texture));
    ++test_texture_counters->destroyed;
    notation_texture = nullptr;
  }
  if (renderer != nullptr) {
    SDL_DestroyRenderer(static_cast<SDL_Renderer*>(renderer));
    renderer = nullptr;
  }
  if (window != nullptr) {
    SDL_DestroyWindow(static_cast<SDL_Window*>(window));
    window = nullptr;
  }
  if (initialised_video) {
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    initialised_video = false;
  }
}

bool WriterShell::backend_compiled_in() {
  return true;
}

ShellResult WriterShell::open_window(const WindowOptions& options) {
  if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
    // No display: a headless CI runner, an SSH session, or a container. The
    // caller decides whether that is fatal.
    return ShellResult{ShellError::kBackendUnavailable, SDL_GetError()};
  }
  impl_->initialised_video = true;

  SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE;
  if (options.high_dpi) {
    flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
  }

  impl_->window =
      SDL_CreateWindow(options.title.c_str(), static_cast<int>(options.width),
                       static_cast<int>(options.height), flags);
  if (impl_->window == nullptr) {
    return ShellResult{ShellError::kWindowCreationFailed, SDL_GetError()};
  }

  const char* const driver = SDL_GetCurrentVideoDriver();
  impl_->backend           = driver != nullptr ? driver : "";

  // ADR 0002 §A5 fixes the GPU presentation backend per platform. Named
  // selection preserves the selected backend's diagnostic and deliberately
  // avoids SDL's software/unnamed fallback path.
  impl_->renderer = SDL_CreateRenderer(static_cast<SDL_Window*>(impl_->window),
                                       renderer_driver_name());
  if (impl_->renderer == nullptr) {
    return ShellResult{ShellError::kRendererUnavailable,
                       std::string("renderer '")
                           .append(renderer_driver_name())
                           .append("' failed: ")
                           .append(SDL_GetError())};
  }

  // A text-consuming focus may have been established before the native
  // window existed. Apply that retained GraphScore-owned state now.
  if (impl_->text_input_active &&
      !SDL_StartTextInput(static_cast<SDL_Window*>(impl_->window))) {
    return ShellResult{
        ShellError::kBackendUnavailable,
        std::string("SDL_StartTextInput failed: ").append(SDL_GetError())};
  }

  ShellResult scale_result =
      recompute_render_scale(impl_->window, impl_->renderer, impl_->dpi_scale_x,
                             impl_->dpi_scale_y, impl_->test_dpi_scale);
  if (!scale_result.ok()) {
    return scale_result;
  }
  deliver_viewport_size(impl_->window, impl_->input_handler);
  initialise_platform_modifiers(&impl_->ordered_modifiers);

  if (!SDL_SetRenderDrawBlendMode(static_cast<SDL_Renderer*>(impl_->renderer),
                                  SDL_BLENDMODE_BLEND)) {
    return ShellResult{ShellError::kRenderingSetupFailed,
                       std::string("SDL_SetRenderDrawBlendMode failed: ")
                           .append(SDL_GetError())};
  }

  ShellResult dimensions = validate_surface_dimensions(impl_->notation_surface);
  if (!dimensions.ok()) {
    return dimensions;
  }
  ShellResult upload_result = upload_notation_surface(
      impl_->renderer, impl_->notation_surface, impl_->notation_texture,
      /*inject_failure_after_create=*/false,
      impl_->test_texture_counters->created,
      impl_->test_texture_counters->destroyed);
  if (!upload_result.ok()) {
    return upload_result;
  }

  auto process_event = [this](SDL_Event& event, bool cancel_quit_and_close,
                              bool refresh_scale) -> ShellResult {
    const bool should_cancel =
        (cancel_quit_and_close &&
         (event.type == SDL_EVENT_QUIT ||
          event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)) ||
        event.type == SDL_EVENT_WINDOW_FOCUS_LOST;
    if (should_cancel && impl_->input_handler != nullptr) {
      impl_->input_handler->on_cancel();
    }

    if (refresh_scale &&
        (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
         event.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED ||
         event.type == SDL_EVENT_WINDOW_RESIZED)) {
      ShellResult refresh = recompute_render_scale(
          impl_->window, impl_->renderer, impl_->dpi_scale_x,
          impl_->dpi_scale_y, impl_->test_dpi_scale);
      if (!refresh.ok()) {
        return refresh;
      }
      deliver_viewport_size(impl_->window, impl_->input_handler);
    }

    dispatch_platform_event(impl_->input_handler, impl_->renderer,
                            &impl_->ordered_modifiers, &event);
    return {};
  };

  if (options.run_event_loop) {
    bool running = true;
    while (running) {
      SDL_Event event;
      if (!SDL_WaitEvent(&event)) {
        break;
      }
      if (event.type == SDL_EVENT_QUIT ||
          event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        running = false;
      }
      ShellResult event_result =
          process_event(event, /*cancel_quit_and_close=*/true,
                        /*refresh_scale=*/true);
      if (!event_result.ok()) {
        return event_result;
      }
      ShellResult frame_result = render_frame(/*present=*/true);
      if (!frame_result.ok()) {
        return frame_result;
      }
    }
  } else {
    // Drain already-queued events so the native window is genuinely realised,
    // then render one frame without entering a blocking loop.
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      ShellResult event_result =
          process_event(event, /*cancel_quit_and_close=*/false,
                        /*refresh_scale=*/false);
      if (!event_result.ok()) {
        return event_result;
      }
    }
    ShellResult frame_result = render_frame(/*present=*/true);
    if (!frame_result.ok()) {
      return frame_result;
    }
  }

  return {};
}

#else

WriterShell::Impl::Impl()  = default;
WriterShell::Impl::~Impl() = default;

bool WriterShell::backend_compiled_in() {
  return false;
}

ShellResult WriterShell::open_window(const WindowOptions& /*options*/) {
  return ShellResult{
      ShellError::kBackendNotCompiledIn,
      "This build was configured with -DGRAPHSCORE_BUILD_WRITER=OFF, so no "
      "windowing backend is available."};
}

#endif

WriterShell::WriterShell() : impl_(std::make_unique<Impl>()) {}

WriterShell::~WriterShell() = default;

WriterShell::WriterShell(WriterShell&&) noexcept            = default;
WriterShell& WriterShell::operator=(WriterShell&&) noexcept = default;

std::string_view WriterShell::backend_name() const {
  return impl_->backend;
}

std::string_view WriterShell::test_renderer_driver_name() {
  return renderer_driver_name();
}

void WriterShell::set_input_handler(InputHandler* handler) {
  // Unregistration severs the only consumer of composed text. Stop platform
  // generation first so no queued/future text can target a stale handler.
  if (handler == nullptr) {
    set_text_input_active(false);
  }
  impl_->input_handler = handler;
}

void WriterShell::set_viewport_transform(const ViewportTransform* transform) {
  impl_->viewport_transform = transform;
}

void WriterShell::set_text_input_active(bool active) {
  if (impl_->text_input_active == active) {
    return;
  }
#ifdef GRAPHSCORE_HAVE_SDL3
  if (impl_->window != nullptr) {
    if (active) {
      if (!SDL_StartTextInput(static_cast<SDL_Window*>(impl_->window))) {
        return;
      }
    } else {
      (void)SDL_StopTextInput(static_cast<SDL_Window*>(impl_->window));
    }
  }
#endif
  impl_->text_input_active = active;
}

bool WriterShell::test_text_input_active() const noexcept {
  return impl_->text_input_active;
}

}  // namespace graphscore
