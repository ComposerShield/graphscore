# SPDX-License-Identifier: Apache-2.0
#
# ADR 0003 §7.1, reverse check: every direct dependency edge of every
# GraphScore-owned target must be explicitly listed in the permitted sets of
# ADR 0003 §2.1 (internal), §2.2 (external), and §2.3 (test framework).
#
# Where audit_link_closure asks "does anything forbidden appear?", this audit
# asks the stricter question "is everything that appears permitted?" — so an
# edge to a target nobody thought to forbid is still a failure.
#
# Run via the audit_architecture target; see cmake/ArchitectureAudit.cmake.

cmake_minimum_required(VERSION 3.25)

include("${GRAPHSCORE_SOURCE_DIR}/cmake/audit_common.cmake")

foreach (target IN LISTS GRAPHSCORE_GRAPH_TARGETS)
  set(permitted
    ${GRAPHSCORE_PUBLIC_EDGES_${target}}
    ${GRAPHSCORE_PRIVATE_EDGES_${target}}
    ${GRAPHSCORE_EXTERNAL_EDGES_${target}}
  )

  # graphscore::warnings carries only compile options — no link
  # dependencies, no include directories, no transitive targets — and is
  # applied to every owned target by graphscore_apply_warnings(). It is
  # infrastructure rather than an architectural edge.
  list(APPEND permitted graphscore_warnings graphscore::warnings)

  list(REMOVE_ITEM permitted "")

  graphscore_direct_edges(${target} actual_edges)

  foreach (edge IN LISTS actual_edges)
    if (edge IN_LIST permitted)
      continue()
    endif()

    # Anything not a CMake target name — a raw library path, a `-framework`
    # flag, a bare system library — is also undeclared. Reported with a
    # distinct hint because the fix is different: those belong in the owning
    # target's external edge list in ADR 0003 §2.2.
    if (edge MATCHES "^(/|-|\\$<)")
      graphscore_audit_violation(
        "${target} links the raw flag or path '${edge}', which is not a "
        "declared edge. Platform and system libraries must be declared as "
        "external edges for the owning target in ADR 0003 §2.2.")
    else()
      graphscore_audit_violation(
        "${target} -> ${edge} is not a permitted edge. Permitted for "
        "${target}: ${permitted}")
    endif()
  endforeach()

  # GTest is permitted for test targets only (ADR 0003 §2.3). A production
  # target linking it would already have failed the check above, but the
  # dedicated message names the rule that was broken.
  if (NOT target IN_LIST GRAPHSCORE_TEST_TARGETS)
    foreach (framework IN LISTS GRAPHSCORE_TEST_FRAMEWORK_TARGETS)
      if (framework IN_LIST actual_edges)
        graphscore_audit_violation(
          "${target} is a production target and links the test framework "
          "target '${framework}'. ADR 0003 §2.3 permits GTest edges only "
          "for the declared test targets.")
      endif()
    endforeach()
  endif()
endforeach()

graphscore_audit_finish("audit_permitted_edges (ADR 0003 §7.1)")
