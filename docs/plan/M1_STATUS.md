# Milestone 01 Status

**Last updated**: 2026-07-21

Scope and acceptance criteria for M1 live in
[01-toolchain-ci.md](01-toolchain-ci.md). Checkbox progress lives in
[CHECKLIST.md](CHECKLIST.md). This file records the working phase
breakdown used to implement M1 incrementally, and which phase is current.
The phases are an implementation sequencing choice, not a separate
source of deliverables — each phase feeds one or more CHECKLIST.md boxes,
and a box is only checked once every detailed bullet in its linked
01-toolchain-ci.md section is actually done.

## Phases

| # | Phase | Status |
|---|---|---|
| 1 | Repository structure & governance — directory scaffold, `AGENTS.md`, tracked `CLAUDE.md` symlink | Done (`84529ae`) |
| 2 | CMake root foundation — compiler gate, target-scoped warnings, sanitizer switch, presets, GTest FetchContent + license promotion | Done (`bd0fd29`) |
| 3 | Full target DAG skeleton — all 26 ADR 0003 targets as stubs with exact permitted edges, plus the `graphscore_<component>_test` targets required by ADR 0003 §2.3 | Next |
| 4 | Machine-audit scripts — the 8 mechanisms specified in ADR 0003 §7, wired to an `audit_architecture` CMake target | Not started |
| 5 | Const-correctness policy — clang-tidy configuration and documented mutability exceptions | Not started |
| 6 | Local quality gates — clang-format, cpplint + CMake lint target, `.githooks/pre-commit`, bootstrap script | Not started |
| 7 | GitHub Actions three-platform matrix | Not started |
| 8 | Skeleton artifact content — SDL3 fetch/adapter, empty writer window, runtime shared library + callable C ABI version function | Not started |
| 9 | Acceptance criteria & test-focus verification pass — remaining tests (symlink metadata, offline-cache configure, intentional lint/format/sanitizer failure validation), final checklist close-out | Not started |

## Scope decisions made during implementation

- **SDL3/HarfBuzz/miniaudio/PortAudio/RtMidi are not fetched in Phase 2.**
  Phase 2 only fetches GTest, the one dependency M1's own foundation needs
  immediately. SDL3 is deferred to Phase 8, where the skeleton writer
  window actually consumes it. The other writer/runtime dependencies in
  ADR 0002's "CMake Adapters Required" table stay PROVISIONAL until the
  milestone that first needs them (M05/M08) fetches and adapts them —
  M1 only needs to prove the FetchContent *mechanism* works, which
  Phase 2 already demonstrates against GTest.
- **GTest was promoted, not re-pinned.** The existing M0 spike-only
  GoogleTest pin (`6910c9d9165801d8827d628cb72eb7ea9dd538c5`, 1.16.0) was
  reused and promoted to POLICY-CLEARED/production status (ADR 0002 §11)
  rather than pinning a newer release, since its license was already
  reviewed and its committed license file already verified byte-identical
  at that SHA.

## Repository Notes

- The `tests/smoke/` target added in Phase 2 is temporary — it exists to
  prove the compiler gate, warning flags, and GTest wiring work end to end
  before real per-target test suites exist. It is removed once Phase 3
  lands the full target DAG and its test targets.
