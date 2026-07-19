# Milestone 00: Architecture And Risk Spikes

## Goal

Retire the highest-risk technical and licensing questions before production architecture depends on them. Produce small disposable vertical slices and written decisions, not partial product implementations.

## Dependencies

None.

## Deliverables

### Permissive dependency policy

- [ ] Confirm Apache-2.0 project licensing and contribution headers/policy.
- [ ] Create a dependency acceptance checklist covering source license, transitive licenses, patent terms, notices, FetchContent support, platform support, and redistribution.
- [ ] Evaluate a permissive writer shell assembled from candidates such as SDL3, AccessKit, a backend-neutral vector renderer, HarfBuzz/FreeType, an audio-device library, and a MIDI utility library.
- [ ] Verify that no selected default-build dependency introduces GPL/AGPL or mandatory commercial licensing.
- [ ] Record exact revisions and identify dependencies that need CMake adapters without forking upstream.

### Architectural boundaries

Define targets and dependency direction for:

- [x] Domain and command model.
- [x] Notation layout, hit testing, and toolkit-neutral drawing commands.
- [x] Adaptive playback scheduler and MIDI model.
- [x] Runtime C ABI and cooked-asset loader.
- [x] Writer platform shell, accessibility bridge, audio devices, and VST3 adapter.
- [x] Plugin scanner helper and game-engine wrappers.

The domain, scheduler, persistence, and C ABI must build without UI, audio-device, or VST3 dependencies.

### Rendering and accessibility spike

- [ ] Open native windows on macOS, Windows, Wayland, and X11/XWayland.
- [ ] Render a zoomable graph with several notation staves using a SMuFL-compatible font.
- [ ] Demonstrate transforms, clipping, text shaping, hit testing, and rounded orthogonal paths.
- [ ] Expose representative graph nodes, connectors, measures, notes, controls, selection, and actions through VoiceOver, Narrator, and a Linux screen reader.
- [ ] Measure pointer/trackpad input fidelity and native-handle access needed for plugin editors.

### Engraving-engine spike

- [ ] Time-box evaluation of embeddable engraving/layout engines that satisfy licensing and interactive editing needs.
- [ ] Test incremental layout, four voices, tuplets, grace notes, clef/key/time changes, ties/slurs, hairpins, articulation placement, hit testing, and arbitrary node widths.
- [ ] Reject page-oriented or serialization-only engines that cannot support low-latency direct manipulation.
- [ ] If no candidate fits, record the owned semantic-layout fallback using SMuFL glyphs and a toolkit-neutral render list.

### Direct VST3 hosting spike

- [ ] Verify the current VST3 SDK license and FetchContent integration.
- [ ] Scan plugins in a helper process with timeout/crash reporting.
- [ ] Instantiate one test instrument and one effect on each primary x86-64 platform and on macOS arm64.
- [ ] Process MIDI/audio, save and restore opaque state, query latency/tail, and attach/resize/focus native editors.
- [ ] Demonstrate native editor behavior on macOS, Windows, Wayland, and X11/XWayland or document a compatible fallback.
- [ ] Confirm that Windows/Linux arm64 builds compile even when native plugin fixtures are unavailable.

### Cooked-format spike

Compare a small owned binary format with permissively licensed schema-based options. Measure:

- [ ] Deterministic byte-for-byte export.
- [ ] Versioning and unknown-field behavior.
- [ ] Bounds/offset validation on hostile input.
- [ ] Load-time allocation count and ability to use host allocators.
- [ ] Compact representation of UUID/index maps, notation schedules, tempo curves, and graph routing.
- [ ] Debuggability and generated-code warning hygiene.

## Acceptance Criteria

- [ ] Written architecture decisions select a permissive UI/render/audio/MIDI baseline and a cooked-format direction.
- [ ] A fallback is recorded for every rejected or not-yet-proven dependency.
- [ ] The direct VST3 prototype completes its lifecycle on macOS, Windows, and Linux x86-64.
- [ ] Representative accessibility nodes are discoverable and actionable on all three desktop systems.
- [ ] The target dependency graph prevents writer-only libraries from entering the runtime.
- [ ] No production milestone is blocked on an unresolved licensing assumption.

## Test And Evidence

- [ ] Check in measurements, platform notes, license links, and minimal reproduction instructions with the decisions.
- [ ] Use small GTest fixtures for cooked-format round trips and malformed buffers.
- [ ] Run spike code under ASan/UBSan and use TSan where concurrency is exercised.
- [ ] Delete or clearly quarantine disposable prototype code after decisions are recorded.

## Exit Decision

Do not start Milestone 01 until the default writer stack can remain permissively reusable and direct VST3 native-editor hosting has a credible implementation path.
