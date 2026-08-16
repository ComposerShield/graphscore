// SPDX-License-Identifier: Apache-2.0

#include "selftests.hpp"

#include "../canvas_gesture_handler.hpp"
#include "../selection_tool_handler.hpp"

#include <graphscore/canvas/graphscore_canvas.hpp>
#include <graphscore/writer_shell/graphscore_writer_shell.hpp>

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace graphscore::writer_app {
// ---- trackpad gesture tests (M6-phase-5) ----------------------------------
//
// Two concerns, both headless (no window, no renderer):
//
//  1. The production SDL translation seam: SDL_EVENT_MOUSE_WHEEL /
//     SDL_EVENT_PINCH_UPDATE / SDL_EVENT_FINGER_* build real SDL events and
//     route through dispatch_sdl_event, so the test asserts what the handler
//     actually receives rather than a reverse mapping written for the test.
//     Without a renderer, SDL_ConvertEventToRenderCoordinates is skipped and
//     the wheel delta, pinch scale/focus, and finger normalized coordinates
//     pass through verbatim (float→double, subpixel preserved).
//
//  2. The app-level CanvasGestureHandler wiring: scroll pans (translation
//     only), pinch zooms exactly once, and finger up/canceled end tracking
//     without synthesizing viewport motion.
//
// The focal-priority cascade, centroid/window-center fallbacks, and
// non-finite rejection are toolkit-neutral controller behaviour covered by
// graphscore_canvas_test; this file does not duplicate them.

namespace {
// Records every gesture callback, so the SDL seam can be asserted against
// the exact translated values. The pointer/cancel methods are no-ops.
class RecordingGestureHandler final : public graphscore::InputHandler {
 public:
  void on_pointer_press(graphscore::PointerEvent /*event*/) override {}

  void on_pointer_move(graphscore::PointerEvent /*event*/) override {}

  void on_pointer_release(graphscore::PointerEvent /*event*/) override {}

  void on_cancel() override {}

  void on_scroll(graphscore::ScrollDelta delta) override {
    scrolls.push_back(delta);
  }

  void on_pinch(graphscore::PinchUpdate update) override {
    pinches.push_back(update);
  }

  void on_finger_down(graphscore::FingerContact finger) override {
    finger_downs.push_back(finger);
  }

  void on_finger_move(graphscore::FingerContact finger) override {
    finger_moves.push_back(finger);
  }

  void on_finger_up(std::uint64_t finger_id) override {
    finger_ups.push_back(finger_id);
  }

  void on_finger_canceled(std::uint64_t finger_id) override {
    finger_cancels.push_back(finger_id);
  }

  std::vector<graphscore::ScrollDelta>   scrolls;
  std::vector<graphscore::PinchUpdate>   pinches;
  std::vector<graphscore::FingerContact> finger_downs;
  std::vector<graphscore::FingerContact> finger_moves;
  std::vector<std::uint64_t>             finger_ups;
  std::vector<std::uint64_t>             finger_cancels;
};

// Records every non-gesture callback the CanvasGestureHandler should forward
// verbatim to its delegate, so M5 pointer/key/text/cancel behavior is proven
// intact through the wrapper.
class RecordingDelegateHandler final : public graphscore::InputHandler {
 public:
  void on_pointer_press(graphscore::PointerEvent event) override {
    presses.push_back(event);
  }

  void on_pointer_move(graphscore::PointerEvent event) override {
    moves.push_back(event);
  }

  void on_pointer_release(graphscore::PointerEvent event) override {
    releases.push_back(event);
  }

  void on_cancel() override { ++cancels; }

  void on_key_press(graphscore::KeyEvent event) override {
    keys.push_back(event);
  }

  void on_text_input(graphscore::TextInputEvent event) override {
    texts.push_back(event.text);
  }

  std::vector<graphscore::PointerEvent> presses;
  std::vector<graphscore::PointerEvent> moves;
  std::vector<graphscore::PointerEvent> releases;
  int                                   cancels = 0;
  std::vector<graphscore::KeyEvent>     keys;
  std::vector<std::string>              texts;
};
}  // namespace

int trackpad_gesture_test() {
  graphscore::WriterShell shell;

  // --- test: no handler registered -> every gesture seam is a no-op ------
  shell.dispatch_sdl_test_scroll_event(1.0, 1.0, 0);
  shell.dispatch_sdl_test_pinch_event(1.5, 100.0, 100.0);
  shell.dispatch_sdl_test_finger_event(0, 1, 0.25, 0.25);

  // --- test: the SDL scroll seam delivers the subpixel delta verbatim and
  //     negates on the FLIPPED direction --------------------------------
  {
    RecordingGestureHandler handler;
    shell.set_input_handler(&handler);

    // Values exactly representable as float so the float->double round trip
    // in the production path is exact.
    shell.dispatch_sdl_test_scroll_event(1.25, -2.75, 0);
    if (handler.scrolls.size() != 1 ||
        handler.scrolls.back() != graphscore::ScrollDelta{1.25, -2.75}) {
      std::fprintf(stderr,
                   "trackpad-gesture-test: scroll delta did not translate "
                   "verbatim through the SDL seam\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    shell.dispatch_sdl_test_scroll_event(1.5, -2.5, 1);
    if (handler.scrolls.size() != 2 ||
        handler.scrolls.back() != graphscore::ScrollDelta{-1.5, 2.5}) {
      std::fprintf(stderr,
                   "trackpad-gesture-test: FLIPPED scroll direction was not "
                   "negated through the SDL seam\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test: the SDL pinch seam delivers scale and focal, and reports the
  //     absent-focus (-1) case as no focal point ------------------------
  {
    RecordingGestureHandler handler;
    shell.set_input_handler(&handler);

    shell.dispatch_sdl_test_pinch_event(1.5, 320.0, 240.0);
    if (handler.pinches.size() != 1 || handler.pinches.back().scale != 1.5 ||
        !handler.pinches.back().focal_point.has_value() ||
        *handler.pinches.back().focal_point !=
            graphscore::ViewportPosition{320.0, 240.0}) {
      std::fprintf(stderr,
                   "trackpad-gesture-test: pinch scale/focus did not "
                   "translate through the SDL seam\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    shell.dispatch_sdl_test_pinch_event(1.5, -1.0, -1.0);
    if (handler.pinches.size() != 2 || handler.pinches.back().scale != 1.5 ||
        handler.pinches.back().focal_point.has_value()) {
      std::fprintf(stderr,
                   "trackpad-gesture-test: absent pinch focus (-1) did not "
                   "translate to nullopt\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test: the SDL finger seams deliver down/move/up/canceled -----------
  {
    RecordingGestureHandler handler;
    shell.set_input_handler(&handler);

    shell.dispatch_sdl_test_finger_event(0, 7, 0.25, 0.75);
    shell.dispatch_sdl_test_finger_event(1, 7, 0.5, 0.5);
    shell.dispatch_sdl_test_finger_event(2, 7, 0.5, 0.5);
    shell.dispatch_sdl_test_finger_event(3, 7, 0.5, 0.5);
    const graphscore::FingerContact expected_down{7, {0.25, 0.75}};
    const graphscore::FingerContact expected_move{7, {0.5, 0.5}};
    if (handler.finger_downs.size() != 1 ||
        handler.finger_downs.back() != expected_down ||
        handler.finger_moves.size() != 1 ||
        handler.finger_moves.back() != expected_move ||
        handler.finger_ups.size() != 1 || handler.finger_ups.back() != 7 ||
        handler.finger_cancels.size() != 1 ||
        handler.finger_cancels.back() != 7) {
      std::fprintf(stderr,
                   "trackpad-gesture-test: finger events did not translate "
                   "through the SDL seam\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test: CanvasGestureHandler wiring -- scroll pans without zoom ------
  {
    CanvasGestureHandler handler;
    handler.set_window_center({640.0, 360.0});
    shell.set_input_handler(&handler);

    shell.dispatch_sdl_test_scroll_event(5.0, -10.0, 0);
    if (handler.transform().zoom() != 1.0 ||
        handler.transform().viewport_anchor() !=
            graphscore::ViewportPosition{5.0, -10.0}) {
      std::fprintf(stderr,
                   "trackpad-gesture-test: scroll did not pan (translation "
                   "only) through the CanvasGestureHandler\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    // Pinch at a focal point: zoom changes exactly once and the world point
    // under the focal viewport position is preserved.
    const auto focal_world = handler.transform().to_world({100.0, 50.0});
    shell.dispatch_sdl_test_pinch_event(2.0, 100.0, 50.0);
    if (handler.transform().zoom() != 2.0 || !focal_world.has_value() ||
        handler.transform().to_world({100.0, 50.0}) != focal_world) {
      std::fprintf(stderr,
                   "trackpad-gesture-test: pinch did not zoom once with the "
                   "focal point preserved\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    // Finger lifecycle: two downs track a centroid, up/canceled end tracking
    // one finger at a time, and none of it moves the viewport.
    const auto zoom_before     = handler.transform().zoom();
    const auto world_before    = handler.transform().world_anchor();
    const auto viewport_before = handler.transform().viewport_anchor();
    shell.dispatch_sdl_test_finger_event(0, 1, 0.2, 0.2);
    shell.dispatch_sdl_test_finger_event(0, 2, 0.4, 0.4);
    if (handler.controller().tracked_finger_count() != 2) {
      std::fprintf(stderr,
                   "trackpad-gesture-test: two finger downs did not track "
                   "both fingers\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.dispatch_sdl_test_finger_event(2, 1, 0.2, 0.2);
    if (handler.controller().tracked_finger_count() != 1) {
      std::fprintf(stderr,
                   "trackpad-gesture-test: finger up did not end tracking "
                   "for that finger\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.dispatch_sdl_test_finger_event(3, 2, 0.4, 0.4);
    if (handler.controller().tracked_finger_count() != 0 ||
        handler.transform().zoom() != zoom_before ||
        handler.transform().world_anchor() != world_before ||
        handler.transform().viewport_anchor() != viewport_before) {
      std::fprintf(stderr,
                   "trackpad-gesture-test: finger cancel did not end "
                   "tracking, or finger events moved the viewport\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    shell.set_input_handler(nullptr);
  }

  // --- test: every non-gesture callback forwards to the delegate -----------
  {
    CanvasGestureHandler     handler;
    RecordingDelegateHandler delegate;
    handler.set_delegate(&delegate);

    handler.on_pointer_press({10.0, 20.0, graphscore::PointerButton::kPrimary});
    handler.on_pointer_move({11.0, 21.0, graphscore::PointerButton::kPrimary});
    handler.on_pointer_release(
        {12.0, 22.0, graphscore::PointerButton::kPrimary});
    handler.on_cancel();
    graphscore::KeyEvent key;
    key.code = graphscore::KeyCode::kBackspace;
    handler.on_key_press(key);
    handler.on_text_input(graphscore::TextInputEvent{"abc"});

    if (delegate.presses.size() != 1 || delegate.presses.back().x != 10.0 ||
        delegate.presses.back().y != 20.0 ||
        delegate.presses.back().button != graphscore::PointerButton::kPrimary ||
        delegate.moves.size() != 1 || delegate.moves.back().x != 11.0 ||
        delegate.moves.back().y != 21.0 || delegate.releases.size() != 1 ||
        delegate.releases.back().x != 12.0 ||
        delegate.releases.back().y != 22.0 || delegate.cancels != 1 ||
        delegate.keys.size() != 1 ||
        delegate.keys.back().code != graphscore::KeyCode::kBackspace ||
        delegate.texts.size() != 1 || delegate.texts.back() != "abc") {
      std::fprintf(stderr,
                   "trackpad-gesture-test: a non-gesture callback was not "
                   "forwarded to the delegate\n");
      return 1;
    }
  }

  // --- test: viewport resize refreshes the window-center focal fallback ----
  {
    CanvasGestureHandler handler;
    shell.set_input_handler(&handler);

    shell.dispatch_test_viewport_resize(800.0, 600.0);
    if (handler.controller().window_center() !=
        graphscore::ViewportPosition{400.0, 300.0}) {
      std::fprintf(stderr,
                   "trackpad-gesture-test: viewport resize did not update the "
                   "window-center fallback\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    // A focal-less pinch (no fingers tracked) zooms around the refreshed
    // window center, preserving the world point under it.
    const auto center_world = handler.transform().to_world({400.0, 300.0});
    shell.dispatch_sdl_test_pinch_event(2.0, -1.0, -1.0);
    if (!center_world.has_value() || handler.transform().zoom() != 2.0 ||
        handler.transform().to_world({400.0, 300.0}) != center_world) {
      std::fprintf(stderr,
                   "trackpad-gesture-test: focal-less pinch did not zoom "
                   "around the resized window center\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    // A later resize updates the center again without disturbing pan/zoom.
    const double zoom_before = handler.transform().zoom();
    shell.dispatch_test_viewport_resize(640.0, 480.0);
    const graphscore::ViewportPosition resized_center{320.0, 240.0};
    if (handler.controller().window_center() != resized_center ||
        handler.transform().zoom() != zoom_before) {
      std::fprintf(stderr,
                   "trackpad-gesture-test: a later resize disturbed the "
                   "viewport or failed to refresh the fallback\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test: a failed pinch focal conversion drops the event ----------------
  {
    RecordingGestureHandler handler;
    shell.set_input_handler(&handler);
    shell.set_test_force_pinch_conversion_failure(true);

    shell.dispatch_sdl_test_pinch_event(1.5, 100.0, 100.0);
    if (!handler.pinches.empty()) {
      std::fprintf(stderr,
                   "trackpad-gesture-test: pinch with focal was dispatched "
                   "despite a failed focal conversion\n");
      shell.set_test_force_pinch_conversion_failure(false);
      shell.set_input_handler(nullptr);
      return 1;
    }

    // A focal-less pinch has nothing to convert and is still delivered.
    shell.dispatch_sdl_test_pinch_event(1.5, -1.0, -1.0);
    if (handler.pinches.size() != 1) {
      std::fprintf(stderr,
                   "trackpad-gesture-test: focal-less pinch was dropped by the "
                   "conversion-failure flag\n");
      shell.set_test_force_pinch_conversion_failure(false);
      shell.set_input_handler(nullptr);
      return 1;
    }

    // With the failure cleared the same focal pinch is delivered again.
    shell.set_test_force_pinch_conversion_failure(false);
    shell.dispatch_sdl_test_pinch_event(1.5, 100.0, 100.0);
    if (handler.pinches.size() != 2) {
      std::fprintf(stderr,
                   "trackpad-gesture-test: pinch was not delivered once the "
                   "conversion failure was cleared\n");
      shell.set_input_handler(nullptr);
      return 1;
    }
    shell.set_input_handler(nullptr);
  }

  // --- test: a gesture changes production presentation and pointer mapping --
  {
    graphscore::WriterShell gesture_shell;

    // A known notation surface makes the render destination observable.
    graphscore::RasterSurface surface;
    surface.width  = 100;
    surface.height = 50;
    surface.rgba.assign(100 * 50 * 4, 0);
    if (!gesture_shell.set_notation_surface(surface).ok()) {
      std::fprintf(stderr,
                   "trackpad-gesture-test: could not register the notation "
                   "surface for the presentation test\n");
      return 1;
    }

    CanvasGestureHandler     gestures;
    RecordingDelegateHandler delegate;
    gestures.set_delegate(&delegate);
    gesture_shell.set_input_handler(&gestures);
    gesture_shell.set_viewport_transform(&gestures.transform());

    // Identity: the notation surface draws at its native rect.
    auto dest = gesture_shell.test_notation_destination();
    if (!dest.has_value() ||
        *dest != graphscore::NotationRect{0.0, 0.0, 100.0, 50.0}) {
      std::fprintf(stderr,
                   "trackpad-gesture-test: identity transform did not draw the "
                   "notation surface at its native rect\n");
      gesture_shell.set_viewport_transform(nullptr);
      gesture_shell.set_input_handler(nullptr);
      return 1;
    }

    // Pinch 2x around the focal point (50,25), which the resize-derived
    // window center also targets.
    gesture_shell.dispatch_test_viewport_resize(100.0, 50.0);
    gesture_shell.dispatch_sdl_test_pinch_event(2.0, 50.0, 25.0);

    // Presentation: the surface now draws scaled 2x around (50,25).
    dest = gesture_shell.test_notation_destination();
    if (!dest.has_value() ||
        *dest != graphscore::NotationRect{-50.0, -25.0, 200.0, 100.0}) {
      std::fprintf(stderr,
                   "trackpad-gesture-test: pinch did not pan/zoom the "
                   "production notation destination\n");
      gesture_shell.set_viewport_transform(nullptr);
      gesture_shell.set_input_handler(nullptr);
      return 1;
    }

    // Pointer mapping: a pointer at viewport (0,0) inverse-maps to world
    // (25,12.5) before reaching the delegate.
    gesture_shell.dispatch_test_pointer_event(
        0, {0.0, 0.0, graphscore::PointerButton::kPrimary});
    if (delegate.presses.size() != 1 || delegate.presses.back().x != 25.0 ||
        delegate.presses.back().y != 12.5 ||
        delegate.presses.back().button != graphscore::PointerButton::kPrimary) {
      std::fprintf(stderr,
                   "trackpad-gesture-test: pointer event was not "
                   "inverse-mapped to notation coordinates\n");
      gesture_shell.set_viewport_transform(nullptr);
      gesture_shell.set_input_handler(nullptr);
      return 1;
    }

    gesture_shell.set_viewport_transform(nullptr);
    gesture_shell.set_input_handler(nullptr);
  }

  // --- test: no callback after unregistration -----------------------------
  {
    RecordingGestureHandler handler;
    shell.set_input_handler(&handler);
    shell.set_input_handler(nullptr);
    shell.dispatch_sdl_test_scroll_event(1.0, 1.0, 0);
    shell.dispatch_sdl_test_pinch_event(1.5, 100.0, 100.0);
    shell.dispatch_sdl_test_finger_event(0, 9, 0.25, 0.25);
    if (!handler.scrolls.empty() || !handler.pinches.empty() ||
        !handler.finger_downs.empty()) {
      std::fprintf(stderr,
                   "trackpad-gesture-test: gesture callback fired after "
                   "unregistration\n");
      return 1;
    }
  }

  std::printf("trackpad-gesture-test: ok\n");
  return 0;
}

}  // namespace graphscore::writer_app
