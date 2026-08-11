// SPDX-License-Identifier: Apache-2.0
//
// The one translation unit in GraphScore permitted to name SDL3 types
// (ADR 0003 §2.2). Everything SDL is confined below; the public header
// exposes only GraphScore-owned types.

#include <graphscore/writer_shell/graphscore_writer_shell.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// The single permitted SDL3 include in the project. Last, so that the
// GraphScore headers above are proven to compile without it.
#ifdef GRAPHSCORE_HAVE_SDL3
#include <SDL3/SDL.h>  // NOLINT(build/include_order)
#endif

namespace graphscore {

// Backend-independent surface-dimension validation.  Compiles in both
// writer-ON and writer-OFF configurations; defined once here so that both
// #ifdef branches and the common code below can call it.
//
// Canonical empty: only (width==0 && height==0 && rgba.empty()) succeeds.
// Any (0,0,non-empty) is rejected; one-zero dimension with non-empty rgba
// is also rejected.  A valid non-empty surface has both dimensions in
// [1, 16384] and rgba.size() exactly equal to width*height*4.
[[nodiscard]] ShellResult validate_surface_dimensions(
    const RasterSurface& surface);

#ifdef GRAPHSCORE_HAVE_SDL3

struct WriterShell::Impl {
  // Platform handle: non-const because SDL's C API takes non-const pointers
  // (cmake/ConstCorrectness.cmake, exception category 4).
  SDL_Window* window            = nullptr;
  bool        initialised_video = false;
  std::string backend;

  // Created once per window. Null when SDL_CreateRenderer fails (a
  // headless machine that passed InitSubSystem but has no GPU-accelerated
  // renderer); the event loop skips rendering in that case.
  SDL_Renderer* renderer = nullptr;

  InputHandler* input_handler = nullptr;

  // Written by set_highlight_rects, consumed by the event loop's render
  // pass. Cleared when the window is closed.
  std::vector<NotationRect> highlight_rects;

  // Rasterised notation surface, uploaded to a GPU texture for composition
  // behind the highlight rects. When the surface width is zero the texture
  // is null and no notation is rendered.
  SDL_Texture*  notation_texture = nullptr;
  RasterSurface notation_surface;

  // DPI scale: pixel_size / logical_size. Event coordinates (in pixel
  // space) are divided by this to produce logical notation coordinates.
  // Rendering is pre-scaled by this value so that notation coordinates
  // land at the correct pixel positions.
  double dpi_scale_x = 1.0;
  double dpi_scale_y = 1.0;

  // Test-only override for the DPI scale. When zero the real DPI scale is
  // used; otherwise this value replaces the real scale.
  double test_dpi_scale = 0.0;

  // Test-only: when true, set_notation_surface forces a failure after
  // validation but before the old texture is touched, proving prior state
  // survives.  The injected error surface is kRenderingSetupFailed.
  bool test_inject_texture_failure = false;

  // Test-only: notation-texture lifetime accounting.  Stored in a
  // shared_ptr so a TextureStatsHandle can observe counter updates
  // from Impl::~Impl after WriterShell destruction.
  std::shared_ptr<WriterShell::TextureStatsHandle::Counters>
      test_texture_counters =
          std::make_shared<WriterShell::TextureStatsHandle::Counters>();

  Impl()                       = default;
  Impl(const Impl&)            = delete;
  Impl& operator=(const Impl&) = delete;
  Impl(Impl&&)                 = delete;
  Impl& operator=(Impl&&)      = delete;

  ~Impl() {
    if (notation_texture != nullptr) {
      SDL_DestroyTexture(notation_texture);
      ++test_texture_counters->destroyed;
      notation_texture = nullptr;
    }
    if (renderer != nullptr) {
      SDL_DestroyRenderer(renderer);
      renderer = nullptr;
    }
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

// Forward-declared: defined after sdl_button_to_pointer_button below.
void dispatch_sdl_event(InputHandler* handler, SDL_Renderer* renderer,
                        SDL_Event event);

// Recompute DPI scale factors from the window's current pixel and logical
// sizes, then reapply SDL_SetRenderScale.  Returns kRenderingSetupFailed
// when the scale update fails; returns kNone on success.
[[nodiscard]] ShellResult recompute_render_scale(SDL_Window*   window,
                                                 SDL_Renderer* renderer,
                                                 double&       dpi_scale_x,
                                                 double&       dpi_scale_y,
                                                 double        test_dpi_scale) {
  int pixel_w = 0;
  int pixel_h = 0;
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

  const double sx = test_dpi_scale != 0.0 ? test_dpi_scale : dpi_scale_x;
  const double sy = test_dpi_scale != 0.0 ? test_dpi_scale : dpi_scale_y;

  if (!SDL_SetRenderScale(renderer, static_cast<float>(sx),  // NOLINT
                          static_cast<float>(sy))) {         // NOLINT
    return ShellResult{
        ShellError::kRenderingSetupFailed,
        std::string("SDL_SetRenderScale failed: ").append(SDL_GetError())};
  }
  return ShellResult{};
}

// Create and upload a GPU texture through `renderer`.  Assumes
// validate_surface_dimensions(surface) already passed (i.e. dimensions and
// buffer size are valid and non-zero).  On success `texture` is set to the
// new texture; on failure returns kRenderingSetupFailed and `texture` is
// left unchanged.
//
// Transactional: the old texture is not destroyed until the new texture
// is fully created, configured, and uploaded.  On any SDL failure the
// old texture survives.
//
// `inject_failure_after_create` is a test-only injection seam: when true,
// the function creates the candidate texture, then destroys it and returns
// kRenderingSetupFailed without touching `texture` — exercising the
// candidate-cleanup path.
//
// `test_created_count` and `test_destroyed_count` are test-only accounting
// references incremented on every SDL_CreateTexture / SDL_DestroyTexture
// call in this function.
[[nodiscard]] ShellResult upload_notation_surface(
    SDL_Renderer* renderer, const RasterSurface& surface, SDL_Texture*& texture,
    bool inject_failure_after_create, std::uint64_t& test_created_count,
    std::uint64_t& test_destroyed_count) {
  if (surface.width == 0U && surface.height == 0U) {
    // Canonical empty: destroy any existing texture and return ok.
    if (texture != nullptr) {
      SDL_DestroyTexture(texture);
      ++test_destroyed_count;
      texture = nullptr;
    }
    return ShellResult{};
  }

  const std::uint32_t w = surface.width;
  const std::uint32_t h = surface.height;

  SDL_Texture* new_texture = SDL_CreateTexture(
      renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC,
      static_cast<int>(w), static_cast<int>(h));
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
                         static_cast<int>(w * 4U))) {
    SDL_DestroyTexture(new_texture);
    ++test_destroyed_count;
    return ShellResult{
        ShellError::kRenderingSetupFailed,
        std::string("SDL_UpdateTexture failed: ").append(SDL_GetError())};
  }

  // All SDL operations succeeded — swap in the new texture.
  if (texture != nullptr) {
    SDL_DestroyTexture(texture);
    ++test_destroyed_count;
  }
  texture = new_texture;
  return ShellResult{};
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

  impl_->window =
      SDL_CreateWindow(options.title.c_str(), static_cast<int>(options.width),
                       static_cast<int>(options.height), flags);

  if (impl_->window == nullptr) {
    return ShellResult{ShellError::kWindowCreationFailed, SDL_GetError()};
  }

  const char* const driver = SDL_GetCurrentVideoDriver();
  impl_->backend           = driver != nullptr ? driver : "";

  // A null renderer on a machine whose InitSubSystem succeeded means the
  // GPU-accelerated backend is unavailable (software-only, headless GPU,
  // driver mismatch). Surface this as an explicit error so the caller can
  // decide whether to proceed with an input-only session.
  impl_->renderer = SDL_CreateRenderer(impl_->window, nullptr);
  if (impl_->renderer == nullptr) {
    return ShellResult{ShellError::kRendererUnavailable, SDL_GetError()};
  }

  // Compute the DPI scale factor and set the render scale. Uses the
  // centralized recompute_and_set_render_scale helper so the event-loop
  // DPI-change path below applies the identical logic.
  ShellResult scale_result =
      recompute_render_scale(impl_->window, impl_->renderer, impl_->dpi_scale_x,
                             impl_->dpi_scale_y, impl_->test_dpi_scale);
  if (!scale_result.ok()) {
    return scale_result;
  }

  // Enable alpha blending so semi-transparent highlight rects and
  // notation-surface alpha compose correctly.
  if (!SDL_SetRenderDrawBlendMode(impl_->renderer, SDL_BLENDMODE_BLEND)) {
    return ShellResult{ShellError::kRenderingSetupFailed,
                       std::string("SDL_SetRenderDrawBlendMode failed: ")
                           .append(SDL_GetError())};
  }

  // Upload the current notation surface (set before open_window()).
  // The surface was validated eagerly in set_notation_surface(), but
  // validate again here as defense against direct impl_ mutation.
  // The rasteriser produces unpremultiplied RGBA bytes [R, G, B, A] in
  // memory. SDL_PIXELFORMAT_RGBA32 is the endian-neutral byte-array alias
  // whose memory bytes are R, G, B, A on every platform. On little-endian
  // (all target platforms) it aliases the same packed format as
  // SDL_PIXELFORMAT_RGBA8888; the RGBA32 name communicates the byte-array
  // contract explicitly.
  {
    ShellResult dims_result =
        validate_surface_dimensions(impl_->notation_surface);
    if (!dims_result.ok()) {
      return dims_result;
    }
    ShellResult upload_result = upload_notation_surface(
        impl_->renderer, impl_->notation_surface, impl_->notation_texture,
        /*inject_failure_after_create=*/false,
        impl_->test_texture_counters->created,
        impl_->test_texture_counters->destroyed);
    if (!upload_result.ok()) {
      return upload_result;
    }
  }

  if (options.run_event_loop) {
    bool running = true;
    while (running) {
      SDL_Event event;
      if (!SDL_WaitEvent(&event)) {
        break;
      }
      const bool should_cancel =
          event.type == SDL_EVENT_QUIT ||
          event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED ||
          event.type == SDL_EVENT_WINDOW_FOCUS_LOST;
      if (should_cancel && impl_->input_handler != nullptr) {
        impl_->input_handler->on_cancel();
      }
      if (event.type == SDL_EVENT_QUIT ||
          event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        running = false;
      }

      // Refresh render scale when the window's pixel size or display scale
      // changes (high-DPI monitor switch, window move between displays,
      // system DPI change).  A refresh failure is a rendering-setup defect
      // and must terminate the event loop.
      if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
          event.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED ||
          event.type == SDL_EVENT_WINDOW_RESIZED) {
        ShellResult scale_refresh = recompute_render_scale(
            impl_->window, impl_->renderer, impl_->dpi_scale_x,
            impl_->dpi_scale_y, impl_->test_dpi_scale);
        if (!scale_refresh.ok()) {
          return scale_refresh;
        }
      }

      dispatch_sdl_event(impl_->input_handler, impl_->renderer, event);

      // Render the frame: notation surface first, then highlight rects
      // on top as a semi-transparent blue overlay.
      if (impl_->renderer != nullptr) {
        SDL_SetRenderDrawColor(impl_->renderer, 30, 30, 30, 255);
        SDL_RenderClear(impl_->renderer);

        // Render the notation surface at its native size.
        if (impl_->notation_texture != nullptr) {
          const SDL_FRect src_rect{
              0.0F, 0.0F, static_cast<float>(impl_->notation_surface.width),
              static_cast<float>(impl_->notation_surface.height)};
          SDL_RenderTexture(impl_->renderer, impl_->notation_texture,
                            /*srcrect=*/nullptr, &src_rect);
        }

        SDL_SetRenderDrawColor(impl_->renderer, 80, 130, 210, 90);
        for (const NotationRect& rect : impl_->highlight_rects) {
          const SDL_FRect dst{
              static_cast<float>(rect.x), static_cast<float>(rect.y),
              static_cast<float>(rect.width), static_cast<float>(rect.height)};
          SDL_RenderFillRect(impl_->renderer, &dst);
        }

        SDL_RenderPresent(impl_->renderer);
      }
    }
  } else {
    // Drain whatever the window manager has already queued, so the window is
    // genuinely realised before returning, then return without blocking.
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      const bool should_cancel = event.type == SDL_EVENT_WINDOW_FOCUS_LOST;
      if (should_cancel && impl_->input_handler != nullptr) {
        impl_->input_handler->on_cancel();
      }
      dispatch_sdl_event(impl_->input_handler, impl_->renderer, event);
    }
    // Render one frame so the window shows content even without an event
    // loop.
    if (impl_->renderer != nullptr) {
      SDL_SetRenderDrawColor(impl_->renderer, 30, 30, 30, 255);
      SDL_RenderClear(impl_->renderer);
      if (impl_->notation_texture != nullptr) {
        SDL_RenderTexture(impl_->renderer, impl_->notation_texture,
                          /*srcrect=*/nullptr, /*dstrect=*/nullptr);
      }
      SDL_RenderPresent(impl_->renderer);
    }
  }

  return ShellResult{};
}

// SDL_BUTTON_LEFT / SDL_BUTTON_MIDDLE / SDL_BUTTON_RIGHT are the
// well-known SDL constants 1/2/3; naming them here would obscure the
// mapping rather than clarifying it.
[[nodiscard]] PointerButton sdl_button_to_pointer_button(
    std::uint8_t sdl_button) noexcept {
  switch (sdl_button) {
    case 1:  // NOLINT(readability-magic-numbers)
      return PointerButton::kPrimary;
    case 3:  // NOLINT(readability-magic-numbers)
      return PointerButton::kSecondary;
    case 2:  // NOLINT(readability-magic-numbers)
      return PointerButton::kMiddle;
    default:
      return PointerButton::kUnknown;
  }
}

void dispatch_sdl_event(InputHandler* handler, SDL_Renderer* renderer,
                        SDL_Event event) {
  if (handler == nullptr) {
    return;
  }

  // Convert pixel-space event coordinates to the renderer's logical
  // coordinate space, accounting for the SDL_SetRenderScale we applied
  // at window creation (and any viewport/logical-presentation state
  // that may have changed since). This is the canonical conversion
  // path; the test-only dispatch_test_pointer_event seam applies the
  // test_dpi_scale itself so the handler always receives logical
  // (notation) coordinates regardless of which path delivered the event.
  //
  // A failed coordinate conversion is a rendering-setup defect — the
  // renderer is in a state where it cannot map pixel to logical space,
  // so no handler callback must be dispatched.
  if (renderer != nullptr) {
    if (!SDL_ConvertEventToRenderCoordinates(renderer, &event)) {
      return;
    }
  }

  switch (event.type) {
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
      PointerEvent pe;
      pe.x      = static_cast<double>(event.button.x);
      pe.y      = static_cast<double>(event.button.y);
      pe.button = sdl_button_to_pointer_button(event.button.button);
      handler->on_pointer_press(pe);
      break;
    }
    case SDL_EVENT_MOUSE_MOTION: {
      PointerEvent pe;
      pe.x = static_cast<double>(event.motion.x);
      pe.y = static_cast<double>(event.motion.y);
      handler->on_pointer_move(pe);
      break;
    }
    case SDL_EVENT_MOUSE_BUTTON_UP: {
      PointerEvent pe;
      pe.x      = static_cast<double>(event.button.x);
      pe.y      = static_cast<double>(event.button.y);
      pe.button = sdl_button_to_pointer_button(event.button.button);
      handler->on_pointer_release(pe);
      break;
    }
    default:
      break;
  }
}

std::optional<std::array<std::uint8_t, 4>>
WriterShell::test_read_notation_pixel(std::uint32_t x, std::uint32_t y) {
  if (impl_->notation_texture == nullptr || impl_->renderer == nullptr) {
    return std::nullopt;
  }
  if (x >= impl_->notation_surface.width ||
      y >= impl_->notation_surface.height) {
    return std::nullopt;
  }

  // Save-and-restore render target and texture blend mode so the test
  // seam does not perturb the production state.
  SDL_Texture* saved_target = SDL_GetRenderTarget(impl_->renderer);
  SDL_SetRenderTarget(impl_->renderer, nullptr);  // back to window

  SDL_BlendMode saved_blend = SDL_BLENDMODE_NONE;
  SDL_GetTextureBlendMode(impl_->notation_texture, &saved_blend);
  SDL_SetTextureBlendMode(impl_->notation_texture, SDL_BLENDMODE_NONE);

  // Clear to black before rendering, so the readback pixel comes only from
  // the texture — no prior frame content bleeds in.
  SDL_SetRenderDrawColor(impl_->renderer, 0, 0, 0, 255);
  SDL_RenderClear(impl_->renderer);

  const SDL_FRect dst_rect{0.0F, 0.0F,
                           static_cast<float>(impl_->notation_surface.width),
                           static_cast<float>(impl_->notation_surface.height)};
  if (!SDL_RenderTexture(impl_->renderer, impl_->notation_texture,
                         /*srcrect=*/nullptr, &dst_rect)) {
    SDL_SetTextureBlendMode(impl_->notation_texture, saved_blend);
    SDL_SetRenderTarget(impl_->renderer, saved_target);
    return std::nullopt;
  }

  std::array<std::uint8_t, 4> pixel{};
  const SDL_Rect read_rect{static_cast<int>(x), static_cast<int>(y), 1, 1};

  SDL_Surface* surface = SDL_RenderReadPixels(impl_->renderer, &read_rect);
  if (surface == nullptr) {
    SDL_SetTextureBlendMode(impl_->notation_texture, saved_blend);
    SDL_SetRenderTarget(impl_->renderer, saved_target);
    return std::nullopt;
  }

  // Read the single pixel's RGBA components from the returned surface.
  if (!SDL_ReadSurfacePixel(surface, 0, 0, &pixel[0], &pixel[1], &pixel[2],
                            &pixel[3])) {
    SDL_DestroySurface(surface);
    SDL_SetTextureBlendMode(impl_->notation_texture, saved_blend);
    SDL_SetRenderTarget(impl_->renderer, saved_target);
    return std::nullopt;
  }

  SDL_DestroySurface(surface);

  SDL_SetTextureBlendMode(impl_->notation_texture, saved_blend);
  SDL_SetRenderTarget(impl_->renderer, saved_target);
  return pixel;
}

#else  // GRAPHSCORE_HAVE_SDL3

// Runtime-only configuration (-DGRAPHSCORE_BUILD_WRITER=OFF). The target
// still builds and still satisfies the architecture audit; it simply has no
// windowing backend.

struct WriterShell::Impl {
  std::string backend;

  InputHandler* input_handler = nullptr;

  // No-op storage: the writer-OFF path never renders, but setters
  // still compile and are safe to call.
  std::vector<NotationRect> highlight_rects;
  RasterSurface             notation_surface;

  // Test-only DPI scale override (no-op in writer-OFF builds).
  double test_dpi_scale = 0.0;

  // Test-only: notation-texture lifetime counters (null in writer-OFF
  // builds — no SDL, no textures).  Stored as shared_ptr for
  // destructor-surviving handle semantics; never allocated.
  std::shared_ptr<WriterShell::TextureStatsHandle::Counters>
      test_texture_counters;
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

std::optional<std::array<std::uint8_t, 4>>
WriterShell::test_read_notation_pixel(std::uint32_t /*x*/,
                                      std::uint32_t /*y*/) {
  return std::nullopt;
}

#endif  // GRAPHSCORE_HAVE_SDL3

// Validate `surface` dimensions and buffer size.  Backend-independent —
// does not require SDL or a renderer, and is compiled in both writer-ON
// and writer-OFF configurations.
//
// Canonical empty: only (width==0 && height==0 && rgba.empty()) succeeds.
// Any (0,0,non-empty) is rejected; one-zero dimension with non-empty rgba
// is also rejected.  A valid non-empty surface has both dimensions in
// [1, 16384] and rgba.size() exactly equal to width*height*4.
[[nodiscard]] ShellResult validate_surface_dimensions(
    const RasterSurface& surface) {
  if (surface.width == 0U && surface.height == 0U) {
    if (!surface.rgba.empty()) {
      return ShellResult{ShellError::kRenderingSetupFailed,
                         "notation surface is (0,0) but rgba buffer is not "
                         "empty"};
    }
    return ShellResult{};  // canonical empty
  }

  const std::uint32_t w = surface.width;
  const std::uint32_t h = surface.height;
  if (w == 0U || h == 0U || w > 16384U || h > 16384U) {
    return ShellResult{ShellError::kRenderingSetupFailed,
                       "notation surface dimensions out of range"};
  }

  // Overflow-safe buffer size check.
  constexpr std::uint64_t kMaxPixels = 16384ULL;
  const std::uint64_t     pixel_count =
      static_cast<std::uint64_t>(w) * static_cast<std::uint64_t>(h);
  if (pixel_count > kMaxPixels * kMaxPixels) {
    return ShellResult{ShellError::kRenderingSetupFailed,
                       "notation surface pixel count overflow"};
  }
  const std::uint64_t required_bytes = pixel_count * 4ULL;
  if (required_bytes > static_cast<std::uint64_t>(SIZE_MAX)) {
    return ShellResult{ShellError::kRenderingSetupFailed,
                       "notation surface byte count exceeds size_t"};
  }
  // Exact match: the caller must supply exactly the buffer the dimensions
  // require, no more and no less.
  if (surface.rgba.size() != static_cast<std::size_t>(required_bytes)) {
    return ShellResult{ShellError::kRenderingSetupFailed,
                       "notation surface rgba buffer size mismatch"};
  }
  return ShellResult{};
}

WriterShell::WriterShell() : impl_(std::make_unique<Impl>()) {}

WriterShell::~WriterShell() = default;

WriterShell::WriterShell(WriterShell&&) noexcept            = default;
WriterShell& WriterShell::operator=(WriterShell&&) noexcept = default;

std::string_view WriterShell::backend_name() const {
  return impl_->backend;
}

void WriterShell::set_input_handler(InputHandler* handler) {
  impl_->input_handler = handler;
}

void WriterShell::set_highlight_rects(std::vector<NotationRect> rects) {
  impl_->highlight_rects = std::move(rects);
}

ShellResult WriterShell::set_notation_surface(RasterSurface surface) {
  // Validate dimensions and buffer size eagerly — no renderer required.
  // This catches one-zero-dim, undersized, and oversized buffers at the
  // point of the call, even before a window is opened.
  ShellResult dims_result = validate_surface_dimensions(surface);
  if (!dims_result.ok()) {
    return dims_result;
  }

#ifdef GRAPHSCORE_HAVE_SDL3
  // When a renderer is available, create and upload a texture.  The
  // function's first parameter is the current impl_->notation_texture: on
  // success it destroys the old texture and replaces it with the new one
  // transactionally; on any failure (including the test injection seam
  // below) the old texture survives.
  if (impl_->renderer != nullptr) {
    const bool inject_failure          = impl_->test_inject_texture_failure;
    impl_->test_inject_texture_failure = false;  // one-shot; never leaks
    ShellResult upload_result          = upload_notation_surface(
        impl_->renderer, surface, impl_->notation_texture, inject_failure,
        impl_->test_texture_counters->created,
        impl_->test_texture_counters->destroyed);
    if (!upload_result.ok()) {
      return upload_result;
    }
    // Success: texture already swapped.  Commit the surface.
    impl_->notation_surface = std::move(surface);
    return ShellResult{};
  }
#endif
  // No renderer: just store the surface.
  impl_->notation_surface = std::move(surface);
  return ShellResult{};
}

void WriterShell::dispatch_test_pointer_event(std::uint8_t kind,
                                              PointerEvent event) {
  InputHandler* handler = impl_->input_handler;
  if (handler == nullptr) {
    return;
  }
  // The headless seam: the event coordinates are already in logical
  // (notation) space.  Apply the test DPI scale divisor so that when
  // test_dpi_scale is set the handler sees the same coordinates it would
  // from the production SDL_ConvertEventToRenderCoordinates path.
  const double scale =
      impl_->test_dpi_scale != 0.0 ? impl_->test_dpi_scale : 1.0;
  if (scale > 0.0) {
    event.x /= scale;
    event.y /= scale;
  }
  switch (kind) {
    case 0:  // press
      handler->on_pointer_press(event);
      break;
    case 1:  // move
      handler->on_pointer_move(event);
      break;
    case 2:  // release
      handler->on_pointer_release(event);
      break;
    case 3:  // cancel
      handler->on_cancel();
      break;
    default:
      break;
  }
}

void WriterShell::dispatch_sdl_test_pointer_event(std::uint8_t kind,
                                                  PointerEvent event) {
#ifdef GRAPHSCORE_HAVE_SDL3
  InputHandler* handler = impl_->input_handler;
  if (handler == nullptr) {
    return;
  }
  // Construct a synthetic SDL event in pixel space and route it through
  // the production dispatch_sdl_event path, which applies
  // SDL_ConvertEventToRenderCoordinates using the current render scale.
  // This exercises the exact SDL conversion that production mouse events
  // undergo, unlike dispatch_test_pointer_event (the headless seam).
  SDL_Event sdl_event{};
  switch (kind) {
    case 0:  // press
      sdl_event.type          = SDL_EVENT_MOUSE_BUTTON_DOWN;
      sdl_event.button.x      = static_cast<float>(event.x);
      sdl_event.button.y      = static_cast<float>(event.y);
      sdl_event.button.button = 1;  // SDL_BUTTON_LEFT
      sdl_event.button.down   = true;
      break;
    case 1:  // move
      sdl_event.type     = SDL_EVENT_MOUSE_MOTION;
      sdl_event.motion.x = static_cast<float>(event.x);
      sdl_event.motion.y = static_cast<float>(event.y);
      break;
    case 2:  // release
      sdl_event.type          = SDL_EVENT_MOUSE_BUTTON_UP;
      sdl_event.button.x      = static_cast<float>(event.x);
      sdl_event.button.y      = static_cast<float>(event.y);
      sdl_event.button.button = 1;  // SDL_BUTTON_LEFT
      sdl_event.button.down   = false;
      break;
    case 3:  // cancel
      handler->on_cancel();
      return;
    default:
      return;
  }
  dispatch_sdl_event(handler, impl_->renderer, sdl_event);
#else
  (void)kind;
  (void)event;
#endif
}

void WriterShell::set_test_dpi_scale(double scale) {
  impl_->test_dpi_scale = scale;
#ifdef GRAPHSCORE_HAVE_SDL3
  // When a renderer exists, push the scale to SDL so the production
  // SDL_ConvertEventToRenderCoordinates path stays consistent with the
  // headless seam.  Without a renderer (or in writer-OFF) only the
  // headless seam is affected.
  if (impl_->renderer != nullptr) {
    ShellResult const result = recompute_render_scale(
        impl_->window, impl_->renderer, impl_->dpi_scale_x, impl_->dpi_scale_y,
        impl_->test_dpi_scale);
    // Scale push failure is a rendering-setup defect, but this is a
    // test-only path and the caller observes the outcome through the
    // handler's coordinate assertions; silently best-effort is acceptable
    // here.
    (void)result;
  }
#endif
}

void WriterShell::set_test_force_texture_failure(bool force) {
#ifdef GRAPHSCORE_HAVE_SDL3
  impl_->test_inject_texture_failure = force;
#else
  (void)force;
#endif
}

WriterShell::NotationTextureStats WriterShell::test_notation_texture_stats()
    const {
  WriterShell::NotationTextureStats stats;
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
  WriterShell::TextureStatsHandle handle;
#ifdef GRAPHSCORE_HAVE_SDL3
  handle.counters_ = impl_->test_texture_counters;
#endif
  return handle;
}

std::vector<NotationRect> WriterShell::test_snapshot_highlight_rects() const {
  return impl_->highlight_rects;
}

}  // namespace graphscore
