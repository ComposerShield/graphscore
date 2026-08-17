# Milestone 09: Integrated Writer Workflow

## Goal

Join notation, graph editing, adaptive scheduling, VST3 audition, persistence, and safe live editing into a complete composition workflow.

## Dependencies

- [ ] M9-phase-1 Milestones 05 through 08.
- [ ] M9-phase-2 Milestone 03 persistence and recovery.

## Deliverables

### Document lifecycle

- [ ] M9-phase-3 New, open, save, save-as, close, recent-project, and one-project-per-window workflows.
- [ ] M9-phase-4 Dirty-state prompts plus autosave/recovery status that never blocks the audio thread.
- [ ] M9-phase-5 Missing-plugin resolution UI that retains silent placeholders and opaque state.
- [ ] M9-phase-6 Export command with complete validation report, cooked-asset destination, deterministic summary, and success metadata.

### Live playback snapshots

- [ ] M9-phase-7 Build immutable scheduler snapshots off the audio thread and publish atomically at audio-block boundaries.
- [ ] M9-phase-8 Use preallocated publication slots plus epoch-based or equivalent off-thread reclamation; processing never destroys the final snapshot reference or frees memory.
- [ ] M9-phase-9 Apply future note, tempo, connector, priority, random weight, and event-listener edits on the next block when state can be reconciled.
- [ ] M9-phase-10 Anchor an accepted tempo edit at the current sample and musical position; the replacement curve affects only future integration and never retimes elapsed playback.
- [ ] M9-phase-11 Send note-off when a currently sounding note is deleted or shortened past the playhead.
- [ ] M9-phase-12 Never replay newly inserted events whose musical position is in the past.
- [ ] M9-phase-13 Defer measure insertion/deletion or other edits that change elapsed active-node structure until restart; mark them visibly as pending.
- [ ] M9-phase-14 Keep current tails on their original immutable snapshot until completion.
- [ ] M9-phase-15 Migrate playhead anchoring, note/CC64 ownership, queues, PRNG state, and stable connector/node references explicitly; deleted or replaced entities cannot leave dangling realtime state.
- [ ] M9-phase-16 Document and test a complete safe/deferred edit matrix, including tempo edits that shift future sample mapping and track/node/connector removal.

### Graph playback interaction

- [ ] M9-phase-17 Highlight active node, playhead, queued sequential connector, mapped vertical target, active pickdown tails, and plugin/mute state.
- [ ] M9-phase-18 Normal connector click always edits/selects.
- [ ] M9-phase-19 During playback, connector double-click or destination action-circle activation queues a sequential connector or immediately takes a vertical connector only when its source is active; otherwise show an unavailable reason.
- [ ] M9-phase-20 A manual sequential queue is current-node writer state and is cleared/replaced deterministically on transition or node-play restart.
- [ ] M9-phase-21 Node play sends required note-offs, clears event/manual queues and tails, resets deterministic state as documented, and begins the chosen node.
- [ ] M9-phase-22 Vertical jump removes only source-main ownership; stop/reset/node play/panic removes all note and pedal contributions, emits CC64-up where held, and leaves no stale ownership. Pause retains all ownership.
- [ ] M9-phase-23 Event simulator searches registered UTF-8 names, displays UUIDs, and injects exact sample-offset events through the scheduler input path.

### Composition workflows

- [ ] M9-phase-24 Integrate palette hover preview, note insertion audition, keyboard commands, measure/arbitrary-range selection and clipboard editing, measure structural editing, and graph navigation without focus conflicts.
- [ ] M9-phase-25 Support node copy/paste with explicit choices for external incoming/outgoing connectors and no duplicate UUIDs.
- [ ] M9-phase-26 Preserve selection/focus sensibly after undo/redo, save/reopen, track archive/restore, and live snapshot publication.
- [ ] M9-phase-27 Provide clear status for invalid vertical edges, random totals, event conflicts, deferred edits, unavailable plugins, and export blockers.
- [ ] M9-phase-28 Ensure custom connector routes, node colors, freeform notes, tempo lanes, and plugin chains participate in undo/redo and persistence.

### Application preferences

- [ ] M9-phase-29 Audio device/sample-rate/block settings, plugin search paths/blacklist, metronome/count-in settings, and keyboard shortcut display.
- [ ] M9-phase-30 Keep project-semantic settings in the project and machine-specific device/plugin paths in per-user preferences.
- [ ] M9-phase-31 Use atomic preference persistence and safe defaults when devices/plugins disappear.

## Acceptance Criteria

- [ ] M9-phase-32 A composer can create a multi-track project, author notation/tempo, connect nodes, configure events/randomness/pickdowns, load plugin chains, audition every transition type, save/recover, and export without leaving the writer.
- [ ] M9-phase-33 Writer MIDI decisions match the standalone runtime for recorded seed/event/block traces.
- [ ] M9-phase-34 Every declared safe live edit becomes audible on the next block without allocation, lock, duplicate event, or stuck note.
- [ ] M9-phase-35 Every deferred edit is visible and becomes active after a deterministic restart.
- [ ] M9-phase-36 Connector and node playback controls remain distinct from editing and are keyboard actionable.
- [ ] M9-phase-37 Crash-recovery tests preserve the last complete edit and plugin state.

## Test Focus

- [ ] M9-phase-38 End-to-end scripted composition scenarios with deterministic scheduler/MIDI traces.
- [ ] M9-phase-39 Live-edit matrix tests at block, beat, measure, transition, tempo-point, vertical, and pickdown boundaries.
- [ ] M9-phase-40 Snapshot publication/reclamation tests under allocation traps with active queues, owned notes, pedal spans, and old tails.
- [ ] M9-phase-41 Focus/selection tests across notation, graph, plugin editor, dialogs, and keyboard shortcuts.
- [ ] M9-phase-42 Save/export/reopen/recover scenarios with missing and restored plugins.
- [ ] M9-phase-43 Manual connector queue versus event/random precedence and node-play reset behavior.
- [ ] M9-phase-44 Multi-window device/plugin service ownership and shutdown tests.
