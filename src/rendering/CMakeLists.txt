# SPDX-License-Identifier: Apache-2.0

add_library(graphscore_rendering STATIC
  rendering.cpp
)

target_compile_features(graphscore_rendering PRIVATE cxx_std_23)
graphscore_apply_warnings(graphscore_rendering)

target_include_directories(graphscore_rendering
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(graphscore_rendering
  PUBLIC
    graphscore_notation
    graphscore_core
)

# FreeType, HarfBuzz, and ThorVG are PRIVATE and stay PRIVATE: named only in
# rendering.cpp, never in a public header (ADR 0003 §2.2). Writer-only,
# exactly like writer_shell's SDL3 edge: with -DGRAPHSCORE_BUILD_WRITER=OFF,
# graphscore_rendering still builds, with no rendering backend linked at all
# (ADR 0002 §A7.1).
if (GRAPHSCORE_BUILD_WRITER)
  target_compile_definitions(graphscore_rendering PRIVATE
    GRAPHSCORE_HAVE_RENDERING_BACKEND)

  target_link_libraries(graphscore_rendering PRIVATE
    freetype
    harfbuzz
    thorvg
  )
endif()
