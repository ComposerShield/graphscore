# SPDX-License-Identifier: Apache-2.0

add_library(graphscore_loader STATIC
  loader.cpp
)

target_compile_features(graphscore_loader PRIVATE cxx_std_23)
graphscore_apply_warnings(graphscore_loader)

target_include_directories(graphscore_loader
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(graphscore_loader
  PUBLIC
    graphscore_core
    graphscore_c_abi
    graphscore_cooked_format
)
