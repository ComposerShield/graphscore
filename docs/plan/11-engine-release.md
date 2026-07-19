# Milestone 11: Engine Integrations And 0.1.0 Release

## Goal

Ship the runtime, writer, Unity package, Unreal plugin, documentation, and unsigned cross-platform archives as GraphScore `0.1.0`.

## Dependencies

- [ ] Milestone 10 release gates.
- [ ] Stable runtime C ABI and cooked schema from Milestones 03/04/07.

## Deliverables

### Unity integration

- [ ] Package the platform runtime dynamic libraries with a C# P/Invoke facade over the stable C ABI.
- [ ] Expose asset load from Unity-managed bytes, allocator/lifecycle integration, start/stop/reset/pause/resume, start-node UUID, host seed, event UUID/name submission, process, MIDI event retrieval, and diagnostics.
- [ ] Avoid managed allocation from the audio/process callback after initialization.
- [ ] Provide a sample component and scene demonstrating deterministic block processing and host-side MIDI routing.
- [ ] Document supported Unity versions, scripting backend, architectures, and thread ownership.

### Unreal integration

- [ ] Package a native Unreal plugin/module that wraps the same runtime shared library and C ABI.
- [ ] Expose cooked asset loading, lifecycle/transport, event submission, processing, MIDI retrieval, diagnostics, and UUID-friendly Blueprint/C++ helpers where safe.
- [ ] Keep processing ownership explicit and avoid UObject/game-thread access from the realtime callback.
- [ ] Provide a sample project/module demonstrating host-side MIDI routing.
- [ ] Document supported Unreal versions, build configurations, architectures, and packaging steps.

### SDK and examples

- [ ] Public C header, dynamic libraries, import libraries where required, cooked-asset documentation, and minimal plain-C host example.
- [ ] C++ convenience wrapper may be header-only over the C ABI but is not the stable ABI contract.
- [ ] API reference documents ownership, allocator lifetime, thread rules, realtime-safe functions, block/event ordering, capacity preflight, status flags, and version compatibility.
- [ ] Include deterministic reference assets and expected MIDI traces usable by engine integrators.

### Release automation

- [ ] GitHub Actions produces unsigned macOS arm64/x86-64, Windows x86-64/arm64, and Linux x86-64/arm64 archives.
- [ ] Native smoke tests gate primary tested architectures; Windows/Linux arm64 remain clearly labeled build-only.
- [ ] Produce writer archive, runtime SDK archive, Unity package, Unreal plugin archive, checksums, dependency notices, license, and source revision metadata.
- [ ] Verify archives from a clean consumer environment rather than only the build tree.
- [ ] Tag/version writer, C ABI, editable schema, and cooked schema coherently as `0.1.0`.

### User and integrator documentation

- [ ] Composer quick start covering tracks, notation, tempo, graph connections, events, pickdowns, plugins, playback, save/recovery, and export.
- [ ] Runtime quick start covering clock blocks, buffers, UUID/name events, deterministic seeds, MIDI routing, diagnostics, and shutdown.
- [ ] Unity/Unreal guides and complete sample build instructions.
- [ ] Accessibility/keyboard shortcut reference including connector editing and playback actions.
- [ ] Known limitations explicitly list no opening pickups, no transposing instruments, no plugin automation, general MIDI CC deferred except CC64, and arm64 test status.

## Acceptance Criteria

- [ ] A fresh user on a natively tested release architecture can author/export a project and run its cooked graph through plain C, Unity, and Unreal examples; Windows/Linux arm64 archives remain explicitly build-only.
- [ ] All hosts produce the expected deterministic MIDI trace from the reference asset.
- [ ] Engine processing paths allocate no memory after initialization and obey single-owner thread rules.
- [ ] Every release archive is reproducible enough to trace to exact source/dependency revisions and contains required notices.
- [ ] `0.1.0` release checklist passes all Milestone 10 gates and archive smoke tests.
- [ ] General MIDI CC is not silently added to the release contract beyond documented CC64 pedal behavior.

## Test Focus

- [ ] ABI/package consumer tests outside the repository build graph.
- [ ] Unity managed/native lifecycle, GC-allocation, event marshalling, buffer capacity, and domain-reload tests.
- [ ] Unreal module load/unload, packaging, thread boundary, event marshalling, and shutdown tests.
- [ ] Cross-host reference MIDI trace comparison.
- [ ] Archive content, symbol, architecture, license/notice, and clean-install smoke tests.
