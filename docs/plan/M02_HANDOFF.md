# Milestone 02 (Domain And Command Model) — Orchestration Handoff

**Status as of this doc:** Phases 1–7 complete. Phases 8a (command foundation),
8b (metadata/audition-mix commands), and 8c (fourteen reversible
structural/config commands, split 8c-i + 8c-ii) are complete and committed.
Phase 8's remaining work — add/remove structural commands, selection,
clipboard, and measure ops — is Phase 8d onward; the add/remove commands need
new domain restore-with-id/removal API before they can be exactly reversible
(see the Phase 8c row and the "Plan for the remaining phases" 8d entry). Phase 7
(Normative playback specification) was split into 7a/7b/7c per Adam's locked
scoping rulings (see "Plan for the remaining phases"). 7a is committed
(`063f1af`) after three review rounds that caught a false continuity claim, a
non-additive integration bug, and a sanitizer-fatal `Rational` overflow. 7b is
committed (`dc7550a`) after two review rounds that caught an inverted
precedence claim and a second sanitizer-fatal `Rational`/shift-overflow bug in
the grace-steal math. 7c is the completed spec-only same-sample MIDI ordering
contract. This file lets a different orchestrator pick up mid-milestone without
re-deriving the plan.
Source of truth remains
[02-domain-model.md](02-domain-model.md) and [CHECKLIST.md](CHECKLIST.md); this
doc records *how* the milestone is being executed.

## Milestone scope recap

Implement the toolkit-independent domain: value types, project/track/node,
notation, graph, event/transition semantics, commands, selection, clipboard,
and validation. **Only two CMake targets are in scope:**

- `graphscore_core` — pure value types (Layer 0).
- `graphscore_domain` — everything else (Layer 1); links **only** `graphscore_core`.

No new CMake targets. No `cmake/architecture_contract.cmake` or ADR changes
(that edge — domain→core — already exists in the contract). Dependencies are
satisfied: Milestone 01 is fully complete; Milestone 00 ADRs 0001–0007 are
recorded. (M00 still has two open housekeeping boxes — spike deletion, exit
decision — that are NOT inputs to M02.)

## Execution model (the loop, per phase)

For each plan section, strictly in order, no interleaving:

1. **One fresh `worker`** implements the phase from a fully self-contained brief
   (milestone path, deliverables verbatim, exact file paths, conventions,
   scope boundaries, verification gates). One task per worker, then retire it.
2. **One fresh `reviewer`** audits against the plan and independently runs the
   four gates. Verdict APPROVED / NEEDS WORK / REJECTED.
3. On NEEDS WORK/REJECTED (or a chosen advisory fix): a **fresh worker** applies
   fixes from the findings verbatim; then a **fresh reviewer** re-audits the
   delta. Repeat until APPROVED.
4. **One fresh worker** checks off the section's `- [ ]` boxes in
   02-domain-model.md + the matching CHECKLIST.md box, and commits.

Large phases are split into internal increments (e.g. 4a/4b), each its own
worker+reviewer, committed separately, with the CHECKLIST section box checked
only when the whole section is approved.

### The four gates (definition of done, run from repo root)

1. `cmake --preset debug && cmake --build --preset debug` → **zero warnings**
   (warnings are errors).
2. `ctest --preset debug --output-on-failure` → all green.
3. `cmake --build --preset debug --target lint` → cpplint + clang-format clean.
4. `cmake --build --preset debug --target audit_architecture` → all seven audits
   pass.

## Commit policy

- Commits go **directly to `main`** (repo has no meaningful remote; matches the
  M01 history convention). One commit per phase/increment, made by the
  check-off worker (which also flips the checklist boxes in the same commit).
- Commit messages: **describe only the project change. NEVER mention any
  AI/assistant/model/vendor, and NEVER add a `Co-Authored-By` or any attribution
  trailer** (AGENTS.md rule; it overrides any global default).
- **Never stage** `.claude/agents/orchestrator.md` or
  `.claude/settings.local.json` — they carry pre-existing unrelated
  modifications from before the milestone. Stage by explicit paths.

## Progress: phases and commits

| # | Section | State | Commit |
|---|---|---|---|
| 1 | Identity and value types | ✅ done | `0d6693b` |
| 2 | Project and track model | ✅ done | `511045f` |
| 3 | Node timeline | ✅ done (incl. tempo/region revalidation fix) | `b330675` |
| 4 | Notation model | ✅ done — split 4a + 4b | `af8ff75` (4a), `99c1452` (4b) |
| 5 | Graph model | ✅ done — split 5a + 5b | `87e4b92` (5a), `e13d4b5` (5b) |
| 6a | Adaptive playback semantics — transition selection | ✅ done | `c6be9a4` |
| 6b-i | Adaptive playback semantics — pickdown coordinates/ownership/bound oracle | ✅ done | (this commit) |
| 6b-ii | Adaptive playback semantics — MIDI ownership + lifecycle | ✅ done | (this commit) |
| 6c | Adaptive playback semantics — writer audition model | ✅ done | (this commit) |
| 7a | Normative playback specification — tempo curve math | ✅ done | `063f1af` |
| 7b | Normative playback specification — articulation/dynamic/grace mapping | ✅ done | `dc7550a` |
| 7c | Normative playback specification — simultaneous MIDI ordering (spec only) | ✅ done | — |
| 8a | Command foundation (protocol, history, transaction, proving commands) | ✅ done | (this commit) |
| 8b | Metadata/audition-mix reversible commands | ✅ done | `ffc9f2c` |
| 8c-i | Reversible track-archive + connector-config commands (9) | ✅ done | `54543c1` |
| 8c-ii | Reversible connection/route/event-binding commands (5) | ✅ done | `138458d` |
| 8d | Add/remove of graph entities (new restore-with-id domain API + commands); scoped, split 8d-i connectors → 8d-ii events → 8d-iii tracks → 8d-iv nodes | ⬜ next | — |
| 8e..f | Selection model, clipboard, cut/copy/paste + clip/reconnect, node copy/paste id remapping, measure ops | ⬜ remaining | — |
| 9 | Validation service | ⬜ remaining | — |
| — | Acceptance criteria + Test focus | ⬜ remaining (final boxes) | — |

Test suite currently: **896 tests, 100% pass** after Phase 8c (`CommandTest.*`
105→222); debug + ASan/UBSan clean with zero findings across both 8c
increments' review rounds.

CHECKLIST.md M02 boxes checked so far: Dependencies, Identity and value types,
Project and track model, Node timeline, Notation model, Graph model, Adaptive
playback semantics, Normative playback specification (plus the 8a/8b/8c
descriptive sub-boxes). Remaining M02 boxes: **Command and selection model**
(still unchecked — 8c completed the reversible-today subset but the section's
add/remove structural, selection, clipboard, and measure-op deliverables are
not done), Validation service, Acceptance criteria, Test focus, and the
top-level "Milestone 02 complete".

## What each completed phase delivered (so the next agent knows what exists)

- **Phase 1 (`graphscore_core`):** `StrongId<Tag>` over `Uuid` (Project/Track/
  Stave/Node/Connector/Event/NotationEntity ids) + `StrongIndex<Tag>` project-
  order indexes; exact GCD-reduced `Rational` with cross-multiplied comparison
  (no float); validated value types via `static std::optional<T> create(...)`:
  `MidiPitch` (0–127), `MidiChannel` (**0–15**), `MidiVelocity`, `Accidental`
  (double-flat..double-sharp), `Clef`, `KeySignature` (−7..7), `TimeSignature`
  (power-of-two denom), `Voice` (**1–4**), `Tempo` (10–400 BPM + `NoteValue`
  beat unit).
- **Phase 2 (`graphscore_domain`):** `Project` (metadata, designated start node,
  tempo/dynamic defaults), `Track` with `StaffLayout`/`StaveDefinition`
  (`single_staff()`/`grand_staff()`), fixed `MidiChannel`, **64-track cap**,
  recoverable **archived tracks**; a **minimal `Node`** (id/name/color/notes/
  `GraphPosition` + per-`TrackId` `TrackLane` map kept aligned across all nodes);
  `EventRegistry` (stable `EventId`, **case-sensitive** unique names). `Dynamic`
  enum lives in domain.
- **Phase 3 (`graphscore_domain`):** `NodeTimeline` (optional on `Node`) =
  node-wide `MeasureMap` (ordered measures, each a per-measure `TimeSignature`+
  `KeySignature`), per-`StaveId` `ClefLane`s at exact positions, optional
  **pickdown** (0 < dur < boundary measure), node-wide `TempoLane` (points +
  step/linear/smooth segment kinds), and a main/tail `classify(MusicalSpan)`
  boundary splitter. Canonical musical unit = **whole notes as `Rational`**.
  Region changes revalidate any stored tempo lane (no stale coverage).
- **Phase 4a (`core`+`domain`):** `Duration` (base + 0/1/2 dots + optional
  single-level `TupletRatio`, → exact `Rational`) and `SpelledPitch`
  (letter/octave/accidental → validated `MidiPitch`) in core; note/chord/rest
  `VoiceEvent`s with structural ties, four rhythmically-complete voices per
  stave in `TrackLane`, automatic normalized-rest filling against node length.
- **Phase 4b (`graphscore_domain`):** per-event `Articulation` set + `StemDirection`;
  per-voice dynamics, hairpins, slurs, beam overrides, grace groups (by id);
  stave-scoped `PedalSpan`s; and `notation_validation` — a **focused referential
  validator** (`NotationDiagnostic{entity_id, code, message}`) for ties, slurs,
  hairpins, pedal spans, conflicting duration articulations, incomplete tuplet
  groups, invalid beam overrides.
- **Phase 5 (`graphscore_domain`, split 5a + 5b):** `Connector`/
  `InputConnector`/`OutputConnector` with stable `ConnectorId`, sequential/
  vertical types, priority/weight, 0-or-1 destination and export-readiness;
  `EventListener` (`QueuePolicy` first-wins / latest-valid-wins (default) /
  FIFO + capacity) stored once per (node, event) with vertical-vs-sequential
  clash rejection; `RouteGeometry` (automatic vs user-customized orthogonal
  interior segments); `Graph` façade over `Project` (connect/disconnect,
  `remove_input` and `remove_event` cascades, `is_export_ready`); and
  `EventOccurrence`/`EventQueue`/`EventStateMachine` — the normative state
  machine with bounded per-(node,event) storage, monotonic arrival ordinals,
  per-policy consume/discard, three-tier arbitration (priority desc → newest
  arrival → stable connector order = `Node::outputs()` index), FIFO-overflow
  drop-oldest + pollable/resettable diagnostic counter, vertical
  non-persistence, and `clear_node`/`clear_event`/`clear_all` for stop/reset/
  node-play (pause = no call).
- **Phase 6a (`core` + `graphscore_domain`) — transition selection:** a
  `DeterministicPrng` (SplitMix64) in **core** — placed there rather than
  domain because the ADR 0003 runtime closure sees core but not domain, and
  writer and runtime must reproduce identical streams from the same seed —
  with an explicit host-seeded `reset`. `weighted_selection` (domain): exact
  `Rational` weights; the eligible group (destination-having, positive-weight
  candidates) must total exactly `Rational(1)`; zero-weight outputs are
  structurally excluded from the group rather than assigned an empty bucket;
  negative weights are rejected; `kDenominatorOverflow` closes the path
  rather than approximating; exact common-denominator bucketing consumes
  exactly one PRNG draw per selection, drawn only after the group passes
  validation. `vertical_transition` (domain): compatibility over matching
  main-region measure counts and per-measure time signatures with pickdowns
  excluded from the comparison, and exact rational vertical position mapping.
  On `EventStateMachine`: the three-tier `resolve_sequential_transition`
  (persistent event intent > manual queue > weighted random); a manual-queue
  slot (`queue_manual_transition`) that is writer-only, active-source-only,
  with stale entries re-validated at consume time rather than at queue time;
  `assemble_vertical_candidates` (applies the destination-required filter
  before candidates reach selection); `resolve_vertical_transition`;
  `reset_random`; and `TransportInstant` — a validated project-wide instant,
  **deliberately distinct from node-local `EventOccurrence::sample_offset`**,
  used to guard at most one vertical jump per instant via a fail-closed `<=`
  comparison.
- **Phase 7a (`core`) — tempo curve math:** `TempoPoint`/`TempoSegmentKind`
  relocated from `graphscore_domain` into `graphscore_core` (new
  `tempo_point.hpp`) since core-level integration math needs them and core
  cannot depend on domain — mirrors the Phase 6a `DeterministicPrng`
  placement rationale exactly. New `tempo_curve.hpp`/`.cpp`: instantaneous
  rate evaluation for `kStep`/`kLinear`/`kSmooth` segments with BPM+beat-unit
  normalization (a point expressed as 120bpm-quarter and one expressed as
  60bpm-half interpolate identically — verified by a dedicated trap test,
  not just claimed); `kSmooth` auto-shaped as a uniform-parametrization
  Catmull-Rom cubic (tension 0.5, the textbook default) through neighboring
  points, reflected at lane boundaries, **C0 (value-)continuous everywhere,
  C1 (slope-)continuous only when both adjacent segments are `kSmooth` and
  exactly equal-length** — the header states this precisely after a review
  round caught and corrected an earlier overclaim; deterministic integration
  of elapsed real time between two `Rational` positions (closed-form for
  `kStep`/`kLinear`, a fixed-32-step Simpson quadrature anchored at each
  segment's own start for `kSmooth`, making integration exactly additive
  across any interior split — a second review round caught and fixed a
  non-additive first version); deterministic fixed-52-iteration bisection
  inversion (elapsed seconds → `Rational` position), position quantized to a
  `2^20` grid (not `2^32` — a third review round caught a `Rational`
  cross-multiply overflow, fatal under the `asan-ubsan` preset, reachable
  from any position past ~0.5 whole notes at the original `2^32` grid; `2^20`
  keeps ~37x margin under one sample period at 192kHz while staying safe to
  ~8.39e6 whole notes); integer sample-count rounding from a caller-supplied
  sample rate (never hardcoded), round-half-to-even, with `isfinite`/
  saturating guards against NaN/±inf/huge-finite input (a UB fix from the
  first review round). `double` confined to this module only; every
  `Rational`-typed boundary stays exact. Resolves the `TODO(Phase 7)` seams
  at `tempo_lane.hpp:47-48` and the cross-reference in
  `pickdown_bound_oracle.hpp:88-94` (comment-only updates, zero logic
  change to `segment_index_at` or the oracle). 619 tests after 7a (+38 over
  Phase 6c's 581).
- **Phase 7b (`core` + `domain`) — articulation/dynamic/grace mapping:**
  `Articulation`/`is_duration_articulation`, `Dynamic`, and `GraceNoteType`
  relocated from `graphscore_domain` into `graphscore_core` (new
  `articulation.hpp`, `dynamic.hpp`, `grace_note_type.hpp`), same
  core-cannot-depend-on-domain reasoning as 7a's `TempoPoint` move;
  `StemDirection` stays in domain (engraving-only, not a playback concern).
  New `playback_mapping.hpp`/`.cpp` (core, exact `Rational`/integer
  arithmetic throughout — **no floating-point exception here**, unlike 7a):
  an 8-level dynamic→velocity table; exact-Rational hairpin interpolation
  between two already-resolved velocities with round-half-to-even; a
  documented, non-stacking accent/marcato emphasis rule (marcato wins
  outright if both present) saturating at `MidiVelocity::kMax`;
  articulation→sounded-duration ratios (default detache 7/8, staccato 1/2,
  staccatissimo 1/4, tenuto 1/1); slur legato overlap
  (`articulated_duration + gap_to_next_onset`); and grace-note steal math
  (acciaccatura front-loaded geometric division, appoggiatura even
  division, a steal cap leaving the preceding note >= half its duration,
  a fixed absolute fallback when there is no preceding sounded note).
  **Locked precedence: slur wins outright** — a note that is tied AND
  slurred AND duration-articulated is governed entirely by legato overlap;
  `is_tied` and any duration-articulation are silently bypassed on that
  path (a review round caught this contradicting the original doc prose,
  which claimed the two "never conflict" — they do, and slur wins, now
  documented as a deliberate call, not an accident). A second review round
  caught real UB — shift-exponent overflow and a fatal `Rational`
  summation overflow in the grace-steal weighting, reachable from an
  ordinary (unvalidated) `GraceGroup` size around 32+ notes, well short of
  any "non-musical extreme operand" — fixed with a named, enforced
  `kMaxGraceNotesPerGroup = 16` bound (weights sum to exactly 1 either side
  of the bound; beyond it, notes past the bound share the smallest slot
  equally rather than continuing to halve) and a closed-form (loop-free)
  `grace_steal_remaining_duration`. Thin domain wiring
  (`notation_playback.hpp`/`.cpp`) applies this math to existing
  `Note`/`Chord`/`GraceGroup` structures — deliberately does **not** walk
  `TrackLane`/`VoiceContent` to resolve which `Dynamic`/`Hairpin` governs
  an arbitrary event; callers supply pre-resolved context, same
  spec-now-wire-later boundary as 7c draws for MIDI ordering. Resolves the
  `TODO(Phase 7)` seam at `notation_markings.hpp:112` (was `GraceGroup`)
  and the untagged dynamic/hairpin/slur/pedal-to-MIDI deferrals at
  `articulation.hpp:11` and the former `notation_markings.hpp:13,30,41,73`.
  674 tests after 7b (+55 over Phase 7a's 619).
- **Phase 7c (`domain`, spec only) — simultaneous MIDI ordering:** added the
  normative `midi_ownership.hpp` contract for atomic per-sample note and CC64
  ownership resolution, source ordering, lifecycle/retrigger provenance, and
  note-off/CC64/note-on stream serialization. It deliberately adds no runtime
  API, production logic, or tests; M04 owns the scheduler implementation.
- **Phase 8a (`core` + `domain`) — command foundation:** delivered the
  non-throwing `Command` ABC (`execute`/`undo`/`redo` all `noexcept`, returning
  `Result` via extended `ResultCode` with two new terminal codes:
  `kTransactionRollbackFailed` and `kCommandFaulted`). `CommandHistory` —
  standalone undo/redo double-stack service, allocation-safe with pre-reserve
  before any model mutation, undo/redo failure keeps the command on its
  original stack (no silent loss), empty-stack operations are safe no-ops.
  `CommandTransaction` — ordered-child atomic grouping implementing the
  plan's Phase 8 transaction spec: children execute in insertion order; any
  child failure triggers best-effort rollback (compensating undo in reverse
  order, every compensator attempted even if an earlier one fails). Three
  explicit fault paths: (1) execute failure + successful rollback →
  `kFaulted` (terminal, transaction must be discarded — children are now in
  `kUndone` and the transaction demands `kFresh` for re-execution);
  (2) any operation failure where compensation ALSO fails →
  `kTransactionRollbackFailed` + `kFaulted` (project may retain partial
  mutations); (3) undo/redo compensation failure where restoration succeeded
  → original failure returned, state preserved for retry. Empty transactions
  are valid. After a review round: best-effort semantics verified across all
  three phases; undo/redo restoration insertion order confirmed model-observable;
  one-shot retry after successful compensation added. Three stable-ID proving
  commands exercising the full lifecycle against real domain types:
  `SetNodeNameCommand`, `SetTrackNameCommand`, `SetProjectTempoCommand` — each
  snapshots old state on first execute, double-execute/undo-without-execute/
  redo-without-undo all rejected, missing-ID lookup returns `kInvalidArgument`
  without touching the model. Precondition: `Node::set_name` and
  `Track::set_name` tightened to `noexcept` so concrete command implementations
  can uphold the base-class `noexcept` contract. 60 new test cases (60 added,
  total 734), ASan/UBSan green with zero findings across two review rounds.
- **Phase 8b (`domain`) — ten reversible non-structural commands (approved,
  no required findings):** delivered the remaining single-field project,
  track, and node commands against stable `NodeId`/`TrackId` lookup with
  identical noexcept-snapshot semantics as 8a. Project commands:
  `SetProjectNameCommand` (UTF-8 `std::string`, allocation-safe with
  `std::bad_alloc`/`std::length_error` guard; empty, 10k-char, and multibyte
  round-trips all verified), `SetStartNodeCommand` (optional set or clear
  with `kInvalidArgument` on unowned `NodeId`, correctly re-validates on
  each phase — including the case where an undo/redo attempts to set an
  id that has since been removed from the model, returning
  `kInvalidArgument` rather than silently persisting a stale reference);
  `SetProjectDynamicCommand` (all eight `Dynamic` values round-trip,
  snapshotting the project's `default_dynamic()`). Track audition-mix
  commands: `SetTrackGainCommand`, `SetTrackPanCommand`, `SetTrackMuteCommand`,
  `SetTrackSoloCommand` — each rejects missing and archived `TrackId`s
  without mutation via `find_active_track` (not `find_track`); gain and pan
  store every IEEE 754 `float` bit-pattern exactly (NaN, inf, subnormal,
  ±0 included); mute and solo store `bool` exactly; legal-range validation
  is deferred to M08 per the existing `AuditionMixSettings` contract.
  `SetTrackGainCommand` and `SetTrackPanCommand` include bitwise round-trip
  tests over 15 and 13 IEEE 754 cases respectively, and
  `SetTrackGainCommand` verifies unrelated fields (pan, mute) are unchanged
  after gain mutation. Node metadata commands: `SetNodeColorCommand` (packed
  `uint32_t` RGBA, six explicit color values verified exact including
  `0x00000000` and `0xFFFFFFFF`), `SetNodeNotesCommand` (freeform
  `std::string`, empty/long/UTF-8 round-trips, `set_notes` precondition
  pre-verified), `SetNodePositionCommand` (`GraphPosition` double-pair, nine
  IEEE 754 `double` bit-pattern pairs verified round-trip exact, unrelated
  fields unchanged). Deterministic replay evidence: all ten commands
  snapshot the old value on first successful execute, restore it on undo,
  and reapply the new value on redo; same-ID paths produce identical
  project state regardless of interleaving with other commands (verified
  via `SetNodeNotesCommand` + `SetNodeColorCommand` interleaved undo/redo
  against the same id). 64 new test cases (798 total, +64 over Phase 8a's
  734), debug and ASan/UBSan clean with 798/798 and zero findings.

  **Reviewer LOW advisory (accepted, not blocking):**
  `NodeCommandsSurviveReallocation` appends 200 nodes to a project and
  verifies that `SetNodeColorCommand`/`SetNodeNotesCommand` against the
  first inserted node still work. It relies on `std::vector` reallocation
  being _likely_ rather than capacity-proven; a move-constructed `Node`
  with a stable `NodeId` is well-defined but the test asserts a
  probabilistic guarantee. Accepted as test-strengthening only if useful;
  no counterexample has been observed with reasonable node counts.

  **Remaining categories explicitly unscoped for 8b:** plugin-chain commands,
  structural graph commands (add/remove node, add/remove track), node
  timeline commands, notation entry/delete commands, selection model,
  clipboard, and measure insert/delete — all reserved for
  Phase 8c and later. The eight original Phase 8 deliverable boxes in
  `02-domain-model.md` remain unchecked.

  **Orchestration must stop after this commit until Adam says continue.
  Do not begin Phase 8c.**
- **Phase 8c (`domain`) — fourteen reversible structural/config commands
  (approved, split 8c-i + 8c-ii):** the "reversible-today subset" — every
  graph/connector/track edit that is exactly undo/redo-able with stable ids
  using the domain mutators that already exist, wrapping-only, **no new
  domain API**. Same noexcept-snapshot-once shape as 8a/8b. **8c-i
  (`54543c1`)**, nine commands: `ArchiveTrackCommand`/`RestoreTrackCommand`
  (inverse pair, restore in place, no value snapshot), `SetOutputTypeCommand`
  (restores the shared `EventListener::bound_type()`; propagates the
  vertical/sequential clash rejection), `SetListenerPolicyCommand` (snapshots
  and restores both policy and capacity; execute fails `kInvalidArgument`
  when no listener exists), `SetOutputPriorityCommand`,
  `SetOutputWeightCommand` (propagates the negative-weight rejection),
  `SetOutputExportEnabledCommand`, `SetInputConnectorNameCommand`,
  `SetOutputConnectorNameCommand` (string commands guard allocation as
  `kOutOfMemory`). **8c-ii (`138458d`)**, five commands with genuine
  reversibility subtleties: `ConnectCommand` and `DisconnectCommand` — each
  snapshots the source output's `RouteGeometry` and restores it after any
  `disconnect`, because `OutputConnector::set_destination(nullopt)` (which
  `disconnect` triggers) also resets a customized route to automatic;
  `DisconnectCommand` also snapshots the exact `ConnectorDestination`;
  `BindOutputEventCommand` — snapshots the old event's listener existence +
  policy + capacity before binding, and on undo rebinds then restores that
  config via `set_listener_policy`, so a listener destroyed when the last
  bound output is rebound away is recreated exactly (**resolves the
  `TODO(Phase 8)` at `node.hpp:145`**, though the comment itself was left in
  place — a candidate for a later docs pass); `SetCustomRouteCommand`
  (propagates the axis-aligned/finite/no-zero-length waypoint validation);
  `ResetRouteCommand`. Both increments: reviewer independently ran all four
  gates green, verified snapshot-once discipline, the two hazard tests
  (route-preservation across undo; sole-listener destroy-and-restore to
  `kFifo`/capacity 5), and no scope creep. 112 new command test cases
  (896 total). **Two LOW non-blocking observations, accepted:** the
  anonymous-namespace `restore_route` helper is duplicated verbatim across
  four `.cpp` (internal linkage, no ODR issue; a domain-internal header
  could dedupe later); and there is no dedicated 8c-ii test for a wrapped
  call failing at undo/redo (saved dest input removed meanwhile) — the
  commands correctly propagate the failure without faulting the project, and
  the generic `CommandHistory` undo-failure tests exercise that mechanism.

  **Orchestration stopped after 8c per Adam's instruction to check in and
  update docs before proceeding to Phase 8d.**

## Plan for the remaining phases

- **Phase 6b — pickdown/MIDI-ownership lifecycle (done, split 6b-i + 6b-ii):**
  **6b-i** delivered `pickdown_coordinates` (pickdown meter/tempo coordinates
  in the source node's system, plus a structural `TempoLane::
  segment_index_at`), `pickdown_ownership` (`tied_note_spans` tie-chain
  resolution and `classify_note_ownership`/`classify_voice_ownership`
  splitting a note across the boundary into main and pickdown-tail
  ownership, with MIDI-only tail material as a semantic flag), and
  `pickdown_bound_oracle` (a finite upper bound on concurrent tails;
  cycles bounded by construction per Adam's ruling). Minimum main-region
  duration was verified already enforced on every path (`MeasureMap` has
  no mutator at all) and pinned by new tests. **6b-ii** delivered
  `midi_ownership`: `MidiOwnershipTracker` with per-(channel, pitch)
  newest-owner retrigger and explicit suppression of a superseded
  attack's later release, logical-OR CC64 ownership per (channel) across
  main material and tails, `transfer_main_to_pickdown_tail` for
  boundary-crossing notes/spans, and the full lifecycle set
  (`vertical_jump` releasing only source-main entries while tails survive,
  `clear_all` for stop/reset/node-play, `panic`,
  `retire_pickdown_tail_snapshot`) with pause modeled as no method at all.
- **Phase 6c — writer audition model (done):** a toolkit-independent
  writer audition model — one opaque instrument slot, zero or more opaque
  effect slots, plugin identity/state blobs, bypass/order, writer-only mix
  values — **no VST3 SDK types**.
- **Phase 7 — Normative playback specification (done, split 7a/7b/7c
  per Adam's scoping rulings below):**
  - **Locked scope rulings (Adam, interactively, before 7a started) — apply to
    7b/7c too:** (1) spec everything in header prose (no `docs/spec/`
    directory — none exists in this repo; the convention is normative prose
    inline in headers, matching `midi_ownership.hpp`); implement only what is
    self-contained today without a real consumer — tempo curve math (7a) and
    articulation/dynamic/grace → velocity+duration mapping (7b); leave
    same-sample MIDI emission ordering **spec-only** (7c) since its real
    consumer is the M04 scheduler and `MidiOwnershipTracker` has no timestamp
    parameter to hang ordering on yet. (2) New playback math belongs in
    `graphscore_core`, not `graphscore_domain` — mirrors the Phase 6a
    `DeterministicPrng` placement, since the ADR 0003 runtime closure sees
    core but never domain. (3) `double` is permitted, narrowly, inside
    tempo-curve integration/inversion only; every `Rational`-typed
    input/output at a function boundary stays exact; `Rational::to_double()`
    stays display-only everywhere else. (4) Concrete musical constants
    (tension/tolerance/rounding constants in 7a; velocity tables, duration
    ratios, steal fractions in 7b) are worker-proposed with cited rationale,
    reviewer-checked for internal consistency, Adam has final sign-off —
    not pre-specified by Adam up front. (5) `kSmooth` tempo curves are
    auto-shaped (Catmull-Rom-style) from neighboring `TempoPoint` data only —
    Adam explicitly rejected adding control-handle fields to `TempoPoint`,
    so this carries **no M03 persistence schema change**.
  - **7a — tempo curve math (done):** see the Phase 7a delivery note below.
  - **7b — articulation/dynamic/grace → velocity+duration mapping
    (done):** in `graphscore_core`, alongside `tempo_curve.hpp`/
    `tempo_point.hpp`; wired into `graphscore_domain`'s notation types
    (`Articulation`, `Dynamic`, `Hairpin`, `Slur`, `GraceGroup`). Resolves
    the `TODO(Phase 7)` seam at `notation_markings.hpp:112` (grace steal
    fraction/limits/division) and the untagged Phase-7 deferrals at
    `articulation.hpp:11` and `notation_markings.hpp:13,30,41,73`
    (dynamic/hairpin/slur/pedal → MIDI mapping presence-only today).
  - **7c — simultaneous MIDI ordering + note/CC64 ownership transitions,
    spec-only (done):** normative header-prose rules define atomic ownership
    resolution, deterministic source and stream orders, CC64 net transitions,
    and lifecycle/boundary integration on top of `MidiOwnershipTracker`.
    No code was added because the M04 scheduler is the real consumer.
- **Phase 8 — Command and selection model:**
  - **8a (done):** foundational non-throwing `Command` protocol,
    standalone `CommandHistory`, atomic `CommandTransaction` with best-effort
    rollback, three stable-ID proving commands. See delivery note above.
  - **8b (done):** ten reversible non-structural metadata/audition-mix
    commands (`SetProjectName`, `SetStartNode`, `SetProjectDynamic`,
    `SetTrackGain`/`Pan`/`Mute`/`Solo`, `SetNodeColor`/`Notes`/`Position`).
    See delivery note above.
  - **8c (done, split 8c-i `54543c1` + 8c-ii `138458d`):** the fourteen
    reversible-today structural/config commands (graph connections, connector
    config, route geometry, event binding, track archive/restore) that need
    no new domain API. See the delivery note above.
  - **8d (next — scoped with Adam, REQUIRES NEW DOMAIN API):** reversible
    **add/remove** of nodes, tracks, connectors, and events. These are NOT
    expressible as exactly-reversible commands ("undo/redo exactly / stable
    intended IDs", `02-domain-model.md` acceptance criteria) with today's
    domain layer, because `Project::add_node` mints a `NodeId` and there is
    **no `remove_node`**; tracks have only soft `archive_track` (reversed
    in-place by 8c) and **no hard remove**; `Node::add_input`/`add_output`
    **mint fresh `ConnectorId`s with no restore-with-id path**; and
    `EventRegistry::add_event` mints a fresh `EventId` with **no
    register-with-id** and its cascade cannot be rebound to the same id. So
    8d adds the minimal new domain API first, then the commands wrapping it.

    **Locked scoping decisions (Adam, interactively, before 8d-i):**
    1. **Reversibility mechanism = snapshot + restore-with-id** (not
       tombstone/soft-delete). *Add* commands mint the id on first execute,
       remember it, and redo re-inserts with the remembered id; undo removes
       by id. *Remove* commands snapshot the full removed entity **plus every
       piece of state their cascade clears**, remove, and reverse all of it
       on undo; redo removes again. Every new insert/restore-with-id domain
       primitive **must guard id-uniqueness** (reject a duplicate id) so the
       new surface cannot corrupt the model. Tombstoning was rejected as too
       invasive (every iterator/query/validation/persistence path would have
       to skip removed entities); track *archive* stays the one legitimate
       product-visible soft-remove, distinct from generic undo.
    2. **Tracks: `AddTrackCommand` + an undo-only hard remove.** The product's
       only user-facing track "removal" is archive (`06-graph-canvas.md:31`,
       "removed-track music remains archived and recoverable"), already
       reversible via 8c's Archive/Restore. But adding a track is a normal
       undoable edit whose undo must **fully erase** the just-created track,
       so 8d adds an undo-only `hard_remove_track` (erases the track and its
       lane in every node) used ONLY as `AddTrackCommand`'s inverse — never
       exposed as a user delete. No standalone `RemoveTrackCommand`.
    3. **One 8d, split into per-entity sub-increments, lowest-risk first**
       (each adds its domain primitive + command together, own
       worker+reviewer+commit; the CHECKLIST "Command and selection model"
       box is checked only when *all* of Phase 8's remaining deliverables —
       through selection/clipboard/measure ops — are done, not at the end of
       8d).

    **Per-entity increments:**
    - **8d-i — Connectors** (lowest risk; `Graph::remove_input` cross-node
      cascade already exists). Domain: `restore_input`/`restore_output`
      reinserting a full connector *value* preserving its id and every field
      (type/priority/weight/export/destination/route/event_binding),
      guarding dup id. Commands: `AddInputConnectorCommand`,
      `AddOutputConnectorCommand`, `RemoveOutputConnectorCommand`,
      `RemoveInputConnectorCommand`. Reversibility hazards to snapshot:
      `remove_output` can destroy the node's `EventListener` when it removes
      the last output bound to an event (same hazard as 8c
      `BindOutputEventCommand` → snapshot the listener policy/capacity);
      `Graph::remove_input` clears every *other* node's output that targeted
      the removed input → snapshot that (node, output) list and reconnect
      each on undo.
    - **8d-ii — Events** (`register-with-id` + existing `remove_event`
      cascade). Domain: `EventRegistry::add_event_with_id` guarding dup id
      **and** dup name. Commands: `RegisterEventCommand`,
      `RemoveEventCommand`. `RemoveEventCommand` snapshots the
      `EventDefinition` + every bound (node, output) + each affected node's
      listener policy/capacity, and reverses register + rebind + listener
      config on undo.
    - **8d-iii — Tracks** (`AddTrackCommand` + undo-only hard remove per
      decision 2). Domain: `add_track_with_id` (guard dup id + 64-active
      cap) and undo-only `hard_remove_track`. Command: `AddTrackCommand`
      only. Test that undo is safe after intervening lane edits — linear
      `CommandHistory` guarantees the added track's lanes are back to empty
      before its add can be undone; pin it.
    - **8d-iv — Nodes** (heaviest). Domain: `add_node_with_id`, a public
      `remove_node` (cascade: clear every other node's output targeting the
      removed node's inputs; clear `start_node` if it referenced the removed
      node — resolves the Phase-2 "dangling designated start-node" deferral),
      and `restore_node` reinserting the full `Node` value (it carries its
      own lanes). Commands: `AddNodeCommand`, `RemoveNodeCommand` (full
      Node-aggregate snapshot + the inbound-destination and start-node
      cascade snapshot). Ids are **preserved** on restore — the
      "deterministic connector remapping" of `06-graph-canvas.md:63` applies
      to copy/paste/duplicate, NOT delete, so no remapping here. Verify
      `Node` is cleanly copy/move-constructible before dispatch.

    **Per-increment gate:** the four standard gates + reversibility/cascade
    tests + a deterministic-replay test; a 64-track/64-measure practicality
    check on the track and node increments (acceptance criterion).
  - **8e..f (remaining, after 8d):** all selection kinds; toolkit-independent
    clipboard fragments; cut/copy/paste with identity remapping + rest
    normalization; boundary-crossing clip/reconnection rules; node copy/paste
    id remapping; atomic measure insert/delete (measure ops also need new
    `MeasureMap`/`NodeTimeline` mutators — none exist today, confirmed in the
    Phase 3 note). Notation and tempo edit commands beyond metadata likewise
    need the append/clear-only `VoiceContent` and whole-lane-replace
    `TempoLane` surfaces extended before fine-grained reversible commands are
    possible.
- **Phase 9 — Validation service:** fast incremental + complete validation;
  diagnostics with stable ids/severity/machine code/user text; validates
  rhythmic completeness, UUID uniqueness, references, track alignment, signature
  legality, graph edge integrity, event-name uniqueness, connector cardinality.
- **Finally:** verify all Acceptance Criteria + Test Focus boxes, check them and
  the top-level "Milestone 02 complete", update MILESTONES/status, summarize for
  Adam, and **stop** (do not start Milestone 03).

## Deferrals carried forward (do NOT treat these gaps as defects)

Recorded here so the owning phase picks them up:

- **→ Phase 6/7:** articulation/dynamic/hairpin/slur → MIDI velocity/duration/
  legato **mappings**, and CC64 emission, are NOT implemented in the notation
  model — only presence/structure. `TODO(Phase 7)` seams in
  `notation_markings.hpp`.
- **→ Phase 7:** grace-note **steal-time math** (fraction/limits/multi-note
  division/no-preceding-note behavior) — structure only in 4b; `TODO(Phase 7)`
  seam. Cubic smooth **tempo-curve math** (integration/inversion/tolerances/
  sample rounding) — `TempoLane` is structural only; `TODO(Phase 7)` seam in
  `tempo_lane.hpp`.
- **→ Phase 9:** **Deterministic ordering in the notation validator.**
  `notation_validation.cpp::validate_lane_references` iterates
  `TrackLane::stave_ids()` derived from a `std::unordered_map`, so cross-stave
  diagnostic order is non-deterministic even though the header promises an
  "ordered list" (within a stave it is deterministic). The general
  ValidationService in Phase 9 is expected to subsume this focused validator;
  make its output deterministic there (e.g. sort stave ids). No current test
  exercises >1 stave. (Reviewer LOW finding, 4b.)
- **→ Phase 9:** **Dangling designated start-node** is currently structurally
  impossible (no node-removal API exists yet) so it is unguarded; revalidate
  when node removal lands. (Reviewer LOW finding, Phase 2.)
- **→ Phase 6+:** the `EventStateMachine::clear_event` **caller obligation is
  unenforced**. `EventStateMachine` is deliberately not wired into
  `Node`/`Graph` mutation paths, so a caller who unbinds the last output for
  an event and forgets `clear_event` gets a stale occurrence resurrecting on
  rebind. Documented at `event_state_machine.hpp` ("Invariant: an
  EventListener removal must clear its queue"). Whoever wires the
  transport/edit layer should make the call automatic at the
  `Node::bind_output_event` / `remove_output` / graph-edit site rather than by
  convention.
- **Resolved in Phase 6a** (was: "→ Phase 6: the destination-eligibility rule
  is scoped to sequential only"): `assemble_vertical_candidates` now applies
  the `destination().has_value()` filter before candidates reach selection.
- **Adam's ruling (product decision, Phase 6a):** random weights are exact
  `Rational`, not whole percent — whole percent cannot express an exact
  three-way split (33/33/33 = 99 is rejected; 34/33/33 is silently biased).
  "Totals exactly 100 percent" in the `02-domain-model.md`/`README.md` plan
  prose means **the eligible group sums to exactly `Rational(1)`**.
- **→ docs pass (deferred):** `docs/plan/02-domain-model.md` and
  `docs/plan/README.md` still say "must total exactly 100 percent", and
  `weighted_selection.hpp` retains a "100-percent" section heading and
  cross-reference to that prose. This is internally consistent and
  documented today, but the prose should be reconciled with the `Rational`
  framing in a later docs pass.
- **→ open product question, NOT yet decided by Adam:** the 100%-total check
  is computed over the **eligible** group (destination-having outputs only),
  so a correct 50/50 branch node whose second branch is transiently
  disconnected mid-edit sums to 1/2 → invalid group → **playback stops**
  rather than the still-connected branch winning outright. Related: the
  default weight is `Rational(1)`, so any freshly created node with two
  connected sequential outputs sums to 2 and stops playback until weights are
  explicitly assigned. Both behaviors are documented in
  `weighted_selection.hpp` but deliberately left unchanged pending a product
  decision. Candidate for a Phase 9 ValidationService authoring diagnostic.
- **→ Phase 6b:** `assemble_vertical_candidates` applies the
  `destination().has_value()` filter, but `resolve_vertical_match`/
  `resolve_vertical_transition` take a **pre-assembled** candidate set — any
  future vertical candidate assembly path must apply the same filter, or a
  destination-less connector could win an unfollowable jump.
- **→ Phase 7 / runtime:** `select_winner`, `resolve_vertical_match`, and
  `resolve_vertical_transition` still take `const std::vector<ArbitrationCandidate>&`,
  and `assemble_vertical_candidates` returns a `std::vector`;
  `weighted_selection.hpp` already uses `std::span`. These should migrate to
  `std::span` plus caller-supplied storage before the runtime closure
  consumes them. Also `compute_group` allocates a `std::vector` per call and
  is called twice per boundary — fine for the writer-side domain layer, not
  for the realtime `process` path.
- **Phase 6c (`graphscore_domain`) — writer audition model:** `PluginIdentity`
  (toolkit-independent opaque plugin type identifier: human-readable name,
  format string, 16-byte uid — no VST3 SDK types), `PluginStateBlob`
  (opaque `std::vector<std::uint8_t>` wrapper for serialised processor
  state), `EffectSlot` (identity + state + bypass flag, ordered by position
  in the chain vector), `TrackPluginChain` (0-or-1 instrument slot + ordered
  effect slots per track; `set_instrument`/`clear_instrument`,
  `add_effect`/`insert_effect`/`remove_effect`/`move_effect`,
  `set_effect_bypass`/`set_effect_state` with documented index preconditions,
  default equality), and `AuditionMixSettings` (gain/pan/mute/solo with
  defaults; gain/pan stored as-is, legal ranges deferred to the M08 mixer).
  Integrated as accessor-only private members on `Track` with a non-const
  `Project::find_active_track` mirroring `find_node`. Archive/restore
  preserves the full chain and mix. 49 test cases covering identity
  equality/comparison, blob storage, effect slot bypass/state, chain
  mutators, move semantics both directions, instrument/effect independence,
  copy independence, mix defaults/setters/equality, track integration,
  archive/restore round-trips, and multi-track isolation.
- **→ Phase 7 / transport:** `TransportInstant` currently lives in
  `event_state_machine.hpp` though documented as project-wide; consider
  relocating it to `graphscore/core/` when a real transport lands. Note it
  has `operator==` and a hand-written `operator<=` only (no `<`/`>`), and its
  unit is deliberately unspecified since only ordering/equality are compared.
- **→ noted:** `weighted_selection.cpp` uses `__builtin_mul_overflow`/
  `__builtin_add_overflow`, a Clang/AppleClang/clang-cl extension (the
  project hard-fails configure on non-Clang, so this is safe today);
  `<stdckdint.h>`'s `ckd_add`/`ckd_mul` is the portable C23 successor.
- **→ Milestone 03 (persistence):** **connector order is now semantically
  load-bearing.** Tier-3 arbitration depends on `Node::outputs()` insertion
  order, which was previously only cosmetic. Persistence must preserve and
  round-trip `outputs()` order exactly — a serializer that writes connectors
  from a map or re-sorts by UUID would silently change playback. Warrants a
  round-trip test in M03.
- **→ Phase 9:** ValidationService should flag a **zero-capacity FIFO
  listener** (reachable only via the raw `EventListener::set_capacity`;
  `Node::set_listener_policy` rejects it), which otherwise silently swallows
  every occurrence.
- **→ later:** **orphan queue growth** — `EventStateMachine::queues_` entries
  for removed nodes/events are never reclaimed (clears empty in place by
  design). Harmless for correctness since ids are UUIDs and can never be
  re-matched, but a long editing session grows the map monotonically; a
  `prune(const Graph&)` / `erase_node(NodeId)` would address it.
- **Adam's ruling (product decision, Phase 6b-i):** a pickdown-bearing node
  sitting on a cycle is legal and bounded, not an export failure. Verbatim:
  "Allow. In that scenario the pickdown measure still plays on every loop by
  overlapping with the start of the loop." The oracle no longer performs
  reachability-from-self graph analysis: because a node has at least one
  complete main-region measure and a pickdown is always strictly shorter
  than one complete measure (`README.md`, `02-domain-model.md`;
  `NodeTimeline::set_pickdown`), the musical time a cycle must traverse
  before revisiting a pickdown-bearing node is never shorter than the
  previous tail, so concurrency per pickdown-bearing node is at most 1
  whenever tempo is comparable across the cycle. Note the proof is
  **not** "the node replays a whole measure on every revisit" — a vertical
  jump can re-enter a node close to its own boundary. It is that vertical
  jumps *telescope*: `vertical_regions_compatible` forces identical
  main-region lengths and makes `map_vertical_position` the identity, so a
  jump skips exactly the distance already played, consuming no musical
  time or position; only sequential transitions reset position, always to
  0. Summing the runs between sequential transitions, elapsed position
  between two boundary crossings of a node is at least its full
  main-region length. (Reviewer-supplied correction, 6b-i: the original
  "replays one complete measure" argument was refuted by an A→B→vertical→A
  counterexample re-entering A at 15/16 of its only measure.) `prove_pickdown_overlap_bound` now
  always returns `kBounded` for any structural input, with the bound equal
  to the count of pickdown-bearing nodes; `PickdownBoundStatus::kUnbounded`
  is retained on the enum, unreachable today, as the reserved verdict for
  Phase 7's timing analysis (see the Phase 7 entry immediately below).
- **→ Phase 7:** bounding concurrent pickdown tails under **tempo disparity
  around a cycle** is the one case the Phase 6b-i structural proof above
  does not close — a tail on a slow source tempo curve could in principle
  still be sounding when a much faster route back around the cycle (through
  another node's own tempo curve, or a vertical jump landing close to the
  node's own boundary) brings playback back around for a second tail before
  the first tail's real elapsed time has caught up. Proving or bounding
  that requires integrating real time over the cubic smooth-tempo curve
  (`tempo_lane.hpp`'s `TODO(Phase 7)`); this is where
  `PickdownBoundStatus::kUnbounded` becomes reachable, if it ever is.
- **Adam's rulings (product decisions, Phase 6b-ii):** (1) **panic is
  global** — verbatim: "Since panic refers to the audio processing itself,
  which is only in one place, and by extension the transport, this is
  global. It clears audio processing, stops it, stops the transport." So
  `MidiOwnershipTracker::panic()` releases all note and pedal ownership
  project-wide including pickdown tails; stopping the transport and
  clearing `EventStateMachine` state are the caller's obligations, outside
  that class. (2) **Snapshot retirement** = retirement of a pickdown-tail
  snapshot when it completes naturally, releasing whatever ownership it
  still holds — the ordinary end of a tail's life, contrasted with panic,
  which cuts it early.
- **Ruling (reviewer-settled, Phase 6b-ii): pedal spans ARE released on a
  vertical jump**, not just notes. `README.md:110` names only notes, but a
  source-main pedal span left down across a jump has **no reachable
  release event** — its notated end lies in a node that is no longer
  playing — so under `README.md:79` ("pedal-up emits only after the last
  active logical span releases") CC64 would stay at 127 for the remainder
  of playback, blurring the destination node and everything after it.
  Cutting at the jump costs only a pedal shorter than notated, exactly the
  cost `README.md:110` already accepts for notes. Tail pedal spans remain
  active per `README.md:76`.
- **→ Phase 7 / transport wiring:** `MidiOwnershipTracker` trusts
  caller-supplied `MidiOwnerScope` correctness (it has no `Project`/`Graph`
  access, so it cannot verify that `MidiOwnerScope::main(node)` is only
  attached while `node` is genuinely active), and
  `transfer_main_to_pickdown_tail` is a **caller obligation** that must be
  invoked once at each sequential boundary, paired with
  `begin_pickdown_tail`. A wrong scope or a missed transfer produces a
  stuck note or a wrongly cut tail with **no diagnostic**. Same shape as
  the `EventStateMachine::clear_event` caller-obligation entry above, and
  the same Phase 7 transport-wiring site should make both automatic.
  `MidiOwnershipTracker` is deliberately a standalone state machine
  alongside `EventStateMachine`, not wired into it.
- **→ later (advisory, 6b-ii):** the `ReleaseFilter` enum refactor of
  `MidiOwnershipTracker::release_where` (its `(bool, optional, optional)`
  signature admits only three legal combinations) was judged coherent
  as-is; revisit if a fourth filter kind appears. `panic()` and
  `clear_all()` have byte-identical bodies — kept as separate named entry
  points so Phase 7 can give panic a diagnostic flag or forced snapshot
  drop without an API break.
- **→ Phase 7 / runtime (performance, advisory only):**
  `TempoLane::segment_index_at` (`src/domain/tempo_lane.cpp:38-44`) is an
  O(n) scan over strictly-ordered points where `std::upper_bound` would be
  O(log n); `Project::find_node` is linear, making the oracle O(pickdown_nodes
  × V × E); `pickdown_ownership.cpp:57` heap-allocates a fresh `next_active`
  per sounding column and `classify_voice_ownership` (:90-93) does not
  `reserve()`. All fine at 64-node/64-measure scale in a non-realtime domain
  layer. Same profile as the `compute_group`/`select_winner` entry already in
  that list. `MidiOwnershipTracker` (6b-ii) is allocation-light — a fixed
  16-slot array for per-channel pedal counts — but still uses
  `unordered_map` for note/pedal ownership; same profile as `EventQueue`.
- **→ Phase 7 / runtime (performance, advisory only, 7a):**
  `tempo_curve.cpp`'s `integrate_elapsed_seconds` re-runs a linear
  `locate_segment` scan on every internal loop iteration, and
  `invert_elapsed_seconds`'s fixed 52-iteration bisection calls it again
  each step, giving `O(52·n²)` per inversion; each `smooth_rate` evaluation
  re-normalizes up to four `Rational`→`double` BPM conversions per call with
  no caching. Fine at authoring scale in `graphscore_core` today, but this
  code sits inside the ADR 0003 runtime closure and is therefore subject to
  the realtime "no unbounded work on `process`" rule once something calls
  it from that path — wants an advancing index / `std::upper_bound` and
  per-segment rate memoization before then. Same profile as the existing
  `compute_group`/`segment_index_at` entries above.
- **→ later (advisory only, 7a):** `tempo_rate_at`'s doc comment doesn't
  warn that a `kSmooth` segment's returned rate can overshoot the interval
  `[min(r_i, r_(i+1)), max(...)]` (the header documents the overshoot
  property elsewhere but not on this specific function) — a caller taking
  the reciprocal without a floor could hit a very small divisor; add one
  sentence if a Phase 7-adjacent consumer surfaces this.
  `quantize_position` (`tempo_curve.cpp`) has no explicit magnitude guard of
  its own (`std::llround` is UB above ~8.8e12) — unreachable at realistic
  node/position scale and covered by the existing Phase 1
  accepted-as-designed `Rational` overflow entry below, not a new risk.
  `tempo_curve.cpp` obtains `Tempo`/`NoteValue` transitively through
  `tempo_point.hpp` rather than including `tempo.hpp` directly — harmless
  today, worth tightening to include-what-you-use if the file is touched
  again.
- **→ Milestone 03 (persistence, 7b):** `GraceGroup` has no cardinality
  validation anywhere in the model (`make_grace_group` accepts any
  `notes.size()`). `playback_mapping.hpp`'s `kMaxGraceNotesPerGroup = 16`
  bound keeps the core math UB-free for any size, but a caller that sums
  `grace_steal_durations`' returned per-note vector itself (rather than
  using the closed-form `grace_steal_remaining_duration`) can still
  overflow `Rational` around `note_count` ~1e5 — reviewer-verified, not
  reachable through any code this milestone ships, but real once M03 loads
  `GraceGroup` off disk. The persistence layer should validate group size
  at load time rather than relying on every future caller preferring the
  closed-form function.
- **→ later (advisory only, 7b):** `grace_steal_durations` still returns
  a heap-allocated `std::vector<Rational>` — same "wants `std::span` plus a
  caller-supplied fixed buffer before the realtime path uses it" profile
  already on this list for `compute_group`/`select_winner`/tempo-curve
  integration. `velocity_for_dynamic` indexes an 8-element table with an
  unchecked `operator[]` on the raw enum value rather than a `switch`
  (`sounded_duration_for_articulation`, two functions below it in the same
  file, uses a `switch` and is immune) — harmless today since `Dynamic` has
  no invalid-construction path, but inconsistent within the file; worth
  matching if that function is touched again.

Accepted-as-designed (no action needed): beam-override contiguity check ignores
list order (4b #2); `Node::lane_count()` counts archived lanes too (Phase 2);
`Rational` cross-multiplication can overflow only for non-musical extreme
operands (Phase 1); `select_winner` takes `const std::vector&` and
`resolve_sequential_boundary` heap-allocates a candidate vector per boundary —
Phase 7's realtime path will want `std::span` plus a caller-supplied fixed
buffer; `EventQueue` is a bounded FIFO using `erase(begin())`, documented as
satisfying the capacity invariant a realtime preallocated ring buffer would
implement (Phase 5).

## Environment quirks (every worker/reviewer brief should mention these)

- **cpplint** is not on the default PATH; it lives at
  `/Users/adamshield/Library/Python/3.9/bin`. Prepend it before
  `cmake --preset debug` so `find_program` locates it, then run the lint target.
  The **pre-commit hook prints "cpplint not found, skipping"** and lets the
  commit through — that is an environment skip, not a lint pass; workers verify
  cpplint manually and reviewers run the lint gate with the PATH fix.
- **clang-format** binary: `/Library/Developer/CommandLineTools/usr/bin/clang-format`
  (run `-i` on touched files if the lint target flags formatting).
- **IDE/clangd diagnostics** on new headers ("`graphscore_core.hpp` not found",
  "`std::optional` not found", "`<=>` is a C++20 extension") are **false
  positives** — the editor analyzes headers in isolation without `-std=c++23`
  and `-I include`. Judge only by the clean `cmake` build. Every header is kept
  self-contained (includes what it uses); verify that, ignore the IDE noise.
- `ld: warning: ignoring duplicate libraries` on the writer app / plugin scanner
  is a **pre-existing linker note, not a compiler warning-as-error** — harmless.

## Conventions (locked, match exactly)

Flat `namespace graphscore` (no nested namespaces). Files snake_case; types
PascalCase; methods/members snake_case with trailing `_` on private members;
constants `kPascalCase`; `enum class` with `k`-prefixed enumerators. Every file
starts `// SPDX-License-Identifier: Apache-2.0`; headers use `#pragma once`.
Umbrella headers `graphscore_core.hpp` / `graphscore_domain.hpp` re-`#include`
each new public header. New `.cpp` go in the target's `CMakeLists.txt` source
list; new tests in `tests/<target>/CMakeLists.txt`. Validated value types use
`static std::optional<T> create(...)` with a private constructor — no silent
invalid construction. Musical positions/durations are exact `Rational` in
whole-note units, never floating point. Pure value types → `graphscore_core`;
notation/graph/model structures → `graphscore_domain`. `.clang-format` is
Google-based, 80 columns, 2-space indent, sorted includes.
