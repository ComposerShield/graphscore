// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <graphscore/accessibility/graphscore_accessibility.hpp>
#include <graphscore/canvas/graphscore_canvas.hpp>
#include <graphscore/notation/graphscore_notation.hpp>
#include <graphscore/rendering/graphscore_rendering.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace graphscore {

// Platform shell for the GraphScore Writer. Owns the native window and the
// event loop.
//
// Every platform type — SDL3's included — is confined to writer_shell.cpp
// (ADR 0003 §2.2). Nothing in this header names, includes, or forward-
// declares an SDL type, so no consumer of graphscore_writer_shell acquires a
// dependency on the windowing backend.

enum class ShellError : std::uint8_t {
  kNone = 0,
  // The windowing backend could not be initialised. On a headless machine
  // this is the expected outcome, not a defect.
  kBackendUnavailable,
  kWindowCreationFailed,
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

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace graphscore
