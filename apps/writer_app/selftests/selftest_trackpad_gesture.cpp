// SPDX-License-Identifier: Apache-2.0

#include "selftests.hpp"

#include "../app_project.hpp"
#include "../canvas_gesture_handler.hpp"
#include "../selection_tool_handler.hpp"
#include "selftest_fixtures.hpp"
#include "selftest_support.hpp"

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

  void on_cancel() override { ++cancels; }

  void on_wheel(graphscore::WheelEvent event) override {
    wheels.push_back(event);
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

  std::vector<graphscore::WheelEvent>    wheels;
  std::vector<graphscore::PinchUpdate>   pinches;
  std::vector<graphscore::FingerContact> finger_downs;
  std::vector<graphscore::FingerContact> finger_moves;
  std::vector<std::uint64_t>             finger_ups;
  std::vector<std::uint64_t>             finger_cancels;
  int                                    cancels = 0;
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

  void on_wheel(graphscore::WheelEvent event) override {
    wheels.push_back(event);
  }

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
  std::vector<graphscore::WheelEvent>   wheels;
};

class FixedFocusProvider final : public graphscore::FocusPointProvider {
 public:
  std::optional<graphscore::WorldBounds> bounds;

  [[nodiscard]] std::optional<graphscore::WorldBounds> focused_world_bounds()
      const override {
    return bounds;
  }
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
    graphscore::KeyModifiers control;
    control.control = true;
    shell.dispatch_sdl_test_modifier_transition(control, true);
    shell.dispatch_sdl_test_scroll_event(1.25, -2.75, 0, 45.0, 55.0);
    shell.dispatch_sdl_test_modifier_transition({}, false);
    if (handler.wheels.size() != 1 ||
        handler.wheels.back().delta != (graphscore::ScrollDelta{1.25, -2.75}) ||
        handler.wheels.back().pointer !=
            (graphscore::ViewportPosition{45.0, 55.0}) ||
        !handler.wheels.back().modifiers.control) {
      std::fprintf(stderr,
                   "trackpad-gesture-test: scroll delta did not translate "
                   "verbatim through the SDL seam\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    shell.dispatch_sdl_test_scroll_event(1.5, -2.5, 1);
    if (handler.wheels.size() != 2 ||
        handler.wheels.back().delta != graphscore::ScrollDelta{-1.5, 2.5}) {
      std::fprintf(stderr,
                   "trackpad-gesture-test: FLIPPED scroll direction was not "
                   "negated through the SDL seam\n");
      shell.set_input_handler(nullptr);
      return 1;
    }

    // The aggregate state remains set when one side of a combined modifier
    // is released while the other is still held, then focus loss clears it.
    shell.dispatch_sdl_test_modifier_transition(control, true);
    shell.dispatch_sdl_test_modifier_transition(control, false);
    shell.dispatch_sdl_test_scroll_event(0.0, 1.0, 0);
    shell.dispatch_sdl_test_focus_loss();
    shell.dispatch_sdl_test_scroll_event(0.0, 1.0, 0);
    if (handler.wheels.size() != 4 || !handler.wheels[2].modifiers.control ||
        handler.wheels[3].modifiers.control || handler.cancels != 1) {
      std::fprintf(stderr,
                   "trackpad-gesture-test: ordered combined modifier or "
                   "focus-loss reset was not preserved\n");
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

  // --- test: Primary+Shift+R belongs to the focused canvas ---------------
  {
    for (const PrimaryModifier primary :
         {PrimaryModifier::kMeta, PrimaryModifier::kControl}) {
      CanvasGestureHandler     handler(primary);
      RecordingDelegateHandler delegate;
      int                      resets = 0;
      handler.set_delegate(&delegate);
      handler.set_keyboard_focus(CanvasKeyboardFocus::kCanvas);
      handler.set_reset_selected_connector_handler([&resets] { ++resets; });

      graphscore::KeyEvent reset;
      reset.modifiers.shift = true;
      reset.logical         = graphscore::LogicalKey::kR;
      if (primary == PrimaryModifier::kMeta) {
        reset.modifiers.meta = true;
      } else {
        reset.modifiers.control = true;
      }
      handler.on_key_press(reset);
      reset.repeat = true;
      handler.on_key_press(reset);
      if (resets != 1 || !delegate.keys.empty()) {
        std::fprintf(stderr,
                     "trackpad-gesture-test: canvas Primary+Shift+R was not "
                     "consumed exactly once\n");
        return 1;
      }

      handler.set_keyboard_focus(CanvasKeyboardFocus::kNotation);
      reset.repeat = false;
      handler.on_key_press(reset);
      if (delegate.keys.size() != 1 ||
          delegate.keys.back().logical != graphscore::LogicalKey::kR) {
        std::fprintf(stderr,
                     "trackpad-gesture-test: notation focus stole canvas "
                     "Primary+Shift+R ownership\n");
        return 1;
      }
    }
  }

  // --- test: wheel policy normalizes Primary for both platform mappings ----
  for (const PrimaryModifier primary :
       {PrimaryModifier::kMeta, PrimaryModifier::kControl}) {
    CanvasGestureHandler     handler(primary);
    RecordingDelegateHandler delegate;
    handler.set_delegate(&delegate);

    graphscore::WheelEvent pan_event;
    pan_event.delta   = {1.25, -2.75};
    pan_event.pointer = {100.0, 50.0};
    handler.on_wheel(pan_event);
    if (handler.transform().viewport_anchor() !=
            (graphscore::ViewportPosition{1.25, -2.75}) ||
        handler.transform().zoom() != 1.0) {
      std::fprintf(stderr,
                   "trackpad-gesture-test: unmodified wheel did not pan both "
                   "high-resolution axes\n");
      return 1;
    }

    graphscore::WheelEvent zoom_event;
    zoom_event.delta.y = 2.0;
    zoom_event.pointer = {100.0, 50.0};
    if (primary == PrimaryModifier::kMeta) {
      zoom_event.modifiers.meta = true;
    } else {
      zoom_event.modifiers.control = true;
    }
    const auto focal_before = handler.transform().to_world(zoom_event.pointer);
    handler.on_wheel(zoom_event);
    if (!focal_before.has_value() || handler.transform().zoom() == 1.0 ||
        handler.transform().to_world(zoom_event.pointer) != focal_before) {
      std::fprintf(stderr,
                   "trackpad-gesture-test: Primary+wheel did not preserve the "
                   "current pointer focal point\n");
      return 1;
    }

    graphscore::WheelEvent other_modifier;
    other_modifier.modifiers.alt = true;
    handler.on_wheel(other_modifier);
    if (delegate.wheels.size() != 1) {
      std::fprintf(stderr,
                   "trackpad-gesture-test: unbound modified wheel was not "
                   "delegated\n");
      return 1;
    }
  }

  // --- test: middle drag is viewport-space and has complete lifecycle ------
  {
    CanvasGestureHandler     handler;
    RecordingDelegateHandler delegate;
    handler.set_delegate(&delegate);
    handler.on_pointer_press({10.0, 20.0, graphscore::PointerButton::kMiddle});
    handler.on_pointer_move({25.5, 12.0, graphscore::PointerButton::kUnknown});
    if (handler.transform().viewport_anchor() !=
            (graphscore::ViewportPosition{15.5, -8.0}) ||
        !delegate.presses.empty() || !delegate.moves.empty()) {
      std::fprintf(stderr,
                   "trackpad-gesture-test: middle drag was not consumed as a "
                   "1:1 viewport pan\n");
      return 1;
    }
    handler.on_pointer_release(
        {25.5, 12.0, graphscore::PointerButton::kMiddle});
    handler.on_pointer_move({30.0, 30.0, graphscore::PointerButton::kUnknown});
    if (delegate.moves.size() != 1 ||
        handler.transform().viewport_anchor() !=
            graphscore::ViewportPosition{15.5, -8.0}) {
      std::fprintf(stderr,
                   "trackpad-gesture-test: middle release left a stuck drag\n");
      return 1;
    }

    handler.on_pointer_press({0.0, 0.0, graphscore::PointerButton::kMiddle});
    handler.on_cancel();
    handler.on_pointer_move({5.0, 5.0, graphscore::PointerButton::kUnknown});
    if (delegate.cancels != 1 || delegate.moves.size() != 2) {
      std::fprintf(stderr,
                   "trackpad-gesture-test: cancel left a stuck middle drag or "
                   "was not delegated\n");
      return 1;
    }
  }

  // --- test: notation/palette focus wins through the real wrapper ----------
  {
    const SelfTestMetrics metrics;
    auto                  fixture = build_notehead_move_fixture(metrics);
    if (!fixture.has_value()) {
      std::fprintf(stderr,
                   "trackpad-gesture-test: keyboard routing fixture failed\n");
      return 1;
    }

    graphscore::WriterShell route_shell;
    SelectionToolHandler    notation(std::move(fixture->project),
                                     std::move(fixture->layout), &route_shell);
    notation.set_metrics(&metrics);
    notation.set_surface_publisher(
        [&route_shell](const graphscore::NotationLayout& layout) {
          return publish_headless_test_surface(layout, &route_shell);
        });
    notation.set_active_tool(graphscore::ActiveTool::kSelection);
    if (!select_noteheads(
            notation, {{fixture->node_id, fixture->track_id, fixture->stave_id,
                        voice_one(), fixture->first_note_id}})) {
      std::fprintf(
          stderr, "trackpad-gesture-test: keyboard routing selection failed\n");
      return 1;
    }

    CanvasGestureHandler wrapper;
    wrapper.set_delegate(&notation);
    route_shell.set_input_handler(&wrapper);
    const auto  canvas_before    = wrapper.transform().viewport_anchor();
    std::size_t expected_history = notation.test_undo_stack_size();
    for (const graphscore::KeyCode code :
         {graphscore::KeyCode::kUp, graphscore::KeyCode::kDown,
          graphscore::KeyCode::kEquals, graphscore::KeyCode::kMinus}) {
      route_shell.dispatch_test_key_event(plain_key(code));
      ++expected_history;
      if (notation.test_undo_stack_size() != expected_history ||
          wrapper.transform().viewport_anchor() != canvas_before ||
          wrapper.transform().zoom() != 1.0) {
        std::fprintf(stderr,
                     "trackpad-gesture-test: notation key was intercepted by "
                     "the canvas wrapper\n");
        route_shell.set_input_handler(nullptr);
        return 1;
      }
    }

    notation.toggle_command_palette();
    graphscore::KeyEvent palette_down = plain_key(graphscore::KeyCode::kDown);
    route_shell.dispatch_test_key_event(palette_down);
    if (notation.command_palette_selected_index() != 1) {
      std::fprintf(stderr,
                   "trackpad-gesture-test: palette Down did not reach palette "
                   "routing through the wrapper\n");
      route_shell.set_input_handler(nullptr);
      return 1;
    }
    route_shell.dispatch_test_key_event(plain_key(graphscore::KeyCode::kUp));
    route_shell.dispatch_test_key_event(
        plain_key(graphscore::KeyCode::kEquals));
    route_shell.dispatch_test_key_event(
        shift_key(graphscore::KeyCode::kEquals));
    if (notation.command_palette_selected_index() != 0 ||
        wrapper.transform().viewport_anchor() != canvas_before ||
        wrapper.transform().zoom() != 1.0) {
      std::fprintf(stderr,
                   "trackpad-gesture-test: palette key navigated the canvas "
                   "or bypassed palette routing\n");
      route_shell.set_input_handler(nullptr);
      return 1;
    }
    route_shell.set_input_handler(nullptr);
  }

  // --- test: keyboard pan/repeat and accessibility-focus zoom --------------
  {
    CanvasGestureHandler     handler;
    RecordingDelegateHandler delegate;
    FixedFocusProvider       focus;
    focus.bounds = graphscore::WorldBounds{{100.0, 200.0}, 40.0, 20.0};
    handler.set_delegate(&delegate);
    handler.set_focus_point_provider(&focus);
    handler.set_keyboard_focus(CanvasKeyboardFocus::kCanvas);
    handler.on_viewport_size_changed(800.0, 600.0);

    graphscore::KeyEvent left;
    left.code = graphscore::KeyCode::kLeft;
    handler.on_key_press(left);
    if (handler.transform().viewport_anchor() !=
        graphscore::ViewportPosition{graphscore::kKeyboardPanStep, 0.0}) {
      std::fprintf(stderr,
                   "trackpad-gesture-test: canvas Left did not pan left\n");
      return 1;
    }
    graphscore::KeyEvent right;
    right.code = graphscore::KeyCode::kRight;
    handler.on_key_press(right);
    if (handler.transform().viewport_anchor() !=
        graphscore::ViewportPosition{0.0, 0.0}) {
      std::fprintf(stderr,
                   "trackpad-gesture-test: canvas Right did not pan right\n");
      return 1;
    }
    graphscore::KeyEvent up;
    up.code = graphscore::KeyCode::kUp;
    handler.on_key_press(up);
    if (handler.transform().viewport_anchor() !=
        graphscore::ViewportPosition{0.0, graphscore::kKeyboardPanStep}) {
      std::fprintf(stderr, "trackpad-gesture-test: canvas Up did not pan up\n");
      return 1;
    }
    graphscore::KeyEvent down;
    down.code = graphscore::KeyCode::kDown;
    handler.on_key_press(down);
    if (handler.transform().viewport_anchor() !=
        graphscore::ViewportPosition{0.0, 0.0}) {
      std::fprintf(stderr,
                   "trackpad-gesture-test: canvas Down did not pan down\n");
      return 1;
    }
    right.repeat = true;
    handler.on_key_press(right);
    if (handler.transform().viewport_anchor() !=
        graphscore::ViewportPosition{-graphscore::kKeyboardPanStep, 0.0}) {
      std::fprintf(stderr,
                   "trackpad-gesture-test: repeated canvas arrow did not use "
                   "the fixed pan step\n");
      return 1;
    }

    const graphscore::GraphPosition focus_center{120.0, 210.0};
    const auto focus_viewport = handler.transform().to_viewport(focus_center);
    graphscore::KeyEvent zoom_in;
    zoom_in.code = graphscore::KeyCode::kEquals;
    handler.on_key_press(zoom_in);
    if (!focus_viewport.has_value() ||
        handler.transform().zoom() != graphscore::kKeyboardZoomStep ||
        handler.transform().to_viewport(focus_center) != focus_viewport) {
      std::fprintf(stderr,
                   "trackpad-gesture-test: keyboard zoom did not preserve the "
                   "focused semantic bounds center\n");
      return 1;
    }
    const double zoom_before_plus = handler.transform().zoom();
    zoom_in.modifiers.shift       = true;
    zoom_in.repeat                = true;
    handler.on_key_press(zoom_in);
    if (handler.transform().zoom() !=
        zoom_before_plus * graphscore::kKeyboardZoomStep) {
      std::fprintf(stderr,
                   "trackpad-gesture-test: repeated '+' did not apply the "
                   "keyboard zoom step\n");
      return 1;
    }

    graphscore::KeyEvent unbound;
    unbound.code = graphscore::KeyCode::kBackspace;
    handler.on_key_press(unbound);
    graphscore::KeyEvent modified_arrow;
    modified_arrow.code          = graphscore::KeyCode::kUp;
    modified_arrow.modifiers.alt = true;
    handler.on_key_press(modified_arrow);
    if (delegate.keys.size() != 2 || delegate.keys[0].code != unbound.code ||
        delegate.keys[1].code != modified_arrow.code) {
      std::fprintf(stderr,
                   "trackpad-gesture-test: non-canvas key behavior did not "
                   "delegate unchanged\n");
      return 1;
    }
  }

  // --- test: absent/invalid focus deterministically uses viewport center ----
  for (const auto bounds :
       {std::optional<graphscore::WorldBounds>{},
        std::optional<graphscore::WorldBounds>{
            graphscore::WorldBounds{{0.0, 0.0}, -1.0, 20.0}}}) {
    CanvasGestureHandler handler;
    FixedFocusProvider   focus;
    focus.bounds = bounds;
    handler.set_focus_point_provider(&focus);
    handler.set_keyboard_focus(CanvasKeyboardFocus::kCanvas);
    handler.on_viewport_size_changed(800.0, 600.0);
    constexpr graphscore::ViewportPosition kCenter{400.0, 300.0};
    const auto           center_before = handler.transform().to_world(kCenter);
    graphscore::KeyEvent zoom_out;
    zoom_out.code = graphscore::KeyCode::kMinus;
    handler.on_key_press(zoom_out);
    if (!center_before.has_value() ||
        handler.transform().to_world(kCenter) != center_before) {
      std::fprintf(stderr,
                   "trackpad-gesture-test: missing/invalid focus did not use "
                   "the viewport-center fallback\n");
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
    if (!handler.wheels.empty() || !handler.pinches.empty() ||
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
