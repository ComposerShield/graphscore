# SPDX-License-Identifier: Apache-2.0

add_library(graphscore_plugin_scanner_client STATIC
  plugin_scanner_client.cpp
)

target_compile_features(graphscore_plugin_scanner_client PRIVATE cxx_std_23)
graphscore_apply_warnings(graphscore_plugin_scanner_client)
graphscore_apply_const_correctness(graphscore_plugin_scanner_client)

target_include_directories(graphscore_plugin_scanner_client
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(graphscore_plugin_scanner_client
  PUBLIC
    graphscore_core
    graphscore_domain
  PRIVATE
    graphscore_writer_shell
)
