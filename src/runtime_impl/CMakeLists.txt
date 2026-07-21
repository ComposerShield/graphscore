# SPDX-License-Identifier: Apache-2.0

add_library(graphscore_runtime_impl STATIC
  runtime_impl.cpp
)

target_compile_features(graphscore_runtime_impl PRIVATE cxx_std_23)
graphscore_apply_warnings(graphscore_runtime_impl)
graphscore_apply_const_correctness(graphscore_runtime_impl)

target_include_directories(graphscore_runtime_impl
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(graphscore_runtime_impl
  PUBLIC
    graphscore_core
    graphscore_c_abi
    graphscore_scheduler
    graphscore_loader
)
