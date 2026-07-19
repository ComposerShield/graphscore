# ADR 0001: Permissive Dependency Policy and Acceptance Checklist

| Status | Author | Date |
|--------|--------|------|
| Accepted | Phase A evaluation | 2026-07-18 |

## Context

GraphScore is licensed under Apache-2.0. The default complete build must
use only dependencies compatible with permissive commercial reuse. GPL,
AGPL, and mandatory commercial licensing are prohibited in the default
build. The decision record establishes a reusable acceptance checklist
that every dependency must pass before it can be included through CMake
`FetchContent`.

## Decision

Every default-build dependency must satisfy all items on the checklist
below. Dependencies are immutable-pinned by 40-character commit SHA in a
`cmake/dependencies.cmake` file that calls `FetchContent_Declare` with
`GIT_TAG` set to that SHA. Build documentation describes
`FetchContent` source overrides (`FETCHCONTENT_SOURCE_DIR_<NAME>`) and
populated caches (`FETCHCONTENT_FULLY_DISCONNECTED=ON`) for offline
builds.

### Reusable Dependency Acceptance Checklist

- [ ] **Source license** is confirmed at the pinned commit SHA. License
  file URL is recorded at that exact SHA (not a branch/tag reference).
- [ ] **Source license** is a known permissive license (MIT, BSD 2/3-clause,
  Apache-2.0, zlib, ISC, Old MIT/FTL, Unlicense, public-domain equivalent)
  or is dual-licensed with at least one such option. GPL, LGPL, AGPL, SSPL,
  and any license requiring mandatory commercial purchase or per-seat fees
  are rejected for the default build.
- [ ] **Patent grant** is present or absence is noted. Apache-2.0 provides
  an explicit patent grant. MIT, BSD, zlib, and Unlicense do not; this
  absence is recorded but is acceptable for the default build.
- [ ] **Notices and attribution** obligations are recorded. The project
  maintains a `docs/NOTICES.md` listing every dependency, its license,
  its SHA, and any required attribution text. Build artifacts and binary
  distributions must include this file.
- [ ] **Transitive source dependencies** (submodules, vendored-in sources,
  nested `FetchContent` pulls) are reviewed. Each transitive dependency
  must pass the same checklist independently. Dependencies that pull in
  GPL/LGPL code transitively (e.g. a permissive library that optionally
  links GMP) must have that path disabled.
- [ ] **Build-tool dependencies** (e.g. Meson, Python scripts, Rust
  toolchain requirements) are identified and recorded. Build-tool licenses
  are noted but do not require source-level GPL compatibility unless the
  tool's output embeds its own code.
- [ ] **`FetchContent` support** is present. The dependency must compile
  under CMake 3.20+ when fetched via `FetchContent`. Dependencies that
  require out-of-band build system bootstrapping (e.g. Rust `cargo`,
  Meson+ninja) are noted and require an adapter CMake wrapper.
- [ ] **Platform support** covers at minimum macOS 14+ arm64/x86-64,
  Windows 11 x86-64/arm64 (build-only for arm64), and Ubuntu 24.04-class
  Linux x86-64/arm64 (build-only for arm64) with Wayland and
  X11/XWayland.
- [ ] **Architecture support** covers x86-64 and arm64. Dependencies with
  SIMD assembly paths (SSE, AVX, NEON, SVE2) must fall back gracefully on
  unsupported architectures.
- [ ] **Redistribution terms** allow inclusion in a permissively licensed
  combined work and in binary distributions. Copyleft licensing, field-of-use
  restrictions, and mandatory royalty payments are rejected.
- [ ] **Warning isolation** is achievable. Third-party warnings must not
  cause GraphScore build failures. The dependency's CMake target will be
  consumed via `SYSTEM` include directories and separate compilation
  units where practical.
- [ ] **No third-party types leak** across the domain boundary, runtime
  C ABI, or persistence schemas. Dependency types are confined to writer
  and platform-shell translation units.
- [ ] **Rejection criteria**: any dependency that fails any of the above
  is rejected for the default build. A fallback or replacement must be
  recorded in a dependency ADR.

## Consequences

Every dependency added to the project must pass this checklist, recorded
in the dependency baseline ADR ([0002](0002-default-writer-dependency-baseline.md))
or a subsequent ADR. The checklist is reusable for future candidate
evaluations.

Build documentation will include instructions for:
- Overriding `FetchContent` source directories for offline/air-gapped
  builds.
- Populating CMake caches for fully disconnected builds.
- Enabling or disabling optional dependency features.
