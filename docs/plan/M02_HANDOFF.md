# Milestone 02 (Domain And Command Model) — Orchestration Handoff

**Maintenance rule:** this is an active handoff, not a changelog. After every
completed phase, remove or compress completed detail so the file gets SMALLER,
not larger. Git history preserves old detail. It should contain only what the
next agent needs for the next task plus active downstream deferrals.

## Status: PHASE 8h-i COMPLETE, COMMITTED, REVIEWER-APPROVED

Phases 1–8h-i are complete and committed; **8h-ii is next**. 8h-i (measure/clef
mutation primitives, 23 new cases) is reviewer-approved after two documentation
fix rounds; its production logic was correct from the first candidate. Tier 3
passed pre-fix (1529/1529, lint, seven audits, clang-tidy 18, ASan/UBSan); final
tree is 1531/1531 with lint and build clean, the sanitizer cycle not repeated for
a comment-only delta per AGENTS.md.

**The four-way 8h split and every load-bearing fact for 8h-ii/iii/iv live in
`02-domain-model.md` under the measure-operations box. Read it before 8h-ii.**

## Progress

| # | Section | State |
|---|---|---|
| 1–8h-i | Domain model through measure/clef mutation primitives | ✅ complete |
| 8h-ii | Key-signature, clef, and pickdown commands | ⬜ next |
| 8h-iii | Measure insert/delete + time-signature, atomic cascade | ⬜ |
| 8h-iv | Paste applies copied clef/signature context | ⬜ |
| 9 | Validation service | ⬜ |

The CHECKLIST.md “Command and selection model” box remains unchecked until 8h
finishes. Validation, Acceptance Criteria, Test Focus, and the top-level
Milestone 02 box also remain unchecked.

## Load-bearing facts

- **8h-ii's `NodeTimeline` measure-removal entry point must route through
  `MeasureMap::remove_measure`** to inherit its non-empty guard, never reaching
  into `measures_`. Two live sites compute `measure_count() - 1` on an unsigned
  `std::size_t` and underflow on an empty map. See `pickdown_coordinates.hpp`.
- Any edit changing a measure's *length* (insert, delete, time-signature change)
  must cascade into the tempo lane, pickdown duration, and per-voice
  normalization; a key-signature change must not. `MeasureMap` has no pickdown or
  tempo knowledge and does neither itself — the caller owns that.
- Clipboard rules R1–R12 live in `notation_fragment.hpp`. Map staves by
  `(track_ordinal, stave_ordinal, voice)`, never unordered `stave_ids()`. Pitch
  is clef-independent: copy `SpelledPitch` verbatim, never apply
  `clef_at_origin`. Identity is regenerated on every copy/paste/duplicate even
  when the source is already id-disjoint.
- `NodeTimeline`, `MeasureMap`, `ClefLane`, `TempoLane` carry **no** ids —
  8g-iii copies timelines verbatim on that basis. Do not add one.
- `Project::remove_node` cascades into *other* nodes' destinations and routes;
  a multi-node command must repair that cascade on rollback.
- Reversibility pattern throughout 8e–8h: whole-container snapshot (copy the
  `VoiceContent`/`TrackLane`/`Node`, restore on undo), not per-edit inverses.

## Remaining roadmap

- **8h-ii, 8h-iii, 8h-iv** — scope and load-bearing facts in `02-domain-model.md`.
- **Phase 9 — Validation service:** incremental and complete validation with
  stable ids, severity, machine-readable codes, text, deterministic diagnostics.
- Then verify Acceptance Criteria and Test Focus, update the remaining M02 boxes,
  and stop; do not start M03.

## Active downstream deferrals

- **M06:** route-segment selection deferred — drawn geometry belongs to the
  canvas; the selection deliverable stays unchecked.
- **M03:** persistence must preserve connector insertion order and round-trip
  `ChordNote::id` and `GraceNote::id`; changing or regenerating them breaks
  arbitration, selection, and clipboard remapping.
- **Phase 9:** sort stave ids for deterministic validator diagnostics; enforce
  `EventStateMachine::clear_event` when the last output is unbound.
- **Later:** orphan event queues unreclaimed; zero-capacity FIFO listeners
  swallow occurrences silently.
- **8g-iii (open):** `DuplicateNodesCommand`'s stale-context pre-check (undo
  after a duplicate vanished) is reachable without fault injection but untested.

## Environment quirks

- **Testability limit (8g-iii):** no fault-injection allocator exists and tests
  see only `include/`, so OOM and `kFaulted` branches of leaf commands have no
  seam and are inspection-verified. Design rollbacks fail-safe so a residual bug
  latches `kFaulted` rather than under-reporting.
- Clang-tidy 18 is a pre-commit gate and uses the warm `build/tidy` tree. On
  macOS it needs the SDK sysroot flags documented in `AGENTS.md`.
- cpplint is at `/Users/adamshield/Library/Python/3.9/bin`; clang-format is at
  `/Library/Developer/CommandLineTools/usr/bin/clang-format`.
- Watch for out-of-band commits and verify HEAD before committing.
- Any `bind_output_event` call on a noexcept command path must be wrapped for
  allocation safety.
