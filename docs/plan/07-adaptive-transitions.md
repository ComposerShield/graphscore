# Milestone 07: Adaptive Tempo And Transitions

## Goal

Complete GraphScore's adaptive playback semantics in the shared scheduler, exporter, validator, and graph editor.

## Dependencies

- [ ] Milestone 04 runtime scheduler.
- [ ] Milestone 06 graph canvas.
- [ ] Milestone 05 notation model/editor.

## Deliverables

### Tempo lane

- [ ] Add a collapsible tempo lane inside every node aligned with its musical timeline.
- [ ] Create, select, move, and delete musically anchored tempo points carrying BPM and explicit beat unit.
- [ ] Enforce 10-400 BPM and finite coordinates.
- [ ] Support step, linear, and smooth rounded interpolation; smooth points expose constrained handles/tension without reversing musical time.
- [ ] Display sampled BPM and beat unit during hover/drag and support exact keyboard/text entry.
- [ ] Keep tempo edits undoable and publishable through immutable playback snapshots.
- [ ] Apply the exact inheritance rule when creating a node: copy the selected/source node's tempo value and beat unit at the end of its main region, or use project defaults.

### Sequential routing

- [ ] At the end of the main region, choose a matching persistent event intent first, then writer-only manually queued output, then deterministic weighted random output.
- [ ] Store occurrences in a bounded persistent queue per registered event. A node/event listener applies first-wins, latest-valid-wins by default, or FIFO when that node reaches its boundary.
- [ ] First-wins consumes the earliest candidate and discards later duplicates accumulated before the boundary; latest-wins consumes the newest and discards older duplicates; FIFO consumes one earliest occurrence and preserves later ones.
- [ ] Event intent that does not match the current node persists across node changes. Stop, reset, and node play clear queues; pause retains them.
- [ ] Queue storage is preallocated from cooked capacity; overflow drops the oldest occurrence and increments diagnostics.
- [ ] Resolve different event candidates through connector priority, then newest candidate sequence, then stable connector order. Non-winning event queues remain pending.
- [ ] Require positive eligible random weights to total exactly 100 percent at authoring/export; zero-weight outputs are disabled.
- [ ] Stop cleanly when no event/manual/random output is eligible.

### Vertical routing

- [ ] Accept sample-local event triggers only from the current process block.
- [ ] Match the highest-priority active vertical output at the exact event sample; stable connector order breaks priority ties.
- [ ] Select at most one jump from the node active at that sample offset. Other same-offset events cannot chain through the new destination.
- [ ] Map to the same measure and exact rational offset within that measure in the destination's main region.
- [ ] Validate identical main-region measure count and corresponding time signatures; tempo curves may differ.
- [ ] Release only source-main notes that currently own their track/channel/pitch, remove the source main's pedal contributions, and leave concurrent tails untouched. Do not chase target notes already in progress; emit target events beginning at the mapped coordinate normally.
- [ ] Emit CC64-up only when removing source contributions leaves no active logical pedal owner on the track/channel.
- [ ] Only the active main node routes vertically; pickdown tails never route.
- [ ] Reject a node that binds the same event to both vertical and sequential behavior.

### Pickdown overlap

- [ ] The destination begins exactly when the source enters its explicit pickdown.
- [ ] The pickdown is greater than zero and shorter than one measure under the boundary's active meter; its source tempo curve continues through its full duration.
- [ ] Continue all pickdown rests, sustained notes, new attacks, articulation, and CC64 events on the source tempo curve.
- [ ] Transfer the post-boundary ownership of crossing notes, ties, and pedal spans into the tail context.
- [ ] Route no graph triggers or connections from a pickdown; it is MIDI-only.
- [ ] Let earlier tails finish if the active node transitions again.
- [ ] Reconcile same-pitch overlap by making the newest attack the MIDI owner: retrigger with note-off/note-on and suppress displaced logical releases.
- [ ] Merge overlapping CC64 ownership with logical OR so pedal-up occurs only after the last active main/tail span releases.
- [ ] Export computes a finite asset-specific tail-slot bound from minimum transition intervals, maximum tail durations, tempo bounds, and graph cycles.
- [ ] Reject any asset for which the bound cannot be proven finite or represented safely.
- [ ] Require at least one complete main-region measure, reject zero-time/same-sample sequential cycles, and verify the computed bound with an independent test oracle.

### Event and random APIs

- [ ] Resolve trigger input by stable UUID or unique UTF-8 name to the same registered event.
- [ ] Perform string lookup without allocation using cooked immutable tables and caller-provided spans no longer than 255 UTF-8 bytes; reject a longer span in constant time before validation/hashing. UUID/index submission remains available for fixed-size realtime input.
- [ ] Define deterministic behavior for duplicate same-sample input, invalid UTF-8/name, unknown UUID, out-of-order offset, and queue overflow.
- [ ] Use the Milestone 04 same-sample phase order: vertical selection precedes source events/boundaries, sequential occurrences are visible to a same-sample boundary, and a new destination never reprocesses the same-offset input.
- [ ] Seed/reset the PRNG solely through the host API and use a specified algorithm/version for reproducible sequences.

### Writer graph feedback

- [ ] Show invalid vertical compatibility, ambiguous event use, incomplete random totals, unconnected outputs, queue configuration, and priority order directly on connectors/nodes.
- [ ] Display the active node, active pickdown tails, mapped vertical position, manually queued sequential output, and event queue state during audition.
- [ ] Permit connection edits during playback through the safe publication policy.

## Acceptance Criteria

- [ ] Sequential, vertical, random, event queue, and pickdown behavior is identical in writer playback and the runtime library.
- [ ] Every transition is sample-accurate across arbitrary legal block partitioning.
- [ ] Vertical compatibility updates immediately after meter/measure edits and blocks invalid export.
- [ ] Same seed and event stream yields the same graph path and MIDI bytes across platforms.
- [ ] Persistent event queues and all simultaneous pickdown tails remain allocation-free and bounded.
- [ ] Tempo curves remain continuous where authored, exact at steps, monotonic in musical-to-sample mapping, and stable across block boundaries.

## Test Focus

- [ ] Exhaustive precedence tests for event, manual, random, priority, and stable order.
- [ ] First/latest/FIFO listener behavior across many nodes, overflow, reset, pause, and stop.
- [ ] Cross-event priority/sequence arbitration, non-winning queue retention, and exact duplicate-discard behavior.
- [ ] Vertical events at block start/end, note onset, mid-note, measure boundary, tempo point, and pickdown boundary.
- [ ] Mismatched meter/count validation and edit-induced invalidation.
- [ ] Pickdown overlap chains, source/destination tempo differences, CC64, and natural tail completion.
- [ ] Exact-bound and one-over-capacity fixtures compared with an independent overlap-bound oracle.
- [ ] Same-pitch attacks and pedal spans shared by several concurrent tails and the active node.
- [ ] Weighted random distribution sanity plus exact deterministic sequence goldens.
- [ ] Tempo step/linear/smooth integration and inverse-position tests.
- [ ] Allocation/lock traps and sanitizer/concurrency soak tests over cyclic graphs.
