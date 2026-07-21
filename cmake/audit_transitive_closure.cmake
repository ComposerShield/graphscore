# SPDX-License-Identifier: Apache-2.0
#
# ADR 0003 §7.2: no forbidden third-party library may appear in the
# transitive link closure of any protected target (§3.3).
#
# This is the backstop for the case audit_permitted_edges cannot see: a
# developer adds a private link that was never declared in the inventory, so
# no permitted-edge rule mentions it, but it still ends up linked into the
# runtime. Checking the closure against the third-party denylist catches it
# regardless of how it was declared.
#
# In M1, graphscore_accessibility_platform has zero external edges and must
# not appear with any forbidden library in its closure either.
#
# Run via the audit_architecture target; see cmake/ArchitectureAudit.cmake.

cmake_minimum_required(VERSION 3.25)

include("${GRAPHSCORE_SOURCE_DIR}/cmake/audit_common.cmake")

set(audited_count 0)

foreach (target IN LISTS GRAPHSCORE_PROTECTED_TARGETS)
  if (NOT target IN_LIST GRAPHSCORE_GRAPH_TARGETS)
    message(FATAL_ERROR
      "Protected target '${target}' (ADR 0003 §3.3) is missing from the "
      "target-graph dump. Reconfigure, or amend ADR 0003 if the target was "
      "intentionally removed.")
  endif()

  math(EXPR audited_count "${audited_count} + 1")

  graphscore_link_closure(${target} closure)

  foreach (entry IN LISTS closure)
    if (entry IN_LIST GRAPHSCORE_FORBIDDEN_EXTERNAL_TARGETS)
      graphscore_audit_violation(
        "Protected target ${target} reaches third-party library '${entry}'. "
        "ADR 0003 §3.3 guarantees ${target} builds with no windowing, "
        "audio-device, VST3, accessibility-bridge, GPU, text-shaping, "
        "font-rasterization, or vector-rendering dependency.")
    endif()
  endforeach()
endforeach()

# graphscore_accessibility_platform is not protected, but M1 pins it at zero
# external edges (ADR 0003 §7.1 "M1 audit accommodations", §2.2). Phase C
# adds exactly one private bridge edge, and adding it requires amending both
# the ADR and cmake/architecture_contract.cmake.
if (graphscore_accessibility_platform IN_LIST GRAPHSCORE_GRAPH_TARGETS)
  graphscore_link_closure(graphscore_accessibility_platform bridge_closure)
  foreach (entry IN LISTS bridge_closure)
    if (entry IN_LIST GRAPHSCORE_FORBIDDEN_EXTERNAL_TARGETS)
      graphscore_audit_violation(
        "graphscore_accessibility_platform reaches '${entry}'. In M1 this "
        "target has zero external edges; a bridge implementation may not be "
        "linked until Phase C records the selection and ADR 0003 §2.2 is "
        "amended.")
    endif()
  endforeach()
endif()

message(STATUS
  "audit_transitive_closure: examined ${audited_count} protected closures")

graphscore_audit_finish("audit_transitive_closure (ADR 0003 §7.2)")
