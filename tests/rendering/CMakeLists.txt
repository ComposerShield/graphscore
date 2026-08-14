# SPDX-License-Identifier: Apache-2.0

add_executable(graphscore_rendering_test
  rendering_test.cpp
)

target_compile_features(graphscore_rendering_test PRIVATE cxx_std_23)
graphscore_apply_warnings(graphscore_rendering_test)

target_include_directories(graphscore_rendering_test
  PRIVATE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(graphscore_rendering_test
  PRIVATE
    GTest::gtest_main
    graphscore_rendering
)

# The pinned Bravura font path (ADR 0002 §A4) is only known when the writer
# dependencies were fetched. -DGRAPHSCORE_BUILD_WRITER=OFF builds this test
# with the macro undefined; the test itself skips its real assertions in
# that configuration (graphscore::RenderingBackendAvailable() reports which
# case applies), matching the tests/writer_shell precedent for a
# writer-only smoke test.
if (GRAPHSCORE_BUILD_WRITER)
  target_compile_definitions(graphscore_rendering_test PRIVATE
    "GRAPHSCORE_BRAVURA_FONT_PATH=\"${GRAPHSCORE_BRAVURA_FONT_PATH}\"")
endif()

gtest_discover_tests(graphscore_rendering_test)
