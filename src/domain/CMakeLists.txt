# SPDX-License-Identifier: Apache-2.0

add_library(graphscore_domain STATIC
  domain.cpp
)

target_compile_features(graphscore_domain PRIVATE cxx_std_23)
graphscore_apply_warnings(graphscore_domain)
graphscore_apply_const_correctness(graphscore_domain)

target_include_directories(graphscore_domain
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(graphscore_domain PUBLIC graphscore_core)
