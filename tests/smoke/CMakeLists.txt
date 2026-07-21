# SPDX-License-Identifier: Apache-2.0
#
# Temporary Milestone 01 Phase 2 verification target: proves the compiler
# gate, warning flags, and GTest FetchContent wiring work end to end before
# Phase 3 defines the real graphscore_<component>_test targets. Removed once
# Phase 3 lands.

add_executable(graphscore_foundation_smoke_test foundation_smoke_test.cpp)
target_compile_features(graphscore_foundation_smoke_test PRIVATE cxx_std_23)
graphscore_apply_warnings(graphscore_foundation_smoke_test)
target_link_libraries(graphscore_foundation_smoke_test PRIVATE GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(graphscore_foundation_smoke_test)
