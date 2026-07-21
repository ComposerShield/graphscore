# SPDX-License-Identifier: Apache-2.0
#
# Negative half of the consumer test (ADR 0003 §7.8 step 3). This project is
# expected to FAIL to configure: it imports the installed GraphScore Runtime
# package and then tries to link a writer target.
#
# The driver (tests/cmake/run_consumer_test.cmake) treats a successful
# configure here as the test failure. If this project ever configures
# cleanly, the runtime package has started exporting writer targets and a
# game shipping the runtime would pull in SDL3, the VST3 SDK, and the
# rendering backend.

cmake_minimum_required(VERSION 3.25)

project(GraphScoreWriterLeakProbe
  DESCRIPTION "Must not configure: writer targets are not consumable"
  LANGUAGES CXX
)

find_package(GraphScoreRuntime REQUIRED)

add_executable(writer_leak_probe writer_leak_probe.cpp)

# graphscore::writer_app is not in the runtime export set, so this must be an
# unresolvable target name. CMake reports it as an error at generate time
# because the name contains `::`.
target_link_libraries(writer_leak_probe PRIVATE graphscore::writer_app)
