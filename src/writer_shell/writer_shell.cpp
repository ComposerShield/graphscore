// SPDX-License-Identifier: Apache-2.0
//
// The one translation unit in GraphScore permitted to name SDL3 types
// (ADR 0003 §2.2). Everything SDL is confined below; the public header
// exposes only GraphScore-owned types.

#include <graphscore/writer_shell/graphscore_writer_shell.hpp>

#include <memory>
#include <string>

// The single permitted SDL3 include in the project. Last, so that the
// GraphScore headers above are proven to compile without it.
#ifdef GRAPHSCORE_HAVE_SDL3
#include <SDL3/SDL.h>  // NOLINT(build/include_order)
#endif

namespace graphscore {

#ifdef GRAPHSCORE_HAVE_SDL3

struct WriterShell::Impl {
  // Platform handle: non-const because SDL's C API takes non-const pointers
  // (cmake/ConstCorrectness.cmake, exception category 4).
  SDL_Window* window            = nullptr;
  bool        initialised_video = false;
  std::string backend;

  Impl()                       = default;
  Impl(const Impl&)            = delete;
  Impl& operator=(const Impl&) = delete;
  Impl(Impl&&)                 = delete;
  Impl& operator=(Impl&&)      = delete;

  ~Impl() {
    if (window != nullptr) {
      SDL_DestroyWindow(window);
      window = nullptr;
    }
    if (initialised_video) {
      SDL_QuitSubSystem(SDL_INIT_VIDEO);
      initialised_video = false;
    }
  }
};

bool WriterShell::backend_compiled_in() {
  return true;
}

ShellResult WriterShell::open_window(const WindowOptions& options) {
  if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
    // No display: a headless CI runner, an SSH session, a container. The
    // caller decides whether that is fatal.
    return ShellResult{ShellError::kBackendUnavailable, SDL_GetError()};
  }
  impl_->initialised_video = true;

  SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE;
  if (options.high_dpi) {
    flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
  }

  impl_->window = SDL_CreateWindow(options.title.c_str(), options.width,
                                   options.height, flags);

  if (impl_->window == nullptr) {
    return ShellResult{ShellError::kWindowCreationFailed, SDL_GetError()};
  }

  const char* const driver = SDL_GetCurrentVideoDriver();
  impl_->backend           = driver != nullptr ? driver : "";

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
    }
  } else {
    // Drain whatever the window manager has already queued, so the window is
    // genuinely realised before returning, then return without blocking.
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      // Discarded: nothing consumes input until M5.
    }
  }

  return ShellResult{};
}

#else  // GRAPHSCORE_HAVE_SDL3

// Runtime-only configuration (-DGRAPHSCORE_BUILD_WRITER=OFF). The target
// still builds and still satisfies the architecture audit; it simply has no
// windowing backend.

struct WriterShell::Impl {
  std::string backend;
};

bool WriterShell::backend_compiled_in() {
  return false;
}

ShellResult WriterShell::open_window(const WindowOptions& /*options*/) {
  return ShellResult{
      ShellError::kBackendNotCompiledIn,
      "This build was configured with -DGRAPHSCORE_BUILD_WRITER=OFF, so no "
      "windowing backend is available."};
}

#endif  // GRAPHSCORE_HAVE_SDL3

WriterShell::WriterShell() : impl_(std::make_unique<Impl>()) {}

WriterShell::~WriterShell() = default;

WriterShell::WriterShell(WriterShell&&) noexcept            = default;
WriterShell& WriterShell::operator=(WriterShell&&) noexcept = default;

std::string_view WriterShell::backend_name() const {
  return impl_->backend;
}

}  // namespace graphscore
