# SPDX-License-Identifier: Apache-2.0
#
# The machine-readable form of the ADR 0003 boundary contract. This file is
# data only: it declares no targets, runs no checks, and is safe to
# `include()` from CMake script mode (`cmake -P`).
#
# It is the single source of truth shared by every audit in ADR 0003 §7:
#
#   cmake/audit_permitted_edges.cmake     (§7.1, reverse check)
#   cmake/audit_link_closure.cmake        (§7.1, forbidden-closure check)
#   cmake/audit_transitive_closure.cmake  (§7.2)
#   scripts/audit_includes.py             (§7.3)
#   scripts/audit_runtime_symbols.py      (§7.5)
#   scripts/audit_cycles.py               (§7.6)
#   scripts/audit_third_party_types.py    (§7.7)
#
# Editing this file without a corresponding ADR 0003 amendment is a boundary
# violation in itself. Adding a dependency edge requires the ADR change
# first, then the change here.

# ---------------------------------------------------------------------------
# Target inventory (ADR 0003 §1)
# ---------------------------------------------------------------------------

# Layers 0-5. These, plus graphscore_core, form the runtime closure and must
# never reach domain, persistence, notation, rendering, canvas, or any
# writer-only target.
set(GRAPHSCORE_CLEAN_TARGETS
  graphscore_core
  graphscore_c_abi
  graphscore_domain
  graphscore_cooked_format
  graphscore_compiler
  graphscore_persistence
  graphscore_scheduler
  graphscore_loader
  graphscore_runtime_impl
  graphscore_runtime
)

# Layers 6-11. Reachable only from graphscore_writer_app and
# graphscore_plugin_scanner.
set(GRAPHSCORE_WRITER_TARGETS
  graphscore_notation
  graphscore_rendering
  graphscore_accessibility
  graphscore_canvas
  graphscore_writer_shell
  graphscore_audio_device
  graphscore_midi_io
  graphscore_vst3_host
  graphscore_writer_audio
  graphscore_accessibility_platform
  graphscore_plugin_scanner_client
  graphscore_writer_app
  graphscore_plugin_scanner
)

set(GRAPHSCORE_PRODUCTION_TARGETS
  ${GRAPHSCORE_CLEAN_TARGETS}
  ${GRAPHSCORE_WRITER_TARGETS}
)

# Test targets, admitted to the contract by the ADR 0003 §2.3 amendment that
# M1 records. Each links exactly GTest::gtest_main plus the target under test
# and that target's own permitted edges.
set(GRAPHSCORE_TEST_TARGETS
  graphscore_core_test
  graphscore_domain_test
  graphscore_cooked_format_test
  graphscore_compiler_test
  graphscore_persistence_test
  graphscore_scheduler_test
  graphscore_loader_test
  graphscore_runtime_test
  gs_c_consumer
)

set(GRAPHSCORE_ALL_TARGETS
  ${GRAPHSCORE_PRODUCTION_TARGETS}
  ${GRAPHSCORE_TEST_TARGETS}
)

# ---------------------------------------------------------------------------
# Protected targets (ADR 0003 §3.3)
# ---------------------------------------------------------------------------
#
# Guaranteed to build with no compile-time or link-time dependency on any
# windowing, audio-device, VST3, accessibility-bridge, GPU, text-shaping,
# font-rasterization, or vector-rendering library.

set(GRAPHSCORE_PROTECTED_TARGETS
  graphscore_core
  graphscore_c_abi
  graphscore_domain
  graphscore_cooked_format
  graphscore_compiler
  graphscore_persistence
  graphscore_scheduler
  graphscore_loader
  graphscore_runtime_impl
  graphscore_runtime
)

# Third-party CMake target names (and the common aliases each project
# exports) that must never appear in a protected target's link closure.
set(GRAPHSCORE_FORBIDDEN_EXTERNAL_TARGETS
  SDL3::SDL3 SDL3::SDL3-static SDL3::SDL3-shared SDL3 SDL3-static SDL3-shared
  freetype Freetype::Freetype FreeType::FreeType
  harfbuzz harfbuzz::harfbuzz hb
  thorvg thorvg::thorvg
  miniaudio miniaudio::miniaudio
  portaudio PortAudio::PortAudio portaudio_static
  rtmidi RtMidi::rtmidi
  sdk base pluginterfaces vstgui sdk_hosting
  accesskit accesskit-c accesskit::accesskit
)

# ---------------------------------------------------------------------------
# Permitted internal edges (ADR 0003 §2.1)
# ---------------------------------------------------------------------------
#
# GRAPHSCORE_PUBLIC_EDGES_<target> — permitted PUBLIC/INTERFACE link edges.
# GRAPHSCORE_PRIVATE_EDGES_<target> — permitted PRIVATE link edges.
#
# A target's effective permitted direct-edge set is the union of the two. An
# unset variable means "no permitted edges of that kind"; any actual edge
# then fails the audit.

set(GRAPHSCORE_PUBLIC_EDGES_graphscore_core "")
set(GRAPHSCORE_PUBLIC_EDGES_graphscore_c_abi "")
set(GRAPHSCORE_PUBLIC_EDGES_graphscore_domain graphscore_core)
set(GRAPHSCORE_PUBLIC_EDGES_graphscore_cooked_format graphscore_core)
set(GRAPHSCORE_PUBLIC_EDGES_graphscore_compiler
  graphscore_domain graphscore_cooked_format)
set(GRAPHSCORE_PUBLIC_EDGES_graphscore_persistence
  graphscore_domain graphscore_cooked_format graphscore_compiler)
set(GRAPHSCORE_PUBLIC_EDGES_graphscore_scheduler
  graphscore_core graphscore_cooked_format)
set(GRAPHSCORE_PUBLIC_EDGES_graphscore_loader
  graphscore_core graphscore_c_abi graphscore_cooked_format)
set(GRAPHSCORE_PUBLIC_EDGES_graphscore_runtime_impl
  graphscore_core graphscore_c_abi graphscore_scheduler graphscore_loader)
set(GRAPHSCORE_PUBLIC_EDGES_graphscore_runtime graphscore_c_abi)
set(GRAPHSCORE_PRIVATE_EDGES_graphscore_runtime graphscore_runtime_impl)

set(GRAPHSCORE_PUBLIC_EDGES_graphscore_notation
  graphscore_domain graphscore_core)
set(GRAPHSCORE_PUBLIC_EDGES_graphscore_rendering
  graphscore_notation graphscore_core)
set(GRAPHSCORE_PUBLIC_EDGES_graphscore_accessibility
  graphscore_domain graphscore_notation graphscore_core)
set(GRAPHSCORE_PUBLIC_EDGES_graphscore_canvas
  graphscore_domain graphscore_notation graphscore_core
  graphscore_accessibility)
set(GRAPHSCORE_PUBLIC_EDGES_graphscore_writer_shell
  graphscore_core graphscore_canvas graphscore_notation graphscore_rendering
  graphscore_accessibility)
set(GRAPHSCORE_PUBLIC_EDGES_graphscore_audio_device graphscore_core)
set(GRAPHSCORE_PUBLIC_EDGES_graphscore_midi_io graphscore_core)
set(GRAPHSCORE_PUBLIC_EDGES_graphscore_vst3_host
  graphscore_domain graphscore_audio_device graphscore_core)
set(GRAPHSCORE_PUBLIC_EDGES_graphscore_writer_audio
  graphscore_core graphscore_domain graphscore_scheduler
  graphscore_audio_device graphscore_vst3_host graphscore_compiler)
set(GRAPHSCORE_PUBLIC_EDGES_graphscore_accessibility_platform
  graphscore_accessibility)
set(GRAPHSCORE_PUBLIC_EDGES_graphscore_plugin_scanner_client
  graphscore_core graphscore_domain)
set(GRAPHSCORE_PRIVATE_EDGES_graphscore_plugin_scanner_client
  graphscore_writer_shell)
set(GRAPHSCORE_PUBLIC_EDGES_graphscore_writer_app
  graphscore_writer_shell graphscore_writer_audio
  graphscore_plugin_scanner_client graphscore_canvas graphscore_rendering
  graphscore_notation graphscore_accessibility
  graphscore_accessibility_platform graphscore_persistence
  graphscore_compiler graphscore_domain graphscore_scheduler graphscore_core)
set(GRAPHSCORE_PUBLIC_EDGES_graphscore_plugin_scanner graphscore_core)
set(GRAPHSCORE_PRIVATE_EDGES_graphscore_plugin_scanner graphscore_vst3_host)

# ---------------------------------------------------------------------------
# Permitted external edges (ADR 0003 §2.2)
# ---------------------------------------------------------------------------
#
# Every external edge is PRIVATE by construction: third-party types are
# confined to the .cpp files of the single owning target.
#
# Only targets whose dependency is not DEFERRED in ADR 0002 appear with a
# non-empty list. graphscore_accessibility_platform has zero external edges
# in M1 (ADR 0003 §7.1, "M1 audit accommodations") and must stay that way
# until Phase C records an implementation selection.

set(GRAPHSCORE_EXTERNAL_EDGES_graphscore_writer_shell
  SDL3::SDL3 SDL3::SDL3-static SDL3::SDL3-shared)
set(GRAPHSCORE_EXTERNAL_EDGES_graphscore_rendering
  freetype harfbuzz thorvg)
set(GRAPHSCORE_EXTERNAL_EDGES_graphscore_audio_device
  miniaudio portaudio)
set(GRAPHSCORE_EXTERNAL_EDGES_graphscore_midi_io rtmidi)
set(GRAPHSCORE_EXTERNAL_EDGES_graphscore_vst3_host sdk base pluginterfaces)
set(GRAPHSCORE_EXTERNAL_EDGES_graphscore_accessibility_platform "")

# ---------------------------------------------------------------------------
# Permitted test-target edges (ADR 0003 §2.3, resolved in M1)
# ---------------------------------------------------------------------------

set(GRAPHSCORE_PUBLIC_EDGES_graphscore_core_test graphscore_core)
set(GRAPHSCORE_PUBLIC_EDGES_graphscore_domain_test graphscore_domain)
set(GRAPHSCORE_PUBLIC_EDGES_graphscore_cooked_format_test
  graphscore_cooked_format)
set(GRAPHSCORE_PUBLIC_EDGES_graphscore_compiler_test graphscore_compiler)
set(GRAPHSCORE_PUBLIC_EDGES_graphscore_persistence_test
  graphscore_persistence)
set(GRAPHSCORE_PUBLIC_EDGES_graphscore_scheduler_test graphscore_scheduler)
set(GRAPHSCORE_PUBLIC_EDGES_graphscore_loader_test graphscore_loader)
set(GRAPHSCORE_PUBLIC_EDGES_graphscore_runtime_test graphscore_runtime)
set(GRAPHSCORE_PUBLIC_EDGES_gs_c_consumer graphscore_runtime)

# GTest is permitted for test targets only. A production target linking
# GTest fails audit_permitted_edges (ADR 0003 §2.3).
set(GRAPHSCORE_TEST_FRAMEWORK_TARGETS
  GTest::gtest GTest::gtest_main GTest::gmock GTest::gmock_main
  gtest gtest_main gmock gmock_main)

foreach (gs_test_target IN LISTS GRAPHSCORE_TEST_TARGETS)
  # gs_c_consumer is a pure-C ABI compile probe and links no test framework.
  if (NOT gs_test_target STREQUAL "gs_c_consumer")
    set(GRAPHSCORE_EXTERNAL_EDGES_${gs_test_target}
      ${GRAPHSCORE_TEST_FRAMEWORK_TARGETS})
  endif()
endforeach()
unset(gs_test_target)

# ---------------------------------------------------------------------------
# Forbidden internal closures (ADR 0003 §3.2)
# ---------------------------------------------------------------------------
#
# For each clean target, the internal targets that must not appear anywhere
# in its transitive link closure. Stated explicitly rather than derived from
# layer numbers so that the audit and the ADR can be diffed line for line.

set(GRAPHSCORE_FORBIDDEN_CLOSURE_graphscore_domain
  ${GRAPHSCORE_WRITER_TARGETS}
  graphscore_persistence graphscore_compiler graphscore_cooked_format
  graphscore_c_abi graphscore_loader graphscore_runtime_impl
  graphscore_runtime)

set(GRAPHSCORE_FORBIDDEN_CLOSURE_graphscore_cooked_format
  ${GRAPHSCORE_WRITER_TARGETS}
  graphscore_domain graphscore_compiler graphscore_persistence
  graphscore_scheduler graphscore_loader graphscore_runtime_impl
  graphscore_runtime graphscore_c_abi)

set(GRAPHSCORE_FORBIDDEN_CLOSURE_graphscore_compiler
  ${GRAPHSCORE_WRITER_TARGETS}
  graphscore_persistence graphscore_scheduler graphscore_loader
  graphscore_runtime_impl graphscore_runtime graphscore_c_abi)

set(GRAPHSCORE_FORBIDDEN_CLOSURE_graphscore_scheduler
  ${GRAPHSCORE_WRITER_TARGETS}
  graphscore_domain graphscore_compiler graphscore_persistence
  graphscore_loader graphscore_runtime_impl graphscore_runtime
  graphscore_c_abi)

set(GRAPHSCORE_FORBIDDEN_CLOSURE_graphscore_persistence
  ${GRAPHSCORE_WRITER_TARGETS}
  graphscore_scheduler graphscore_loader graphscore_runtime_impl
  graphscore_runtime graphscore_c_abi)

# The runtime closure additionally excludes every writer-only target and the
# domain/persistence/compiler chain (ADR 0003 §3.1).
set(GRAPHSCORE_FORBIDDEN_CLOSURE_graphscore_runtime
  ${GRAPHSCORE_WRITER_TARGETS}
  graphscore_domain graphscore_compiler graphscore_persistence)

set(GRAPHSCORE_FORBIDDEN_CLOSURE_graphscore_runtime_impl
  ${GRAPHSCORE_FORBIDDEN_CLOSURE_graphscore_runtime})

set(GRAPHSCORE_FORBIDDEN_CLOSURE_graphscore_loader
  ${GRAPHSCORE_WRITER_TARGETS}
  graphscore_domain graphscore_compiler graphscore_persistence
  graphscore_scheduler)

set(GRAPHSCORE_FORBIDDEN_CLOSURE_graphscore_core
  ${GRAPHSCORE_WRITER_TARGETS}
  graphscore_domain graphscore_cooked_format graphscore_compiler
  graphscore_persistence graphscore_scheduler graphscore_loader
  graphscore_runtime_impl graphscore_runtime graphscore_c_abi)

set(GRAPHSCORE_FORBIDDEN_CLOSURE_graphscore_c_abi
  ${GRAPHSCORE_WRITER_TARGETS}
  ${GRAPHSCORE_CLEAN_TARGETS})
list(REMOVE_ITEM GRAPHSCORE_FORBIDDEN_CLOSURE_graphscore_c_abi
  graphscore_c_abi)
