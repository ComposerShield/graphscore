// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <graphscore/accessibility/graphscore_accessibility.hpp>
#include <graphscore/canvas/graphscore_canvas.hpp>
#include <graphscore/notation/graphscore_notation.hpp>
#include <graphscore/rendering/graphscore_rendering.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace graphscore {

// Platform shell for the GraphScore Writer. Owns the native window and the
// event loop.
//
// Every platform type — SDL3's included — is confined to writer_shell.cpp
// (ADR 0003 §2.2). Nothing in this header names, includes, or forward-
// declares an SDL type, so no consumer of graphscore_writer_shell acquires a
// dependency on the windowing backend.

// Platform-neutral pointer button. The SDL3 button code is translated to
// this enum inside writer_shell.cpp; no consumer of this header sees an
// SDL constant.
enum class PointerButton : std::uint8_t {
  kPrimary,    // Left button, or single-finger tap.
  kSecondary,  // Right button.
  kMiddle,     // Middle button.
  kUnknown,    // Any SDL button that is not left, right, or middle.
};

// A single pointer event in GraphScore-owned logical (notation) coordinates.
// The shell translates SDL3 event coordinates from pixel space to the
// logical coordinate space before dispatching.
struct PointerEvent {
  double        x      = 0.0;
  double        y      = 0.0;
  PointerButton button = PointerButton::kPrimary;
};

// Input event callback. graphscore_writer_app implements this and
// registers it with WriterShell::set_input_handler(). Every callback runs
// on the main thread (the same thread that calls SDL_WaitEvent /
// SDL_PollEvent during open_window()).
//
// Thread affinity: all methods are called from the main thread only.
// The handler must not assume any concurrency protection beyond that.
class InputHandler {
 public:
  virtual ~InputHandler() = default;

  virtual void on_pointer_press(PointerEvent event)   = 0;
  virtual void on_pointer_move(PointerEvent event)    = 0;
  virtual void on_pointer_release(PointerEvent event) = 0;

  // Called when the window loses focus, the application is quitting, or
  // the window is being closed.  Any in-progress drag or transient state
  // must be cancelled here — the pointer is no longer tracked.
  virtual void on_cancel() = 0;
};

enum class ShellError : std::uint8_t {
  kNone = 0,
  // The windowing backend could not be initialised. On a headless machine
  // this is the expected outcome, not a defect.
  kBackendUnavailable,
  kWindowCreationFailed,
  // The GPU-accelerated renderer could not be created. Rendering cannot
  // proceed; the event loop still runs but produces no visual output.
  kRendererUnavailable,
  // A renderer was created, but a subsequent rendering-setup step — scale,
  // blend mode, texture creation, or texture upload — failed. This is a
  // real defect (not a headless-CI skip) and must surface as a test
  // failure.  Distinct from kRendererUnavailable so the PASS regex for
  // display-requiring tests can accept only the genuine-no-GPU outcome.
  kRenderingSetupFailed,
  // The build was configured with -DGRAPHSCORE_BUILD_WRITER=OFF, so no
  // windowing backend was compiled in.
  kBackendNotCompiledIn,
};

struct WindowOptions {
  std::string title  = "GraphScore Writer";
  int         width  = 1280;
  int         height = 800;
  // High-DPI backing surfaces on displays that provide them.
  bool high_dpi = true;
  // Off for automated tests, which must not block on a user closing a
  // window; the shell creates the window, pumps pending events once, and
  // returns.
  bool run_event_loop = true;
};

// Result of a window session. `error` is kNone only when a real native
// window was created.
struct ShellResult {
  ShellError  error = ShellError::kNone;
  std::string message;

  [[nodiscard]] bool ok() const { return error == ShellError::kNone; }
};

class WriterShell {
 public:
  WriterShell();

  // Declared and defined out of line, not defaulted here. Impl is incomplete
  // at this point, and std::unique_ptr's deleter requires a complete type at
  // the point the destructor is instantiated — defaulting in-class would not
  // compile. performance-trivially-destructible sees only the translation
  // unit where Impl is complete and cannot know that.
  // NOLINTNEXTLINE(performance-trivially-destructible)
  ~WriterShell();

  WriterShell(const WriterShell&)            = delete;
  WriterShell& operator=(const WriterShell&) = delete;
  WriterShell(WriterShell&&) noexcept;
  WriterShell& operator=(WriterShell&&) noexcept;

  // Create the native window and, if options.run_event_loop is set, run the
  // event loop until the window is closed. Returns kBackendUnavailable
  // rather than aborting when no display is present.
  ShellResult open_window(const WindowOptions& options);

  // The name of the windowing backend actually in use ("cocoa", "x11",
  // "wayland", "windows"), or an empty view before a window is opened.
  [[nodiscard]] std::string_view backend_name() const;

  // Whether this build has a windowing backend compiled in at all.
  [[nodiscard]] static bool backend_compiled_in();

  // Register an input handler. The shell calls it from the main-thread
  // event loop during open_window(). Pass nullptr to unregister. The
  // handler must outlive the open_window() call; the shell stores a raw
  // pointer and never owns the handler.
  //
  // Thread affinity: must be called from the main thread only (the thread
  // that owns the event loop). No internal synchronisation is provided.
  void set_input_handler(InputHandler* handler);

  // Set the highlight rectangles the next frame should draw on top of the
  // notation. These are in layout (notation) coordinates; the shell
  // applies no additional transform before rendering. Pass an empty vector
  // to clear the highlight.
  //
  // Thread affinity: must be called from the main thread only. The shell
  // reads the vector inside the event loop without synchronisation.
  void set_highlight_rects(std::vector<NotationRect> rects);

  // Set the rasterised notation surface the shell should render behind the
  // highlight rectangles every frame. The surface is uploaded to a GPU
  // texture once and re-rendered each frame without re-rasterisation.
  // Pass a canonical empty surface (width==0 && height==0 && rgba.empty())
  // to render no notation.
  //
  // Both dimensions must be exactly zero (with an empty rgba buffer) or
  // both positive and ≤16384; the rgba buffer must be exactly
  // width*height*4 bytes.  A (0,0) surface with a non-empty rgba buffer is
  // rejected.  An invalid surface or an SDL texture-creation/upload failure
  // returns kRenderingSetupFailed.  In a writer-OFF build this validates
  // dimensions (the same rules apply) and returns an ok ShellResult for a
  // valid surface.
  //
  // Thread affinity: must be called from the main thread, before
  // open_window() or between frames.
  ShellResult set_notation_surface(RasterSurface surface);

  // Test-only: inject a synthetic pointer event that calls the registered
  // InputHandler with logical-coordinate PointerEvent values, without
  // exercising any SDL conversion.  This is the headless seam; it does NOT
  // route through SDL_ConvertEventToRenderCoordinates.  Does nothing when
  // no handler is registered.
  //
  // `kind` selects the handler method (0=press, 1=move, 2=release,
  // 3=cancel); unknown values are a no-op.
  void dispatch_test_pointer_event(std::uint8_t kind, PointerEvent event);

  // Test-only: inject a synthetic pointer event through the actual
  // production SDL event-conversion path, including
  // SDL_ConvertEventToRenderCoordinates when a renderer exists.  The event
  // coordinates are in pixel space; the production path converts them to
  // logical notation coordinates before calling the handler.  Does nothing
  // when no handler is registered.  In a writer-OFF build this is a no-op.
  //
  // `kind` selects the handler method (0=press, 1=move, 2=release,
  // 3=cancel); unknown values are a no-op.
  void dispatch_sdl_test_pointer_event(std::uint8_t kind, PointerEvent event);

  // Test-only: override the DPI scale factor the headless test seam
  // dispatch_test_pointer_event applies to incoming coordinates.
  // Default 0.0 (pass through unmodified).  In a writer-ON build with an
  // open renderer this also calls SDL_SetRenderScale so production
  // SDL_ConvertEventToRenderCoordinates coordinates stay consistent;
  // without a renderer only the headless seam is affected.
  // Set to 0.0 to restore production DPI behaviour.
  void set_test_dpi_scale(double scale);

  // Test-only: return a copy of the last highlight-rect vector handed to
  // set_highlight_rects(), so the test can observe highlight-delivery
  // outcomes against the actual shell state.
  //
  // Thread affinity: must be called from the main thread only; the shell
  // reads highlight_rects inside the event loop without synchronisation.
  [[nodiscard]] std::vector<NotationRect> test_snapshot_highlight_rects() const;

  // Test-only: read back a single pixel from the current notation texture
  // at (x, y).  Returns the RGBA byte values (offset 0 = R, 1 = G, 2 = B,
  // 3 = A) or std::nullopt when no texture or renderer is available or the
  // readback fails.
  //
  // Requires GRAPHSCORE_HAVE_SDL3 (the writer-ON configuration); in a
  // writer-OFF build this always returns std::nullopt.
  //
  // Thread affinity: must be called from the main thread only.
  [[nodiscard]] std::optional<std::array<std::uint8_t, 4>>
  test_read_notation_pixel(std::uint32_t x, std::uint32_t y);

  // Test-only: notation-texture lifetime statistics. Created and destroyed
  // are cumulative totals; alive is computed by the caller as
  // created - destroyed.  Returns zero counts in a writer-OFF build.
  //
  // Thread affinity: must be called from the main thread only.
  struct NotationTextureStats {
    std::uint64_t created   = 0;
    std::uint64_t destroyed = 0;
  };

  [[nodiscard]] NotationTextureStats test_notation_texture_stats() const;

  // Test-only: a handle to notation-texture lifetime counters that
  // survives WriterShell destruction.  Acquire the handle before
  // destroying the shell; the handle's snapshot() reflects counter
  // updates made by Impl::~Impl after the shell is gone.
  //
  // Thread affinity: main-thread-only.
  class TextureStatsHandle {
   public:
    TextureStatsHandle() = default;

    struct Counters {
      std::uint64_t created   = 0;
      std::uint64_t destroyed = 0;
    };

    [[nodiscard]] Counters snapshot() const {
      if (!counters_) {
        return {};
      }
      return *counters_;
    }

   private:
    friend class WriterShell;
    std::shared_ptr<Counters> counters_;
  };

  [[nodiscard]] TextureStatsHandle test_acquire_texture_stats_handle() const;

  // Test-only: when set to true, the next call to set_notation_surface will
  // pass validation but then fail as though the SDL texture upload itself
  // failed, without destroying the current surface or texture.  Resets to
  // false after one consumed failure.  Only has effect in a writer-ON build
  // with an active renderer; a no-op otherwise.
  void set_test_force_texture_failure(bool force);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace graphscore
