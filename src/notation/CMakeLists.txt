# SPDX-License-Identifier: Apache-2.0

add_library(graphscore_notation STATIC
  notation_cache.cpp
  notation_edit_audition.cpp
  notation_engraving.cpp
  notation_engraving_rhythm.cpp
  notation_hit_resolution.cpp
  notation_index.cpp
  notation_layout.cpp
  notation_measure_math.cpp
  notation_measure_selection.cpp
  notation_note_entry.cpp
  notation_preview.cpp
  notation_range_selection.cpp
  notation_selection.cpp
  notation_staff_step.cpp
  notation_types.cpp
)

target_compile_features(graphscore_notation PRIVATE cxx_std_23)
graphscore_apply_warnings(graphscore_notation)

target_include_directories(graphscore_notation
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(graphscore_notation
  PUBLIC
    graphscore_domain
    graphscore_core
)
