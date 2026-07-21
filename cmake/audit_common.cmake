# SPDX-License-Identifier: Apache-2.0
#
# Shared script-mode helpers for the three CMake architecture audits. Loads
# the ADR 0003 contract and the configure-time target-graph dump, then
# provides closure walking and violation reporting.
#
# Not included from the live buildsystem — only from `cmake -P` scripts that
# receive GRAPHSCORE_SOURCE_DIR and GRAPHSCORE_TARGET_GRAPH_FILE on the
# command line.

if (NOT GRAPHSCORE_SOURCE_DIR)
  message(FATAL_ERROR "GRAPHSCORE_SOURCE_DIR must be set")
endif()

if (NOT GRAPHSCORE_TARGET_GRAPH_FILE OR
    NOT EXISTS "${GRAPHSCORE_TARGET_GRAPH_FILE}")
  message(FATAL_ERROR
    "Target-graph dump not found at '${GRAPHSCORE_TARGET_GRAPH_FILE}'. "
    "Reconfigure the build tree (cmake --preset <preset>) to regenerate it.")
endif()

include("${GRAPHSCORE_SOURCE_DIR}/cmake/architecture_contract.cmake")
include("${GRAPHSCORE_TARGET_GRAPH_FILE}")

set(GRAPHSCORE_AUDIT_VIOLATIONS "")

# Record a violation. Audits collect every violation and report them
# together, so one run tells a contributor everything that is wrong rather
# than only the first thing.
macro(graphscore_audit_violation message)
  list(APPEND GRAPHSCORE_AUDIT_VIOLATIONS "${message}")
endmacro()

# Fail with the accumulated violations, or report success.
macro(graphscore_audit_finish audit_name)
  list(LENGTH GRAPHSCORE_AUDIT_VIOLATIONS violation_count)
  if (violation_count GREATER 0)
    list(JOIN GRAPHSCORE_AUDIT_VIOLATIONS "\n\n  - " violation_text)
    message(FATAL_ERROR
      "${audit_name}: ${violation_count} boundary violation(s).\n\n"
      "  - ${violation_text}\n\n"
      "Every dependency edge must be declared in ADR 0003 "
      "(docs/decisions/0003-architecture-target-dag.md). Adding an edge "
      "requires an ADR amendment first, then a matching change to "
      "cmake/architecture_contract.cmake.")
  endif()
  message(STATUS "${audit_name}: clean")
endmacro()

# The direct link edges of `target`: LINK_LIBRARIES plus
# INTERFACE_LINK_LIBRARIES, deduplicated. $<LINK_ONLY:...> wrappers were
# already unwrapped when the dump was written.
function(graphscore_direct_edges target out_var)
  set(edges ${GRAPHSCORE_GRAPH_LINK_${target}}
            ${GRAPHSCORE_GRAPH_INTERFACE_${target}})
  list(REMOVE_DUPLICATES edges)
  list(REMOVE_ITEM edges "")
  set(${out_var} "${edges}" PARENT_SCOPE)
endfunction()

# The full transitive link closure of `target`, excluding `target` itself.
# Walks both LINK_LIBRARIES and INTERFACE_LINK_LIBRARIES so that private
# dependencies inherited through a static library are included — an edge
# hidden behind PRIVATE is still a real link-time dependency.
#
# Third-party targets terminate the walk: the dump contains only
# GraphScore-owned targets, so a third-party name has no recorded edges and
# is reported as a leaf. That is the correct behaviour — the contract
# constrains which third-party targets may appear, not what they pull in.
function(graphscore_link_closure target out_var)
  set(pending ${target})
  set(visited "")

  while (pending)
    list(POP_FRONT pending current)

    if (current IN_LIST visited)
      continue()
    endif()
    list(APPEND visited ${current})

    graphscore_direct_edges(${current} current_edges)
    foreach (edge IN LISTS current_edges)
      if (NOT edge IN_LIST visited)
        list(APPEND pending ${edge})
      endif()
    endforeach()
  endwhile()

  list(REMOVE_ITEM visited ${target})
  set(${out_var} "${visited}" PARENT_SCOPE)
endfunction()
