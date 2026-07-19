# Milestone 12: General MIDI CC (Post-0.1.0)

## Goal

Add general MIDI Control Change authoring and runtime output after the complete `0.1.0` scope is shipped and stabilized.

## Dependencies

- [ ] Released GraphScore `0.1.0`.
- [ ] Stable notation/tempo lane interaction patterns and runtime capacity model.
- [ ] Field feedback on CC64, cooked assets, and host MIDI routing.

## Proposed Scope

- [ ] Track/node CC lanes for controller numbers 0-127, excluding or specially coordinating existing CC64 pedal notation.
- [ ] Point, step, linear, and smooth controller curves anchored to musical positions.
- [ ] Explicit value range, thinning/resampling, and sample-accurate emission policy.
- [ ] Writer palette/inspector, graph-node lane UI, selection, copy/paste, undo/redo, accessibility, and plugin audition.
- [ ] Cooked schema additions with backward-compatible version negotiation.
- [ ] Runtime MIDI-capacity analysis accounting for worst-case controller density.
- [ ] Host/API documentation for ordering CC against note-off, program/state events if later added, and note-on at the same sample.

## Questions To Resolve At Milestone Start

- [ ] Whether curves emit at fixed musical intervals, fixed sample/control rate, value changes only, or a host-selected policy.
- [ ] Whether registered game parameters should map directly to CC values.
- [ ] Whether 14-bit CC pairs, NRPN/RPN, channel pressure, pitch bend, MPE, or MIDI 2.0 remain separate milestones.
- [ ] How CC state is chased on start-node, pause/resume, vertical transition, sequential transition, and seek/reset.
- [ ] Whether overlapping pickdowns retain independent CC state and how conflicts on the same track/channel/controller resolve.

## Acceptance Criteria

- [ ] General CC data is editable, accessible, persisted, exported, and emitted identically by writer and runtime.
- [ ] Controller output is deterministic across block partitioning and platforms.
- [ ] Processing remains allocation-free and capacity can be queried before playback.
- [ ] Older `0.1.0` libraries reject assets that require general CC semantics; only explicitly optional non-semantic fields may be ignored.
- [ ] CC64 notation remains semantically stable and does not double-emit when a general CC64 lane is present.

## Test Focus

- [ ] Curve interpolation, sampling/thinning, and exact endpoint tests.
- [ ] Same-sample ordering with notes and pedal notation.
- [ ] Transition/reset/chase behavior across every graph path type.
- [ ] Dense-lane capacity and realtime stress tests.
- [ ] Schema migration and old/new runtime compatibility tests.
