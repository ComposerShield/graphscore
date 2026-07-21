# SPDX-License-Identifier: Apache-2.0

add_library(graphscore_runtime SHARED
  runtime.cpp
)

target_compile_features(graphscore_runtime PRIVATE cxx_std_23)
graphscore_apply_warnings(graphscore_runtime)
graphscore_apply_const_correctness(graphscore_runtime)

target_compile_definitions(graphscore_runtime PRIVATE GS_BUILDING_RUNTIME)

target_include_directories(graphscore_runtime
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(graphscore_runtime
  PUBLIC
    graphscore_c_abi
  PRIVATE
    graphscore_runtime_impl
)
