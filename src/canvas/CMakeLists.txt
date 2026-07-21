# SPDX-License-Identifier: Apache-2.0

add_library(graphscore_canvas STATIC
  canvas.cpp
)

target_compile_features(graphscore_canvas PRIVATE cxx_std_23)
graphscore_apply_warnings(graphscore_canvas)
graphscore_apply_const_correctness(graphscore_canvas)

target_include_directories(graphscore_canvas
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(graphscore_canvas
  PUBLIC
    graphscore_domain
    graphscore_notation
    graphscore_core
    graphscore_accessibility
)
