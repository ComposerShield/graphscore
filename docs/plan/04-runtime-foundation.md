# Milestone 04: Realtime Runtime Foundation

## Goal

Deliver a stable C ABI and deterministic, sample-accurate, hard-realtime MIDI scheduler for loaded cooked assets.

## Dependencies

- [ ] Milestone 03 cooked assets and loader.
- [ ] Milestone 02 musical model.

## Deliverables

### Public C ABI

- [ ] Opaque asset and player handles, fixed-width integer types, explicit struct sizes/versions, and exported symbol macros.
- [ ] API version query plus compatibility checks between library, header, and cooked asset.
- [ ] Host allocator callbacks used only from documented non-realtime functions.
- [ ] Creation, destruction, load, capacity query, start, start-node-UUID, pause, resume, stop-with-output, reset-with-output, process, and diagnostic polling/reset functions.
- [ ] No exceptions, C++ runtime types, ownership ambiguity, or platform framework types across the ABI.

### Processing contract

- [ ] Configure sample rate, maximum block frames, and maximum input events per block before starting.
- [ ] Each process call supplies a continuous absolute sample position, a variable frame count up to the configured maximum, and ordered input events with in-block sample offsets.
- [ ] Reject or flag discontinuous clocks until the host explicitly resets playback state.
- [ ] Caller-owned output contains MIDI 1.0 bytes, sample offset, stable track UUID/index, and the track's fixed MIDI channel.
- [ ] Stable ordering is defined for note-offs, CC64, note-ons, simultaneous voices, tracks, and graph-generated events.
- [ ] Track logical ownership of sounding notes. A newer attack of the same track/channel/pitch emits note-off then note-on, becomes the owner, and suppresses the displaced note's later release.
- [ ] Combine overlapping CC64 spans as logical OR per track/channel so an older tail cannot release sustain still held by current material.
- [ ] Stop/reset clears all logical note and pedal ownership, emits required note-offs plus CC64-up for every held track/channel, and leaves no stale releases; pause freezes musical time while retaining ownership.
- [ ] Stop/reset are called only by the processing owner or while processing is quiescent. They accept caller-owned MIDI storage, use the lifecycle capacity from the preflight query, and fail transactionally with no state change when undersized.

### Capacity and diagnostics

- [ ] Off-thread capacity query reports the required MIDI event slots for the configured maximum block, maximum input-event count, lifecycle flushes, and loaded asset.
- [ ] If supplied output capacity is below the queried requirement, processing emits no MIDI, advances no scheduler/queue/PRNG/ownership state, returns the required count, and permits the same block to be retried with already allocated storage.
- [ ] Input counts or offsets outside configured bounds fail transactionally under the same no-output/no-advance rule.
- [ ] Event queue, output capacity, invalid input offset, clock discontinuity, and internal invariant diagnostics are pollable off-thread.
- [ ] No realtime logging, callbacks, formatting, or error strings.

### Same-sample phase order

- [ ] Validate bounded input, then ingest all events at a sample against the node active at the start of that sample.
- [ ] Select at most one vertical transition before emitting source scheduled events or evaluating a source boundary. A selected vertical releases source-main ownership, enters the mapped target, and suppresses source-boundary processing at that sample.
- [ ] Enqueue non-vertical sequential occurrences before boundary evaluation, allowing an event arriving exactly at a boundary to select that boundary's output.
- [ ] When no vertical jump wins, emit due events under the stable MIDI ordering, create/transfer the pickdown tail at the boundary, choose the sequential destination, and emit destination events at that same sample.
- [ ] Never re-evaluate same-offset input against a newly entered destination; later sample offsets may transition again.

### Core scheduling

- [ ] Integrate exact musical positions against step, linear, and smooth tempo segments at 10-400 BPM.
- [ ] Schedule notes, chords, rests, ties, grace notes, dynamics/hairpin velocities, articulation duration/velocity, slur overlap, and CC64 pedal spans.
- [ ] Do not chase notes or CC64 spans that began before a start-node/vertical mapped position unless a future API explicitly requests state chase.
- [ ] Handle variable block boundaries without duplicate, missing, or shifted events.
- [ ] Keep source notation spelling separate from sounding MIDI pitch.
- [ ] Use a host-provided deterministic random seed and expose reset behavior precisely.

### Realtime implementation rules

- [ ] Preallocate every queue, cursor, active-note table, and tail slot during load/configuration.
- [ ] Use immutable asset storage and instance-local bounded mutable playback state.
- [ ] Publish writer snapshots through preallocated handoff slots and reclaim retired snapshots off the audio thread through epochs or an equivalent bounded protocol; never release a last reference from processing.
- [ ] One processing thread owns each instance; lifecycle functions cannot race with processing unless explicitly documented as atomic status reads.
- [ ] No locks, allocation, exceptions, file I/O, locale, symbol lookup, or unbounded graph traversal in processing.

## Acceptance Criteria

- [ ] A pure C test program loads the shared library, starts any node by UUID, processes variable blocks, and receives deterministic MIDI.
- [ ] Equivalent elapsed sample ranges produce identical MIDI regardless of legal block partitioning.
- [ ] Replaying with the same asset, seed, event stream, and clock produces byte-identical output.
- [ ] Process-path allocation traps, lock instrumentation, ASan/UBSan, and TSan suites pass.
- [ ] At 44.1 and 48 kHz, block sizes varying from 64 through 1024 frames remain within measured realtime budgets on release reference hardware.
- [ ] ABI symbols and struct layouts are checked in CI on each platform.

## Test Focus

- [ ] Golden MIDI streams for every notation element and simultaneous-event ordering.
- [ ] Same-pitch retrigger/ownership and overlapping-pedal tests across active nodes and tails.
- [ ] Tempo integration/inversion tests at segment boundaries and numerical extremes.
- [ ] Block-partition property tests with randomized legal block sizes.
- [ ] Pause/resume, stop/reset, node-start, clock discontinuity, capacity violation, and diagnostic-counter tests.
- [ ] Maximum/one-over-maximum input events, undersized output retry equivalence, and lifecycle-flush capacity tests.
- [ ] Same-sample phase-order tests combining vertical input, sequential input, scheduled note/CC64 events, pickdown creation, and graph cycles.
- [ ] Host allocator accounting and forced allocation-failure tests outside processing.
- [ ] Long deterministic soak tests under sanitizers and TSan.
