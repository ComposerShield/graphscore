# Milestone 04: Realtime Runtime Foundation

## Goal

Deliver a stable C ABI and deterministic, sample-accurate, hard-realtime MIDI scheduler for loaded cooked assets.

## Dependencies

- [ ] M4-phase-1 Milestone 03 cooked assets and loader.
- [ ] M4-phase-2 Milestone 02 musical model.

## Deliverables

### Public C ABI

- [ ] M4-phase-3 Opaque asset and player handles, fixed-width integer types, explicit struct sizes/versions, and exported symbol macros.
- [ ] M4-phase-4 API version query plus compatibility checks between library, header, and cooked asset.
- [ ] M4-phase-5 Host allocator callbacks used only from documented non-realtime functions.
- [ ] M4-phase-6 Creation, destruction, load, capacity query, start, start-node-UUID, pause, resume, stop-with-output, reset-with-output, process, and diagnostic polling/reset functions.
- [ ] M4-phase-7 No exceptions, C++ runtime types, ownership ambiguity, or platform framework types across the ABI.

### Processing contract

- [ ] M4-phase-8 Configure sample rate, maximum block frames, and maximum input events per block before starting.
- [ ] M4-phase-9 Each process call supplies a continuous absolute sample position, a variable frame count up to the configured maximum, and ordered input events with in-block sample offsets.
- [ ] M4-phase-10 Reject or flag discontinuous clocks until the host explicitly resets playback state.
- [ ] M4-phase-11 Caller-owned output contains MIDI 1.0 bytes, sample offset, stable track UUID/index, and the track's fixed MIDI channel.
- [ ] M4-phase-12 Stable ordering is defined for note-offs, CC64, note-ons, simultaneous voices, tracks, and graph-generated events.
- [ ] M4-phase-13 Track logical ownership of sounding notes. A newer attack of the same track/channel/pitch emits note-off then note-on, becomes the owner, and suppresses the displaced note's later release.
- [ ] M4-phase-14 Combine overlapping CC64 spans as logical OR per track/channel so an older tail cannot release sustain still held by current material.
- [ ] M4-phase-15 Stop/reset clears all logical note and pedal ownership, emits required note-offs plus CC64-up for every held track/channel, and leaves no stale releases; pause freezes musical time while retaining ownership.
- [ ] M4-phase-16 Stop/reset are called only by the processing owner or while processing is quiescent. They accept caller-owned MIDI storage, use the lifecycle capacity from the preflight query, and fail transactionally with no state change when undersized.

### Capacity and diagnostics

- [ ] M4-phase-17 Off-thread capacity query reports the required MIDI event slots for the configured maximum block, maximum input-event count, lifecycle flushes, and loaded asset.
- [ ] M4-phase-18 If supplied output capacity is below the queried requirement, processing emits no MIDI, advances no scheduler/queue/PRNG/ownership state, returns the required count, and permits the same block to be retried with already allocated storage.
- [ ] M4-phase-19 Input counts or offsets outside configured bounds fail transactionally under the same no-output/no-advance rule.
- [ ] M4-phase-20 Event queue, output capacity, invalid input offset, clock discontinuity, and internal invariant diagnostics are pollable off-thread.
- [ ] M4-phase-21 No realtime logging, callbacks, formatting, or error strings.

### Same-sample phase order

- [ ] M4-phase-22 Validate bounded input, then ingest all events at a sample against the node active at the start of that sample.
- [ ] M4-phase-23 Select at most one vertical transition before emitting source scheduled events or evaluating a source boundary. A selected vertical releases source-main ownership, enters the mapped target, and suppresses source-boundary processing at that sample.
- [ ] M4-phase-24 Enqueue non-vertical sequential occurrences before boundary evaluation, allowing an event arriving exactly at a boundary to select that boundary's output.
- [ ] M4-phase-25 When no vertical jump wins, emit due events under the stable MIDI ordering, create/transfer the pickdown tail at the boundary, choose the sequential destination, and emit destination events at that same sample.
- [ ] M4-phase-26 Never re-evaluate same-offset input against a newly entered destination; later sample offsets may transition again.

### Core scheduling

- [ ] M4-phase-27 Integrate exact musical positions against step, linear, and smooth tempo segments at 10-400 BPM.
- [ ] M4-phase-28 Schedule notes, chords, rests, ties, grace notes, dynamics/hairpin velocities, articulation duration/velocity, slur overlap, and CC64 pedal spans.
- [ ] M4-phase-29 Do not chase notes or CC64 spans that began before a start-node/vertical mapped position unless a future API explicitly requests state chase.
- [ ] M4-phase-30 Handle variable block boundaries without duplicate, missing, or shifted events.
- [ ] M4-phase-31 Keep source notation spelling separate from sounding MIDI pitch.
- [ ] M4-phase-32 Use a host-provided deterministic random seed and expose reset behavior precisely.

### Realtime implementation rules

- [ ] M4-phase-33 Preallocate every queue, cursor, active-note table, and tail slot during load/configuration.
- [ ] M4-phase-34 Use immutable asset storage and instance-local bounded mutable playback state.
- [ ] M4-phase-35 Publish writer snapshots through preallocated handoff slots and reclaim retired snapshots off the audio thread through epochs or an equivalent bounded protocol; never release a last reference from processing.
- [ ] M4-phase-36 One processing thread owns each instance; lifecycle functions cannot race with processing unless explicitly documented as atomic status reads.
- [ ] M4-phase-37 No locks, allocation, exceptions, file I/O, locale, symbol lookup, or unbounded graph traversal in processing.

## Acceptance Criteria

- [ ] M4-phase-38 A pure C test program loads the shared library, starts any node by UUID, processes variable blocks, and receives deterministic MIDI.
- [ ] M4-phase-39 Equivalent elapsed sample ranges produce identical MIDI regardless of legal block partitioning.
- [ ] M4-phase-40 Replaying with the same asset, seed, event stream, and clock produces byte-identical output.
- [ ] M4-phase-41 Process-path allocation traps, lock instrumentation, ASan/UBSan, and TSan suites pass.
- [ ] M4-phase-42 At 44.1 and 48 kHz, block sizes varying from 64 through 1024 frames remain within measured realtime budgets on release reference hardware.
- [ ] M4-phase-43 ABI symbols and struct layouts are checked in CI on each platform.

## Test Focus

- [ ] M4-phase-44 Golden MIDI streams for every notation element and simultaneous-event ordering.
- [ ] M4-phase-45 Same-pitch retrigger/ownership and overlapping-pedal tests across active nodes and tails.
- [ ] M4-phase-46 Tempo integration/inversion tests at segment boundaries and numerical extremes.
- [ ] M4-phase-47 Block-partition property tests with randomized legal block sizes.
- [ ] M4-phase-48 Pause/resume, stop/reset, node-start, clock discontinuity, capacity violation, and diagnostic-counter tests.
- [ ] M4-phase-49 Maximum/one-over-maximum input events, undersized output retry equivalence, and lifecycle-flush capacity tests.
- [ ] M4-phase-50 Same-sample phase-order tests combining vertical input, sequential input, scheduled note/CC64 events, pickdown creation, and graph cycles.
- [ ] M4-phase-51 Host allocator accounting and forced allocation-failure tests outside processing.
- [ ] M4-phase-52 Long deterministic soak tests under sanitizers and TSan.
