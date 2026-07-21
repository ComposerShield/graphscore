# Milestone 03: Persistence And Runtime Export

## Goal

Persist editable projects safely and export deterministic, validated, editor-free runtime assets.

## Dependencies

- [ ] Milestone 02 domain model.
- [ ] Cooked-format decision from Milestone 00.

## Deliverables

### Editable project bundle

- [ ] Define one versioned GraphScore project file implemented as a ZIP-like bundle.
- [ ] Store structured project/domain data separately from binary VST3 state blobs, recovery metadata, and optional thumbnails/cache data.
- [ ] Keep cache/thumbnail data disposable and excluded from semantic equality.
- [ ] Use atomic replace-on-save and preserve the previous valid file until replacement succeeds.
- [ ] Detect malformed archives, duplicate entries, path traversal, excessive sizes, unsupported schema versions, and truncated plugin blobs.

**Carried from M0 — plugin state is not byte-deterministic.** ADR 0007
observed Kontakt 8 save 3897 bytes, restore, then save 3913 bytes over an
identical cycle. Plugin state blobs are opaque and unstable.

- [ ] Never hash, diff, or content-compare plugin state blobs for dirty/change
      detection. Track modification by identity and explicit edit events.
- [ ] Exclude plugin state from semantic equality and from any determinism
      assertion.
- [ ] Ensure autosave and undo/redo do not treat a re-saved identical plugin
      state as a document change.

### Autosave and recovery

- [ ] Journal or snapshot unsaved command state without corrupting the primary project.
- [ ] Recover after process termination and identify the base project/version used by the recovery data.
- [ ] Clear recovery data only after a successful explicit save or deliberate discard.
- [ ] Keep opaque missing-plugin state intact through open, edit, autosave, and save.

### Schema evolution

- [ ] Version project and cooked schemas independently.
- [ ] Define minimum reader/writer versions and actionable unsupported-version diagnostics.
- [ ] Add migration fixtures from every released schema starting with the first public `0.1.0` schema.
- [ ] Preserve unknown non-semantic bundle entries where safe, but never silently accept unknown runtime semantics.

### Export pipeline

- [ ] Validate the complete project and block export on errors while retaining warnings.
- [ ] Remove graph positions, route geometry, colors, freeform notes, selection, undo history, autosave data, audio settings, and all plugin identities/state.
- [ ] Resolve stable track/event/node/connector UUIDs to compact indexes while preserving required IDs for host APIs and diagnostics.
- [ ] Precompute note schedules, tempo integration data, event lookup tables, transition tables, finite pickdown-tail capacity, and per-event queue capacities from Milestone 02 semantics.
- [ ] Reject unbounded/pathological tail overlap, incompatible vertical destinations, random groups not totaling 100 percent, and ambiguous vertical/sequential event bindings.
- [ ] Produce byte-for-byte deterministic output for semantically identical projects.

### Runtime loader boundary

- [ ] Load from a caller-provided immutable memory span using host allocator callbacks.
- [ ] Validate magic, version, lengths, offsets, indexes, sorted schedules, bounds, finite numeric values, and graph references before creating an instance.
- [ ] Never retain unsafe pointers into temporary caller memory unless the API explicitly accepts a lifetime-bound zero-copy mode.
- [ ] Return structured C-compatible error codes and optional off-thread diagnostic text.

## Acceptance Criteria

- [ ] Saving and reopening preserves all semantic writer data and opaque plugin state.
- [ ] Autosave recovers the last complete command state after simulated interruption at each save phase.
- [ ] Cooked assets contain no writer/plugin state and are deterministic across repeated exports and supported hosts.
- [ ] Every invalid buffer in the malformed corpus fails safely under ASan/UBSan without excessive allocation or recursion.
- [ ] Export computes and stores all capacities needed to keep processing allocation-free.
- [ ] Loader allocations use only the host callbacks when callbacks are supplied.

## Test Focus

- [ ] Golden bundle manifests and cooked binary fixtures.
- [ ] Semantic round trips independent of archive entry order and disposable caches.
- [ ] Save interruption/failure injection at create, write, flush, rename, and cleanup phases.
- [ ] Unknown version, truncation, integer overflow, duplicate-ID, invalid UTF-8, zip-bomb limit, and malicious-offset cases.
- [ ] Determinism tests across Debug/Release and all three primary operating systems.
- [ ] Migration tests retained permanently once a schema ships.
