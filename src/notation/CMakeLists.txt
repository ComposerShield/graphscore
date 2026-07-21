# SPDX-License-Identifier: Apache-2.0

add_library(graphscore_notation STATIC
  notation.cpp
)

target_compile_features(graphscore_notation PRIVATE cxx_std_23)
graphscore_apply_warnings(graphscore_notation)
graphscore_apply_const_correctness(graphscore_notation)

target_include_directories(graphscore_notation
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(graphscore_notation
  PUBLIC
    graphscore_domain
    graphscore_core
)
