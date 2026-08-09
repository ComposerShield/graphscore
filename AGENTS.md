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
| `.githooks/` | Tracked Git hooks (`pre-commit`: cpplint + clang-format 18 on staged files; `pre-push`: the const-correctness clang-tidy analysis). Installed by `scripts/bootstrap.sh`; never installed automatically. |
| `docs/plan/` | The milestone plan and the source-controlled execution checklist. |
| `docs/decisions/` | Accepted ADRs. |
| `docs/licenses/`, `NOTICES.md` | Third-party license inventory. |
| `spikes/` | Disposable Milestone 00 spike code. Never shipped; see `docs/plan/00-architecture-spikes.md` for the rules governing it. |

## Canonical commands

All commands run from the repository root unless noted.

```sh
# One-time: install the tracked Git hooks (pre-commit lint, pre-push
# clang-tidy).
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

# Lint: cpplint + clang-format 18 verification (also run by the pre-commit
# hook on staged files, and by CI over the whole tree).
cmake --build --preset debug --target lint

# Const-correctness analysis. Canonical build tree is `build/tidy` (matching
# `.githooks/pre-push`, which runs the same clang-tidy 18 analysis and blocks
# the commit on findings — it is now a pre-commit gate, not only pre-push).
# Configure this once per session; subsequent builds are incremental.
#
# Use clang-tidy 18 to match CI. Check selections and their defaults change
# between releases, so another version will report a different set — newer
# ones both add and drop findings. Note that `brew install llvm` gives the
# latest major version, not 18. Matching installs:
#   brew install llvm@18            (macOS; keg-only, the hook finds it)
#   sudo apt install clang-tidy-18  (Debian/Ubuntu)
#   pip install clang-tidy==18.1.8  (any platform)
cmake --preset debug -B build/tidy \
  -DGRAPHSCORE_BUILD_WRITER=OFF \
  -DGRAPHSCORE_ENABLE_CLANG_TIDY=ON \
  -DGRAPHSCORE_CLANG_TIDY_EXECUTABLE=/opt/homebrew/opt/llvm@18/bin/clang-tidy
#
# On macOS, a non-Apple clang-tidy (Homebrew or the official LLVM builds
# pip installs) does not inherit AppleClang's implicit SDK include path and
# fails to find libc++ headers without it; the pre-push hook adds it itself.
# For a manual run, append:
#   -DCMAKE_CXX_FLAGS="-isysroot $(xcrun --show-sdk-path)" \
#   -DCMAKE_C_FLAGS="-isysroot $(xcrun --show-sdk-path)"
cmake --build build/tidy -- -k 0   # -k 0: report every file, not the first

# Architecture boundary audit (ADR 0003 §7).
cmake --build --preset debug --target audit_architecture
```

GraphScore requires **clang-format major 18 exactly** because formatting output
differs between majors. The local hook and CI both enforce 18; never use Apple
Command Line Tools clang-format 17. Matching installs are
`brew install llvm@18` (macOS), `sudo apt install clang-format-18`
(Debian/Ubuntu), or `pip install clang-format==18.1.8`. Discovery checks
`clang-format-18`, Apple Silicon and Intel Homebrew `llvm@18` paths, then an
unversioned executable only when it reports major 18. If CMake cannot discover
it, configure with
`-DGRAPHSCORE_CLANG_FORMAT_EXECUTABLE=/opt/homebrew/opt/llvm@18/bin/clang-format`
(or the matching installed path). This formatting policy is separate from the
clang-tidy 18 const-correctness guidance above.

`FETCHCONTENT_SOURCE_DIR_<NAME>` (e.g. `FETCHCONTENT_SOURCE_DIR_SDL3`) points
a dependency at a local checkout instead of fetching over the network, for
offline/air-gapped builds. See `cmake/` for the exact `<NAME>` per dependency.
Combine with `-DFETCHCONTENT_FULLY_DISCONNECTED=ON` to forbid network access
outright; the `graphscore_offline_dependencies` test exercises exactly this
across every `FetchContent`-fetched dependency.

Bravura (the shipped SMuFL font, ADR 0002 §A4) is acquired by
`file(DOWNLOAD)`, not `FetchContent`, so it needs its own override:
`BRAVURA_FONT_SRC` is a `FILEPATH` cache variable pointing at a
pre-acquired, SHA-256-verified `Bravura.otf`. Unlike every `FetchContent`
dependency, Bravura has **no disconnected-network escape hatch of its own**
— a default `GRAPHSCORE_BUILD_WRITER=ON` configure on a genuinely
air-gapped host fails outright unless `BRAVURA_FONT_SRC` is already set.
`graphscore_offline_dependencies` also exercises this override (threaded
through independently of the `FetchContent` overrides above) whenever the
main build tree was configured with the writer on.

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
are installed as *that exported package* — no writer library, no writer
header, and no C++ header of the closure's internal targets. A consumer
needs:

```cmake
find_package(GraphScoreRuntime REQUIRED)
target_link_libraries(my_app PRIVATE graphscore::runtime)
```

This is scoped to the exported CMake package, not to every file a
`cmake --install` writes: a default, writer-ON install tree additionally
carries the writer's own resources (below) alongside the runtime closure —
`GraphScoreRuntimeConfig.cmake` still resolves only `graphscore::runtime`
and its dependencies, so a consumer that only calls `find_package` sees
nothing writer-related regardless of what else is on disk.

The `graphscore_cmake_consumer` test installs the package to a scratch
prefix, builds C and C++ consumers against it out of tree, and asserts that a
writer target cannot be linked from it. It also asserts the writer-only
resource install below, under both `GRAPHSCORE_BUILD_WRITER` values.

Writer-only resource install (ADR 0002 §A4, §5), active only when
`GRAPHSCORE_BUILD_WRITER=ON` (`apps/CMakeLists.txt`):

| Destination | Contents |
|---|---|
| `share/licenses/GraphScore/` | GraphScore's own `LICENSE`/`NOTICE`. Installed regardless of `GRAPHSCORE_BUILD_WRITER` (`cmake/RuntimePackage.cmake`). |
| `share/licenses/GraphScoreWriter/` | License text for every third-party dependency the writer links (ThorVG, HarfBuzz, SDL3, FreeType's two license files, Bravura's OFL text), plus `NOTICE` (`docs/NOTICE-writer.txt`) — GraphScore's own attribution statement, required to discharge FreeType FTL §2's binary-distribution disclaimer obligation. |
| `share/graphscore/fonts/` | `Bravura.otf` (the SMuFL font, installed under this fixed name regardless of its acquisition path) and its own `Bravura-OFL.txt`. |

See `docs/NOTICES.md`'s "Shipped artifact layout" section for the full
rationale.

## Platform caveats

- Windows arm64 and Linux arm64 are build-only in CI; native test execution
  is not required there. macOS arm64/x86-64 and Windows/Linux x86-64 build
  and run tests natively.
- The sanitizer and clang-tidy CI jobs configure with
  `-DGRAPHSCORE_BUILD_WRITER=OFF`. Instrumenting or analysing SDL3 costs most
  of those jobs' time on third-party code GraphScore does not own.
- The clang-tidy CI job is currently commented out in
  `.github/workflows/ci.yml`. It was the workflow's critical path, and the
  fix — a 16-core larger runner — needs a Team or Enterprise Cloud plan that
  a personal-account repository cannot provision. Until this repository moves
  to an organization account, const-correctness is enforced only by
  `.githooks/pre-commit`, so running `./scripts/bootstrap.sh` is no longer
  optional: without the hooks installed, nothing checks it.
- Because `.githooks/pre-push`'s const-correctness analysis configures with
  `-DGRAPHSCORE_BUILD_WRITER=OFF` (matching the disabled CI job's own scope,
  for the same third-party-code-cost reason above), any code inside a
  `#if defined(GRAPHSCORE_HAVE_RENDERING_BACKEND)` block (or any other
  writer-only, `GRAPHSCORE_BUILD_WRITER`-gated branch) is never reached by
  either the hook or CI's clang-tidy analysis — `src/rendering/rendering.cpp`
  is the current example. A second, writer-ON local pass was considered and
  rejected for the hook specifically: configuring with
  `-DGRAPHSCORE_BUILD_WRITER=ON` unconditionally fetches and configures SDL3
  and drives ThorVG's separate Meson/Ninja build (`cmake/ThorVG.cmake`) even
  when the tidy target list is scoped to `graphscore_rendering` alone, since
  all four M05 rendering dependencies are fetched together behind one
  `if (GRAPHSCORE_BUILD_WRITER)` gate — the same cost profile that took the
  CI clang-tidy job off the critical path in the first place, now forced
  onto every local `git push` rather than only on changes that touch a
  gated branch. Until this repository moves to an organization account and
  restores the CI clang-tidy job (at which point it should run the writer-ON
  configuration too, closing this gap centrally), a reviewer or worker
  touching a `GRAPHSCORE_BUILD_WRITER`-gated branch must run the canonical
  `build/tidy` command above with `-DGRAPHSCORE_BUILD_WRITER=ON` by hand and
  report the result, as this milestone's rendering-dependency work did.
- Linux needs X11, Wayland, xkbcommon, and OpenGL (`libgl-dev`,
  `mesa-common-dev`) development packages for the SDL3 build; see
  `.github/workflows/ci.yml` for the exact list. The OpenGL headers back
  `SDL_OPENGL=ON`, which `cmake/SDL3.cmake` enables on Linux so ThorVG's
  rasterized output can present through a GPU-backed `SDL_Renderer`
  (ADR 0002 §A5); without them, SDL's own `CheckOpenGL` compiled probe fails
  and `cmake/SDL3.cmake`'s derived-result assertion (`HAVE_OPENGL`) fails
  configure rather than silently shipping a renderer-less build.
- Building the writer (`GRAPHSCORE_BUILD_WRITER=ON`, the default) needs a
  Python 3.7+ interpreter on the build host: `cmake/ThorVG.cmake` drives
  ThorVG's own Meson build, and Meson enforces that floor itself. Unlike
  every other dependency adapter in `cmake/`, ThorVG's own compilation is
  not a native CMake subproject configured in the same pass as GraphScore
  itself — it has no `CMakeLists.txt` at its pinned SHA. `cmake/ThorVG.cmake`
  instead fetches Meson's own source and locates a host `ninja` (>= 1.8.2,
  Meson's own documented floor) at CMake *configure* time, then drives
  `meson setup`/`ninja`/`ninja install` as an `ExternalProject_Add` step that
  runs during `cmake --build`, not during configure. A from-scratch
  `cmake --build --preset debug` therefore includes a second, separate
  build-tool invocation before `graphscore_rendering` can link the resulting
  static archive, and takes noticeably longer than a build touching only
  native CMake subprojects.
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
