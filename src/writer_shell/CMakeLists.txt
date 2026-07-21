# SPDX-License-Identifier: Apache-2.0

add_library(graphscore_writer_shell STATIC
  writer_shell.cpp
)

target_compile_features(graphscore_writer_shell PRIVATE cxx_std_23)
graphscore_apply_warnings(graphscore_writer_shell)

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
