# SPDX-License-Identifier: Apache-2.0

add_library(graphscore_c_abi INTERFACE)

target_include_directories(graphscore_c_abi
  INTERFACE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
