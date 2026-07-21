# SPDX-License-Identifier: Apache-2.0
#
# ADR 0003 §7 enforcement. Included from the root CMakeLists.txt *after*
# every subdirectory has been added, so that all targets exist and their link
# properties are readable.
#
# Two stages:
#
#   1. Configure time — dump the effective target graph to
#      ${CMAKE_BINARY_DIR}/graphscore_target_graph.cmake. CMake script mode
#      (`cmake -P`) cannot query targets, so the audits read this dump
#      instead of the live buildsystem.
#
#   2. Build time — the `audit_architecture` target runs the three CMake
#      audits (§7.1, §7.2) and the four Python audits (§7.3, §7.5, §7.6,
#      §7.7) against the dump and the source tree.
#
#      cmake --build --preset debug --target audit_architecture

include("${CMAKE_CURRENT_LIST_DIR}/architecture_contract.cmake")

set(GRAPHSCORE_TARGET_GRAPH_FILE
  "${CMAKE_BINARY_DIR}/graphscore_target_graph.cmake")

# Recursively collect the buildsystem targets defined under `dir`.
function(graphscore_collect_targets dir out_var)
  get_property(dir_targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
  get_property(subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)

  set(collected ${dir_targets})
  foreach (subdir IN LISTS subdirs)
    graphscore_collect_targets("${subdir}" sub_targets)
    list(APPEND collected ${sub_targets})
  endforeach()

  set(${out_var} "${collected}" PARENT_SCOPE)
endfunction()

# CMake records a static/shared library's PRIVATE dependencies in
# INTERFACE_LINK_LIBRARIES wrapped in $<LINK_ONLY:...> so they participate in
# link ordering without becoming usage requirements. The audits reason about
# target names, so unwrap them here rather than in four separate places.
function(graphscore_unwrap_link_only in_list out_var)
  set(unwrapped "")
  foreach (entry IN LISTS in_list)
    if (entry MATCHES "^\\$<LINK_ONLY:(.+)>$")
      list(APPEND unwrapped "${CMAKE_MATCH_1}")
    else()
      list(APPEND unwrapped "${entry}")
    endif()
  endforeach()
  set(${out_var} "${unwrapped}" PARENT_SCOPE)
endfunction()

function(graphscore_write_target_graph)
  graphscore_collect_targets("${CMAKE_SOURCE_DIR}" all_targets)

  set(dump
"# SPDX-License-Identifier: Apache-2.0
#
# GENERATED FILE - DO NOT EDIT AND DO NOT COMMIT.
#
# Written by cmake/ArchitectureAudit.cmake at configure time; consumed by the
# ADR 0003 §7 audits in CMake script mode. Regenerate by reconfiguring.

")

    set(dumped_targets "")

  foreach (target IN LISTS all_targets)
    if (NOT TARGET ${target})
      continue()
    endif()

    # Audit only GraphScore-owned targets. FetchContent dependency targets
    # are third-party and are audited by their appearance in a GraphScore
    # target's edges, not by their own internal wiring.
    if (NOT target IN_LIST GRAPHSCORE_ALL_TARGETS)
      continue()
    endif()

    get_target_property(target_type ${target} TYPE)

    set(link_libraries "")
    if (NOT target_type STREQUAL "INTERFACE_LIBRARY")
      get_target_property(link_libraries ${target} LINK_LIBRARIES)
      if (NOT link_libraries)
        set(link_libraries "")
      endif()
    endif()

    get_target_property(interface_libraries ${target} INTERFACE_LINK_LIBRARIES)
    if (NOT interface_libraries)
      set(interface_libraries "")
    endif()

    graphscore_unwrap_link_only("${link_libraries}" link_libraries)
    graphscore_unwrap_link_only("${interface_libraries}" interface_libraries)

    list(APPEND dumped_targets ${target})

    string(APPEND dump
      "set(GRAPHSCORE_GRAPH_TYPE_${target} \"${target_type}\")\n"
      "set(GRAPHSCORE_GRAPH_LINK_${target} \"${link_libraries}\")\n"
      "set(GRAPHSCORE_GRAPH_INTERFACE_${target} \"${interface_libraries}\")\n\n")
  endforeach()

  string(APPEND dump "set(GRAPHSCORE_GRAPH_TARGETS \"${dumped_targets}\")\n")

  file(WRITE "${GRAPHSCORE_TARGET_GRAPH_FILE}" "${dump}")

  # Every target named in the contract must actually exist. A typo in
  # architecture_contract.cmake, or a target deleted without an ADR
  # amendment, would otherwise silently reduce audit coverage to nothing.
  foreach (expected IN LISTS GRAPHSCORE_ALL_TARGETS)
    if (NOT expected IN_LIST dumped_targets)
      message(FATAL_ERROR
        "Architecture contract lists target '${expected}' (ADR 0003 §1/§2.3) "
        "but no such target is defined in this build. Either the target was "
        "removed without amending ADR 0003 and "
        "cmake/architecture_contract.cmake, or GRAPHSCORE_BUILD_TESTS is OFF "
        "while a test target is still listed in the contract.")
    endif()
  endforeach()
endfunction()

graphscore_write_target_graph()

# ---------------------------------------------------------------------------
# audit_architecture
# ---------------------------------------------------------------------------

find_package(Python3 COMPONENTS Interpreter QUIET)

set(GRAPHSCORE_AUDIT_CMAKE_SCRIPTS
  audit_permitted_edges
  audit_link_closure
  audit_transitive_closure
)

set(audit_commands "")

foreach (audit IN LISTS GRAPHSCORE_AUDIT_CMAKE_SCRIPTS)
  list(APPEND audit_commands
    COMMAND ${CMAKE_COMMAND}
      -D "GRAPHSCORE_SOURCE_DIR=${CMAKE_SOURCE_DIR}"
      -D "GRAPHSCORE_TARGET_GRAPH_FILE=${GRAPHSCORE_TARGET_GRAPH_FILE}"
      -P "${CMAKE_SOURCE_DIR}/cmake/${audit}.cmake")
endforeach()

# The Python audits inspect sources and built binaries rather than the target
# graph. They are skipped with a warning when no interpreter is available, so
# a developer without Python still gets the three CMake audits; CI always has
# Python and runs the full set.
set(GRAPHSCORE_AUDIT_PYTHON_SCRIPTS
  audit_includes
  audit_third_party_types
  audit_cycles
)

if (Python3_Interpreter_FOUND)
  foreach (audit IN LISTS GRAPHSCORE_AUDIT_PYTHON_SCRIPTS)
    list(APPEND audit_commands
      COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_SOURCE_DIR}/scripts/${audit}.py"
        --source-dir "${CMAKE_SOURCE_DIR}"
        --binary-dir "${CMAKE_BINARY_DIR}")
  endforeach()

  # The exported-symbol audit needs the built shared library, so it depends
  # on graphscore_runtime and reads it from disk.
  list(APPEND audit_commands
    COMMAND ${Python3_EXECUTABLE}
      "${CMAKE_SOURCE_DIR}/scripts/audit_runtime_symbols.py"
      --source-dir "${CMAKE_SOURCE_DIR}"
      --binary-dir "${CMAKE_BINARY_DIR}"
      --runtime-library "$<TARGET_FILE:graphscore_runtime>")
else()
  message(WARNING
    "Python3 interpreter not found: the audit_architecture target will run "
    "only the CMake audits (ADR 0003 §7.1, §7.2). The include, symbol, "
    "cycle, and type-leakage audits require Python 3.")
endif()

add_custom_target(audit_architecture
  ${audit_commands}
  DEPENDS graphscore_runtime
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  COMMENT "Auditing architecture boundaries (ADR 0003 §7)"
  VERBATIM
  USES_TERMINAL
)
