# SPDX-License-Identifier: Apache-2.0

# CMAKE_INSTALL_DATAROOTDIR is used by the writer-only resource install rule
# below; cmake/RuntimePackage.cmake also include()s this, but only later
# (add_subdirectory(apps) runs before include(RuntimePackage) in the root
# CMakeLists.txt), so it is needed here too. GNUInstallDirs guards its own
# re-inclusion.
include(GNUInstallDirs)

add_executable(graphscore_writer_app
  graphscore_writer_app.cpp
  writer_app/app_project.cpp
  writer_app/app_run.cpp
  writer_app/app_shell_report.cpp
  writer_app/selection_tool_handler.cpp
  writer_app/selection_tool_handler_edits.cpp
  writer_app/selection_tool_handler_keys.cpp
  writer_app/selection_tool_handler_layout.cpp
  writer_app/selection_tool_handler_pointer.cpp
  writer_app/selection_tool_handler_range.cpp
  writer_app/selftests/selftest_accidental_step.cpp
  writer_app/selftests/selftest_convert_to_rest.cpp
  writer_app/selftests/selftest_fixtures.cpp
  writer_app/selftests/selftest_interval_entry.cpp
  writer_app/selftests/selftest_interval_entry_shell.cpp
  writer_app/selftests/selftest_key_events.cpp
  writer_app/selftests/selftest_key_selection.cpp
  writer_app/selftests/selftest_notehead_delete.cpp
  writer_app/selftests/selftest_notehead_move.cpp
  writer_app/selftests/selftest_notehead_move_rollback.cpp
  writer_app/selftests/selftest_selection_tool.cpp
  writer_app/selftests/selftest_selection_tool_shell.cpp
  writer_app/selftests/selftest_staff_step.cpp
  writer_app/selftests/selftest_support.cpp
)

target_compile_features(graphscore_writer_app PRIVATE cxx_std_23)
graphscore_apply_warnings(graphscore_writer_app)

target_include_directories(graphscore_writer_app
  PRIVATE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(graphscore_writer_app
  PRIVATE
    graphscore_writer_shell
    graphscore_writer_audio
    graphscore_plugin_scanner_client
    graphscore_canvas
    graphscore_rendering
    graphscore_notation
    graphscore_accessibility
    graphscore_accessibility_platform
    graphscore_persistence
    graphscore_compiler
    graphscore_domain
    graphscore_scheduler
    graphscore_core
)

# The pinned Bravura font path (ADR 0002 §A4) is only known when the writer
# dependencies were fetched. With -DGRAPHSCORE_BUILD_WRITER=OFF the macro is
# undefined; the app's headless test path uses SelfTestMetrics instead.
if (GRAPHSCORE_BUILD_WRITER)
  target_compile_definitions(graphscore_writer_app PRIVATE
    "GRAPHSCORE_BRAVURA_FONT_PATH=\"${GRAPHSCORE_BRAVURA_FONT_PATH}\"")
endif()

# --- writer-only resource install (ADR 0002 §A4, §5) -----------------------
#
# The Bravura SMuFL font, its own OFL text, and the full third-party notice
# set for everything graphscore_writer_app actually links (FreeType,
# HarfBuzz, ThorVG, SDL3, Bravura) install into the writer's own tree here.
# This is deliberately separate from cmake/RuntimePackage.cmake's
# licenses/GraphScore destination, which installs only GraphScore's own
# top-level LICENSE/NOTICE for the runtime package and must not also carry a
# writer/font dependency it does not ship (ADR 0002 §A4). A runtime-only
# build (GRAPHSCORE_BUILD_WRITER=OFF) ships none of this: none of these
# dependencies are fetched in that configuration (ADR 0002 §A7.1), and
# GRAPHSCORE_BRAVURA_FONT_PATH does not exist to install.
if (GRAPHSCORE_BUILD_WRITER)
  # RENAME requires a single-file install(FILES); GRAPHSCORE_BRAVURA_FONT_PATH
  # is host-determined on the BRAVURA_FONT_SRC offline-override path
  # (cmake/Bravura.cmake), so it cannot share the multi-file call below and
  # must be installed under its own fixed, versioned name (ADR 0002 §A4).
  install(FILES "${GRAPHSCORE_BRAVURA_FONT_PATH}"
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/graphscore/fonts"
    RENAME Bravura.otf
  )
  install(FILES "${CMAKE_SOURCE_DIR}/docs/licenses/Bravura-OFL.txt"
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/graphscore/fonts"
  )

  install(FILES
    "${CMAKE_SOURCE_DIR}/docs/licenses/Bravura-OFL.txt"
    "${CMAKE_SOURCE_DIR}/docs/licenses/FreeType-FTL.TXT"
    "${CMAKE_SOURCE_DIR}/docs/licenses/FreeType-zlib-license.txt"
    "${CMAKE_SOURCE_DIR}/docs/licenses/HarfBuzz-COPYING.txt"
    "${CMAKE_SOURCE_DIR}/docs/licenses/ThorVG-LICENSE.txt"
    "${CMAKE_SOURCE_DIR}/docs/licenses/SDL3-LICENSE.txt"
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/licenses/GraphScoreWriter"
  )

  # FTL §2's binary-distribution bullet requires an affirmative disclaimer
  # authored by the redistributor, not merely FTL.TXT's own retained text
  # (which discharges only the source-distribution bullet). NOTICE-writer.txt
  # is GraphScore's own statement carrying that disclaimer; installed as
  # plain "NOTICE" here, alongside this directory's six license files, the
  # same naming RuntimePackage.cmake's licenses/GraphScore/NOTICE already
  # establishes for the runtime package (docs/NOTICES.md §5).
  install(FILES "${CMAKE_SOURCE_DIR}/docs/NOTICE-writer.txt"
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/licenses/GraphScoreWriter"
    RENAME NOTICE
  )
endif()

# --- plugin scanner --------------------------------------------------------

add_executable(graphscore_plugin_scanner
  graphscore_plugin_scanner.cpp
)

target_compile_features(graphscore_plugin_scanner PRIVATE cxx_std_23)
graphscore_apply_warnings(graphscore_plugin_scanner)

target_include_directories(graphscore_plugin_scanner
  PRIVATE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(graphscore_plugin_scanner
  PUBLIC
    graphscore_core
  PRIVATE
    graphscore_vst3_host
)

# CMake 3.25-3.30 cannot both de-duplicate static libraries and guarantee that
# the first occurrence is retained. Preserve repeated direct-edge ordering on
# those versions while suppressing only the Apple linker's duplicate warning.
if (APPLE AND NOT GRAPHSCORE_HAS_ORDERED_STATIC_LIBRARY_DEDUPLICATION)
  include(CheckLinkerFlag)
  check_linker_flag(CXX "LINKER:-no_warn_duplicate_libraries"
    GRAPHSCORE_LINKER_SUPPORTS_NO_WARN_DUPLICATE_LIBRARIES)
  if (NOT GRAPHSCORE_LINKER_SUPPORTS_NO_WARN_DUPLICATE_LIBRARIES)
    message(FATAL_ERROR
      "CMake 3.25-3.30 requires an Apple linker with "
      "-no_warn_duplicate_libraries; upgrade CMake to 3.31 or newer")
  endif()

  target_link_options(graphscore_writer_app PRIVATE
    "LINKER:-no_warn_duplicate_libraries")
  target_link_options(graphscore_plugin_scanner PRIVATE
    "LINKER:-no_warn_duplicate_libraries")
endif()
