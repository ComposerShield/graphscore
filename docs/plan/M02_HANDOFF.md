# Milestone 02 (Domain And Command Model) — Orchestration Handoff

**Maintenance rule:** this is an active handoff, not a changelog. After every
completed phase, remove or compress completed detail so the file gets SMALLER,
not larger. Git history preserves old detail. It should contain only what the
next agent needs for the next task plus active downstream deferrals.

## Status: PHASE 8g-iii COMPLETE, COMMITTED, REVIEWER-APPROVED

Phases 1–8g-iii are complete and committed; 8h is next. 8g-iii
(`DuplicateNodesCommand`, 25 new cases) is reviewer-approved. Final exact-tree
Tier 3 passed: warning-free debug build, 1506/1506 tests, lint, all seven
audits, clang-tidy 18, and ASan/UBSan (1506/1506). TSan N/A — no concurrency.

## Progress

| # | Section | State |
|---|---|---|
| 1–8g-iii | Domain model through clipboard and node duplication | ✅ complete |
| 8h | Measure insert/delete + node-timeline edit commands | ⬜ next |
| 9 | Validation service | ⬜ |

The CHECKLIST.md “Command and selection model” box remains unchecked until 8h
finishes. Validation, Acceptance Criteria, Test Focus, and the top-level
Milestone 02 box also remain unchecked.

## Load-bearing facts for 8h

- **`MeasureMap` and `ClefLane` have no mutators — 8h must add them.** Hence
  paste applies no copied interior clef/key/time change and rewrites no
  destination signature; the clipping/reconnection box stays unchecked till 8h.
- Clipboard rules R1–R12 are documented in `notation_fragment.hpp`. Map staves by
  `(track_ordinal, stave_ordinal, voice)`, never by unordered
  `TrackLane::stave_ids()`. Pitch is clef-independent: copy `SpelledPitch`
  verbatim, never apply `clef_at_origin`.
- Identity is regenerated on every copy/paste/duplicate even when the source is
  already id-disjoint, so one fragment pasted twice cannot duplicate a UUID.
- `NodeTimeline`, `MeasureMap`, `ClefLane`, and `TempoLane` carry **no** ids —
  8g-iii relies on this to copy a timeline verbatim. If 8h adds an id-bearing
  measure or clef entity, `DuplicateNodesCommand` must regenerate it too.
- `Project::remove_node` cascades into *other* nodes' output destinations and
  resets their routes. Any command removing more than one node must repair that
  cascade on rollback, not just re-add its own nodes.
- Reversibility pattern throughout 8e–8g: whole-container snapshot (copy the
  `VoiceContent`/`TrackLane`/`Node`, restore on undo), not per-edit inverses.

## Remaining roadmap

- **8h — Measure insert/delete + node-timeline edit commands:** add the domain
  mutation needed for an atomic cascade across every lane. This phase also owns
  per-measure time/key changes, clef changes (including copied interior clef
  application), and pickdown set/clear.
- **Phase 9 — Validation service:** incremental and complete validation with
  stable ids, severity, machine-readable codes, text, deterministic diagnostics.
- Finally verify Acceptance Criteria and Test Focus, update the remaining M02
  boxes, and stop; do not start M03.

## Active downstream deferrals

- **M06:** route-segment selection remains deferred because drawn route
  geometry belongs to the canvas. The selection deliverable stays unchecked.
- **M03:** persistence must preserve connector insertion order and round-trip
  `ChordNote::id` and `GraceNote::id`; changing or regenerating them breaks
  arbitration, selection, and clipboard remapping.
- **Phase 9:** sort stave ids for deterministic notation-validator diagnostics;
  enforce `EventStateMachine::clear_event` when the last output is unbound.
- **Later advisory:** orphan event queues are not reclaimed, and zero-capacity
  FIFO listeners silently swallow occurrences.
- **8g-iii advisory (open):** `DuplicateNodesCommand`'s stale-context pre-check
  (undo after a duplicate was removed behind the command's back) is reachable
  without fault injection but untested. ~10 lines; take it when next in the file.

## Testability limits (learned in 8g-iii)

No fault-injection allocator exists, and tests see only `include/` — never
`src/domain/*.hpp`. So OOM and `kFaulted` branches of leaf commands talking
directly to `Project` have no substitutable seam (unlike `CommandTransaction`,
which mocks sub-`Command`s) and are verifiable by inspection only. Design such
rollbacks fail-safe by construction, so a residual bug latches `kFaulted`
(visible) rather than silently under-reporting. To actually cover one, lift the
repair body into a named operation and drive it by damaging the project first.

## Environment quirks

- Clang-tidy 18 is a pre-commit gate and uses the warm `build/tidy` tree. On
  macOS it needs the SDK sysroot flags documented in `AGENTS.md`.
- cpplint is at `/Users/adamshield/Library/Python/3.9/bin`; clang-format is at
  `/Library/Developer/CommandLineTools/usr/bin/clang-format`.
- Watch for out-of-band commits and verify HEAD before committing.
- Any `bind_output_event` call on a noexcept command path must be wrapped for
  allocation safety.
