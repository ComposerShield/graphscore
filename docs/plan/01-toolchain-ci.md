# Milestone 01: Toolchain And CI Foundation

## Goal

Create a reproducible, warning-clean C++23 repository foundation that continuously builds and tests every supported desktop platform.

## Dependencies

- [ ] Milestone 00 decisions.

## Deliverables

### Repository structure

- [ ] Root CMake project with clear `src`, `include`, `tests`, `apps`, `tools`, `cmake`, and `docs` boundaries.
- [ ] Initialize the Git repository if it is still absent and track `docs/plan`, root `AGENTS.md`, root `CLAUDE.md` symlink, `.githooks`, CMake presets, dependency revisions, tests, and all other project configuration needed to reproduce the build.
- [ ] Separate targets for the domain, runtime implementation, runtime C ABI shared library, writer shell, plugin scanner, and tests.
- [ ] No monolithic target that makes writer dependencies transitively required by the runtime.
- [ ] Apache-2.0 license, contribution guidance, code style, dependency policy, and build documentation.
- [ ] Create a root `AGENTS.md` before production implementation begins. It records repository layout, canonical configure/build/test/lint commands, C++23/const rules, realtime prohibitions, architecture boundaries, FetchContent/license policy, generated/third-party file rules, platform caveats, and links to this plan.
- [ ] State in `AGENTS.md` that commit messages describe only the project change and must not mention the model, assistant, or vendor used to produce it, including names such as Claude, ChatGPT, or OpenAI, and must not add AI-generated attribution trailers.
- [ ] Create and track `CLAUDE.md` as a relative symbolic link to `AGENTS.md`, never as a duplicated instruction file, so both entry points always expose identical guidance.
- [ ] Treat `AGENTS.md` as maintained engineering guidance: later milestones update it whenever commands, target names, or non-obvious constraints change.

### CMake and dependencies

- [ ] Require C++23 and Clang/AppleClang with an actionable configure-time failure for unsupported compilers.
- [ ] Use target-scoped warning, sanitizer, and analysis configuration.
- [ ] Enable Clang `-Weverything` or the clang-cl equivalent, maintain a small commented/audited suppression list, and treat every remaining warning as an error for GraphScore-owned code.
- [ ] Provide checked-in CMake presets for developer debug, release, ASan/UBSan, TSan, and CI builds.
- [ ] Fetch GTest and accepted dependencies at immutable commit hashes.
- [ ] Support `FETCHCONTENT_SOURCE_DIR_<NAME>` overrides and a documented offline cache workflow.
- [ ] Generate or validate a dependency/license inventory.

### Const-correctness policy

- [ ] Configure clang-tidy checks that encourage `constexpr`, `const`, narrow scopes, immutable data flow, and const member functions.
- [ ] Document exceptions for realtime state, atomics, caches, platform handles, and other genuinely mutable objects.
- [ ] Avoid meaningless `const` on returned values or interfaces where it harms usability without increasing safety.

### Local quality gates

- [ ] Add project clang-format and clang-tidy configuration.
- [ ] Add cpplint configuration and a CMake lint target.
- [ ] Add executable `.githooks/pre-commit` invoking cpplint and formatting checks on relevant files.
- [ ] Provide a bootstrap command that sets `core.hooksPath=.githooks` without changing global Git configuration.
- [ ] Keep CI authoritative by invoking the same checks independently of local hooks.

### GitHub Actions

- [ ] Build and run GTest on macOS arm64/x86-64 release paths, Windows x86-64, and Linux x86-64.
- [ ] Build Windows arm64 and Linux arm64 artifacts without initially claiming native test coverage.
- [ ] Exercise Debug and optimized configurations across the matrix without needlessly duplicating every expensive job.
- [ ] Run cpplint, clang-format verification, clang-tidy, ASan/UBSan, and TSan on suitable Clang runners.
- [ ] Cache immutable FetchContent downloads without making cache presence required.
- [ ] Upload test reports and failure diagnostics.

### Skeleton artifacts

- [ ] Build an empty writer window on each desktop platform.
- [ ] Build/load the runtime shared library and call a version function through its C header.
- [ ] Establish hidden symbol visibility with explicitly exported C ABI symbols.

## Acceptance Criteria

- [ ] A clean checkout configures, builds, and tests using documented commands on macOS, Windows, and Linux.
- [ ] Every plan/checklist, agent instruction, hook, build file, schema, test fixture, and required non-generated asset is present in source control.
- [ ] Git records `CLAUDE.md` as a symbolic link targeting `AGENTS.md`, and CI detects replacement by a regular copied file.
- [ ] All pull requests run three-platform Clang build/test jobs.
- [ ] macOS x86-64/arm64 and Windows/Linux x86-64 artifacts execute smoke tests; Windows/Linux arm64 artifacts compile.
- [ ] GraphScore code is warning-clean with warnings treated as errors.
- [ ] The hook is version-controlled, installable with one documented command, and uses the same cpplint rules as CI.
- [ ] Sanitizer smoke tests pass and demonstrate that failures are reported correctly.
- [ ] Runtime consumers need only the public C header and shared library.
- [ ] A new contributor or coding agent can use `AGENTS.md` to build, test, lint, and identify realtime/runtime boundaries without undocumented setup knowledge.
- [ ] `AGENTS.md` explicitly prohibits model/vendor references and AI-attribution trailers in commit messages.

## Test Focus

- [ ] C ABI version/symbol smoke test from a C translation unit.
- [ ] CMake consumer test proving the runtime does not pull writer dependencies.
- [ ] Dependency override/offline-cache configure test.
- [ ] Source-control metadata test verifies `CLAUDE.md` uses symlink mode and resolves to `AGENTS.md` on symlink-capable checkouts.
- [ ] Intentional lint/format/sanitizer failure checks in CI setup validation, removed or disabled after validation.
