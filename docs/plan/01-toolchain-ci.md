# Milestone 01: Toolchain And CI Foundation

## Goal

Create a reproducible, warning-clean C++23 repository foundation that continuously builds and tests every supported desktop platform.

## Dependencies

- [x] `M1-phase-1` Milestone 00 decisions.

## Deliverables

### Repository structure

- [x] `M1-phase-2` Root CMake project with clear `src`, `include`, `tests`, `apps`, `tools`, `cmake`, and `docs` boundaries.
- [x] `M1-phase-3` Initialize the Git repository if it is still absent and track `docs/plan`, root `AGENTS.md`, root `CLAUDE.md` symlink, `.githooks`, CMake presets, dependency revisions, tests, and all other project configuration needed to reproduce the build.
- [x] `M1-phase-4` Separate targets for the domain, runtime implementation, runtime C ABI shared library, writer shell, plugin scanner, and tests.
- [x] `M1-phase-5` No monolithic target that makes writer dependencies transitively required by the runtime.
- [x] `M1-phase-6` Apache-2.0 license, contribution guidance, code style, dependency policy, and build documentation.
- [x] `M1-phase-7` Create a root `AGENTS.md` before production implementation begins. It records repository layout, canonical configure/build/test/lint commands, C++23/const rules, realtime prohibitions, architecture boundaries, FetchContent/license policy, generated/third-party file rules, platform caveats, and links to this plan.
- [x] `M1-phase-8` State in `AGENTS.md` that commit messages describe only the project change and must not mention the model, assistant, or vendor used to produce it, including names such as Claude, ChatGPT, or OpenAI, and must not add AI-generated attribution trailers.
- [x] `M1-phase-9` Create and track `CLAUDE.md` as a relative symbolic link to `AGENTS.md`, never as a duplicated instruction file, so both entry points always expose identical guidance.
- [x] `M1-phase-10` Treat `AGENTS.md` as maintained engineering guidance: later milestones update it whenever commands, target names, or non-obvious constraints change.

### CMake and dependencies

- [x] `M1-phase-11` Require C++23 and Clang/AppleClang with an actionable configure-time failure for unsupported compilers.
- [x] `M1-phase-12` Use target-scoped warning, sanitizer, and analysis configuration.
- [x] `M1-phase-13` Enable Clang `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wold-style-cast -Wnon-virtual-dtor -Wcast-align -Wunused -Wnull-dereference -Wdouble-promotion -Wformat=2 -Wimplicit-fallthrough` or the clang-cl equivalent, and treat every remaining warning as an error for GraphScore-owned code.
- [x] `M1-phase-14` Provide checked-in CMake presets for developer debug, release, ASan/UBSan, TSan, and CI builds.
- [x] `M1-phase-15` Fetch GTest and accepted dependencies at immutable commit hashes.
- [x] `M1-phase-16` Support `FETCHCONTENT_SOURCE_DIR_<NAME>` overrides and a documented offline cache workflow.
- [x] `M1-phase-17` Generate or validate a dependency/license inventory.

### Const-correctness policy

- [x] `M1-phase-18` Configure clang-tidy checks that encourage `constexpr`, `const`, narrow scopes, immutable data flow, and const member functions.
- [x] `M1-phase-19` Document exceptions for realtime state, atomics, caches, platform handles, and other genuinely mutable objects.
- [x] `M1-phase-20` Avoid meaningless `const` on returned values or interfaces where it harms usability without increasing safety.

### Local quality gates

- [x] `M1-phase-21` Add project clang-format 18 and clang-tidy configuration.
- [x] `M1-phase-22` Add cpplint configuration and a CMake lint target.
- [x] `M1-phase-23` Add executable `.githooks/pre-commit` invoking cpplint and clang-format 18 checks on relevant files.
- [x] `M1-phase-24` Provide a bootstrap command that sets `core.hooksPath=.githooks` without changing global Git configuration.
- [x] `M1-phase-25` Keep CI authoritative by invoking the same checks independently of local hooks.

### GitHub Actions

- [ ] `M1-phase-26` Build and run GTest on macOS arm64/x86-64 release paths, Windows x86-64, and Linux x86-64.
- [ ] `M1-phase-27` Build Windows arm64 and Linux arm64 artifacts without initially claiming native test coverage.
- [x] `M1-phase-28` Exercise Debug and optimized configurations across the matrix without needlessly duplicating every expensive job.
- [x] `M1-phase-29` Run cpplint, clang-format 18 verification, clang-tidy, ASan/UBSan, and TSan on suitable Clang runners.
- [x] `M1-phase-30` Cache immutable FetchContent downloads without making cache presence required.
- [x] `M1-phase-31` Upload test reports and failure diagnostics.

### Skeleton artifacts

- [ ] `M1-phase-32` Build an empty writer window on each desktop platform.
- [x] `M1-phase-33` Build/load the runtime shared library and call a version function through its C header.
- [x] `M1-phase-34` Establish hidden symbol visibility with explicitly exported C ABI symbols.

## Acceptance Criteria

- [ ] `M1-phase-35` A clean checkout configures, builds, and tests using documented commands on macOS, Windows, and Linux.
- [x] `M1-phase-36` Every plan/checklist, agent instruction, hook, build file, schema, test fixture, and required non-generated asset is present in source control.
- [x] `M1-phase-37` Git records `CLAUDE.md` as a symbolic link targeting `AGENTS.md`, and CI detects replacement by a regular copied file.
- [ ] `M1-phase-38` All pull requests run three-platform Clang build/test jobs.
- [ ] `M1-phase-39` macOS x86-64/arm64 and Windows/Linux x86-64 artifacts execute smoke tests; Windows/Linux arm64 artifacts compile.
- [x] `M1-phase-40` GraphScore code is warning-clean with warnings treated as errors.
- [x] `M1-phase-41` The hook is version-controlled, installable with one documented command, and uses the same cpplint rules as CI.
- [x] `M1-phase-42` Sanitizer smoke tests pass and demonstrate that failures are reported correctly.
- [x] `M1-phase-43` Runtime consumers need only the public C header and shared library.
- [x] `M1-phase-44` A new contributor or coding agent can use `AGENTS.md` to build, test, lint, and identify realtime/runtime boundaries without undocumented setup knowledge.
- [x] `M1-phase-45` `AGENTS.md` explicitly prohibits model/vendor references and AI-attribution trailers in commit messages.

## Test Focus

- [x] `M1-phase-46` C ABI version/symbol smoke test from a C translation unit.
- [x] `M1-phase-47` CMake consumer test proving the runtime does not pull writer dependencies.
- [x] `M1-phase-48` Dependency override/offline-cache configure test.
- [x] `M1-phase-49` Source-control metadata test verifies `CLAUDE.md` uses symlink mode and resolves to `AGENTS.md` on symlink-capable checkouts.
- [x] `M1-phase-50` Intentional lint/format/sanitizer failure checks in CI setup validation, removed or disabled after validation.

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
