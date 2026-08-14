# SPDX-License-Identifier: Apache-2.0

add_library(graphscore_writer_audio STATIC
  writer_audio.cpp
)

target_compile_features(graphscore_writer_audio PRIVATE cxx_std_23)
graphscore_apply_warnings(graphscore_writer_audio)

target_include_directories(graphscore_writer_audio
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(graphscore_writer_audio
  PUBLIC
    graphscore_core
    graphscore_domain
    graphscore_scheduler
    graphscore_audio_device
    graphscore_vst3_host
    graphscore_compiler
)
