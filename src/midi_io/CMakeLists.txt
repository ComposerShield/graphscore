# SPDX-License-Identifier: Apache-2.0

add_library(graphscore_midi_io STATIC
  midi_io.cpp
)

target_compile_features(graphscore_midi_io PRIVATE cxx_std_23)
graphscore_apply_warnings(graphscore_midi_io)

target_include_directories(graphscore_midi_io
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(graphscore_midi_io PUBLIC graphscore_core)
