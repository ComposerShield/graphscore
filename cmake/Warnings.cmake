# SPDX-License-Identifier: Apache-2.0
#
# graphscore::warnings — the target-scoped warning set from docs/plan/README.md
# ("Toolchain and quality"). Link PRIVATE into every GraphScore-owned target
# via graphscore_apply_warnings(); never link into fetched third-party
# targets, whose warnings are isolated and must not become GraphScore build
# failures (ADR 0001, "Warning isolation").

add_library(graphscore_warnings INTERFACE)
add_library(graphscore::warnings ALIAS graphscore_warnings)

set(GRAPHSCORE_WARNING_FLAGS
  -Wall
  -Wextra
  -Wpedantic
  -Wshadow
  -Wconversion
  -Wsign-conversion
  -Wold-style-cast
  -Wnon-virtual-dtor
  -Wcast-align
  -Wunused
  -Wnull-dereference
  -Wdouble-promotion
  -Wformat=2
  -Wimplicit-fallthrough
)

if (CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
  # clang-cl: pass the GNU-style diagnostics through verbatim via the
  # /clang: escape hatch, and use the MSVC-frontend spelling of
  # warnings-as-errors.
  list(TRANSFORM GRAPHSCORE_WARNING_FLAGS PREPEND "/clang:" OUTPUT_VARIABLE GRAPHSCORE_WARNING_FLAGS_MSVC)
  target_compile_options(graphscore_warnings INTERFACE ${GRAPHSCORE_WARNING_FLAGS_MSVC} /WX)
else()
  target_compile_options(graphscore_warnings INTERFACE ${GRAPHSCORE_WARNING_FLAGS} -Werror)
endif()

# Link PRIVATE into `target`. GraphScore-owned targets only — never a
# third-party FetchContent target.
function(graphscore_apply_warnings target)
  target_link_libraries(${target} PRIVATE graphscore::warnings)
endfunction()
