# SPDX-License-Identifier: Apache-2.0

add_executable(graphscore_writer_app
  graphscore_writer_app.cpp
)

target_compile_features(graphscore_writer_app PRIVATE cxx_std_23)
graphscore_apply_warnings(graphscore_writer_app)
graphscore_apply_const_correctness(graphscore_writer_app)

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

# --- plugin scanner --------------------------------------------------------

add_executable(graphscore_plugin_scanner
  graphscore_plugin_scanner.cpp
)

target_compile_features(graphscore_plugin_scanner PRIVATE cxx_std_23)
graphscore_apply_warnings(graphscore_plugin_scanner)
graphscore_apply_const_correctness(graphscore_plugin_scanner)

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
