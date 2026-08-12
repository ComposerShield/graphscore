# Milestone 10: Accessibility, Performance, And Hardening

## Goal

Turn the integrated writer/runtime into release-quality `0.1.0` candidates through measurable accessibility, realtime, performance, resilience, and compatibility gates.

## Dependencies

- [ ] M10-phase-1 Milestone 09 integrated writer.
- [ ] M10-phase-2 Milestone 07 complete runtime semantics.

## Deliverables

### Accessibility completion

- [ ] M10-phase-3 Audit every custom graph, notation, tempo, connector, mixer, transport, event, validation, and plugin-generic control.
- [ ] M10-phase-4 Complete keyboard-only creation/editing workflows and visible focus indicators.
- [ ] M10-phase-5 Verify role/name/value/state/action announcements with VoiceOver, Narrator, and Orca on Wayland and X11/XWayland using a recorded OS/assistive-technology version matrix.
- [ ] M10-phase-6 Preserve semantic focus through viewport culling, zoom, live updates, undo/redo, and dialog/plugin-editor transitions.
- [ ] M10-phase-7 Add color-vision-safe themes and ensure connection types, errors, selection, and playback state never rely on color alone.

### Canvas and notation performance

- [ ] M10-phase-8 Establish a versioned representative 1,000-node project fixture with realistic notation, 64-track stress nodes, tempo lanes, and dense connectors.
- [ ] M10-phase-9 Maintain 60 fps target interactions for pan, zoom, node drag, and route drag on documented recommended hardware.
- [ ] M10-phase-10 Profile spatial queries, clipping, engraving invalidation, rendering, accessibility updates, and connector routing separately.
- [ ] M10-phase-11 Bound memory growth while repeatedly navigating, editing, undoing, opening projects, and rescanning plugins.
- [ ] M10-phase-12 Retain full notation semantics at all zoom levels while allowing clipping and sub-pixel raster simplification.

### Realtime and determinism hardening

- [ ] M10-phase-13 Re-run all process-path allocation/lock checks in optimized builds.
- [ ] M10-phase-14 Soak variable 64-1024 frame processing at 44.1 and 48 kHz with cyclic graphs, maximum proven tail overlap, dense events, and tempo curves.
- [ ] M10-phase-15 Compare runtime traces across macOS, Windows, and Linux for byte-identical deterministic behavior.
- [ ] M10-phase-16 Validate transactional no-output/no-state-advance retry after deliberately undersized MIDI buffers, and deterministic drop-oldest diagnostics for full event queues.
- [ ] M10-phase-17 Verify that no writer/plugin operation can block or mutate runtime-owned realtime state.

### Robustness and security

- [ ] M10-phase-18 Expand hostile project/cooked-asset corpus and resource limits.
- [ ] M10-phase-19 Test plugin scanner timeout/crash loops, malicious metadata, missing native editors, device churn, shutdown races, and corrupted state blobs.
- [ ] M10-phase-20 Add structured crash diagnostics that exclude private plugin/project content by default.
- [ ] M10-phase-21 Audit C ABI lengths, integer conversion, alignment, UUID/string handling, allocator failure, and version mismatch paths.
- [ ] M10-phase-22 Review third-party revision/license inventory and required notices.

### Platform compatibility

- [ ] M10-phase-23 Validate macOS arm64 and x86-64, Windows x86-64, Linux x86-64, Wayland, and X11/XWayland behavior.
- [ ] M10-phase-24 Run full VST3 scanner, instrument/effect lifecycle, audio, state, and native-editor fixtures on macOS arm64 as a release gate.
- [ ] M10-phase-25 Confirm Windows/Linux arm64 build output and document build-only status.
- [ ] M10-phase-26 Test representative trackpads, high-DPI/mixed-DPI displays, keyboard layouts, audio devices, and VST3 native editors.
- [ ] M10-phase-27 Document supported OS baseline and known plugin/window-system limitations.

## Acceptance Criteria

- [ ] M10-phase-28 Every core composition/graph workflow is keyboard-complete and exposes tested role/name/value/state/action semantics with no severity-1 accessibility defects under the recorded assistive-technology matrix.
- [ ] M10-phase-29 Performance fixture meets the documented 60 fps target on reference hardware or has an approved, measured exception.
- [ ] M10-phase-30 Realtime stress/soak tests complete without allocation, locks, missed deadlines above the agreed threshold, sanitizer findings, races, or nondeterministic MIDI.
- [ ] M10-phase-31 Malformed input and hostile scanner fixtures cannot crash the writer/runtime host process or exceed documented resource limits.
- [ ] M10-phase-32 All three platform CI paths, analyzers, and architecture builds remain green for the release candidate.
- [ ] M10-phase-33 Dependency inventory contains no unresolved incompatible license or floating revision.

## Test Focus

- [ ] M10-phase-34 Automated accessibility tree/action tests plus a manual assistive-technology checklist.
- [ ] M10-phase-35 Versioned benchmark baselines with regression thresholds and trace capture.
- [ ] M10-phase-36 Multi-hour runtime/audio soak tests and repeated open/close/plugin-rescan loops.
- [ ] M10-phase-37 ASan/UBSan/TSan optimized and debug configurations where supported.
- [ ] M10-phase-38 Cross-platform deterministic asset/MIDI hashes.
- [ ] M10-phase-39 Release-candidate exploratory testing centered on data loss, stuck notes, routing errors, and focus traps.
