# SPDX-License-Identifier: Apache-2.0
#
# Const-correctness policy wiring (docs/plan/01-toolchain-ci.md,
# "Const-correctness policy"). The check selection itself lives in the root
# `.clang-tidy`; this module owns the CMake side and is the single place the
# documented exceptions are recorded.
#
# ---------------------------------------------------------------------------
# Policy
# ---------------------------------------------------------------------------
#
# Prefer `constexpr`. Where a value cannot be constant-evaluated, prefer
# `const`. Mutable state requires a demonstrated need. The checks that carry
# this policy are `misc-const-correctness`,
# `cppcoreguidelines-avoid-non-const-global-variables`,
# `readability-make-member-function-const`,
# `readability-non-const-parameter`, and `cppcoreguidelines-init-variables`.
#
# Two checks deliberately push in the opposite direction so that `const` is
# not applied where it only adds friction: `readability-const-return-type`
# (no const-qualified by-value returns) and
# `readability-avoid-const-params-in-decls` (no const on by-value parameters
# in declarations). Const in those positions does not increase safety.
#
# ---------------------------------------------------------------------------
# Documented exceptions
# ---------------------------------------------------------------------------
#
# The following categories are genuinely mutable. Each occurrence is silenced
# at the line or block with a `NOLINT`/`NOLINTNEXTLINE` naming the check, plus
# a comment naming which category below applies. A bare `NOLINT` with no check
# name and no category comment is itself a review defect.
#
#   1. Realtime state. Player transport state, cursor positions, tail slots,
#      and preallocated queues owned by `graphscore_runtime_impl` and
#      `graphscore_scheduler` mutate on every `process` call by design. They
#      cannot be const and must not be made const-by-copy: copying is an
#      allocation and a realtime-rule violation (AGENTS.md, "Realtime rules").
#
#   2. Atomics. `std::atomic` diagnostic counters and snapshot-publication
#      pointers are mutable by definition, and are frequently mutated through
#      otherwise-const member functions (`mutable std::atomic<...>`). This is
#      the one accepted use of the `mutable` keyword on a data member outside
#      category 3.
#
#   3. Caches. Memoized engraving layout, glyph metrics, and spatial-index
#      results are `mutable` members mutated from const query methods. The
#      observable value of the query is unchanged, so the enclosing method
#      stays const and the member is `mutable`.
#
#   4. Platform handles. Owning handles from SDL3, FreeType, HarfBuzz,
#      ThorVG, miniaudio, PortAudio, RtMidi, the VST3 SDK, and OS
#      accessibility services are non-const because the owning C APIs take
#      non-const pointers. These are confined to `.cpp` files of the single
#      owning target (ADR 0003 §2.2).
#
#   5. Move-from sources and output parameters. A value about to be moved
#      from, and any `T*`/`T&` out-parameter, cannot be const.
#
# Anything outside these five categories requires a written justification in
# review, not a NOLINT.
#
# ---------------------------------------------------------------------------
# CMake wiring
# ---------------------------------------------------------------------------

option(GRAPHSCORE_ENABLE_CLANG_TIDY
  "Run clang-tidy as part of compiling GraphScore-owned targets" OFF)

set(GRAPHSCORE_CLANG_TIDY_COMMAND "" CACHE INTERNAL "")

if (GRAPHSCORE_ENABLE_CLANG_TIDY)
  find_program(GRAPHSCORE_CLANG_TIDY_EXECUTABLE NAMES clang-tidy)

  if (NOT GRAPHSCORE_CLANG_TIDY_EXECUTABLE)
    message(FATAL_ERROR
      "GRAPHSCORE_ENABLE_CLANG_TIDY=ON but clang-tidy was not found on PATH.\n"
      "Install LLVM (macOS: `brew install llvm`; Linux: `apt install "
      "clang-tidy`; Windows: the LLVM installer), or configure with "
      "-DGRAPHSCORE_ENABLE_CLANG_TIDY=OFF.")
  endif()

  # --quiet suppresses the per-file "N warnings generated" noise; the
  # WarningsAsErrors setting in .clang-tidy is what fails the build.
  set(GRAPHSCORE_CLANG_TIDY_COMMAND
    "${GRAPHSCORE_CLANG_TIDY_EXECUTABLE};--quiet"
    CACHE INTERNAL "")
endif()

# Apply the const-correctness analysis to one GraphScore-owned target. No-op
# unless GRAPHSCORE_ENABLE_CLANG_TIDY is ON, so ordinary developer builds stay
# fast and clang-tidy remains an explicit opt-in (and a CI job).
#
# Never call this for a FetchContent third-party target.
function(graphscore_apply_const_correctness target)
  if (GRAPHSCORE_CLANG_TIDY_COMMAND)
    set_target_properties(${target} PROPERTIES
      CXX_CLANG_TIDY "${GRAPHSCORE_CLANG_TIDY_COMMAND}")
  endif()
endfunction()
