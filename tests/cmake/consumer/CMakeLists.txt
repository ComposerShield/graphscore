# SPDX-License-Identifier: Apache-2.0
#
# Standalone consumer project (ADR 0003 §7.8). Configured against an install
# tree of GraphScore Runtime and nothing else — it has no access to the
# GraphScore source tree, no add_subdirectory of the main project, and no
# knowledge of any writer target.
#
# Driven by tests/cmake/run_consumer_test.cmake; not part of the main build.

cmake_minimum_required(VERSION 3.25)

project(GraphScoreRuntimeConsumer
  DESCRIPTION "Out-of-tree consumer of the installed GraphScore Runtime"
  LANGUAGES C CXX
)

find_package(GraphScoreRuntime REQUIRED)

# --- C++ consumer ---------------------------------------------------------

add_executable(consumer_cxx consumer_test.cpp)

set_target_properties(consumer_cxx PROPERTIES
  CXX_STANDARD 23
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
)

target_link_libraries(consumer_cxx PRIVATE graphscore::runtime)

# --- C consumer -----------------------------------------------------------
#
# The same package must be consumable from a project with no C++ at all,
# which is what a plain C game-engine host looks like.

add_executable(consumer_c consumer_test.c)

set_target_properties(consumer_c PROPERTIES
  C_STANDARD 99
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF
)

target_link_libraries(consumer_c PRIVATE graphscore::runtime)

# --- the writer must not be reachable from the installed package ----------
#
# ADR 0003 §7.8 step 3: importing the runtime package must not bring any
# writer target into scope. If any of these existed, a consumer could link
# the writer's SDL3/VST3/rendering dependencies into a shipped game.

foreach (writer_target
  graphscore::writer_app graphscore::writer_shell graphscore::writer_audio
  graphscore::canvas graphscore::rendering graphscore::notation
  graphscore::domain graphscore::persistence graphscore::compiler
  graphscore::vst3_host graphscore::audio_device graphscore::midi_io
  graphscore::accessibility graphscore::accessibility_platform
  graphscore::plugin_scanner_client)

  if (TARGET ${writer_target})
    message(FATAL_ERROR
      "The installed GraphScoreRuntime package exposes the writer target "
      "'${writer_target}'. Runtime consumers must see only "
      "graphscore::runtime and its clean closure (ADR 0003 §3.1, §7.8).")
  endif()
endforeach()

enable_testing()
add_test(NAME consumer_cxx COMMAND consumer_cxx)
add_test(NAME consumer_c COMMAND consumer_c)

# On Windows the shared library must be on PATH at test time (there is
# no rpath).  graphscore::runtime is an imported target whose location
# the installed GraphScoreRuntimeConfig.cmake already knows.
if (WIN32)
  set_tests_properties(consumer_cxx consumer_c PROPERTIES
    ENVIRONMENT_MODIFICATION
      "PATH=path_list_prepend:$<TARGET_FILE_DIR:graphscore::runtime>"
  )
endif()
