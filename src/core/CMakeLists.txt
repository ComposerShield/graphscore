# SPDX-License-Identifier: Apache-2.0

add_library(graphscore_core STATIC
  core.cpp
  tempo_curve.cpp
  uuid.cpp
)

target_compile_features(graphscore_core PRIVATE cxx_std_23)
graphscore_apply_warnings(graphscore_core)
graphscore_apply_const_correctness(graphscore_core)

target_include_directories(graphscore_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
