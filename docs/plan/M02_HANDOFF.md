# Milestone 02 (Domain And Command Model) — Orchestration Handoff

**Maintenance rule:** this is an active handoff, not a changelog. After every
completed phase, remove or compress completed detail so the file gets SMALLER,
not larger. Git history preserves old detail. It should contain only what the
next agent needs for the next task plus active downstream deferrals.

## Status: PHASE 8h-ii COMPLETE, REVIEWER-APPROVED

Phases 1–8h-ii are complete; **8h-iii is next**. The 8h-ii exact tree passed a
clean debug build, 1552/1552 CTest, lint over 318 files, all seven architecture
audits, clang-tidy 18, and ASan/UBSan 1552/1552. TSan is not applicable.

**The four-way 8h split and full load-bearing facts for 8h-iii/iv live in
`02-domain-model.md` under the measure-operations box. Read it before 8h-iii.**

## Progress

| # | Section | State |
|---|---|---|
| 1–8h-ii | Domain model through non-length-changing timeline commands | ✅ complete |
| 8h-iii | Measure insert/delete + time-signature, atomic cascade | ⬜ next |
| 8h-iv | Paste applies copied clef/signature context | ⬜ |
| 9 | Validation service | ⬜ |

The CHECKLIST.md “Command and selection model” box remains unchecked until 8h
finishes. Validation, Acceptance Criteria, Test Focus, and the top-level
Milestone 02 box also remain unchecked.

## Load-bearing facts

- **8h-iii's eventual `NodeTimeline` measure-removal entry point must delegate to
  `MeasureMap::remove_measure`** to inherit its non-empty guard, never reaching
  into `measures_`. Two live sites compute `measure_count() - 1` on an unsigned
  `std::size_t` and underflow on an empty map. See `pickdown_coordinates.hpp`.
- Any edit changing a measure's *length* (insert, delete, time-signature change)
  must atomically cascade through every active and archived track lane, the tempo
  lane, pickdown, and per-voice normalization. A key-signature change does not.
- Only pedal spans, clef changes, and tempo points carry absolute `Rational`
  positions and need shifting; id-referenced markings and ties survive. Rebuild
  each voice in three regions because `VoiceContent::insert_event` does not shift
  later material. Position-zero tempo cannot move. Shift clefs right-to-left when
  moving right and left-to-right when moving left, or restore a whole-lane
  snapshot. Raw-index measure selections are revalidated, not auto-updated;
  compatibility diagnostics are a live query, not stored state.
- Clipboard rules R1–R12 live in `notation_fragment.hpp`. Map staves by
  `(track_ordinal, stave_ordinal, voice)`, never unordered `stave_ids()`. Pitch
  is clef-independent: copy `SpelledPitch` verbatim, never apply
  `clef_at_origin`. Identity is regenerated on every copy/paste/duplicate even
  when the source is already id-disjoint.
- `NodeTimeline`, `MeasureMap`, `ClefLane`, `TempoLane` carry **no** ids —
  8g-iii copies timelines verbatim on that basis. Do not add one.
- Whole-container snapshots, not per-edit inverses, remain the 8e–8h pattern.

## Remaining roadmap

- **8h-iii, 8h-iv** — scope and load-bearing facts in `02-domain-model.md`.
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
