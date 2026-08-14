# SPDX-License-Identifier: Apache-2.0

add_library(graphscore_compiler STATIC
  compiler.cpp
)

target_compile_features(graphscore_compiler PRIVATE cxx_std_23)
graphscore_apply_warnings(graphscore_compiler)

target_include_directories(graphscore_compiler
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(graphscore_compiler
  PUBLIC
    graphscore_domain
    graphscore_cooked_format
)
