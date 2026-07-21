# AGENTS.md

Engineering guidance for anyone or anything working in this repository —
human contributors and coding agents alike. This file is maintained
documentation: whenever a later milestone changes a command, a target name,
or a non-obvious constraint, update this file in the same change.

`CLAUDE.md` is a tracked relative symbolic link to this file. Do not create a
second copy of these instructions; edit `AGENTS.md` only.

## Product

GraphScore produces two things from one repository:

- **GraphScore Writer** — a desktop notation/graph authoring app (macOS,
  Windows, Linux).
- **GraphScore Runtime** — a C++23 implementation exposed through a stable C
  ABI as a dynamic library, for game engines.

Full product scope, locked decisions, and the milestone roadmap live in
[docs/plan/README.md](docs/plan/README.md). Architecture decisions live in
[docs/decisions/](docs/decisions/) (see especially ADR 0003, the target DAG
and layer-boundary contract that every CMake target in this repository must
satisfy).

## Repository layout

| Path | Contents |
|---|---|
| `src/<target>/` | Implementation source for each `graphscore_<target>` library/executable, plus that target's `CMakeLists.txt`. |
| `include/graphscore/<target>/` | Public headers for targets that expose one (writer-only leaf targets and executables do not). |
| `tests/<target>/` | GTest sources for `graphscore_<target>_test`, plus `tests/c_abi/` (pure-C ABI consumer), `tests/cmake/` (out-of-tree consumer projects driven against a real install tree), and `tests/repository/` (checkout and build-system properties). |
| `apps/` | Application entry points (`graphscore_writer_app`, `graphscore_plugin_scanner`). |
| `tools/` | Developer-facing helper executables that are not part of the shipped product. |
| `cmake/` | CMake modules: compiler/warning setup, dependency adapters (one file per third-party dependency, e.g. `SDL3.cmake`), the runtime install/export package (`RuntimePackage.cmake`), and the architecture audit (`architecture_contract.cmake` — the machine-readable ADR 0003 contract — plus `audit_permitted_edges.cmake`, `audit_link_closure.cmake`, `audit_transitive_closure.cmake`). |
| `scripts/` | Non-CMake tooling: Python audit scripts (`audit_includes.py`, `audit_runtime_symbols.py`, `audit_cycles.py`, `audit_third_party_types.py`, sharing `graphscore_audit.py`) and `bootstrap.sh`. |
| `.githooks/` | Tracked Git hooks. Installed by `scripts/bootstrap.sh`; never installed automatically. |
| `docs/plan/` | The milestone plan and the source-controlled execution checklist. |
| `docs/decisions/` | Accepted ADRs. |
| `docs/licenses/`, `NOTICES.md` | Third-party license inventory. |
| `spikes/` | Disposable Milestone 00 spike code. Never shipped; see `docs/plan/00-architecture-spikes.md` for the rules governing it. |

## Canonical commands

All commands run from the repository root unless noted.

```sh
# One-time: install the tracked pre-commit hook.
./scripts/bootstrap.sh

# Configure + build (presets defined in CMakePresets.json).
cmake --preset debug
cmake --build --preset debug

# Runtime-only build: fetches no writer dependency at all. This is the
# configuration an engine integrator uses.
cmake --preset debug -DGRAPHSCORE_BUILD_WRITER=OFF

# Run tests.
ctest --preset debug --output-on-failure

# Other presets: release, asan-ubsan, tsan, ci (macOS/Linux, Clang).
cmake --preset asan-ubsan && cmake --build --preset asan-ubsan && ctest --preset asan-ubsan

# Windows uses clang-cl via the *-windows presets: debug-windows,
# release-windows, ci-windows. ASan/TSan are not provided on Windows.

# Lint: cpplint + clang-format verification (also run by the pre-commit
# hook on staged files, and by CI over the whole tree).
cmake --build --preset debug --target lint

# Const-correctness analysis. Off by default because clang-tidy roughly
# triples compile time; CI runs it as its own job.
cmake --preset debug -DGRAPHSCORE_ENABLE_CLANG_TIDY=ON
cmake --build --preset debug

# Architecture boundary audit (ADR 0003 §7).
cmake --build --preset debug --target audit_architecture
```

`FETCHCONTENT_SOURCE_DIR_<NAME>` (e.g. `FETCHCONTENT_SOURCE_DIR_SDL3`) points
a dependency at a local checkout instead of fetching over the network, for
offline/air-gapped builds. See `cmake/` for the exact `<NAME>` per dependency.
Combine with `-DFETCHCONTENT_FULLY_DISCONNECTED=ON` to forbid network access
outright; the `graphscore_offline_dependencies` test exercises exactly this
across every fetched dependency.

## C++23 and const-correctness

- The project requires C++23 and Clang or AppleClang; other compilers fail
  configure with an actionable message.
- Prefer `constexpr`; otherwise `const`; mutable state requires a
  demonstrated need. The five accepted exception categories — realtime state,
  atomics, caches, platform handles, move-from sources and out-parameters —
  are enumerated in `cmake/ConstCorrectness.cmake`; the check selection is in
  the root `.clang-tidy`. A `NOLINT` must name both the check it suppresses
  and the category that justifies it; a bare `NOLINT` is a review defect.
- Do not add `const` to interfaces or return values where it does not
  increase safety and only adds friction.

## Realtime rules

Every function reachable from `graphscore_runtime_impl`'s `process` call path
must not allocate, lock, block on I/O, throw/catch exceptions, or perform
unbounded work. This applies transitively through `graphscore_scheduler` and
`graphscore_loader`. Diagnostics from the realtime path use process status
flags and pollable/resettable atomic counters only — never logging
callbacks.

## Architecture boundaries

ADR 0003 defines the complete set of GraphScore CMake targets, their layer
ordering, and every permitted dependency edge (internal and third-party).
This is a mechanically enforced contract, not a convention:

- No target may depend on anything outside its permitted edge list. Adding a
  dependency not listed in ADR 0003 §2 requires an ADR amendment first.
- `graphscore_runtime` and everything it depends on (`graphscore_runtime_impl`,
  `graphscore_scheduler`, `graphscore_cooked_format`, `graphscore_loader`,
  `graphscore_c_abi`, `graphscore_core`) must never gain a dependency on
  domain, persistence, notation, rendering, canvas, or any writer-only
  target.
- Third-party types (SDL3, FreeType, HarfBuzz, ThorVG, miniaudio, PortAudio,
  RtMidi, VST3 SDK, accesskit-c) are private to `.cpp` files of the one
  target that owns them (ADR 0003 §2.2) and must never appear in a public
  header.
- `cmake/architecture_contract.cmake` is the machine-readable form of the
  ADR 0003 tables. Changing it without amending the ADR is itself a boundary
  violation.
- `cmake --build --preset debug --target audit_architecture` runs all seven
  audits: permitted edges, link closure, transitive closure, include
  boundaries, third-party type leakage, cycles and layer ordering, and the
  runtime's exported symbols. CI runs the same target on every platform in
  the matrix.

## Dependencies and licensing

- GraphScore is Apache-2.0. The default complete build uses only
  dependencies compatible with permissive commercial reuse (ADR 0001).
- Dependencies are fetched via CMake `FetchContent` pinned to immutable
  commit hashes — never a branch or tag that can move. Exact pins and their
  license review live in ADR 0002.
- Every accepted dependency has a CMake adapter under `cmake/` and a
  committed license file under `docs/licenses/`, indexed in
  `docs/NOTICES.md`.
- Platform SDKs, installed VST3 plugins, and OS services are not vendored
  dependencies.

## Generated and third-party files

- Files under `build/`, `cmake-build-*/`, and any `FetchContent`-managed
  source directory are never committed; see `.gitignore`.
- Third-party and vendored code (once present) keeps its own upstream
  license markers and is never given a GraphScore SPDX header.
- `spikes/**/build*/` is gitignored; the spike source under `spikes/` is
  intentionally tracked and marked `DISPOSABLE` per Milestone 00's rules.

## Commit messages

Commit messages describe only the project change. They must never mention
the model, assistant, or vendor used to produce the change — including names
such as Claude, ChatGPT, GPT, Copilot, Anthropic, or OpenAI — and must never
add an AI-generated attribution trailer (e.g. `Co-Authored-By: <assistant>`).

## Runtime packaging

`graphscore_runtime` installs and exports as the `GraphScoreRuntime` CMake
package. Only the ADR 0003 §3.1 runtime closure and the public C ABI header
are installed — no writer library, no writer header, and no C++ header of the
closure's internal targets. A consumer needs:

```cmake
find_package(GraphScoreRuntime REQUIRED)
target_link_libraries(my_app PRIVATE graphscore::runtime)
```

The `graphscore_cmake_consumer` test installs the package to a scratch
prefix, builds C and C++ consumers against it out of tree, and asserts that a
writer target cannot be linked from it.

## Platform caveats

- Windows arm64 and Linux arm64 are build-only in CI; native test execution
  is not required there. macOS arm64/x86-64 and Windows/Linux x86-64 build
  and run tests natively.
- The sanitizer and clang-tidy CI jobs configure with
  `-DGRAPHSCORE_BUILD_WRITER=OFF`. Instrumenting or analysing SDL3 costs most
  of those jobs' time on third-party code GraphScore does not own.
- Linux needs X11, Wayland, and xkbcommon development packages for the SDL3
  build; see `.github/workflows/ci.yml` for the exact list.
- SDL3 at the pinned SHA needs three macOS frameworks linked that it does not
  link itself; `cmake/SDL3.cmake` documents why. Revisit when the pin moves.
- The VST3 SDK requires an explicit build type at configure time (its
  `fdebug.h` errors on an empty `CMAKE_BUILD_TYPE`) and both `ENV{XCODE_VERSION}`
  and the plain `XCODE_VERSION` CMake variable set, even on a Command Line
  Tools-only macOS machine with no Xcode.app. See ADR 0007's Build
  Integration Notes before touching VST3 SDK CMake wiring (Milestone 08).

## Where to look next

- [docs/plan/README.md](docs/plan/README.md) — product vision, locked
  decisions, milestone roadmap.
- [docs/plan/CHECKLIST.md](docs/plan/CHECKLIST.md) — source-controlled
  execution checklist; check a box only once every detailed deliverable in
  its linked section is done.
- [docs/plan/01-toolchain-ci.md](docs/plan/01-toolchain-ci.md) — this
  milestone's deliverables and acceptance criteria in full.
- [docs/decisions/0003-architecture-target-dag.md](docs/decisions/0003-architecture-target-dag.md)
  — the target graph and boundary contract referenced throughout this file.
