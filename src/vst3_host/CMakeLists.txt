# SPDX-License-Identifier: Apache-2.0

add_library(graphscore_vst3_host STATIC
  vst3_host.cpp
)

target_compile_features(graphscore_vst3_host PRIVATE cxx_std_23)
graphscore_apply_warnings(graphscore_vst3_host)
graphscore_apply_const_correctness(graphscore_vst3_host)

target_include_directories(graphscore_vst3_host
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(graphscore_vst3_host
  PUBLIC
    graphscore_domain
    graphscore_audio_device
    graphscore_core
)
