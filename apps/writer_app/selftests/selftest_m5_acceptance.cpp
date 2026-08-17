// SPDX-License-Identifier: Apache-2.0

#include "selftests.hpp"

#include "../app_project.hpp"

#include <graphscore/rendering/graphscore_rendering.hpp>

#include <cstdio>

namespace graphscore::writer_app {
namespace {

using SelfTest = int (*)();

[[nodiscard]] int run_in_memory_workflows() {
  constexpr SelfTest kWorkflows[]{
      selection_tool_test,
#if defined(GRAPHSCORE_BRAVURA_FONT_PATH)
      selection_tool_shell_test,
#endif
      key_events_test,
#if defined(GRAPHSCORE_BRAVURA_FONT_PATH)
      key_events_shell_test,
#endif
      key_selection_test,
      notehead_move_test,
      accidental_step_test,
      notehead_delete_test,
      convert_to_rest_test,
      staff_step_test,
      interval_entry_test,
#if defined(GRAPHSCORE_BRAVURA_FONT_PATH)
      interval_entry_shell_test,
#endif
      step_entry_test,
      notation_accessibility_test,
      clipboard_test,
      command_palette_test,
      action_table_test,
      measure_edit_test,
      pickdown_edit_test,
      tuplet_edit_test,
      event_style_edit_test,
      marking_style_edit_test,
  };

  for (const SelfTest workflow : kWorkflows) {
    if (workflow() != 0) {
      return 1;
    }
  }

  // The individual workflow tests above cover the edit lifecycle. This final
  // pass verifies that the resulting toolkit-neutral layout is consumable by
  // the production rasterizer as well.
  SelfTestMetrics metrics;
  const auto      project = build_default_project(metrics);
  if (!project.has_value() || project->layout.commands.empty()) {
    std::fprintf(stderr, "m5-acceptance-test: layout setup failed\n");
    return 1;
  }

#if defined(GRAPHSCORE_BRAVURA_FONT_PATH)
  if (rendering_backend_available()) {
    auto font = load_bravura_font(GRAPHSCORE_BRAVURA_FONT_PATH);
    if (!font || font.font == nullptr) {
      std::fprintf(stderr, "m5-acceptance-test: font load failed\n");
      return 1;
    }
    const RasterResult raster = rasterize_notation(
        project->layout.commands, *font.font, RasterOptions{640, 240});
    if (!raster || raster.surface->rgba.empty() || !raster.errors.empty()) {
      std::fprintf(stderr, "m5-acceptance-test: rasterization failed\n");
      return 1;
    }
  }
#endif

  return 0;
}

}  // namespace

int m5_acceptance_test() {
  const int result = run_in_memory_workflows();
  if (result == 0) {
    std::printf("m5-acceptance-test: ok (in-memory; save deferred to M3)\n");
  }
  return result;
}

}  // namespace graphscore::writer_app
