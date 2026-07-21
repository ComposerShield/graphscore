# SPDX-License-Identifier: Apache-2.0

add_library(graphscore_rendering STATIC
  rendering.cpp
)

target_compile_features(graphscore_rendering PRIVATE cxx_std_23)
graphscore_apply_warnings(graphscore_rendering)

target_include_directories(graphscore_rendering
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(graphscore_rendering
  PUBLIC
    graphscore_notation
    graphscore_core
)
