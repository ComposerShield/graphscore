# Milestone 01: Toolchain And CI Foundation

## Goal

Create a reproducible, warning-clean C++23 repository foundation that continuously builds and tests every supported desktop platform.

## Dependencies

- [x] Milestone 00 decisions.

## Deliverables

### Repository structure

- [x] Root CMake project with clear `src`, `include`, `tests`, `apps`, `tools`, `cmake`, and `docs` boundaries.
- [x] Initialize the Git repository if it is still absent and track `docs/plan`, root `AGENTS.md`, root `CLAUDE.md` symlink, `.githooks`, CMake presets, dependency revisions, tests, and all other project configuration needed to reproduce the build.
- [x] Separate targets for the domain, runtime implementation, runtime C ABI shared library, writer shell, plugin scanner, and tests.
- [x] No monolithic target that makes writer dependencies transitively required by the runtime.
- [x] Apache-2.0 license, contribution guidance, code style, dependency policy, and build documentation.
- [x] Create a root `AGENTS.md` before production implementation begins. It records repository layout, canonical configure/build/test/lint commands, C++23/const rules, realtime prohibitions, architecture boundaries, FetchContent/license policy, generated/third-party file rules, platform caveats, and links to this plan.
- [x] State in `AGENTS.md` that commit messages describe only the project change and must not mention the model, assistant, or vendor used to produce it, including names such as Claude, ChatGPT, or OpenAI, and must not add AI-generated attribution trailers.
- [x] Create and track `CLAUDE.md` as a relative symbolic link to `AGENTS.md`, never as a duplicated instruction file, so both entry points always expose identical guidance.
- [x] Treat `AGENTS.md` as maintained engineering guidance: later milestones update it whenever commands, target names, or non-obvious constraints change.

### CMake and dependencies

- [x] Require C++23 and Clang/AppleClang with an actionable configure-time failure for unsupported compilers.
- [x] Use target-scoped warning, sanitizer, and analysis configuration.
- [x] Enable Clang `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wold-style-cast -Wnon-virtual-dtor -Wcast-align -Wunused -Wnull-dereference -Wdouble-promotion -Wformat=2 -Wimplicit-fallthrough` or the clang-cl equivalent, and treat every remaining warning as an error for GraphScore-owned code.
- [x] Provide checked-in CMake presets for developer debug, release, ASan/UBSan, TSan, and CI builds.
- [x] Fetch GTest and accepted dependencies at immutable commit hashes.
- [x] Support `FETCHCONTENT_SOURCE_DIR_<NAME>` overrides and a documented offline cache workflow.
- [x] Generate or validate a dependency/license inventory.

### Const-correctness policy

- [x] Configure clang-tidy checks that encourage `constexpr`, `const`, narrow scopes, immutable data flow, and const member functions.
- [x] Document exceptions for realtime state, atomics, caches, platform handles, and other genuinely mutable objects.
- [x] Avoid meaningless `const` on returned values or interfaces where it harms usability without increasing safety.

### Local quality gates

- [x] Add project clang-format and clang-tidy configuration.
- [x] Add cpplint configuration and a CMake lint target.
- [x] Add executable `.githooks/pre-commit` invoking cpplint and formatting checks on relevant files.
- [x] Provide a bootstrap command that sets `core.hooksPath=.githooks` without changing global Git configuration.
- [x] Keep CI authoritative by invoking the same checks independently of local hooks.

### GitHub Actions

- [ ] Build and run GTest on macOS arm64/x86-64 release paths, Windows x86-64, and Linux x86-64.
- [ ] Build Windows arm64 and Linux arm64 artifacts without initially claiming native test coverage.
- [x] Exercise Debug and optimized configurations across the matrix without needlessly duplicating every expensive job.
- [x] Run cpplint, clang-format verification, clang-tidy, ASan/UBSan, and TSan on suitable Clang runners.
- [x] Cache immutable FetchContent downloads without making cache presence required.
- [x] Upload test reports and failure diagnostics.

### Skeleton artifacts

- [ ] Build an empty writer window on each desktop platform.
- [x] Build/load the runtime shared library and call a version function through its C header.
- [x] Establish hidden symbol visibility with explicitly exported C ABI symbols.

## Acceptance Criteria

- [ ] A clean checkout configures, builds, and tests using documented commands on macOS, Windows, and Linux.
- [x] Every plan/checklist, agent instruction, hook, build file, schema, test fixture, and required non-generated asset is present in source control.
- [x] Git records `CLAUDE.md` as a symbolic link targeting `AGENTS.md`, and CI detects replacement by a regular copied file.
- [ ] All pull requests run three-platform Clang build/test jobs.
- [ ] macOS x86-64/arm64 and Windows/Linux x86-64 artifacts execute smoke tests; Windows/Linux arm64 artifacts compile.
- [x] GraphScore code is warning-clean with warnings treated as errors.
- [x] The hook is version-controlled, installable with one documented command, and uses the same cpplint rules as CI.
- [x] Sanitizer smoke tests pass and demonstrate that failures are reported correctly.
- [x] Runtime consumers need only the public C header and shared library.
- [x] A new contributor or coding agent can use `AGENTS.md` to build, test, lint, and identify realtime/runtime boundaries without undocumented setup knowledge.
- [x] `AGENTS.md` explicitly prohibits model/vendor references and AI-attribution trailers in commit messages.

## Test Focus

- [x] C ABI version/symbol smoke test from a C translation unit.
- [x] CMake consumer test proving the runtime does not pull writer dependencies.
- [x] Dependency override/offline-cache configure test.
- [x] Source-control metadata test verifies `CLAUDE.md` uses symlink mode and resolves to `AGENTS.md` on symlink-capable checkouts.
- [x] Intentional lint/format/sanitizer failure checks in CI setup validation, removed or disabled after validation.

## Remaining To Close This Milestone

Everything in this milestone is implemented and committed. The five
unchecked boxes above are all the same fact: **the CI workflow has not yet
run.** Each is a claim about the platform matrix that only a green run on
GitHub's runners can substantiate, so none is checked on the strength of
local verification alone.

Verified locally on macOS arm64: configure, build, all 25 ctest tests, the
`lint` target, all seven `audit_architecture` audits, the ASan/UBSan and TSan
presets, the runtime-only (`-DGRAPHSCORE_BUILD_WRITER=OFF`) configuration,
and a real Cocoa window opened by `graphscore_writer_app`.

Not yet exercised anywhere: Windows (clang-cl, x86-64 and arm64), Linux
(x86-64 and arm64), macOS x86-64. The Linux and Windows paths in
`.github/workflows/ci.yml` — the SDL3 system dependencies, the clang-cl
warning flags, the `windows-11-arm` and `ubuntu-24.04-arm` runner images —
are written from the documented behaviour of those toolchains and should be
expected to need a round of fixes on their first run.

Close this milestone by pushing to a branch, opening a pull request, and
fixing whatever the matrix reports.
