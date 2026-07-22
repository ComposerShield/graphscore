# SPDX-License-Identifier: Apache-2.0

add_executable(gs_c_consumer
  test_c_consumer.c
)

set_target_properties(gs_c_consumer PROPERTIES
  C_STANDARD 99
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF
)

target_include_directories(gs_c_consumer
  PRIVATE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(gs_c_consumer PRIVATE graphscore_runtime)

add_test(NAME gs_c_consumer_compile_smoke
  COMMAND gs_c_consumer)

# On Windows the shared library must be on PATH at test time.
if (WIN32)
  set_tests_properties(gs_c_consumer_compile_smoke PROPERTIES
    ENVIRONMENT_MODIFICATION
      "PATH=path_list_prepend:$<TARGET_FILE_DIR:graphscore_runtime>"
    TIMEOUT 10
  )
endif()
