# SPDX-License-Identifier: Apache-2.0

add_library(graphscore_cooked_format STATIC
  cooked_format.cpp
)

target_compile_features(graphscore_cooked_format PRIVATE cxx_std_23)
graphscore_apply_warnings(graphscore_cooked_format)
graphscore_apply_const_correctness(graphscore_cooked_format)

target_include_directories(graphscore_cooked_format
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(graphscore_cooked_format PUBLIC graphscore_core)
