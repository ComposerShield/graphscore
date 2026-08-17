# Milestone 00: Architecture And Risk Spikes

## Goal

Retire the highest-risk technical and licensing questions before production
architecture depends on them.

The deliverable of this milestone is a set of **written decisions**. Spike code
exists only to make a decision defensible; it is scratch, and it is deleted or
quarantined before Milestone 01 starts.

## Dependencies

None.

## Spike Rules

These rules override the cross-milestone Definition Of Done for this milestone
only. They exist because M0 previously consumed days producing production-grade
harnesses around disposable code.

- A spike answers one question: **does this work, what breaks, and what is the
  fallback?** Its output is one ADR of two pages or less.
- Spike code is scratch. Every spike source file carries a `DISPOSABLE` header
  comment and lives under `spikes/`. None of it ships.
- Spikes are **exempt** from: GTest coverage requirements, ASan/UBSan/TSan runs,
  clang-tidy, cpplint, warnings-as-errors, evidence catalogs, provenance
  manifests, deterministic-output hashes, and formal reviewer approval cycles.
  A spike may use any of these if they are the fastest route to the answer, but
  none is a gate.
- Prefer a literature/license/API survey over writing code. A candidate that
  fails the ADR 0001 license gate is rejected on paper and never fetched.
- Every spike carries a stated time box. Reaching the time box without a clear
  answer **is** an answer: record the fallback and move on.
- A spike that only confirms a well-established property of a mature dependency
  (that SDL3 opens a window, that HarfBuzz shapes text) is not worth running.
  Spike the parts that are genuinely unknown.

## Deliverables

### Permissive dependency policy — COMPLETE

- [x] `M0-phase-1` Confirm Apache-2.0 project licensing and contribution headers/policy.
- [x] `M0-phase-2` Create a dependency acceptance checklist covering source license,
      transitive licenses, patent terms, notices, FetchContent support, platform
      support, and redistribution.
- [x] `M0-phase-3` Evaluate a permissive writer shell (SDL3, AccessKit, HarfBuzz/FreeType,
      audio device library, MIDI utility library).
- [x] `M0-phase-4` Verify that no selected default-build dependency introduces GPL/AGPL or
      mandatory commercial licensing.
- [x] `M0-phase-5` Record exact revisions and identify dependencies needing CMake adapters.

Recorded in ADR 0001 and ADR 0002.

### Architectural boundaries — COMPLETE

- [x] `M0-phase-6` Domain and command model.
- [x] `M0-phase-7` Notation layout, hit testing, and toolkit-neutral drawing commands.
- [x] `M0-phase-8` Adaptive playback scheduler and MIDI model.
- [x] `M0-phase-9` Runtime C ABI and cooked-asset loader.
- [x] `M0-phase-10` Writer platform shell, accessibility bridge, audio devices, VST3 adapter.
- [x] `M0-phase-11` Plugin scanner helper and game-engine wrappers.

The domain, scheduler, persistence, and C ABI must build without UI,
audio-device, or VST3 dependencies. Recorded in ADR 0003.

### Rendering and accessibility spike — COMPLETE (macOS)

- [x] `M0-phase-12` Open a native window and render a zoomable graph with notation staves
      using a SMuFL-compatible font.
- [x] `M0-phase-13` Demonstrate transforms, clipping, text shaping, hit testing, and rounded
      orthogonal paths.
- [x] `M0-phase-14` Expose representative graph nodes, connectors, measures, notes, controls,
      selection, and actions to VoiceOver.
- [x] `M0-phase-15` Confirm trackpad pan/pinch fidelity and native-handle access for plugin
      editors.

Recorded in ADR 0004.

Windows and Linux physical-GUI verification remains a platform hardening gate,
not an M0 blocker. Production screen-reader integration and validation are
deferred indefinitely; the M0 question was whether a custom-drawn notation
canvas could be made screen-reader navigable at all, and macOS answered it.

### Engraving-engine decision — COMPLETE

- [x] `M0-phase-16` Time-boxed license/API survey of embeddable engraving engines.
- [x] `M0-phase-17` Record the owned semantic-layout direction using SMuFL glyphs and a
      toolkit-neutral render list.

Recorded in ADR 0005. No permissively licensed embeddable interactive engraving
engine exists; GraphScore owns its layout.

- [x] `M0-phase-18` Delete or quarantine `spikes/m0/engraving-engine/` — the decision is made
      and the proof code is no longer load-bearing. Source, tests, and CMake
      removed 2026-07-21; evidence logs and README retained as ADR citations.

### Cooked-format decision — paper ADR, no spike

The runtime constraints in ADR 0003 already discriminate between the candidates:
zero-allocation load, host-supplied allocator callbacks, byte-deterministic
export, and full bounds validation on hostile input. Schema-generated formats
fight all four. This is a design decision, not an empirical unknown, and
Milestone 03 is a cheap place to discover it was wrong.

- [x] `M0-phase-19` Write ADR 0006 selecting a cooked-format direction. **Decided: GraphScore
      owns its binary format.** FlatBuffers rejected on allocator control,
      byte-determinism cost, and the no-third-party-types rule.
- [x] `M0-phase-20` Record the fallback if the selected direction fails in Milestone 03 —
      FlatBuffers confined behind the owned reader API in one library.
- [x] `M0-phase-21` Delete `spikes/m0/cooked-format/` — stub scaffold removed 2026-07-21.

**Time box: 2 hours, documentation only.** No format is implemented in M0.
Round-trip and malformed-buffer tests belong to Milestone 03, where the format
is real.

### Direct VST3 hosting spike — the one remaining spike

This is the only genuine unknown left in M0 and the only place M0 still writes
code. Native plugin-editor embedding and crash-tolerant out-of-process scanning
are real risks that cannot be resolved on paper.

**Time box: one working day.**

- [x] `M0-phase-22` Verify the VST3 SDK license directly against upstream. **Confirmed
      2026-07-21**: MIT across the root repo and all core submodules, including
      `pluginterfaces`, which carried the former GPLv3 + Steinberg proprietary
      dual license. VSTGUI is BSD-3-clause-style and is not required — it must
      stay disabled so it never enters the dependency closure. Full record in
      `spikes/m0/vst3-hosting/README.md`.
- [x] `M0-phase-23` Confirm FetchContent integration at a pinned immutable commit.
- [x] `M0-phase-24` Instantiate one test instrument and one test effect on macOS arm64.
- [x] `M0-phase-25` Process a MIDI/audio block, save and restore opaque state, query latency
      and tail.
- [x] `M0-phase-26` Attach, resize, and focus a native plugin editor. **Human-observed PASS
      on all four criteria**; see `spikes/m0/vst3-hosting/EDITOR-GATE.md`.
- [x] `M0-phase-27` Scan plugins in a helper process and survive a deliberately crashed or
      hung scan with a timeout and a diagnostic.

Scope notes:

- macOS arm64 only. Windows, Linux/Wayland, and x86-64 verification move to
  Milestone 08, which is where cross-platform audio is built anyway.
- Wayland native-editor embedding is the highest residual risk in the whole
  plan. It is **not** resolved by this spike. Record it as an accepted open risk
  with the fallback already in the product spec: the GraphScore-owned generic
  parameter view.
- No test suite, no sanitizers, no evidence catalog. A working binary plus the
  ADR is the deliverable.

Recorded in ADR 0007.

## Acceptance Criteria

- [x] `M0-phase-28` Written decisions select a permissive UI/render/audio/MIDI baseline
      (ADR 0002) and a cooked-format direction (ADR 0006).
- [x] `M0-phase-29` A fallback is recorded for every rejected or not-yet-proven dependency.
- [x] `M0-phase-30` The VST3 prototype completes instantiate → process → state → editor →
      helper-process scan on macOS arm64, or its failure and fallback are
      recorded.
- [x] `M0-phase-31` The target dependency graph prevents writer-only libraries from entering
      the runtime (ADR 0003).
- [x] `M0-phase-32` No production milestone is blocked on an unresolved licensing assumption.
- [ ] `M0-phase-33` All spike directories under `spikes/m0/` are deleted or explicitly
      quarantined.

## Exit Decision

Do not start Milestone 01 until the default writer stack is confirmed
permissively reusable and direct VST3 hosting has a credible implementation
path on at least one platform.

Cross-platform proof and performance targets are Milestone 08 and Milestone 10
gates. Production accessibility integration and validation are deferred
indefinitely. None is an M0 exit condition.
