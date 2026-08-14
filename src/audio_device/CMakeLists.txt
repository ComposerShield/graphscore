# SPDX-License-Identifier: Apache-2.0

add_library(graphscore_audio_device STATIC
  audio_device.cpp
)

target_compile_features(graphscore_audio_device PRIVATE cxx_std_23)
graphscore_apply_warnings(graphscore_audio_device)

target_include_directories(graphscore_audio_device
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(graphscore_audio_device PUBLIC graphscore_core)
