# Milestone 10: Accessibility, Performance, And Hardening

## Goal

Turn the integrated writer/runtime into release-quality `0.1.0` candidates through measurable accessibility, realtime, performance, resilience, and compatibility gates.

## Dependencies

- [ ] Milestone 09 integrated writer.
- [ ] Milestone 07 complete runtime semantics.

## Deliverables

### Accessibility completion

- [ ] Audit every custom graph, notation, tempo, connector, mixer, transport, event, validation, and plugin-generic control.
- [ ] Complete keyboard-only creation/editing workflows and visible focus indicators.
- [ ] Verify role/name/value/state/action announcements with VoiceOver, Narrator, and Orca on Wayland and X11/XWayland using a recorded OS/assistive-technology version matrix.
- [ ] Preserve semantic focus through viewport culling, zoom, live updates, undo/redo, and dialog/plugin-editor transitions.
- [ ] Add color-vision-safe themes and ensure connection types, errors, selection, and playback state never rely on color alone.

### Canvas and notation performance

- [ ] Establish a versioned representative 1,000-node project fixture with realistic notation, 64-track stress nodes, tempo lanes, and dense connectors.
- [ ] Maintain 60 fps target interactions for pan, zoom, node drag, and route drag on documented recommended hardware.
- [ ] Profile spatial queries, clipping, engraving invalidation, rendering, accessibility updates, and connector routing separately.
- [ ] Bound memory growth while repeatedly navigating, editing, undoing, opening projects, and rescanning plugins.
- [ ] Retain full notation semantics at all zoom levels while allowing clipping and sub-pixel raster simplification.

### Realtime and determinism hardening

- [ ] Re-run all process-path allocation/lock checks in optimized builds.
- [ ] Soak variable 64-1024 frame processing at 44.1 and 48 kHz with cyclic graphs, maximum proven tail overlap, dense events, and tempo curves.
- [ ] Compare runtime traces across macOS, Windows, and Linux for byte-identical deterministic behavior.
- [ ] Validate transactional no-output/no-state-advance retry after deliberately undersized MIDI buffers, and deterministic drop-oldest diagnostics for full event queues.
- [ ] Verify that no writer/plugin operation can block or mutate runtime-owned realtime state.

### Robustness and security

- [ ] Expand hostile project/cooked-asset corpus and resource limits.
- [ ] Test plugin scanner timeout/crash loops, malicious metadata, missing native editors, device churn, shutdown races, and corrupted state blobs.
- [ ] Add structured crash diagnostics that exclude private plugin/project content by default.
- [ ] Audit C ABI lengths, integer conversion, alignment, UUID/string handling, allocator failure, and version mismatch paths.
- [ ] Review third-party revision/license inventory and required notices.

### Platform compatibility

- [ ] Validate macOS arm64 and x86-64, Windows x86-64, Linux x86-64, Wayland, and X11/XWayland behavior.
- [ ] Run full VST3 scanner, instrument/effect lifecycle, audio, state, and native-editor fixtures on macOS arm64 as a release gate.
- [ ] Confirm Windows/Linux arm64 build output and document build-only status.
- [ ] Test representative trackpads, high-DPI/mixed-DPI displays, keyboard layouts, audio devices, and VST3 native editors.
- [ ] Document supported OS baseline and known plugin/window-system limitations.

## Acceptance Criteria

- [ ] Every core composition/graph workflow is keyboard-complete and exposes tested role/name/value/state/action semantics with no severity-1 accessibility defects under the recorded assistive-technology matrix.
- [ ] Performance fixture meets the documented 60 fps target on reference hardware or has an approved, measured exception.
- [ ] Realtime stress/soak tests complete without allocation, locks, missed deadlines above the agreed threshold, sanitizer findings, races, or nondeterministic MIDI.
- [ ] Malformed input and hostile scanner fixtures cannot crash the writer/runtime host process or exceed documented resource limits.
- [ ] All three platform CI paths, analyzers, and architecture builds remain green for the release candidate.
- [ ] Dependency inventory contains no unresolved incompatible license or floating revision.

## Test Focus

- [ ] Automated accessibility tree/action tests plus a manual assistive-technology checklist.
- [ ] Versioned benchmark baselines with regression thresholds and trace capture.
- [ ] Multi-hour runtime/audio soak tests and repeated open/close/plugin-rescan loops.
- [ ] ASan/UBSan/TSan optimized and debug configurations where supported.
- [ ] Cross-platform deterministic asset/MIDI hashes.
- [ ] Release-candidate exploratory testing centered on data loss, stuck notes, routing errors, and focus traps.
