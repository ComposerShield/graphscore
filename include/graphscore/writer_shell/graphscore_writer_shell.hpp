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

// Platform-neutral physical key identity. M5-phase-27's action table
// (docs/plan/05-notation-editor-action-table.md §4) binds every positional,
// navigation, editing, digit, and symbol key by its PHYSICAL identity — the
// SDL scancode (USB HID position), which is layout-independent — while
// binding the letter mnemonics by LOGICAL identity (see LogicalKey below).
//
// Every member is therefore translated from SDL's physical scancode in
// writer_shell.cpp, never from the layout-dependent logical keycode. For
// arrows and Home/End that is exact: they are not remapped by keyboard
// layout the way character keys are (compare a QWERTY vs. AZERTY 'A' key,
// which sit at different physical scancodes for the same intended letter).
// kMinus and kEquals are symbol keys, so physical identification binds the
// two keys that carry `-`/`=` on a US layout wherever a different layout
// puts those characters. The top-row digits `0`-`9`, numpad keys, and the
// editing keys (Return/Escape/Tab/Space) are likewise positional.
//
// The one physical letter code retained below, kR, exists only so that the
// historical physical R scancode still round-trips through the headless
// seam; the app binds the R *action* through LogicalKey::kR (the logical
// letter), never through this physical code.
enum class KeyCode : std::uint8_t {
  kUnknown,
  kLeft,
  kRight,
  kUp,
  kDown,
  kHome,
  kEnd,
  kMinus,
  kEquals,
  kBackspace,
  kDelete,
  kR,
  kDigit1,
  kDigit2,
  kDigit3,
  kDigit4,
  kDigit5,
  kDigit6,
  kDigit7,
  kDigit8,
  // Top-row digits 9 and 0 (physical, unbound no-ops in the notation
  // context — the action table's explicit remainder names them so a test
  // can enumerate the full (chord, key) space).
  kDigit9,
  kDigit0,
  // Editing/whitespace keys the action table names as explicit no-ops in
  // the notation context; delivered so a future focus context can interpret
  // them.
  kReturn,
  kEscape,
  kTab,
  kSpace,
  // Numpad key family (physical, Num Lock-independent). M5-phase-27 binds
  // KP_1..KP_7 to durations, KP_0 to the step-entry rest, and KP_DECIMAL to
  // the dot cycle.
  kNumPad1,
  kNumPad2,
  kNumPad3,
  kNumPad4,
  kNumPad5,
  kNumPad6,
  kNumPad7,
  kNumPad0,
  kNumPadDecimal,
};

// Platform-neutral logical key identity, for the letter-mnemonic bindings
// only (docs/plan/05-notation-editor-action-table.md §4): `R` (rest), `N`
// (step entry), `A`-`G` (pitch), `C`/`X`/`V` (copy/cut/paste), `Z`
// (undo/redo), `K` (command palette). A mnemonic must mean the same
// character on every layout, so these are translated from SDL's logical
// keycode (the character the active layout produces), never from the
// physical scancode: on AZERTY the physical key that is `R` on QWERTY is
// labelled `T`, and a physical binding would force the composer to hunt for
// the US position of a mnemonic.
//
// Only the letters that are bound are represented; every other logical key
// is kUnknown.
enum class LogicalKey : std::uint8_t {
  kUnknown,
  kA,
  kB,
  kC,
  kD,
  kE,
  kF,
  kG,
  kN,
  kR,
  kX,
  kV,
  kZ,
  kK,
};

// Platform-neutral key modifiers, translated from SDL3's combined
// left/right modifier masks in writer_shell.cpp.
//
// `meta` is the Command key on macOS and the Windows/Super key elsewhere.
// This struct deliberately does not collapse it into a "Primary" concept:
// M5-phase-24 defines Primary as Command on macOS and Control on
// Windows/Linux, and that mapping is a product decision for the action
// table, not something the shell should bake in.
struct KeyModifiers {
  bool               shift                                 = false;
  bool               control                               = false;
  bool               alt                                   = false;
  bool               meta                                  = false;
  [[nodiscard]] bool operator==(const KeyModifiers&) const = default;
};

// A single key-press event, platform-neutral. There is no release
// counterpart — actions are driven entirely by presses. The two identity
// axes follow docs/plan/05-notation-editor-action-table.md §4: `code` is
// the physical scancode-derived key (positional/digit/symbol keys),
// `logical` is the layout-mapped letter (letter mnemonics), and `repeat`
// flags an OS auto-repeat press so the app can honour the action table's
// repeat-safe vs repeat-once policies (§6).
struct KeyEvent {
  KeyCode      code = KeyCode::kUnknown;
  KeyModifiers modifiers;
  // True when the OS reports this press as an auto-repeat, not the initial
  // physical key-down. The shell surfaces it verbatim; the app decides,
  // per binding, whether to re-fire or suppress (§6).
  bool       repeat  = false;
  LogicalKey logical = LogicalKey::kUnknown;
};

// A single composed text-input event, platform-neutral: the UTF-8 text the
// active keyboard layout produced (SDL_EVENT_TEXT_INPUT). Delivered on a
// separate channel from KeyEvent, whose `code`/`logical` axes name a key's
// PHYSICAL/LOGICAL identity for the mnemonic bindings. That separation is
// what lets a text-consumer focus context (the command palette filter,
// docs/plan/05-notation-editor-action-table.md §5) accept arbitrary
// printable text -- letters beyond the bound mnemonics, digits, symbols,
// shifted characters, and non-US-layout compositions -- without any of it
// being mistaken for a notation action.
struct TextInputEvent {
  std::string text;
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

  // Called on a key press (including auto-repeat; see the dispatch site in
  // writer_shell.cpp). Deliberately not pure virtual, unlike the pointer
  // methods above: a handler that only responds to pointer input should
  // not be forced to write an empty override, and this keeps key-event
  // delivery independent of any app-layer handler, which lands in
  // M5-phase-19b-iii. Default: ignore the key.
  virtual void on_key_press(KeyEvent /*event*/) {}

  // Called on a composed text-input event: the UTF-8 text the active
  // keyboard layout produced. Deliberately not pure virtual, like
  // on_key_press above, so a handler that only responds to pointer/key
  // input is not forced to write an empty override. Default: ignore.
  virtual void on_text_input(TextInputEvent /*event*/) {}
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

  // Enable or disable platform text composition for the current GraphScore
  // window. The app uses this when a text-consuming focus context (currently
  // the command palette) opens or closes. The requested state is retained
  // until a window exists; writer-OFF and windowless shells safely retain the
  // state without calling a platform API. Passing false is always safe.
  //
  // SDL/platform types remain confined to writer_shell.cpp.
  void set_text_input_active(bool active);

  // Test seam for the GraphScore-owned activation state driven through
  // set_text_input_active(). This is the state production window creation and
  // teardown apply to SDL_StartTextInput/SDL_StopTextInput.
  [[nodiscard]] bool test_text_input_active() const noexcept;

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

  // Test-only: inject a synthetic key event that calls the registered
  // InputHandler's on_key_press() directly with a platform-neutral
  // KeyEvent, without exercising any SDL conversion.  This is the headless
  // seam; unlike the pointer headless seam there is no DPI scaling to
  // apply — key events carry no coordinates.  Does nothing when no handler
  // is registered.  Compiles and behaves identically in writer-ON and
  // writer-OFF builds, and lives in the shared (non-#ifdef) region of
  // writer_shell.cpp for that reason, unlike the SDL translation helpers in
  // writer_shell.cpp.
  void dispatch_test_key_event(KeyEvent event);

  // Test-only: inject a synthetic key event through the actual production
  // SDL event-conversion path (the physical-scancode and modifier-mask
  // translation in writer_shell.cpp), so a test can assert what the
  // handler actually receives rather than testing a reverse mapping
  // written only for the test.  Builds a real SDL_EVENT_KEY_DOWN and
  // routes it through the production dispatch_sdl_event.  Does nothing
  // when no handler is registered.  In a writer-OFF build this is a no-op.
  //
  // Parameters are plain integers, not an SDL type, so no SDL type reaches
  // this header: `sdl_scancode` is the raw SDL_Scancode value,
  // `sdl_key_modifiers` is the raw SDL_Keymod bitmask, and `sdl_keycode` is
  // the raw SDL_Keycode value (the layout-mapped logical key; 0 means "none
  // recorded", which the translation reports as LogicalKey::kUnknown).
  void dispatch_sdl_test_key_event(std::uint32_t sdl_scancode,
                                   std::uint16_t sdl_key_modifiers,
                                   std::uint32_t sdl_keycode = 0);

  // Test-only: inject a synthetic text-input event that calls the registered
  // InputHandler's on_text_input() directly with a platform-neutral
  // TextInputEvent, without exercising any SDL conversion. This is the
  // headless seam for composed text; like the key headless seam it applies
  // no coordinate/DPI transform and compiles identically in writer-ON and
  // writer-OFF builds.
  void dispatch_test_text_input(TextInputEvent event);

  // Test-only: inject a synthetic text-input event through the actual
  // production SDL event-conversion path, building a real
  // SDL_EVENT_TEXT_INPUT and routing it through dispatch_sdl_event, so a
  // test asserts what the handler actually receives. `text` is the UTF-8
  // string the composed event carries; it is truncated to SDL's own
  // SDL_TEXTINPUTEVENT_TEXT_SIZE buffer bound. In a writer-OFF build this is
  // a no-op.
  void dispatch_sdl_test_text_input(std::string_view text);

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

  // Test-only: return a copy of the last notation surface handed to
  // set_notation_surface() (the surface the shell renders behind the
  // highlight rects). A headless test observes whether a mutation refresh
  // actually re-published different pixels, in both writer-ON and writer-OFF
  // builds: set_notation_surface validates and stores the surface in either
  // configuration. Returns the canonical empty surface (width==0) before any
  // surface has been set.
  //
  // Thread affinity: must be called from the main thread only.
  [[nodiscard]] std::optional<RasterSurface> test_snapshot_notation_surface()
      const;

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
