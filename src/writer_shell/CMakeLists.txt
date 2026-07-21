# SPDX-License-Identifier: Apache-2.0

add_library(graphscore_writer_shell STATIC
  writer_shell.cpp
)

target_compile_features(graphscore_writer_shell PRIVATE cxx_std_23)
graphscore_apply_warnings(graphscore_writer_shell)
graphscore_apply_const_correctness(graphscore_writer_shell)

target_include_directories(graphscore_writer_shell
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(graphscore_writer_shell
  PUBLIC
    graphscore_core
    graphscore_canvas
    graphscore_notation
    graphscore_rendering
    graphscore_accessibility
)

# SDL3 is PRIVATE and stays PRIVATE: it is named only in writer_shell.cpp,
# never in a public header (ADR 0003 §2.2). The static SDL3 target is used so
# that the writer ships one executable with no SDL runtime to locate.
if (GRAPHSCORE_BUILD_WRITER)
  target_compile_definitions(graphscore_writer_shell
    PRIVATE GRAPHSCORE_HAVE_SDL3)

  target_link_libraries(graphscore_writer_shell PRIVATE SDL3::SDL3-static)
endif()
