# Milestone 09: Integrated Writer Workflow

## Goal

Join notation, graph editing, adaptive scheduling, VST3 audition, persistence, and safe live editing into a complete composition workflow.

## Dependencies

- [ ] Milestones 05 through 08.
- [ ] Milestone 03 persistence and recovery.

## Deliverables

### Document lifecycle

- [ ] New, open, save, save-as, close, recent-project, and one-project-per-window workflows.
- [ ] Dirty-state prompts plus autosave/recovery status that never blocks the audio thread.
- [ ] Missing-plugin resolution UI that retains silent placeholders and opaque state.
- [ ] Export command with complete validation report, cooked-asset destination, deterministic summary, and success metadata.

### Live playback snapshots

- [ ] Build immutable scheduler snapshots off the audio thread and publish atomically at audio-block boundaries.
- [ ] Use preallocated publication slots plus epoch-based or equivalent off-thread reclamation; processing never destroys the final snapshot reference or frees memory.
- [ ] Apply future note, tempo, connector, priority, random weight, and event-listener edits on the next block when state can be reconciled.
- [ ] Anchor an accepted tempo edit at the current sample and musical position; the replacement curve affects only future integration and never retimes elapsed playback.
- [ ] Send note-off when a currently sounding note is deleted or shortened past the playhead.
- [ ] Never replay newly inserted events whose musical position is in the past.
- [ ] Defer measure insertion/deletion or other edits that change elapsed active-node structure until restart; mark them visibly as pending.
- [ ] Keep current tails on their original immutable snapshot until completion.
- [ ] Migrate playhead anchoring, note/CC64 ownership, queues, PRNG state, and stable connector/node references explicitly; deleted or replaced entities cannot leave dangling realtime state.
- [ ] Document and test a complete safe/deferred edit matrix, including tempo edits that shift future sample mapping and track/node/connector removal.

### Graph playback interaction

- [ ] Highlight active node, playhead, queued sequential connector, mapped vertical target, active pickdown tails, and plugin/mute state.
- [ ] Normal connector click always edits/selects.
- [ ] During playback, connector double-click or destination action-circle activation queues a sequential connector or immediately takes a vertical connector only when its source is active; otherwise show an unavailable reason.
- [ ] A manual sequential queue is current-node writer state and is cleared/replaced deterministically on transition or node-play restart.
- [ ] Node play sends required note-offs, clears event/manual queues and tails, resets deterministic state as documented, and begins the chosen node.
- [ ] Vertical jump removes only source-main ownership; stop/reset/node play/panic removes all note and pedal contributions, emits CC64-up where held, and leaves no stale ownership. Pause retains all ownership.
- [ ] Event simulator searches registered UTF-8 names, displays UUIDs, and injects exact sample-offset events through the scheduler input path.

### Composition workflows

- [ ] Integrate palette hover preview, note insertion audition, keyboard commands, measure/arbitrary-range selection and clipboard editing, measure structural editing, and graph navigation without focus conflicts.
- [ ] Support node copy/paste with explicit choices for external incoming/outgoing connectors and no duplicate UUIDs.
- [ ] Preserve selection/focus sensibly after undo/redo, save/reopen, track archive/restore, and live snapshot publication.
- [ ] Provide clear status for invalid vertical edges, random totals, event conflicts, deferred edits, unavailable plugins, and export blockers.
- [ ] Ensure custom connector routes, node colors, freeform notes, tempo lanes, and plugin chains participate in undo/redo and persistence.

### Application preferences

- [ ] Audio device/sample-rate/block settings, plugin search paths/blacklist, metronome/count-in settings, keyboard shortcut display, and accessibility preferences.
- [ ] Keep project-semantic settings in the project and machine-specific device/plugin paths in per-user preferences.
- [ ] Use atomic preference persistence and safe defaults when devices/plugins disappear.

## Acceptance Criteria

- [ ] A composer can create a multi-track project, author notation/tempo, connect nodes, configure events/randomness/pickdowns, load plugin chains, audition every transition type, save/recover, and export without leaving the writer.
- [ ] Writer MIDI decisions match the standalone runtime for recorded seed/event/block traces.
- [ ] Every declared safe live edit becomes audible on the next block without allocation, lock, duplicate event, or stuck note.
- [ ] Every deferred edit is visible and becomes active after a deterministic restart.
- [ ] Connector and node playback controls remain distinct from editing and are keyboard/screen-reader actionable.
- [ ] Crash-recovery tests preserve the last complete edit and plugin state.

## Test Focus

- [ ] End-to-end scripted composition scenarios with deterministic scheduler/MIDI traces.
- [ ] Live-edit matrix tests at block, beat, measure, transition, tempo-point, vertical, and pickdown boundaries.
- [ ] Snapshot publication/reclamation tests under allocation traps with active queues, owned notes, pedal spans, and old tails.
- [ ] Focus/selection tests across notation, graph, plugin editor, dialogs, and keyboard shortcuts.
- [ ] Save/export/reopen/recover scenarios with missing and restored plugins.
- [ ] Manual connector queue versus event/random precedence and node-play reset behavior.
- [ ] Multi-window device/plugin service ownership and shutdown tests.
