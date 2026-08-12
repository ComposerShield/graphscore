# Milestone 07: Adaptive Tempo And Transitions

## Goal

Complete GraphScore's adaptive playback semantics in the shared scheduler, exporter, validator, and graph editor.

## Dependencies

- [ ] M7-phase-1 Milestone 04 runtime scheduler.
- [ ] M7-phase-2 Milestone 06 graph canvas.
- [ ] M7-phase-3 Milestone 05 notation model/editor.

## Deliverables

### Tempo lane

- [ ] M7-phase-4 Add a collapsible tempo lane inside every node aligned with its musical timeline.
- [ ] M7-phase-5 Create, select, move, and delete musically anchored tempo points carrying BPM and explicit beat unit.
- [ ] M7-phase-6 Enforce 10-400 BPM and finite coordinates.
- [ ] M7-phase-7 Support step, linear, and smooth rounded interpolation; smooth points expose constrained handles/tension without reversing musical time.
- [ ] M7-phase-8 Display sampled BPM and beat unit during hover/drag and support exact keyboard/text entry.
- [ ] M7-phase-9 Keep tempo edits undoable and publishable through immutable playback snapshots.
- [ ] M7-phase-10 Apply the exact inheritance rule when creating a node: copy the selected/source node's tempo value and beat unit at the end of its main region, or use project defaults.

### Sequential routing

- [ ] M7-phase-11 At the end of the main region, choose a matching persistent event intent first, then writer-only manually queued output, then deterministic weighted random output.
- [ ] M7-phase-12 Store occurrences in a bounded persistent queue per registered event. A node/event listener applies first-wins, latest-valid-wins by default, or FIFO when that node reaches its boundary.
- [ ] M7-phase-13 First-wins consumes the earliest candidate and discards later duplicates accumulated before the boundary; latest-wins consumes the newest and discards older duplicates; FIFO consumes one earliest occurrence and preserves later ones.
- [ ] M7-phase-14 Event intent that does not match the current node persists across node changes. Stop, reset, and node play clear queues; pause retains them.
- [ ] M7-phase-15 Queue storage is preallocated from cooked capacity; overflow drops the oldest occurrence and increments diagnostics.
- [ ] M7-phase-16 Resolve different event candidates through connector priority, then newest candidate sequence, then stable connector order. Non-winning event queues remain pending.
- [ ] M7-phase-17 Require positive eligible random weights to total exactly 100 percent at authoring/export; zero-weight outputs are disabled.
- [ ] M7-phase-18 Stop cleanly when no event/manual/random output is eligible.

### Vertical routing

- [ ] M7-phase-19 Accept sample-local event triggers only from the current process block.
- [ ] M7-phase-20 Match the highest-priority active vertical output at the exact event sample; stable connector order breaks priority ties.
- [ ] M7-phase-21 Select at most one jump from the node active at that sample offset. Other same-offset events cannot chain through the new destination.
- [ ] M7-phase-22 Map to the same measure and exact rational offset within that measure in the destination's main region.
- [ ] M7-phase-23 Validate identical main-region measure count and corresponding time signatures; tempo curves may differ.
- [ ] M7-phase-24 Release only source-main notes that currently own their track/channel/pitch, remove the source main's pedal contributions, and leave concurrent tails untouched. Do not chase target notes already in progress; emit target events beginning at the mapped coordinate normally.
- [ ] M7-phase-25 Emit CC64-up only when removing source contributions leaves no active logical pedal owner on the track/channel.
- [ ] M7-phase-26 Only the active main node routes vertically; pickdown tails never route.
- [ ] M7-phase-27 Reject a node that binds the same event to both vertical and sequential behavior.

### Pickdown overlap

- [ ] M7-phase-28 The destination begins exactly when the source enters its explicit pickdown.
- [ ] M7-phase-29 The pickdown is greater than zero and shorter than one measure under the boundary's active meter; its source tempo curve continues through its full duration.
- [ ] M7-phase-30 Continue all pickdown rests, sustained notes, new attacks, articulation, and CC64 events on the source tempo curve.
- [ ] M7-phase-31 Transfer the post-boundary ownership of crossing notes, ties, and pedal spans into the tail context.
- [ ] M7-phase-32 Route no graph triggers or connections from a pickdown; it is MIDI-only.
- [ ] M7-phase-33 Let earlier tails finish if the active node transitions again.
- [ ] M7-phase-34 Reconcile same-pitch overlap by making the newest attack the MIDI owner: retrigger with note-off/note-on and suppress displaced logical releases.
- [ ] M7-phase-35 Merge overlapping CC64 ownership with logical OR so pedal-up occurs only after the last active main/tail span releases.
- [ ] M7-phase-36 Export computes a finite asset-specific tail-slot bound from minimum transition intervals, maximum tail durations, tempo bounds, and graph cycles.
- [ ] M7-phase-37 Reject any asset for which the bound cannot be proven finite or represented safely.
- [ ] M7-phase-38 Require at least one complete main-region measure, reject zero-time/same-sample sequential cycles, and verify the computed bound with an independent test oracle.

### Event and random APIs

- [ ] M7-phase-39 Resolve trigger input by stable UUID or unique UTF-8 name to the same registered event.
- [ ] M7-phase-40 Perform string lookup without allocation using cooked immutable tables and caller-provided spans no longer than 255 UTF-8 bytes; reject a longer span in constant time before validation/hashing. UUID/index submission remains available for fixed-size realtime input.
- [ ] M7-phase-41 Define deterministic behavior for duplicate same-sample input, invalid UTF-8/name, unknown UUID, out-of-order offset, and queue overflow.
- [ ] M7-phase-42 Use the Milestone 04 same-sample phase order: vertical selection precedes source events/boundaries, sequential occurrences are visible to a same-sample boundary, and a new destination never reprocesses the same-offset input.
- [ ] M7-phase-43 Seed/reset the PRNG solely through the host API and use a specified algorithm/version for reproducible sequences.

### Writer graph feedback

- [ ] M7-phase-44 Show invalid vertical compatibility, ambiguous event use, incomplete random totals, unconnected outputs, queue configuration, and priority order directly on connectors/nodes.
- [ ] M7-phase-45 Display the active node, active pickdown tails, mapped vertical position, manually queued sequential output, and event queue state during audition.
- [ ] M7-phase-46 Permit connection edits during playback through the safe publication policy.

## Acceptance Criteria

- [ ] M7-phase-47 Sequential, vertical, random, event queue, and pickdown behavior is identical in writer playback and the runtime library.
- [ ] M7-phase-48 Every transition is sample-accurate across arbitrary legal block partitioning.
- [ ] M7-phase-49 Vertical compatibility updates immediately after meter/measure edits and blocks invalid export.
- [ ] M7-phase-50 Same seed and event stream yields the same graph path and MIDI bytes across platforms.
- [ ] M7-phase-51 Persistent event queues and all simultaneous pickdown tails remain allocation-free and bounded.
- [ ] M7-phase-52 Tempo curves remain continuous where authored, exact at steps, monotonic in musical-to-sample mapping, and stable across block boundaries.

## Test Focus

- [ ] M7-phase-53 Exhaustive precedence tests for event, manual, random, priority, and stable order.
- [ ] M7-phase-54 First/latest/FIFO listener behavior across many nodes, overflow, reset, pause, and stop.
- [ ] M7-phase-55 Cross-event priority/sequence arbitration, non-winning queue retention, and exact duplicate-discard behavior.
- [ ] M7-phase-56 Vertical events at block start/end, note onset, mid-note, measure boundary, tempo point, and pickdown boundary.
- [ ] M7-phase-57 Mismatched meter/count validation and edit-induced invalidation.
- [ ] M7-phase-58 Pickdown overlap chains, source/destination tempo differences, CC64, and natural tail completion.
- [ ] M7-phase-59 Exact-bound and one-over-capacity fixtures compared with an independent overlap-bound oracle.
- [ ] M7-phase-60 Same-pitch attacks and pedal spans shared by several concurrent tails and the active node.
- [ ] M7-phase-61 Weighted random distribution sanity plus exact deterministic sequence goldens.
- [ ] M7-phase-62 Tempo step/linear/smooth integration and inverse-position tests.
- [ ] M7-phase-63 Allocation/lock traps and sanitizer/concurrency soak tests over cyclic graphs.
