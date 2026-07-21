# SPDX-License-Identifier: Apache-2.0

add_library(graphscore_accessibility_platform STATIC
  accessibility_platform.cpp
)

target_compile_features(graphscore_accessibility_platform PRIVATE cxx_std_23)
graphscore_apply_warnings(graphscore_accessibility_platform)
graphscore_apply_const_correctness(graphscore_accessibility_platform)

target_include_directories(graphscore_accessibility_platform
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(graphscore_accessibility_platform
  PUBLIC
    graphscore_accessibility
)
