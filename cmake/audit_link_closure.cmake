# SPDX-License-Identifier: Apache-2.0
#
# ADR 0003 §7.1, forbidden-closure check: for each clean target, no target
# named in its §3.2 forbidden list may appear anywhere in its transitive link
# closure — including edges inherited through a dependency's PRIVATE links.
#
# This is the check that catches "graphscore_scheduler now reaches
# graphscore_domain, three hops away". audit_permitted_edges catches the
# direct edge; this one catches what the direct edge drags in.
#
# Run via the audit_architecture target; see cmake/ArchitectureAudit.cmake.

cmake_minimum_required(VERSION 3.25)

include("${GRAPHSCORE_SOURCE_DIR}/cmake/audit_common.cmake")

set(audited_count 0)

foreach (target IN LISTS GRAPHSCORE_GRAPH_TARGETS)
  set(forbidden ${GRAPHSCORE_FORBIDDEN_CLOSURE_${target}})

  if (NOT forbidden)
    continue()
  endif()

  math(EXPR audited_count "${audited_count} + 1")

  graphscore_link_closure(${target} closure)

  foreach (entry IN LISTS closure)
    if (entry IN_LIST forbidden)
      # Name one intermediate hop so the report points at the edge to
      # delete rather than only at the endpoints.
      graphscore_direct_edges(${target} direct)
      set(via "")
      foreach (candidate IN LISTS direct)
        if (candidate STREQUAL entry)
          set(via " (direct edge)")
          break()
        endif()
        graphscore_link_closure(${candidate} candidate_closure)
        if (entry IN_LIST candidate_closure)
          set(via " (reached through ${candidate})")
          break()
        endif()
      endforeach()

      graphscore_audit_violation(
        "${target} reaches forbidden target ${entry}${via}. ADR 0003 §3.2 "
        "forbids ${entry} in the link closure of ${target}.")
    endif()
  endforeach()
endforeach()

if (audited_count EQUAL 0)
  message(FATAL_ERROR
    "audit_link_closure examined no targets. The contract's "
    "GRAPHSCORE_FORBIDDEN_CLOSURE_* sets and the target-graph dump have "
    "diverged; this is an audit-wiring bug, not a clean result.")
endif()

message(STATUS
  "audit_link_closure: examined ${audited_count} protected closures")

graphscore_audit_finish("audit_link_closure (ADR 0003 §7.1)")
