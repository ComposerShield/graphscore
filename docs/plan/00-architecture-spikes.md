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

- [x] Confirm Apache-2.0 project licensing and contribution headers/policy.
- [x] Create a dependency acceptance checklist covering source license,
      transitive licenses, patent terms, notices, FetchContent support, platform
      support, and redistribution.
- [x] Evaluate a permissive writer shell (SDL3, AccessKit, HarfBuzz/FreeType,
      audio device library, MIDI utility library).
- [x] Verify that no selected default-build dependency introduces GPL/AGPL or
      mandatory commercial licensing.
- [x] Record exact revisions and identify dependencies needing CMake adapters.

Recorded in ADR 0001 and ADR 0002.

### Architectural boundaries — COMPLETE

- [x] Domain and command model.
- [x] Notation layout, hit testing, and toolkit-neutral drawing commands.
- [x] Adaptive playback scheduler and MIDI model.
- [x] Runtime C ABI and cooked-asset loader.
- [x] Writer platform shell, accessibility bridge, audio devices, VST3 adapter.
- [x] Plugin scanner helper and game-engine wrappers.

The domain, scheduler, persistence, and C ABI must build without UI,
audio-device, or VST3 dependencies. Recorded in ADR 0003.

### Rendering and accessibility spike — COMPLETE (macOS)

- [x] Open a native window and render a zoomable graph with notation staves
      using a SMuFL-compatible font.
- [x] Demonstrate transforms, clipping, text shaping, hit testing, and rounded
      orthogonal paths.
- [x] Expose representative graph nodes, connectors, measures, notes, controls,
      selection, and actions to VoiceOver.
- [x] Confirm trackpad pan/pinch fidelity and native-handle access for plugin
      editors.

Recorded in ADR 0004.

**Windows and Linux screen-reader and physical-GUI verification is an
Milestone 10 hardening gate, not an M0 blocker.** SDL3 and AccessKit both
support those platforms; the M0 question was whether a custom-drawn notation
canvas can be made screen-reader navigable at all, and macOS answered it.

### Engraving-engine decision — COMPLETE

- [x] Time-boxed license/API survey of embeddable engraving engines.
- [x] Record the owned semantic-layout direction using SMuFL glyphs and a
      toolkit-neutral render list.

Recorded in ADR 0005. No permissively licensed embeddable interactive engraving
engine exists; GraphScore owns its layout.

- [x] Delete or quarantine `spikes/m0/engraving-engine/` — the decision is made
      and the proof code is no longer load-bearing. Source, tests, and CMake
      removed 2026-07-21; evidence logs and README retained as ADR citations.

### Cooked-format decision — paper ADR, no spike

The runtime constraints in ADR 0003 already discriminate between the candidates:
zero-allocation load, host-supplied allocator callbacks, byte-deterministic
export, and full bounds validation on hostile input. Schema-generated formats
fight all four. This is a design decision, not an empirical unknown, and
Milestone 03 is a cheap place to discover it was wrong.

- [x] Write ADR 0006 selecting a cooked-format direction. **Decided: GraphScore
      owns its binary format.** FlatBuffers rejected on allocator control,
      byte-determinism cost, and the no-third-party-types rule.
- [x] Record the fallback if the selected direction fails in Milestone 03 —
      FlatBuffers confined behind the owned reader API in one library.
- [x] Delete `spikes/m0/cooked-format/` — stub scaffold removed 2026-07-21.

**Time box: 2 hours, documentation only.** No format is implemented in M0.
Round-trip and malformed-buffer tests belong to Milestone 03, where the format
is real.

### Direct VST3 hosting spike — the one remaining spike

This is the only genuine unknown left in M0 and the only place M0 still writes
code. Native plugin-editor embedding and crash-tolerant out-of-process scanning
are real risks that cannot be resolved on paper.

**Time box: one working day.**

- [x] Verify the VST3 SDK license directly against upstream. **Confirmed
      2026-07-21**: MIT across the root repo and all core submodules, including
      `pluginterfaces`, which carried the former GPLv3 + Steinberg proprietary
      dual license. VSTGUI is BSD-3-clause-style and is not required — it must
      stay disabled so it never enters the dependency closure. Full record in
      `spikes/m0/vst3-hosting/README.md`.
- [ ] Confirm FetchContent integration at a pinned immutable commit.
- [ ] Instantiate one test instrument and one test effect on macOS arm64.
- [ ] Process a MIDI/audio block, save and restore opaque state, query latency
      and tail.
- [ ] Attach, resize, and focus a native plugin editor.
- [ ] Scan plugins in a helper process and survive a deliberately crashed or
      hung scan with a timeout and a diagnostic.

Scope notes:

- macOS arm64 only. Windows, Linux/Wayland, and x86-64 verification move to
  Milestone 08, which is where cross-platform audio is built anyway.
- Wayland native-editor embedding is the highest residual risk in the whole
  plan. It is **not** resolved by this spike. Record it as an accepted open risk
  with the fallback already in the product spec: the GraphScore-owned accessible
  generic parameter view.
- No test suite, no sanitizers, no evidence catalog. A working binary plus the
  ADR is the deliverable.

## Acceptance Criteria

- [ ] Written decisions select a permissive UI/render/audio/MIDI baseline
      (ADR 0002) and a cooked-format direction (ADR 0006).
- [ ] A fallback is recorded for every rejected or not-yet-proven dependency.
- [ ] The VST3 prototype completes instantiate → process → state → editor →
      helper-process scan on macOS arm64, or its failure and fallback are
      recorded.
- [ ] The target dependency graph prevents writer-only libraries from entering
      the runtime (ADR 0003).
- [ ] No production milestone is blocked on an unresolved licensing assumption.
- [ ] All spike directories under `spikes/m0/` are deleted or explicitly
      quarantined.

## Exit Decision

Do not start Milestone 01 until the default writer stack is confirmed
permissively reusable and direct VST3 hosting has a credible implementation
path on at least one platform.

Cross-platform proof, performance targets, and hardened accessibility are
Milestone 08 and Milestone 10 gates. They are not M0 exit conditions.
