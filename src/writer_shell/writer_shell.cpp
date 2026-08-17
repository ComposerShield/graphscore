// SPDX-License-Identifier: Apache-2.0
//
// The one translation unit in GraphScore permitted to name SDL3 types
// (ADR 0003 §2.2). Everything SDL is confined below; the public header
// exposes only GraphScore-owned types.

#include <graphscore/writer_shell/graphscore_writer_shell.hpp>

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
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

// Converts a notation destination rect (doubles) to the float rectangle the
// render pass submits, rejecting it when the float corners are not finite and
// strictly advancing. SDL draws from an SDL_FRect, so `float(x)` and
// `float(x) + float(width)` must remain distinct and finite: a width absorbed
// into the float origin (right edge == left edge) or a right/bottom edge that
// overflows to infinity would make SDL draw a degenerate or clipped rectangle.
// map_rect_to_viewport's double-width check is not sufficient — a double rect
// can be strictly positive while its float corners collapse. Returns the
// float-rounded rect (as doubles) so the check is SDL-free; the SDL-only
// notation_rect_to_sdl_frect wrapper re-casts it to SDL_FRect.
[[nodiscard]] std::optional<NotationRect> notation_rect_to_float_rect(
    const NotationRect& rect) noexcept;

// Maps a world-space rectangle through the viewport transform into logical
// viewport coordinates. A null transform (identity) returns the rect
// unchanged. Returns nullopt when the transform cannot map the corners
// (non-finite), or when the resulting rectangle is not a drawable destination
// — any coordinate or dimension non-finite, or a width/height that is not
// strictly positive (collapsed). The render pass must then surface the defect
// rather than silently draw nothing.
[[nodiscard]] std::optional<NotationRect> map_rect_to_viewport(
    const NotationRect& rect, const ViewportTransform* transform) noexcept;

// ADR 0002 §A5 fixes the writer's presentation backend per platform: Metal
// on macOS, D3D11 on Windows, OpenGL on Linux. These are the SDL render
// driver names (the `name` field each driver registers in SDL_render.c):
// "metal" (SDL_render_metal.m), "direct3d11" (SDL_render_d3d11.c), "opengl"
// (SDL_render_gl.c). The shell selects this driver by name rather than using
// SDL's unnamed auto-selection, which silently tries every compiled driver,
// discards each backend's specific error, and reports only the generic
// "Couldn't find matching render driver" after all candidates fail. Named
// selection makes SDL preserve the selected backend's own SDL error.
[[nodiscard]] const char* renderer_driver_name() noexcept {
#if defined(__APPLE__)
  return "metal";
#elif defined(_WIN32)
  return "direct3d11";
#else
  return "opengl";
#endif
}

#ifdef GRAPHSCORE_HAVE_SDL3

struct WriterShell::Impl {
  // Platform handle: non-const because SDL's C API takes non-const pointers.
  SDL_Window* window            = nullptr;
  bool        initialised_video = false;
  std::string backend;

  // Created once per window. Null when SDL_CreateRenderer fails (a
  // headless machine that passed InitSubSystem but has no GPU-accelerated
  // renderer); the event loop skips rendering in that case.
  SDL_Renderer* renderer = nullptr;

  InputHandler* input_handler     = nullptr;
  bool          text_input_active = false;
  KeyModifiers  ordered_modifiers;

  // The authoritative viewport transform the render pass applies (non-owning;
  // owned by the app's input handler). Null renders at native scale.
  const ViewportTransform* viewport_transform = nullptr;

  // Test-only: when true, a pinch event carrying a focal point is dropped as
  // though SDL_RenderCoordinatesFromWindow had failed.
  bool test_force_pinch_conversion_failure = false;

  // Written by set_highlight_rects, consumed by the event loop's render
  // pass. Cleared when the window is closed.
  std::vector<NotationRect> highlight_rects;
  std::vector<NotationRect> paste_preview_rects;

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

  // Test-only: when true, the next render_frame with present=true reports a
  // failure as though SDL_FlushRenderer had returned false, before
  // SDL_RenderPresent is reached. This surfaces the observable queued
  // command-execution flush/API failure without presenting; it does not —
  // and cannot at this SDL pin — model a backend failure that occurs only at
  // the present step itself.  One-shot; reset after consumption.
  bool test_force_flush_failure = false;

  // Test-only: the number of SDL_RenderPresent calls actually reached.
  // Incremented at the sole present call site; a test asserts the forced
  // flush failure returns before incrementing it (so the flush gate skipped
  // presentation) and that a subsequent present advances it (recovery).
  std::uint64_t test_present_call_count = 0;

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
    if (window != nullptr && text_input_active) {
      (void)SDL_StopTextInput(window);
      text_input_active = false;
    }
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
[[nodiscard]] KeyModifiers sdl_keymod_to_key_modifiers(
    SDL_Keymod sdl_mod) noexcept;
void dispatch_sdl_event(InputHandler* handler, SDL_Renderer* renderer,
                        KeyModifiers* ordered_modifiers, SDL_Event event,
                        bool force_pinch_conversion_failure = false);

// Deliver the current logical window size to the handler's viewport-size
// callback, which derives size-dependent fallbacks (e.g. the pinch focal
// window center) from it. Called on window creation and on resize; a failed
// size query leaves the handler's prior fallback untouched.
void deliver_viewport_size(SDL_Window* window, InputHandler* handler) {
  if (window == nullptr || handler == nullptr) {
    return;
  }
  int width  = 0;
  int height = 0;
  if (!SDL_GetWindowSize(window, &width, &height)) {
    return;
  }
  handler->on_viewport_size_changed(static_cast<double>(width),
                                    static_cast<double>(height));
}

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

  if (!SDL_SetRenderScale(renderer, static_cast<float>(sx),
                          static_cast<float>(sy))) {
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

// Converts a validated notation destination rect to the SDL_FRect the render
// pass submits. Delegates the float-corner validation to the SDL-free
// notation_rect_to_float_rect helper below (so the check is testable headlessly
// and compiles in writer-OFF builds), then re-casts the float-rounded result.
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

// Renders one complete frame — clear, notation surface through the viewport
// transform, highlight rects, and (when `present`) present — checking every
// SDL result. Any failure returns kRenderingSetupFailed WITHOUT presenting a
// cleared or partial frame, so a transient subcommand failure can never put a
// blank background on screen: the last valid presentation survives. `present`
// is false only for the test seam, which reads the composed back buffer back
// without presenting.
//
// Presentation checking at the pinned SDL SHA (08b9c553): SDL_RenderPresent
// records a backend present failure in an internal `presented` flag but
// returns true unconditionally (SDL_render.c), so its return value cannot
// detect a failed present, and a backend failure that occurs only at the
// present step is unobservable at this pin. What IS observable and checked
// here is the queued draw-command flush: SDL_FlushRenderer returns false when
// FlushRenderCommands fails, so a queued command-execution failure is surfaced
// before SDL_RenderPresent rather than silently presented. SDL_GetError is not
// used as a portable present result, and a failed present is not assumed to
// preserve the prior frame. Physical presentation itself is manual/smoke
// evidence only: the automated present tests assert the flush/API gate, not
// that pixels reached the display.
[[nodiscard]] ShellResult WriterShell::render_frame(bool present) {
  SDL_Renderer* renderer = impl_->renderer;
  if (renderer == nullptr) {
    return ShellResult{};
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

  if (impl_->notation_texture != nullptr) {
    const auto notation_dst = map_rect_to_viewport(
        NotationRect{0.0, 0.0,
                     static_cast<double>(impl_->notation_surface.width),
                     static_cast<double>(impl_->notation_surface.height)},
        impl_->viewport_transform);
    if (!notation_dst.has_value()) {
      return ShellResult{ShellError::kRenderingSetupFailed,
                         "notation destination geometry is not a valid "
                         "viewport rectangle"};
    }
    const auto dst = notation_rect_to_sdl_frect(*notation_dst);
    if (!dst.has_value()) {
      return ShellResult{ShellError::kRenderingSetupFailed,
                         "notation destination geometry is not representable "
                         "as a positive SDL_FRect"};
    }
    // M6-phase-8: zoom changes only the destination geometry. Rendering the
    // complete source texture keeps every notation element present instead of
    // cropping or substituting a zoom-dependent semantic summary.
    if (!SDL_RenderTexture(renderer, impl_->notation_texture,
                           /*srcrect=*/nullptr, &*dst)) {
      return fail("SDL_RenderTexture");
    }
  }

  if (!SDL_SetRenderDrawColor(renderer, 225, 165, 45, 70)) {
    return fail("SDL_SetRenderDrawColor (paste preview color)");
  }
  for (const NotationRect& rect : impl_->paste_preview_rects) {
    const auto preview_dst =
        map_rect_to_viewport(rect, impl_->viewport_transform);
    if (!preview_dst.has_value()) {
      continue;
    }
    const auto dst = notation_rect_to_sdl_frect(*preview_dst);
    if (!dst.has_value()) {
      continue;
    }
    if (!SDL_RenderFillRect(renderer, &*dst)) {
      return fail("SDL_RenderFillRect (paste preview)");
    }
  }

  if (!SDL_SetRenderDrawColor(renderer, 80, 130, 210, 90)) {
    return fail("SDL_SetRenderDrawColor (highlight color)");
  }
  for (const NotationRect& rect : impl_->highlight_rects) {
    const auto highlight_dst =
        map_rect_to_viewport(rect, impl_->viewport_transform);
    if (!highlight_dst.has_value()) {
      // A highlight rect the transform cannot map is skipped: it is
      // decorative, and the notation surface above is the load-bearing frame.
      continue;
    }
    const auto dst = notation_rect_to_sdl_frect(*highlight_dst);
    if (!dst.has_value()) {
      continue;
    }
    if (!SDL_RenderFillRect(renderer, &*dst)) {
      return fail("SDL_RenderFillRect");
    }
  }

  if (!present) {
    return ShellResult{};
  }

  // Flush the queued draw commands and surface any execution failure before
  // presenting. This is the observable failure gate at the pinned SHA: see the
  // comment above for why SDL_RenderPresent's return value alone cannot detect
  // a backend present failure. The flush injection below returns before
  // SDL_RenderPresent, so the test-only present-call counter does not advance
  // and independently proves presentation was skipped.
  if (impl_->test_force_flush_failure) {
    impl_->test_force_flush_failure = false;  // one-shot; never leaks
    return ShellResult{ShellError::kRenderingSetupFailed,
                       "test-injected render flush failure"};
  }
  if (!SDL_FlushRenderer(renderer)) {
    return fail("SDL_FlushRenderer");
  }
  // Test-only accounting at the actual present call site: a test asserts this
  // does not advance when the flush gate above returns early, and does advance
  // once presentation is reached again.
  ++impl_->test_present_call_count;
  if (!SDL_RenderPresent(renderer)) {
    // Not reached in practice at the pinned SHA, where SDL_RenderPresent
    // returns true unconditionally; retained for a future pin that reports
    // present failure through its return value again.
    return fail("SDL_RenderPresent");
  }
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

  // Create the renderer against the ADR 0002 §A5-fixed backend for this
  // platform (see renderer_driver_name above). A null renderer here means
  // the selected GPU backend failed on a machine whose video subsystem
  // initialised — a real defect, not a headless skip (a genuinely headless
  // host already returned kBackendUnavailable above). Surface the backend
  // name and its own SDL error so the failure is diagnosable rather than
  // SDL's generic auto-selection error. No software fallback: ADR 0002 §A5
  // chooses the GPU path explicitly.
  impl_->renderer = SDL_CreateRenderer(impl_->window, renderer_driver_name());
  if (impl_->renderer == nullptr) {
    return ShellResult{ShellError::kRendererUnavailable,
                       std::string("renderer '")
                           .append(renderer_driver_name())
                           .append("' failed: ")
                           .append(SDL_GetError())};
  }

  // A text-consuming app focus may have been established before the native
  // window existed (notably in a headless/test assembly). Apply that retained
  // GraphScore-owned state now, using SDL3's window-taking signature.
  if (impl_->text_input_active && !SDL_StartTextInput(impl_->window)) {
    return ShellResult{
        ShellError::kBackendUnavailable,
        std::string("SDL_StartTextInput failed: ").append(SDL_GetError())};
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

  // Derive size-dependent fallbacks (the pinch focal window center) from the
  // actual logical size now that the window is realized.
  deliver_viewport_size(impl_->window, impl_->input_handler);
  impl_->ordered_modifiers = sdl_keymod_to_key_modifiers(SDL_GetModState());

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
        // The logical size may have changed: refresh the size-derived
        // fallbacks (the pinch focal window center) from the actual size.
        deliver_viewport_size(impl_->window, impl_->input_handler);
      }

      dispatch_sdl_event(impl_->input_handler, impl_->renderer,
                         &impl_->ordered_modifiers, event);

      // Render the frame: notation surface first, then highlight rects on
      // top as a semi-transparent blue overlay. Any SDL failure is a
      // rendering-setup defect and terminates the loop — without presenting a
      // cleared or partial frame, so the last valid presentation survives.
      if (impl_->renderer != nullptr) {
        ShellResult frame_result = render_frame(/*present=*/true);
        if (!frame_result.ok()) {
          return frame_result;
        }
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
      dispatch_sdl_event(impl_->input_handler, impl_->renderer,
                         &impl_->ordered_modifiers, event);
    }
    // Render one frame so the window shows content even without an event
    // loop. Any SDL failure is a rendering-setup defect and is surfaced
    // without presenting a cleared or partial frame.
    if (impl_->renderer != nullptr) {
      ShellResult frame_result = render_frame(/*present=*/true);
      if (!frame_result.ok()) {
        return frame_result;
      }
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
    case 1:
      return PointerButton::kPrimary;
    case 3:
      return PointerButton::kSecondary;
    case 2:
      return PointerButton::kMiddle;
    default:
      return PointerButton::kUnknown;
  }
}

// Translates SDL's physical scancode (event.key.scancode), not the layout-
// dependent logical keycode (event.key.key). The physical scancode is what
// keeps arrow/Home/End identification layout-independent, and is also how
// the two character keys `-`/`=` are identified — see KeyCode's doc comment
// in the public header for why that matters here and how it relates to
// M5-phase-27's action table. Any scancode outside this minimal set maps to
// kUnknown.
[[nodiscard]] KeyCode sdl_scancode_to_key_code(
    SDL_Scancode sdl_scancode) noexcept {
  switch (sdl_scancode) {
    case SDL_SCANCODE_LEFT:
      return KeyCode::kLeft;
    case SDL_SCANCODE_RIGHT:
      return KeyCode::kRight;
    case SDL_SCANCODE_UP:
      return KeyCode::kUp;
    case SDL_SCANCODE_DOWN:
      return KeyCode::kDown;
    case SDL_SCANCODE_HOME:
      return KeyCode::kHome;
    case SDL_SCANCODE_END:
      return KeyCode::kEnd;
    case SDL_SCANCODE_MINUS:
      return KeyCode::kMinus;
    case SDL_SCANCODE_EQUALS:
      return KeyCode::kEquals;
    case SDL_SCANCODE_BACKSPACE:
      return KeyCode::kBackspace;
    case SDL_SCANCODE_DELETE:
      return KeyCode::kDelete;
    case SDL_SCANCODE_R:
      return KeyCode::kR;
    case SDL_SCANCODE_1:
      return KeyCode::kDigit1;
    case SDL_SCANCODE_2:
      return KeyCode::kDigit2;
    case SDL_SCANCODE_3:
      return KeyCode::kDigit3;
    case SDL_SCANCODE_4:
      return KeyCode::kDigit4;
    case SDL_SCANCODE_5:
      return KeyCode::kDigit5;
    case SDL_SCANCODE_6:
      return KeyCode::kDigit6;
    case SDL_SCANCODE_7:
      return KeyCode::kDigit7;
    case SDL_SCANCODE_8:
      return KeyCode::kDigit8;
    case SDL_SCANCODE_9:
      return KeyCode::kDigit9;
    case SDL_SCANCODE_0:
      return KeyCode::kDigit0;
    case SDL_SCANCODE_RETURN:
      return KeyCode::kReturn;
    case SDL_SCANCODE_ESCAPE:
      return KeyCode::kEscape;
    case SDL_SCANCODE_TAB:
      return KeyCode::kTab;
    case SDL_SCANCODE_SPACE:
      return KeyCode::kSpace;
    case SDL_SCANCODE_KP_1:
      return KeyCode::kNumPad1;
    case SDL_SCANCODE_KP_2:
      return KeyCode::kNumPad2;
    case SDL_SCANCODE_KP_3:
      return KeyCode::kNumPad3;
    case SDL_SCANCODE_KP_4:
      return KeyCode::kNumPad4;
    case SDL_SCANCODE_KP_5:
      return KeyCode::kNumPad5;
    case SDL_SCANCODE_KP_6:
      return KeyCode::kNumPad6;
    case SDL_SCANCODE_KP_7:
      return KeyCode::kNumPad7;
    case SDL_SCANCODE_KP_0:
      return KeyCode::kNumPad0;
    case SDL_SCANCODE_KP_PERIOD:
      return KeyCode::kNumPadDecimal;
    default:
      return KeyCode::kUnknown;
  }
}

// Translates SDL's logical keycode (event.key.key, the character the active
// layout produces) to the letter-mnemonic LogicalKey, for
// docs/plan/05-notation-editor-action-table.md §4's logical letter bindings.
// Only the bound letters (A-G, N, R, X, V, Z, K) are recognized; every other
// keycode maps to kUnknown. SDL3 reports letter keycodes as their lowercase
// ASCII value (SDLK_A == 'a', ..., SDLK_Z == 'z').
[[nodiscard]] LogicalKey sdl_keycode_to_logical_key(
    SDL_Keycode sdl_keycode) noexcept {
  switch (sdl_keycode) {
    case SDLK_A:
      return LogicalKey::kA;
    case SDLK_B:
      return LogicalKey::kB;
    case SDLK_C:
      return LogicalKey::kC;
    case SDLK_D:
      return LogicalKey::kD;
    case SDLK_E:
      return LogicalKey::kE;
    case SDLK_F:
      return LogicalKey::kF;
    case SDLK_G:
      return LogicalKey::kG;
    case SDLK_N:
      return LogicalKey::kN;
    case SDLK_R:
      return LogicalKey::kR;
    case SDLK_X:
      return LogicalKey::kX;
    case SDLK_V:
      return LogicalKey::kV;
    case SDLK_Z:
      return LogicalKey::kZ;
    case SDLK_K:
      return LogicalKey::kK;
    default:
      return LogicalKey::kUnknown;
  }
}

// SDL_KMOD_SHIFT / SDL_KMOD_CTRL / SDL_KMOD_ALT / SDL_KMOD_GUI are each the
// combined left|right mask for that modifier (e.g. SDL_KMOD_SHIFT ==
// SDL_KMOD_LSHIFT | SDL_KMOD_RSHIFT), so either physical key sets the
// corresponding GraphScore flag.
[[nodiscard]] KeyModifiers sdl_keymod_to_key_modifiers(
    SDL_Keymod sdl_mod) noexcept {
  KeyModifiers modifiers;
  modifiers.shift   = (sdl_mod & SDL_KMOD_SHIFT) != 0;
  modifiers.control = (sdl_mod & SDL_KMOD_CTRL) != 0;
  modifiers.alt     = (sdl_mod & SDL_KMOD_ALT) != 0;
  modifiers.meta    = (sdl_mod & SDL_KMOD_GUI) != 0;
  return modifiers;
}

void dispatch_sdl_event(InputHandler* handler, SDL_Renderer* renderer,
                        KeyModifiers* ordered_modifiers, SDL_Event event,
                        bool force_pinch_conversion_failure) {
  if (ordered_modifiers != nullptr) {
    if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
      *ordered_modifiers = sdl_keymod_to_key_modifiers(event.key.mod);
    } else if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST ||
               event.type == SDL_EVENT_QUIT ||
               event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
      *ordered_modifiers = {};
    }
  }
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
      pe.measure_selection =
          ordered_modifiers != nullptr && ordered_modifiers->shift;
      handler->on_pointer_press(pe);
      break;
    }
    case SDL_EVENT_MOUSE_MOTION: {
      PointerEvent pe;
      pe.x = static_cast<double>(event.motion.x);
      pe.y = static_cast<double>(event.motion.y);
      pe.measure_selection =
          ordered_modifiers != nullptr && ordered_modifiers->shift;
      handler->on_pointer_move(pe);
      break;
    }
    case SDL_EVENT_MOUSE_BUTTON_UP: {
      PointerEvent pe;
      pe.x      = static_cast<double>(event.button.x);
      pe.y      = static_cast<double>(event.button.y);
      pe.button = sdl_button_to_pointer_button(event.button.button);
      pe.measure_selection =
          ordered_modifiers != nullptr && ordered_modifiers->shift;
      handler->on_pointer_release(pe);
      break;
    }
    case SDL_EVENT_KEY_DOWN: {
      // The physical scancode and the layout-mapped logical keycode are
      // delivered side by side (action table §4): positional/digit/symbol
      // bindings key on `code`, letter-mnemonic bindings key on `logical`.
      // OS auto-repeat is surfaced as `repeat` so the app can honour the
      // repeat-safe/repeat-once policies (§6); the shell does not filter
      // or re-fire it.
      KeyEvent ke;
      ke.code      = sdl_scancode_to_key_code(event.key.scancode);
      ke.modifiers = sdl_keymod_to_key_modifiers(event.key.mod);
      ke.repeat    = event.key.repeat;
      ke.logical   = sdl_keycode_to_logical_key(event.key.key);
      handler->on_key_press(ke);
      break;
    }
    case SDL_EVENT_TEXT_INPUT: {
      // Composed text is delivered on a separate channel from key identity
      // (action table §4, §5): the UTF-8 text the active layout produced,
      // so a text-consumer focus context sees shifted and non-US characters
      // exactly as the user typed them, never as a KeyCode/LogicalKey
      // mnemonic. `event.text.text` is a null-terminated UTF-8 buffer.
      TextInputEvent te;
      te.text = event.text.text != nullptr ? event.text.text : "";
      handler->on_text_input(te);
      break;
    }
    case SDL_EVENT_MOUSE_WHEEL: {
      // The native two-finger trackpad pan stream on macOS arrives as a
      // mouse-wheel event (ADR 0004 §8). The shell preserves its data and the
      // app decides whether it is pan or Primary+wheel zoom. The x/y fields are
      // subpixel floats in scroll units and are NOT touched by
      // SDL_ConvertEventToRenderCoordinates (which converts only the
      // mouse_x/mouse_y pointer position); they are delivered as double
      // deltas verbatim. SDL_MOUSEWHEEL_FLIPPED means the platform inverts
      // the direction; the shell negates so the handler always receives
      // content-relative motion.
      WheelEvent wheel;
      wheel.delta.x = static_cast<double>(event.wheel.x);
      wheel.delta.y = static_cast<double>(event.wheel.y);
      if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) {
        wheel.delta.x = -wheel.delta.x;
        wheel.delta.y = -wheel.delta.y;
      }
      wheel.pointer = {static_cast<double>(event.wheel.mouse_x),
                       static_cast<double>(event.wheel.mouse_y)};
      if (ordered_modifiers != nullptr) {
        wheel.modifiers = *ordered_modifiers;
      }
      handler->on_wheel(wheel);
      break;
    }
    case SDL_EVENT_PINCH_UPDATE: {
      // Pinch zoom is driven exclusively by this event (ADR 0004 §8). The
      // scale field is the multiplicative change since the previous update;
      // it is handed through verbatim and must be applied exactly once by
      // the consumer — never re-derived from per-finger distance.
      PinchUpdate update;
      update.scale = static_cast<double>(event.pinch.scale);
      if (event.pinch.focus_x >= 0.0F && event.pinch.focus_y >= 0.0F) {
        // focus_x/focus_y are window-space and are NOT converted by
        // SDL_ConvertEventToRenderCoordinates (unlike pointer/finger
        // events), so convert them here to logical viewport coordinates.
        float focus_x = event.pinch.focus_x;
        float focus_y = event.pinch.focus_y;
        if (force_pinch_conversion_failure) {
          // Test-injected: simulate SDL_RenderCoordinatesFromWindow failing.
          // A failed conversion would dispatch window-space coordinates as
          // though they were logical, so drop the event transactionally —
          // identical to the real failure path below.
          return;
        }
        if (renderer != nullptr &&
            !SDL_RenderCoordinatesFromWindow(renderer, focus_x, focus_y,
                                             &focus_x, &focus_y)) {
          // Conversion failure: the focal point cannot be mapped to logical
          // space, so no handler callback must be dispatched with mislabeled
          // coordinates (mirroring the pointer conversion path above).
          return;
        }
        update.focal_point = ViewportPosition{static_cast<double>(focus_x),
                                              static_cast<double>(focus_y)};
      }
      handler->on_pinch(update);
      break;
    }
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_MOTION: {
      // Diagnostic/optional two-finger tracking. When a renderer exists,
      // SDL_ConvertEventToRenderCoordinates has already scaled tfinger.x/y
      // from normalized (0..1) to logical (render) coordinates; without one
      // they remain normalized, which only affects a no-renderer test seam.
      // These events never synthesize viewport motion; the handler uses them
      // solely to maintain the pinch centroid fallback.
      FingerContact finger;
      finger.finger_id = static_cast<std::uint64_t>(event.tfinger.fingerID);
      finger.position  = ViewportPosition{static_cast<double>(event.tfinger.x),
                                         static_cast<double>(event.tfinger.y)};
      if (event.type == SDL_EVENT_FINGER_DOWN) {
        handler->on_finger_down(finger);
      } else {
        handler->on_finger_move(finger);
      }
      break;
    }
    case SDL_EVENT_FINGER_UP:
      handler->on_finger_up(static_cast<std::uint64_t>(event.tfinger.fingerID));
      break;
    case SDL_EVENT_FINGER_CANCELED:
      handler->on_finger_canceled(
          static_cast<std::uint64_t>(event.tfinger.fingerID));
      break;
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

  // Save-and-restore render target, texture blend mode, and render scale so
  // the test seam does not perturb the production state. The readback forces
  // the render scale to 1.0 so each notation texel occupies exactly one
  // window pixel: SDL_RenderReadPixels addresses its rect in pixel
  // coordinates, so with the production render scale (2.0 on a Retina host)
  // the rect's render-coordinate values would select a different, linearly
  // filtered texel. At 1.0 the rect {x, y, 1, 1} is exactly texel (x, y).
  SDL_Texture* saved_target = SDL_GetRenderTarget(impl_->renderer);
  SDL_SetRenderTarget(impl_->renderer, nullptr);  // back to window

  SDL_BlendMode saved_blend = SDL_BLENDMODE_NONE;
  SDL_GetTextureBlendMode(impl_->notation_texture, &saved_blend);
  SDL_SetTextureBlendMode(impl_->notation_texture, SDL_BLENDMODE_NONE);

  float saved_scale_x = 1.0F;
  float saved_scale_y = 1.0F;
  SDL_GetRenderScale(impl_->renderer, &saved_scale_x, &saved_scale_y);
  SDL_SetRenderScale(impl_->renderer, 1.0F, 1.0F);

  // Clear to black before rendering, so the readback pixel comes only from
  // the texture — no prior frame content bleeds in.
  SDL_SetRenderDrawColor(impl_->renderer, 0, 0, 0, 255);
  SDL_RenderClear(impl_->renderer);

  const SDL_FRect dst_rect{0.0F, 0.0F,
                           static_cast<float>(impl_->notation_surface.width),
                           static_cast<float>(impl_->notation_surface.height)};
  if (!SDL_RenderTexture(impl_->renderer, impl_->notation_texture,
                         /*srcrect=*/nullptr, &dst_rect)) {
    SDL_SetRenderScale(impl_->renderer, saved_scale_x, saved_scale_y);
    SDL_SetTextureBlendMode(impl_->notation_texture, saved_blend);
    SDL_SetRenderTarget(impl_->renderer, saved_target);
    return std::nullopt;
  }

  std::array<std::uint8_t, 4> pixel{};
  const SDL_Rect read_rect{static_cast<int>(x), static_cast<int>(y), 1, 1};

  SDL_Surface* surface = SDL_RenderReadPixels(impl_->renderer, &read_rect);
  if (surface == nullptr) {
    SDL_SetRenderScale(impl_->renderer, saved_scale_x, saved_scale_y);
    SDL_SetTextureBlendMode(impl_->notation_texture, saved_blend);
    SDL_SetRenderTarget(impl_->renderer, saved_target);
    return std::nullopt;
  }

  // Read the single pixel's RGBA components from the returned surface.
  if (!SDL_ReadSurfacePixel(surface, 0, 0, &pixel[0], &pixel[1], &pixel[2],
                            &pixel[3])) {
    SDL_DestroySurface(surface);
    SDL_SetRenderScale(impl_->renderer, saved_scale_x, saved_scale_y);
    SDL_SetTextureBlendMode(impl_->notation_texture, saved_blend);
    SDL_SetRenderTarget(impl_->renderer, saved_target);
    return std::nullopt;
  }

  SDL_DestroySurface(surface);

  SDL_SetRenderScale(impl_->renderer, saved_scale_x, saved_scale_y);
  SDL_SetTextureBlendMode(impl_->notation_texture, saved_blend);
  SDL_SetRenderTarget(impl_->renderer, saved_target);
  return pixel;
}

ShellResult WriterShell::test_render_frame() {
  if (impl_->renderer == nullptr) {
    return ShellResult{ShellError::kRendererUnavailable, "no renderer"};
  }
  // Run the production render pass — clear, notation through the viewport
  // transform, highlight rects — but do NOT present, so a caller can read the
  // composed back buffer back (SDL's Metal readback commits the command
  // buffer itself). This is the composition-proof seam; it does not exercise
  // the flush/present gate — see test_present_frame() for that.
  return render_frame(/*present=*/false);
}

ShellResult WriterShell::test_present_frame() {
  if (impl_->renderer == nullptr) {
    return ShellResult{ShellError::kRendererUnavailable, "no renderer"};
  }
  // Run the production render pass with present=true — the same helper the
  // event loop and one-frame paths call — so a test can assert the observable
  // queued-command flush/API gate and the pre-present flush-failure injection
  // without reading the back buffer. A presented frame's back buffer is
  // invalidated, so this seam makes no readback claim; physical presentation
  // is manual/smoke evidence only.
  return render_frame(/*present=*/true);
}

std::optional<std::array<std::uint8_t, 4>>
WriterShell::test_read_backbuffer_pixel(std::uint32_t x, std::uint32_t y) {
  if (impl_->renderer == nullptr) {
    return std::nullopt;
  }
  // Read one pixel from the window (null) render target in pixel coordinates.
  // The composed frame rendered by test_render_frame() lives there; the
  // readback commits the pending command buffer, so no present is required.
  SDL_Texture* saved_target = SDL_GetRenderTarget(impl_->renderer);
  SDL_SetRenderTarget(impl_->renderer, nullptr);

  const SDL_Rect read_rect{static_cast<int>(x), static_cast<int>(y), 1, 1};
  SDL_Surface*   surface = SDL_RenderReadPixels(impl_->renderer, &read_rect);

  SDL_SetRenderTarget(impl_->renderer, saved_target);
  if (surface == nullptr) {
    return std::nullopt;
  }

  std::array<std::uint8_t, 4> pixel{};
  const bool ok = SDL_ReadSurfacePixel(surface, 0, 0, &pixel[0], &pixel[1],
                                       &pixel[2], &pixel[3]);
  SDL_DestroySurface(surface);
  if (!ok) {
    return std::nullopt;
  }
  return pixel;
}

#else  // GRAPHSCORE_HAVE_SDL3

// Runtime-only configuration (-DGRAPHSCORE_BUILD_WRITER=OFF). The target
// still builds and still satisfies the architecture audit; it simply has no
// windowing backend.

struct WriterShell::Impl {
  std::string backend;

  InputHandler* input_handler     = nullptr;
  bool          text_input_active = false;

  // The authoritative viewport transform (non-owning). No rendering occurs in
  // this configuration, but the test seam still reports the mapped rect.
  const ViewportTransform* viewport_transform = nullptr;

  // No-op storage: the writer-OFF path never renders, but setters
  // still compile and are safe to call.
  std::vector<NotationRect> highlight_rects;
  std::vector<NotationRect> paste_preview_rects;
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

ShellResult WriterShell::test_render_frame() {
  return ShellResult{ShellError::kBackendNotCompiledIn, "no renderer"};
}

ShellResult WriterShell::test_present_frame() {
  return ShellResult{ShellError::kBackendNotCompiledIn, "no renderer"};
}

std::optional<std::array<std::uint8_t, 4>>
WriterShell::test_read_backbuffer_pixel(std::uint32_t /*x*/,
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

// Maps a world-space rectangle through the viewport transform into logical
// viewport coordinates. A null transform (identity) returns the rect
// unchanged. Returns nullopt when the transform cannot map the corners; the
// render pass must then surface the defect rather than drawing nothing.
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
  // Distinct, finite float corners: the right/bottom edge must advance
  // strictly past the origin in float. A width absorbed into the float origin
  // (right == left) or an edge that overflows to infinity would make SDL draw
  // a degenerate or clipped rectangle. This also covers negative origins,
  // where `x + width > x` is exactly the advancement condition.
  const float right  = x + width;
  const float bottom = y + height;
  if (!std::isfinite(right) || !std::isfinite(bottom) || !(right > x) ||
      !(bottom > y)) {
    return std::nullopt;
  }
  return NotationRect{static_cast<double>(x), static_cast<double>(y),
                      static_cast<double>(width), static_cast<double>(height)};
}

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
      if (!SDL_StartTextInput(impl_->window)) {
        return;
      }
    } else {
      (void)SDL_StopTextInput(impl_->window);
    }
  }
#endif
  impl_->text_input_active = active;
}

bool WriterShell::test_text_input_active() const noexcept {
  return impl_->text_input_active;
}

void WriterShell::set_highlight_rects(std::vector<NotationRect> rects) {
  impl_->highlight_rects = std::move(rects);
}

void WriterShell::set_paste_preview_rects(std::vector<NotationRect> rects) {
  impl_->paste_preview_rects = std::move(rects);
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

void WriterShell::dispatch_test_key_event(KeyEvent event) {
  InputHandler* handler = impl_->input_handler;
  if (handler == nullptr) {
    return;
  }
  // Unlike the pointer headless seam, key events carry no coordinates, so
  // there is no DPI scale to apply here — the event is delivered
  // unchanged. This function lives outside any GRAPHSCORE_HAVE_SDL3
  // #ifdef, so it compiles identically in writer-ON and writer-OFF builds.
  handler->on_key_press(event);
}

void WriterShell::dispatch_test_text_input(TextInputEvent event) {
  InputHandler* handler = impl_->input_handler;
  if (handler == nullptr) {
    return;
  }
  // Composed text carries no coordinates and no modifiers, so — like the
  // key headless seam — there is nothing to translate. Delivered unchanged
  // and compiled in both writer-ON and writer-OFF configurations.
  handler->on_text_input(std::move(event));
}

void WriterShell::dispatch_test_viewport_resize(double width, double height) {
  InputHandler* handler = impl_->input_handler;
  if (handler == nullptr) {
    return;
  }
  // Headless seam for the size change the shell delivers on window creation
  // and resize; the handler derives size-dependent fallbacks from it.
  handler->on_viewport_size_changed(width, height);
}

void WriterShell::dispatch_sdl_test_pointer_event(std::uint8_t kind,
                                                  PointerEvent event) {
#ifdef GRAPHSCORE_HAVE_SDL3
  InputHandler* handler = impl_->input_handler;
  if (handler == nullptr) {
    return;
  }
  // Construct a synthetic SDL event in window-coordinate units (points) —
  // the same units real SDL mouse events carry, NOT pixels — and tag it with
  // the shell's own window ID, then route it through the production
  // dispatch_sdl_event path. dispatch_sdl_event applies
  // SDL_ConvertEventToRenderCoordinates, which converts only when the
  // event's windowID matches the renderer's window (a zero/unset windowID
  // would silently skip conversion), so the test exercises the exact
  // window→render coordinate conversion that production mouse events undergo.
  const SDL_WindowID window_id = SDL_GetWindowID(impl_->window);
  SDL_Event          sdl_event{};
  switch (kind) {
    case 0:  // press
      sdl_event.type            = SDL_EVENT_MOUSE_BUTTON_DOWN;
      sdl_event.button.windowID = window_id;
      sdl_event.button.x        = static_cast<float>(event.x);
      sdl_event.button.y        = static_cast<float>(event.y);
      sdl_event.button.button   = 1;  // SDL_BUTTON_LEFT
      sdl_event.button.down     = true;
      break;
    case 1:  // move
      sdl_event.type            = SDL_EVENT_MOUSE_MOTION;
      sdl_event.motion.windowID = window_id;
      sdl_event.motion.x        = static_cast<float>(event.x);
      sdl_event.motion.y        = static_cast<float>(event.y);
      break;
    case 2:  // release
      sdl_event.type            = SDL_EVENT_MOUSE_BUTTON_UP;
      sdl_event.button.windowID = window_id;
      sdl_event.button.x        = static_cast<float>(event.x);
      sdl_event.button.y        = static_cast<float>(event.y);
      sdl_event.button.button   = 1;  // SDL_BUTTON_LEFT
      sdl_event.button.down     = false;
      break;
    case 3:  // cancel
      handler->on_cancel();
      return;
    default:
      return;
  }
  dispatch_sdl_event(handler, impl_->renderer, &impl_->ordered_modifiers,
                     sdl_event);
#else
  (void)kind;
  (void)event;
#endif
}

std::optional<ViewportPosition> WriterShell::test_window_to_render_point(
    double x, double y) const {
#ifdef GRAPHSCORE_HAVE_SDL3
  if (impl_->renderer == nullptr) {
    return std::nullopt;
  }
  float render_x = 0.0F;
  float render_y = 0.0F;
  if (!SDL_RenderCoordinatesFromWindow(impl_->renderer, static_cast<float>(x),
                                       static_cast<float>(y), &render_x,
                                       &render_y)) {
    return std::nullopt;
  }
  return ViewportPosition{static_cast<double>(render_x),
                          static_cast<double>(render_y)};
#else
  (void)x;
  (void)y;
  return std::nullopt;
#endif
}

std::optional<ViewportPosition> WriterShell::test_render_to_window_point(
    double x, double y) const {
#ifdef GRAPHSCORE_HAVE_SDL3
  if (impl_->renderer == nullptr) {
    return std::nullopt;
  }
  float window_x = 0.0F;
  float window_y = 0.0F;
  if (!SDL_RenderCoordinatesToWindow(impl_->renderer, static_cast<float>(x),
                                     static_cast<float>(y), &window_x,
                                     &window_y)) {
    return std::nullopt;
  }
  return ViewportPosition{static_cast<double>(window_x),
                          static_cast<double>(window_y)};
#else
  (void)x;
  (void)y;
  return std::nullopt;
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

void WriterShell::dispatch_sdl_test_key_event(std::uint32_t sdl_scancode,
                                              std::uint16_t sdl_key_modifiers,
                                              std::uint32_t sdl_keycode) {
#ifdef GRAPHSCORE_HAVE_SDL3
  InputHandler* handler = impl_->input_handler;
  if (handler == nullptr) {
    return;
  }
  // Parameters are plain integers, not an SDL type, so no SDL type reaches
  // the public header; they are cast to the real SDL types only here, at
  // the one translation unit permitted to name them, and routed through
  // the production dispatch_sdl_event so a test exercises the actual
  // forward translation rather than a mapping written only for the test.
  // `sdl_keycode` supplies the layout-mapped logical key, which a plain
  // scancode alone cannot (scancode is layout-independent); 0 leaves it as
  // SDLK_UNKNOWN so positional-key tests never depend on the host layout.
  SDL_Event sdl_event{};
  sdl_event.type         = SDL_EVENT_KEY_DOWN;
  sdl_event.key.scancode = static_cast<SDL_Scancode>(sdl_scancode);
  sdl_event.key.mod      = static_cast<SDL_Keymod>(sdl_key_modifiers);
  sdl_event.key.key      = static_cast<SDL_Keycode>(sdl_keycode);
  sdl_event.key.down     = true;
  dispatch_sdl_event(handler, impl_->renderer, &impl_->ordered_modifiers,
                     sdl_event);
#else
  (void)sdl_scancode;
  (void)sdl_key_modifiers;
  (void)sdl_keycode;
#endif
}

void WriterShell::dispatch_sdl_test_modifier_transition(KeyModifiers modifiers,
                                                        bool         key_down) {
#ifdef GRAPHSCORE_HAVE_SDL3
  SDL_Keymod sdl_modifiers = SDL_KMOD_NONE;
  if (modifiers.shift) {
    sdl_modifiers |= SDL_KMOD_LSHIFT;
  }
  if (modifiers.control) {
    sdl_modifiers |= SDL_KMOD_LCTRL;
  }
  if (modifiers.alt) {
    sdl_modifiers |= SDL_KMOD_LALT;
  }
  if (modifiers.meta) {
    sdl_modifiers |= SDL_KMOD_LGUI;
  }

  SDL_Event sdl_event{};
  sdl_event.type     = key_down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
  sdl_event.key.mod  = sdl_modifiers;
  sdl_event.key.down = key_down;
  dispatch_sdl_event(impl_->input_handler, impl_->renderer,
                     &impl_->ordered_modifiers, sdl_event);
#else
  (void)modifiers;
  (void)key_down;
#endif
}

void WriterShell::dispatch_sdl_test_focus_loss() {
#ifdef GRAPHSCORE_HAVE_SDL3
  if (impl_->input_handler != nullptr) {
    impl_->input_handler->on_cancel();
  }
  SDL_Event sdl_event{};
  sdl_event.type = SDL_EVENT_WINDOW_FOCUS_LOST;
  dispatch_sdl_event(impl_->input_handler, impl_->renderer,
                     &impl_->ordered_modifiers, sdl_event);
#endif
}

void WriterShell::dispatch_sdl_test_text_input(std::string_view text) {
#ifdef GRAPHSCORE_HAVE_SDL3
  InputHandler* handler = impl_->input_handler;
  if (handler == nullptr) {
    return;
  }
  // Build a real SDL_EVENT_TEXT_INPUT carrying the UTF-8 `text` and route it
  // through the production dispatch_sdl_event, so a test exercises the actual
  // forward translation (SDL_EVENT_TEXT_INPUT → TextInputEvent) rather than a
  // mapping written only for the test. SDL_TextInputEvent::text is a pointer
  // to a UTF-8 buffer; the pointed-to bytes are read inside dispatch_sdl_event
  // (and copied into the TextInputEvent) before `text` goes out of scope.
  std::string owned(text);
  SDL_Event   sdl_event{};
  sdl_event.type      = SDL_EVENT_TEXT_INPUT;
  sdl_event.text.text = owned.c_str();
  dispatch_sdl_event(handler, impl_->renderer, &impl_->ordered_modifiers,
                     sdl_event);
#else
  (void)text;
#endif
}

void WriterShell::dispatch_sdl_test_scroll_event(double x, double y,
                                                 std::uint32_t direction,
                                                 double        pointer_x,
                                                 double        pointer_y) {
#ifdef GRAPHSCORE_HAVE_SDL3
  InputHandler* handler = impl_->input_handler;
  if (handler == nullptr) {
    return;
  }
  // Build a real SDL_EVENT_MOUSE_WHEEL and route it through the production
  // dispatch_sdl_event, so a test asserts the actual forward translation
  // (wheel x/y delta + FLIPPED negation → ScrollDelta) rather than a mapping
  // written only for the test. No window or renderer is required: without a
  // renderer dispatch_sdl_event skips SDL_ConvertEventToRenderCoordinates and
  // the wheel delta is delivered verbatim.
  SDL_Event sdl_event{};
  sdl_event.type            = SDL_EVENT_MOUSE_WHEEL;
  sdl_event.wheel.x         = static_cast<float>(x);
  sdl_event.wheel.y         = static_cast<float>(y);
  sdl_event.wheel.direction = static_cast<SDL_MouseWheelDirection>(direction);
  sdl_event.wheel.mouse_x   = static_cast<float>(pointer_x);
  sdl_event.wheel.mouse_y   = static_cast<float>(pointer_y);
  const SDL_Keymod prior_modifiers = SDL_GetModState();
  SDL_SetModState(SDL_KMOD_NONE);
  dispatch_sdl_event(handler, impl_->renderer, &impl_->ordered_modifiers,
                     sdl_event);
  SDL_SetModState(prior_modifiers);
#else
  (void)x;
  (void)y;
  (void)direction;
  (void)pointer_x;
  (void)pointer_y;
#endif
}

void WriterShell::dispatch_sdl_test_pinch_event(double scale, double focus_x,
                                                double focus_y) {
#ifdef GRAPHSCORE_HAVE_SDL3
  InputHandler* handler = impl_->input_handler;
  if (handler == nullptr) {
    return;
  }
  // Build a real SDL_EVENT_PINCH_UPDATE and route it through the production
  // dispatch_sdl_event, so a test asserts the actual forward translation
  // (scale + focus_x/focus_y → PinchUpdate) rather than a mapping written
  // only for the test. No window or renderer is required; without a renderer
  // the focus coordinates pass through un-converted (the same behaviour a
  // pinch on a windowless/no-renderer host would show).
  SDL_Event sdl_event{};
  sdl_event.type          = SDL_EVENT_PINCH_UPDATE;
  sdl_event.pinch.scale   = static_cast<float>(scale);
  sdl_event.pinch.focus_x = static_cast<float>(focus_x);
  sdl_event.pinch.focus_y = static_cast<float>(focus_y);
  dispatch_sdl_event(handler, impl_->renderer, &impl_->ordered_modifiers,
                     sdl_event, impl_->test_force_pinch_conversion_failure);
#else
  (void)scale;
  (void)focus_x;
  (void)focus_y;
#endif
}

void WriterShell::dispatch_sdl_test_finger_event(std::uint32_t kind,
                                                 std::uint64_t finger_id,
                                                 double x, double y) {
#ifdef GRAPHSCORE_HAVE_SDL3
  InputHandler* handler = impl_->input_handler;
  if (handler == nullptr) {
    return;
  }
  // Build a real SDL_EVENT_FINGER_* and route it through the production
  // dispatch_sdl_event. `kind` selects down(0)/move(1)/up(2)/canceled(3);
  // x/y are SDL's normalized 0..1 coordinates, matching what SDL reports
  // before a renderer rescales them (none exists here, so the handler sees
  // them verbatim). Unknown kinds are a no-op.
  SDL_Event sdl_event{};
  switch (kind) {
    case 0:
      sdl_event.type = SDL_EVENT_FINGER_DOWN;
      break;
    case 1:
      sdl_event.type = SDL_EVENT_FINGER_MOTION;
      break;
    case 2:
      sdl_event.type = SDL_EVENT_FINGER_UP;
      break;
    case 3:
      sdl_event.type = SDL_EVENT_FINGER_CANCELED;
      break;
    default:
      return;
  }
  sdl_event.tfinger.fingerID = static_cast<SDL_FingerID>(finger_id);
  sdl_event.tfinger.x        = static_cast<float>(x);
  sdl_event.tfinger.y        = static_cast<float>(y);
  dispatch_sdl_event(handler, impl_->renderer, &impl_->ordered_modifiers,
                     sdl_event);
#else
  (void)kind;
  (void)finger_id;
  (void)x;
  (void)y;
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

void WriterShell::set_test_force_pinch_conversion_failure(bool force) {
#ifdef GRAPHSCORE_HAVE_SDL3
  impl_->test_force_pinch_conversion_failure = force;
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

std::vector<NotationRect> WriterShell::test_snapshot_paste_preview_rects()
    const {
  return impl_->paste_preview_rects;
}

std::optional<RasterSurface> WriterShell::test_snapshot_notation_surface()
    const {
  return impl_->notation_surface;
}

}  // namespace graphscore
