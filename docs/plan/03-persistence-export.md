# Milestone 03: Persistence And Runtime Export

## Goal

Persist editable projects safely and export deterministic, validated, editor-free runtime assets.

## Dependencies

- [ ] `M3-phase-1` Milestone 02 domain model.
- [ ] `M3-phase-2` Cooked-format decision from Milestone 00.

## Deliverables

### Editable project bundle

- [ ] `M3-phase-3` Define one versioned GraphScore project file implemented as a ZIP-like bundle.
- [ ] `M3-phase-4` Store structured project/domain data separately from binary VST3 state blobs, recovery metadata, and optional thumbnails/cache data.
- [ ] `M3-phase-5` Keep cache/thumbnail data disposable and excluded from semantic equality.
- [ ] `M3-phase-6` Use atomic replace-on-save and preserve the previous valid file until replacement succeeds.
- [ ] `M3-phase-7` Detect malformed archives, duplicate entries, path traversal, excessive sizes, unsupported schema versions, and truncated plugin blobs.

**Carried from M0 — plugin state is not byte-deterministic.** ADR 0007
observed Kontakt 8 save 3897 bytes, restore, then save 3913 bytes over an
identical cycle. Plugin state blobs are opaque and unstable.

- [ ] `M3-phase-8` Never hash, diff, or content-compare plugin state blobs for dirty/change
      detection. Track modification by identity and explicit edit events.
- [ ] `M3-phase-9` Exclude plugin state from semantic equality and from any determinism
      assertion.
- [ ] `M3-phase-10` Ensure autosave and undo/redo do not treat a re-saved identical plugin
      state as a document change.

### Autosave and recovery

- [ ] `M3-phase-11` Journal or snapshot unsaved command state without corrupting the primary project.
- [ ] `M3-phase-12` Recover after process termination and identify the base project/version used by the recovery data.
- [ ] `M3-phase-13` Clear recovery data only after a successful explicit save or deliberate discard.
- [ ] `M3-phase-14` Keep opaque missing-plugin state intact through open, edit, autosave, and save.

### Schema evolution

- [ ] `M3-phase-15` Version project and cooked schemas independently.
- [ ] `M3-phase-16` Define minimum reader/writer versions and actionable unsupported-version diagnostics.
- [ ] `M3-phase-17` Add migration fixtures from every released schema starting with the first public `0.1.0` schema.
- [ ] `M3-phase-18` Preserve unknown non-semantic bundle entries where safe, but never silently accept unknown runtime semantics.
- [ ] `M3-phase-19` Round-trip every identity the writer's selection model addresses, and preserve connector insertion order. Beyond `ChordNote::id` and `GraceNote::id`, the identities cover `Rest::id` and every marking record's id; and because Milestone 05 addresses articulations, ties and tuplets by composite key rather than by minting an id per marking, articulation membership and order, `tied_to_next`, and tuplet run boundaries are load-bearing across a migration too. Changing or regenerating any of them, or reordering connectors, breaks arbitration, selection, and clipboard remapping.

### Export pipeline

- [ ] `M3-phase-20` Validate the complete project and block export on errors while retaining warnings.
- [ ] `M3-phase-21` Remove graph positions, route geometry, colors, freeform notes, selection, undo history, autosave data, audio settings, and all plugin identities/state.
- [ ] `M3-phase-22` Resolve stable track/event/node/connector UUIDs to compact indexes while preserving required IDs for host APIs and diagnostics.
- [ ] `M3-phase-23` Precompute note schedules, tempo integration data, event lookup tables, transition tables, finite pickdown-tail capacity, and per-event queue capacities from Milestone 02 semantics.
- [ ] `M3-phase-24` Reject unbounded/pathological tail overlap, incompatible vertical destinations, random groups not totaling 100 percent, and ambiguous vertical/sequential event bindings.
- [ ] `M3-phase-25` Produce byte-for-byte deterministic output for semantically identical projects.

### Runtime loader boundary

- [ ] `M3-phase-26` Load from a caller-provided immutable memory span using host allocator callbacks.
- [ ] `M3-phase-27` Validate magic, version, lengths, offsets, indexes, sorted schedules, bounds, finite numeric values, and graph references before creating an instance.
- [ ] `M3-phase-28` Never retain unsafe pointers into temporary caller memory unless the API explicitly accepts a lifetime-bound zero-copy mode.
- [ ] `M3-phase-29` Return structured C-compatible error codes and optional off-thread diagnostic text.

## Acceptance Criteria

- [ ] `M3-phase-30` Saving and reopening preserves all semantic writer data and opaque plugin state.
- [ ] `M3-phase-31` Autosave recovers the last complete command state after simulated interruption at each save phase.
- [ ] `M3-phase-32` Cooked assets contain no writer/plugin state and are deterministic across repeated exports and supported hosts.
- [ ] `M3-phase-33` Every invalid buffer in the malformed corpus fails safely under ASan/UBSan without excessive allocation or recursion.
- [ ] `M3-phase-34` Export computes and stores all capacities needed to keep processing allocation-free.
- [ ] `M3-phase-35` Loader allocations use only the host callbacks when callbacks are supplied.

## Test Focus

- [ ] `M3-phase-36` Golden bundle manifests and cooked binary fixtures.
- [ ] `M3-phase-37` Semantic round trips independent of archive entry order and disposable caches.
- [ ] `M3-phase-38` Save interruption/failure injection at create, write, flush, rename, and cleanup phases.
- [ ] `M3-phase-39` Unknown version, truncation, integer overflow, duplicate-ID, invalid UTF-8, zip-bomb limit, and malicious-offset cases.
- [ ] `M3-phase-40` Determinism tests across Debug/Release and all three primary operating systems.
- [ ] `M3-phase-41` Migration tests retained permanently once a schema ships.
