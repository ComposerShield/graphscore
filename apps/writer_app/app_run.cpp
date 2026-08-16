// SPDX-License-Identifier: Apache-2.0

#include "app_run.hpp"

#include "app_project.hpp"
#include "app_shell_report.hpp"
#include "canvas_gesture_handler.hpp"
#include "selection_tool_handler.hpp"

#include <graphscore/domain/graphscore_domain.hpp>
#include <graphscore/notation/graphscore_notation.hpp>
#include <graphscore/rendering/graphscore_rendering.hpp>
#include <graphscore/writer_shell/graphscore_writer_shell.hpp>

#include <cmath>
#include <cstdio>
#include <string>
#include <utility>

namespace graphscore::writer_app {
// ---- normal run ------------------------------------------------------------

namespace {

// Rasterizes `layout`'s commands with `font` and publishes the resulting
// surface to `shell`. Returns kRenderingSetupFailed when rasterization fails,
// otherwise the shell's own set_notation_surface result. Shared by run()'s
// startup rasterization and the handler's post-mutation surface publisher, so
// the two can never drift on surface dimensions or raster options.
[[nodiscard]] graphscore::ShellResult publish_notation_surface(
    const graphscore::NotationLayout& layout,
    const graphscore::BravuraFont& font, graphscore::WriterShell* shell,
    const graphscore::RasterOptions& base_options) {
  graphscore::RasterOptions opts = base_options;
  opts.width                     = static_cast<std::uint32_t>(
                   std::ceil(layout.bounds.x + layout.bounds.width)) +
               16U;
  opts.height = static_cast<std::uint32_t>(
                    std::ceil(layout.bounds.y + layout.bounds.height)) +
                16U;
  auto raster = graphscore::rasterize_notation(layout.commands, font, opts);
  if (!raster || !raster.surface.has_value()) {
    return graphscore::ShellResult{
        graphscore::ShellError::kRenderingSetupFailed,
        "graphscore_writer_app: failed to rasterise notation"};
  }
  return shell->set_notation_surface(std::move(*raster.surface));
}

// Load the production Bravura font. Returns nullopt on failure; the
// diagnostic is already printed to stderr by the rendering layer.
[[nodiscard]] std::optional<graphscore::BravuraFontLoadResult>
load_font_or_nullopt() {
#ifdef GRAPHSCORE_BRAVURA_FONT_PATH
  auto loaded = graphscore::load_bravura_font(GRAPHSCORE_BRAVURA_FONT_PATH);
  if (!loaded) {
    std::fprintf(stderr,
                 "graphscore_writer_app: failed to load Bravura font "
                 "from " GRAPHSCORE_BRAVURA_FONT_PATH "\n");
  }
  return loaded;
#else
  std::fprintf(stderr,
               "graphscore_writer_app: GRAPHSCORE_BRAVURA_FONT_PATH not "
               "compiled in — writer-OFF build?\n");
  return std::nullopt;
#endif
}

}  // namespace

int run(bool smoke_test) {
  // Writer-OFF: no rendering backend is compiled in.  Skip font loading,
  // project construction, and rasterisation and go straight to the bare
  // open_window path so the smoke test reaches the established
  // GRAPHSCORE_BUILD_WRITER=OFF outcome.
  if (!graphscore::WriterShell::backend_compiled_in()) {
    graphscore::WriterShell   shell;
    graphscore::WindowOptions options;
    options.run_event_loop               = !smoke_test;
    const graphscore::ShellResult result = shell.open_window(options);
    return report(result, smoke_test);
  }

  auto font_loaded = load_font_or_nullopt();
  if (!font_loaded.has_value()) {
    return 1;
  }

  auto default_project = build_default_project(*font_loaded->font);
  if (!default_project.has_value()) {
    std::fprintf(stderr,
                 "graphscore_writer_app: failed to build default "
                 "project for selection tool\n");
    return 1;
  }

  // Rasterise the notation to an RGBA8 surface so the shell can upload it
  // to a GPU texture and compose the highlight on top.
  graphscore::RasterOptions raster_opts;
  raster_opts.pixels_per_unit = 1.0;
  raster_opts.origin          = {0.0, 0.0};
  raster_opts.color           = {0, 0, 0, 255};
  raster_opts.opacity         = 255;

  graphscore::WriterShell       shell;
  const graphscore::ShellResult surf_result = publish_notation_surface(
      default_project->layout, *font_loaded->font, &shell, raster_opts);
  if (!surf_result.ok()) {
    std::fprintf(stderr,
                 "graphscore_writer_app: set_notation_surface failed: %s\n",
                 surf_result.message.c_str());
    return 1;
  }

  SelectionToolHandler handler(std::move(default_project->project),
                               std::move(default_project->layout), &shell);

  handler.set_metrics(font_loaded->font.get());
  // Seed the retained layout cache from the startup layout before any edit,
  // so the first notehead move refreshes only the affected system rather
  // than full-resetting the cold cache (M5-phase-8 incremental engraving).
  handler.warm_layout_cache();
  handler.set_surface_publisher(
      [font = font_loaded->font.get(), &shell,
       raster_opts](const graphscore::NotationLayout& layout) {
        return publish_notation_surface(layout, *font, &shell, raster_opts);
      });
  handler.set_active_tool(graphscore::ActiveTool::kSelection);

  graphscore::WindowOptions options;
  options.run_event_loop = !smoke_test;

  // M6-phase-5: the trackpad gesture controller sits in front of the
  // notation handler — gesture events pan/zoom the canvas viewport, every
  // non-gesture event is inverse-mapped to notation coordinates and
  // forwarded to the selection tool. The gesture handler's transform is the
  // authoritative viewport: the shell renders the notation surface through it
  // and the pinch focal fallback (window center) is derived from the actual
  // logical window size the shell reports on creation and resize.
  CanvasGestureHandler gestures;
  gestures.set_delegate(&handler);
  shell.set_input_handler(&gestures);
  shell.set_viewport_transform(&gestures.transform());

  const graphscore::ShellResult result = shell.open_window(options);
  const int                     status = report(result, smoke_test);

  // Deregister before handler destruction so the shell does not hold a
  // dangling pointer during its own destruction.
  shell.set_viewport_transform(nullptr);
  shell.set_input_handler(nullptr);

  if (result.ok()) {
    std::printf("graphscore_writer_app: window opened via '%s' backend\n",
                std::string(shell.backend_name()).c_str());
  }

  return status;
}

}  // namespace graphscore::writer_app
