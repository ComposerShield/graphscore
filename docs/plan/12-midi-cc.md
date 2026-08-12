# Milestone 12: General MIDI CC (Post-0.1.0)

## Goal

Add general MIDI Control Change authoring and runtime output after the complete `0.1.0` scope is shipped and stabilized.

## Dependencies

- [ ] M12-phase-1 Released GraphScore `0.1.0`.
- [ ] M12-phase-2 Stable notation/tempo lane interaction patterns and runtime capacity model.
- [ ] M12-phase-3 Field feedback on CC64, cooked assets, and host MIDI routing.

## Proposed Scope

- [ ] M12-phase-4 Track/node CC lanes for controller numbers 0-127, excluding or specially coordinating existing CC64 pedal notation.
- [ ] M12-phase-5 Point, step, linear, and smooth controller curves anchored to musical positions.
- [ ] M12-phase-6 Explicit value range, thinning/resampling, and sample-accurate emission policy.
- [ ] M12-phase-7 Writer palette/inspector, graph-node lane UI, selection, copy/paste, undo/redo, accessibility, and plugin audition.
- [ ] M12-phase-8 Cooked schema additions with backward-compatible version negotiation.
- [ ] M12-phase-9 Runtime MIDI-capacity analysis accounting for worst-case controller density.
- [ ] M12-phase-10 Host/API documentation for ordering CC against note-off, program/state events if later added, and note-on at the same sample.

## Questions To Resolve At Milestone Start

- [ ] M12-phase-11 Whether curves emit at fixed musical intervals, fixed sample/control rate, value changes only, or a host-selected policy.
- [ ] M12-phase-12 Whether registered game parameters should map directly to CC values.
- [ ] M12-phase-13 Whether 14-bit CC pairs, NRPN/RPN, channel pressure, pitch bend, MPE, or MIDI 2.0 remain separate milestones.
- [ ] M12-phase-14 How CC state is chased on start-node, pause/resume, vertical transition, sequential transition, and seek/reset.
- [ ] M12-phase-15 Whether overlapping pickdowns retain independent CC state and how conflicts on the same track/channel/controller resolve.

## Acceptance Criteria

- [ ] M12-phase-16 General CC data is editable, accessible, persisted, exported, and emitted identically by writer and runtime.
- [ ] M12-phase-17 Controller output is deterministic across block partitioning and platforms.
- [ ] M12-phase-18 Processing remains allocation-free and capacity can be queried before playback.
- [ ] M12-phase-19 Older `0.1.0` libraries reject assets that require general CC semantics; only explicitly optional non-semantic fields may be ignored.
- [ ] M12-phase-20 CC64 notation remains semantically stable and does not double-emit when a general CC64 lane is present.

## Test Focus

- [ ] M12-phase-21 Curve interpolation, sampling/thinning, and exact endpoint tests.
- [ ] M12-phase-22 Same-sample ordering with notes and pedal notation.
- [ ] M12-phase-23 Transition/reset/chase behavior across every graph path type.
- [ ] M12-phase-24 Dense-lane capacity and realtime stress tests.
- [ ] M12-phase-25 Schema migration and old/new runtime compatibility tests.
