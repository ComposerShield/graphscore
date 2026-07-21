# SPDX-License-Identifier: Apache-2.0

add_library(graphscore_persistence STATIC
  persistence.cpp
)

target_compile_features(graphscore_persistence PRIVATE cxx_std_23)
graphscore_apply_warnings(graphscore_persistence)

target_include_directories(graphscore_persistence
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(graphscore_persistence
  PUBLIC
    graphscore_domain
    graphscore_cooked_format
    graphscore_compiler
)
