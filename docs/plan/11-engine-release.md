# Milestone 11: Engine Integrations And 0.1.0 Release

## Goal

Ship the runtime, writer, Unity package, Unreal plugin, documentation, and unsigned cross-platform archives as GraphScore `0.1.0`.

## Dependencies

- [ ] M11-phase-1 Milestone 10 release gates.
- [ ] M11-phase-2 Stable runtime C ABI and cooked schema from Milestones 03/04/07.

## Deliverables

### Unity integration

- [ ] M11-phase-3 Package the platform runtime dynamic libraries with a C# P/Invoke facade over the stable C ABI.
- [ ] M11-phase-4 Expose asset load from Unity-managed bytes, allocator/lifecycle integration, start/stop/reset/pause/resume, start-node UUID, host seed, event UUID/name submission, process, MIDI event retrieval, and diagnostics.
- [ ] M11-phase-5 Avoid managed allocation from the audio/process callback after initialization.
- [ ] M11-phase-6 Provide a sample component and scene demonstrating deterministic block processing and host-side MIDI routing.
- [ ] M11-phase-7 Document supported Unity versions, scripting backend, architectures, and thread ownership.

### Unreal integration

- [ ] M11-phase-8 Package a native Unreal plugin/module that wraps the same runtime shared library and C ABI.
- [ ] M11-phase-9 Expose cooked asset loading, lifecycle/transport, event submission, processing, MIDI retrieval, diagnostics, and UUID-friendly Blueprint/C++ helpers where safe.
- [ ] M11-phase-10 Keep processing ownership explicit and avoid UObject/game-thread access from the realtime callback.
- [ ] M11-phase-11 Provide a sample project/module demonstrating host-side MIDI routing.
- [ ] M11-phase-12 Document supported Unreal versions, build configurations, architectures, and packaging steps.

### SDK and examples

- [ ] M11-phase-13 Public C header, dynamic libraries, import libraries where required, cooked-asset documentation, and minimal plain-C host example.
- [ ] M11-phase-14 C++ convenience wrapper may be header-only over the C ABI but is not the stable ABI contract.
- [ ] M11-phase-15 API reference documents ownership, allocator lifetime, thread rules, realtime-safe functions, block/event ordering, capacity preflight, status flags, and version compatibility.
- [ ] M11-phase-16 Include deterministic reference assets and expected MIDI traces usable by engine integrators.

### Release automation

- [ ] M11-phase-17 GitHub Actions produces unsigned macOS arm64/x86-64, Windows x86-64/arm64, and Linux x86-64/arm64 archives.
- [ ] M11-phase-18 Native smoke tests gate primary tested architectures; Windows/Linux arm64 remain clearly labeled build-only.
- [ ] M11-phase-19 Produce writer archive, runtime SDK archive, Unity package, Unreal plugin archive, checksums, dependency notices, license, and source revision metadata.
- [ ] M11-phase-20 Verify archives from a clean consumer environment rather than only the build tree.
- [ ] M11-phase-21 Tag/version writer, C ABI, editable schema, and cooked schema coherently as `0.1.0`.

### User and integrator documentation

- [ ] M11-phase-22 Composer quick start covering tracks, notation, tempo, graph connections, events, pickdowns, plugins, playback, save/recovery, and export.
- [ ] M11-phase-23 Runtime quick start covering clock blocks, buffers, UUID/name events, deterministic seeds, MIDI routing, diagnostics, and shutdown.
- [ ] M11-phase-24 Unity/Unreal guides and complete sample build instructions.
- [ ] M11-phase-25 Accessibility/keyboard shortcut reference including connector editing and playback actions.
- [ ] M11-phase-26 Known limitations explicitly list no opening pickups, no transposing instruments, no plugin automation, general MIDI CC deferred except CC64, and arm64 test status.

## Acceptance Criteria

- [ ] M11-phase-27 A fresh user on a natively tested release architecture can author/export a project and run its cooked graph through plain C, Unity, and Unreal examples; Windows/Linux arm64 archives remain explicitly build-only.
- [ ] M11-phase-28 All hosts produce the expected deterministic MIDI trace from the reference asset.
- [ ] M11-phase-29 Engine processing paths allocate no memory after initialization and obey single-owner thread rules.
- [ ] M11-phase-30 Every release archive is reproducible enough to trace to exact source/dependency revisions and contains required notices.
- [ ] M11-phase-31 `0.1.0` release checklist passes all Milestone 10 gates and archive smoke tests.
- [ ] M11-phase-32 General MIDI CC is not silently added to the release contract beyond documented CC64 pedal behavior.

## Test Focus

- [ ] M11-phase-33 ABI/package consumer tests outside the repository build graph.
- [ ] M11-phase-34 Unity managed/native lifecycle, GC-allocation, event marshalling, buffer capacity, and domain-reload tests.
- [ ] M11-phase-35 Unreal module load/unload, packaging, thread boundary, event marshalling, and shutdown tests.
- [ ] M11-phase-36 Cross-host reference MIDI trace comparison.
- [ ] M11-phase-37 Archive content, symbol, architecture, license/notice, and clean-install smoke tests.
