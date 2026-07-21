# SPDX-License-Identifier: Apache-2.0

add_library(graphscore_accessibility STATIC
  accessibility.cpp
)

target_compile_features(graphscore_accessibility PRIVATE cxx_std_23)
graphscore_apply_warnings(graphscore_accessibility)
graphscore_apply_const_correctness(graphscore_accessibility)

target_include_directories(graphscore_accessibility
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(graphscore_accessibility
  PUBLIC
    graphscore_domain
    graphscore_notation
    graphscore_core
)
