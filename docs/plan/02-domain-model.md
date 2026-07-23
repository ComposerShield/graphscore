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

- [x] Any number of named stable input and output connectors per node.
- [x] Zero or one destination edge per output while editing; enabled exportable outputs require exactly one.
- [x] Sequential and vertical connector types with type-appropriate trigger, priority, and weight fields; queue policy/capacity is stored once per node/event listener.
- [x] Registered event UUID/name lookup with unique case-sensitive UTF-8 names.
- [x] Normative persistent event state machine: bounded occurrence storage per event, arrival sequence, per-node/event first/latest/FIFO interpretation, cross-event connector-priority arbitration, stable tie-breaking, consumption/discard rules, pause behavior, and clearing on stop/reset/node-play.
- [x] Orthogonal route geometry stored as automatic or user-customized interior segments, independent of runtime semantics.

### Adaptive playback semantics

- [x] Exact definitions for sequential precedence, 100-percent random groups, zero-weight ineligibility, deterministic PRNG reset, vertical compatibility/mapping, sample-local vertical triggers, and one vertical jump per sample offset.
- [x] Pickdown meter/tempo coordinates, crossing-note ownership, MIDI-only behavior, minimum main-region duration, and requirements for proving finite overlap through cycles.
- [x] Same-pitch newest-owner retrigger behavior and logical-OR CC64 ownership across active main material and tails.
- [x] Lifecycle rules remove note and pedal ownership on vertical jump, stop, reset, node play, panic, and snapshot retirement; pause retains ownership.
- [x] A toolkit-independent writer audition model describes one opaque instrument slot, zero or more opaque effect slots, plugin identity/state blobs, bypass/order, and writer-only mix values without depending on the VST3 SDK.

### Normative playback specification

- [x] Specify cubic smooth-tempo curve equations, legal control handles, deterministic integration/inversion tolerances, and integer sample rounding at boundaries.
- [x] Enumerate articulation mappings and precedence with slurs, ties, dynamics, and hairpins.
- [x] Specify grace-group steal fraction/limits, multi-note division, behavior without a preceding sounded note, and interaction with articulations.
- [x] Define simultaneous MIDI ordering and exact note/CC64 ownership transitions independently of implementation.

### Command and selection model

- [x] **Phase 8a — foundational command protocol, standalone history, and atomic transaction grouping (done, committed).** Delivered the non-throwing `Command` ABC with `execute`/`undo`/`redo` all `noexcept` and returning `Result`; `CommandHistory` (standalone undo/redo stacks with allocation-safe pre-reserve before any model mutation); `CommandTransaction` (ordered-child atomic grouping with best-effort rollback on any child failure, explicit `kCommandFaulted`/`kTransactionRollbackFailed` terminal states, and exact restoration semantics on undo/redo compensation failure); three stable-ID proving commands (`SetNodeNameCommand`, `SetTrackNameCommand`, `SetProjectTempoCommand`) exercising the full three-phase lifecycle against real domain types. `Result` and `ResultCode` extended with two new terminal codes. `Node::set_name` and `Track::set_name` tightened to `noexcept`.
- [x] **Phase 8b — ten reversible non-structural metadata commands (done, committed).** Delivered the remaining single-field project, track, and node commands against stable `NodeId`/`TrackId` lookup with identical-noexcept-snapshot-semantics as 8a. Project commands: `SetProjectNameCommand` (UTF-8 `std::string`, allocation-safe, empty/long/multibyte round-trips), `SetStartNodeCommand` (optional set or clear with `kInvalidArgument` on unowned `NodeId`, re-validated on each phase), `SetProjectDynamicCommand` (all eight `Dynamic` values round-trip). Track audition-mix commands: `SetTrackGainCommand`, `SetTrackPanCommand`, `SetTrackMuteCommand`, `SetTrackSoloCommand` — each rejects missing and archived `TrackId`s without mutation; gain/pan store every IEEE 754 `float` bit-pattern exactly (NaN/inf/subnormal/+0/−0 included, legal-range validation deferred to M08). Node metadata commands: `SetNodeColorCommand` (packed `uint32_t` RGBA, all bytes exact), `SetNodeNotesCommand` (freeform `std::string`, empty/long/UTF-8 round-trips), `SetNodePositionCommand` (`GraphPosition` double-pair, every IEEE 754 bit-pattern stored exactly, unrelated fields unchanged). Identical-command-stream deterministic replay evidence: all ten commands snapshot the old value on first successful execute; restore it on undo; re-apply the new value on redo. The eight original Phase 8 deliverable boxes below remain unchecked: exhaustive edit commands, selection, clipboard, clipping/remapping, and measure operations — plugin-chain, structural, graph, timeline, and notation commands are still outstanding.
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
