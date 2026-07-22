# Milestone 02: Domain And Command Model

## Goal

Implement the toolkit-independent source of truth for notation, graph structure, identities, editing commands, and validation.

## Dependencies

- [x] Milestone 01 foundation.
- [x] Milestone 00 architecture decisions.

## Deliverables

### Identity and value types

- [x] Strong UUID wrappers for projects, tracks, staves, nodes, connectors, events, and notation entities.
- [x] Stable project-order indexes generated separately from persistent identity.
- [x] Exact rational musical positions and durations; avoid floating-point equality for score structure.
- [x] Validated MIDI pitch, channel, velocity, accidental, clef, key signature, time signature, voice, and tempo value types.
- [x] Explicit conversions at API/serialization boundaries and no implicit unit mixing.

### Project and track model

- [x] Project metadata, designated start node, project tempo/dynamic defaults, and event registry.
- [x] Up to 64 globally active track definitions with fixed one-or-more-staff layouts and MIDI channels.
- [x] Standard pitched and grand-staff configurations.
- [x] Archived removed tracks retain recoverable notation but are excluded from active playback/export.
- [x] Adding a track creates an empty aligned track lane in every node.

### Node timeline

- [x] A common ordered measure map shared by every active track in a node.
- [x] Per-measure time and standard key signatures.
- [x] Clef changes at exact musical positions.
- [x] At least one complete main-region measure plus an optional explicit final pickdown duration greater than zero and shorter than one measure under the boundary's active meter.
- [x] Tempo curves cover the main region and pickdown; crossing notes/ties identify their main and tail ownership at the boundary.
- [x] No opening partial measure in the `0.1.0` model.
- [x] A node name, stable UUID, custom color, freeform notes, and graph position.

### Notation model

- [x] Four explicit voices per staff, each rhythmically complete through normalized rests.
- [x] Notes, chords, rests, ties, whole through sixty-fourth durations, single/double dots, and arbitrary single-level `N:M` tuplets.
- [x] Grace-note groups attached to principal events with playback time stolen from the preceding note.
- [x] Dynamics, hairpins, slurs, accent, marcato, staccato, staccatissimo, tenuto, beam overrides, stem overrides, and pedal spans.
- [x] Individual note spelling through double-flat and double-sharp.
- [x] Referential validation for ties, slurs, hairpins, tuplets, beams, and spans after edits.

### Graph model

- [ ] Any number of named stable input and output connectors per node.
- [ ] Zero or one destination edge per output while editing; enabled exportable outputs require exactly one.
- [ ] Sequential and vertical connector types with type-appropriate trigger, priority, and weight fields; queue policy/capacity is stored once per node/event listener.
- [ ] Registered event UUID/name lookup with unique case-sensitive UTF-8 names.
- [ ] Normative persistent event state machine: bounded occurrence storage per event, arrival sequence, per-node/event first/latest/FIFO interpretation, cross-event connector-priority arbitration, stable tie-breaking, consumption/discard rules, pause behavior, and clearing on stop/reset/node-play.
- [ ] Orthogonal route geometry stored as automatic or user-customized interior segments, independent of runtime semantics.

### Adaptive playback semantics

- [ ] Exact definitions for sequential precedence, 100-percent random groups, zero-weight ineligibility, deterministic PRNG reset, vertical compatibility/mapping, sample-local vertical triggers, and one vertical jump per sample offset.
- [ ] Pickdown meter/tempo coordinates, crossing-note ownership, MIDI-only behavior, minimum main-region duration, and requirements for proving finite overlap through cycles.
- [ ] Same-pitch newest-owner retrigger behavior and logical-OR CC64 ownership across active main material and tails.
- [ ] Lifecycle rules remove note and pedal ownership on vertical jump, stop, reset, node play, panic, and snapshot retirement; pause retains ownership.
- [ ] A toolkit-independent writer audition model describes one opaque instrument slot, zero or more opaque effect slots, plugin identity/state blobs, bypass/order, and writer-only mix values without depending on the VST3 SDK.

### Normative playback specification

- [ ] Specify cubic smooth-tempo curve equations, legal control handles, deterministic integration/inversion tolerances, and integer sample rounding at boundaries.
- [ ] Enumerate articulation mappings and precedence with slurs, ties, dynamics, and hairpins.
- [ ] Specify grace-group steal fraction/limits, multi-note division, behavior without a preceding sounded note, and interaction with articulations.
- [ ] Define simultaneous MIDI ordering and exact note/CC64 ownership transitions independently of implementation.

### Command and selection model

- [ ] Reversible commands for every graph, notation, tempo, track, and metadata edit.
- [ ] Transaction grouping for multi-measure operations and drag gestures.
- [ ] Stable notehead, chord, full-measure, arbitrary musical-time range, node, connector, route-segment, staff-focus, and insertion-caret selection, with explicit staff/track/voice scope.
- [ ] Toolkit-independent notation clipboard fragments preserve relative musical positions, staff/voice mapping, notation entities, and source duration without retaining source entity UUIDs.
- [ ] Reversible cut/copy/paste commands support complete measures and non-measure-aligned fragments, remap identities, normalize destination rests, and modify no music outside the destination range.
- [ ] Specify deterministic clipping/reconnection rules for notes, ties, slurs, tuplets, hairpins, pedal spans, dynamics, clef/key/time changes, and other entities crossing a copied range boundary.
- [ ] Node copy/paste identity remapping rules that never duplicate stable UUIDs.
- [ ] Measure insert/delete operations update every track, signature lane, tempo anchors, spans, and compatibility diagnostics atomically.

### Validation service

- [ ] Fast incremental validation for editor feedback and complete validation for save/export.
- [ ] Diagnostics carry stable entity IDs, severity, machine-readable code, and user-facing text.
- [ ] Validate rhythmic completeness, UUID uniqueness, references, track alignment, signature legality, graph edge integrity, event-name uniqueness, and connector cardinality.

## Acceptance Criteria

- [ ] Domain targets have no UI, renderer, audio-device, plugin, or platform dependencies.
- [ ] All edits required by the `0.1.0` notation/graph scope are expressible as commands and undo/redo exactly.
- [ ] Replaying the same command stream produces structurally equal projects with stable intended IDs.
- [ ] Invalid cross-entity references cannot enter normal model APIs without a diagnostic result.
- [ ] The event, transition, pickdown, tempo, articulation, grace, and MIDI-ownership state machines are complete enough to implement independently in writer/runtime and compare with shared golden vectors.
- [ ] Representative 64-track, 64-measure nodes remain practical to construct, validate, copy, and mutate in tests.
- [ ] Archived track data survives removal, save-model round trip, undo, and restoration.

## Test Focus

- [ ] Value-type boundary and invalid-input tests.
- [ ] Table-driven duration, tuplet, accidental, key, clef, and meter tests.
- [ ] Four-voice rhythm normalization and automatic-rest tests.
- [ ] Command execute/undo/redo round trips, including grouped failure rollback.
- [ ] Deterministic generated edit sequences checked against model invariants.
- [ ] UUID remapping, stale selection, span repair, and cross-measure mutation tests.
- [ ] Whole-measure and arbitrary-range clipboard round trips across voices/staves, including boundary-crossing notation and paste into occupied destination ranges.
- [ ] Incremental diagnostics compared with complete validation results.
- [ ] Event state-machine, same-sample vertical-cycle prevention, pickdown-bound proof-oracle, MIDI ownership, and lifecycle clearing tests.
