# Milestone 02 (Domain And Command Model) — Orchestration Handoff

**Status as of this doc:** Phases 1–5 complete and committed. Phase 6
(Adaptive playback semantics) is **in progress**, split into 6a/6b/6c; 6a is
complete and committed. This file lets a different orchestrator pick up
mid-milestone without re-deriving the plan.
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
| 7 | Normative playback specification | ⬜ remaining | — |
| 8 | Command and selection model | ⬜ remaining | — |
| 9 | Validation service | ⬜ remaining | — |
| — | Acceptance criteria + Test focus | ⬜ remaining (final boxes) | — |

Test suite currently: **581 tests, 100% pass** after Phase 6c.

CHECKLIST.md M02 boxes checked so far: Dependencies, Identity and value types,
Project and track model, Node timeline, Notation model, Graph model, Adaptive
playback semantics. Remaining M02 boxes: Normative playback specification,
Command and selection model, Validation service, Acceptance criteria, Test
focus, and the top-level "Milestone 02 complete".

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
- **Phase 6c — writer audition model (remaining):** a toolkit-independent
  writer audition model — one opaque instrument slot, zero or more opaque
  effect slots, plugin identity/state blobs, bypass/order, writer-only mix
  values — **no VST3 SDK types**.
- **Phase 7 — Normative playback specification:** cubic smooth-tempo curve
  equations, legal control handles, deterministic integration/inversion
  tolerances, integer sample rounding; articulation mappings + precedence with
  slurs/ties/dynamics/hairpins; grace-group steal fraction/limits/division;
  simultaneous MIDI ordering + note/CC64 ownership transitions. (This is where
  the deferred playback math below is finally specified.)
- **Phase 8 — Command and selection model:** reversible commands for every edit,
  transaction grouping, all selection kinds, toolkit-independent clipboard
  fragments, cut/copy/paste with identity remapping + rest normalization,
  boundary-crossing clip/reconnection rules, node copy/paste id remapping,
  atomic measure insert/delete.
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
