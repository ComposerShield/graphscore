# SPDX-License-Identifier: Apache-2.0

add_library(graphscore_scheduler STATIC
  scheduler.cpp
)

target_compile_features(graphscore_scheduler PRIVATE cxx_std_23)
graphscore_apply_warnings(graphscore_scheduler)
graphscore_apply_const_correctness(graphscore_scheduler)

target_include_directories(graphscore_scheduler
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(graphscore_scheduler
  PUBLIC
    graphscore_core
    graphscore_cooked_format
)
