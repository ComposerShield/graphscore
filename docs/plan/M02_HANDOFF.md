# Milestone 02 (Domain And Command Model) — Orchestration Handoff

**Maintenance rule:** this is an active handoff, not a changelog. After every
completed phase, remove or compress completed detail so the file gets SMALLER,
not larger. Git history preserves old detail. It should contain only what the
next agent needs for the next task plus active downstream deferrals.

## Status: PHASE 8g-ii COMPLETE, COMMITTED, REVIEWER-APPROVED

Phases 1–8g-ii are complete and committed. Phase 8g-ii (paste + cut commands)
is complete and reviewer-approved; its implementation and phase documentation
were committed together. Consult git history for the commit identity if needed.
Phase 8g-iii is next.

Final exact-tree Tier 3 passed: warning-free debug build; 1481/1481 debug
tests; lint; all seven architecture audits; canonical clang-tidy 18; and
ASan/UBSan build plus 1481/1481 tests. TSan is not applicable because this
phase adds no concurrent behavior. The phase adds 78 `ClipboardCommand` tests.

## Progress

| # | Section | State |
|---|---|---|
| 1–8g-ii | Domain model through clipboard cut + paste commands | ✅ complete |
| 8g-iii | Node copy/paste id remapping | ⬜ next |
| 8h | Measure insert/delete + node-timeline edit commands | ⬜ |
| 9 | Validation service | ⬜ |

The CHECKLIST.md “Command and selection model” box remains unchecked until
8g-iii and 8h finish. Validation, Acceptance Criteria, Test Focus, and the
top-level Milestone 02 box also remain unchecked.

## Load-bearing clipboard facts

- `NotationFragment` is a validated value type; `extract_fragment` is a pure
  copy query over full-measure and arbitrary-range selections. Copy-side rules
  R1–R12 are documented in `notation_fragment.hpp`.
- `PasteFragmentCommand` and `CutFragmentCommand` perform complete-span
  replacement, including genuinely empty/no-stave destinations. They preserve
  exact undo/redo, no-throw allocation/failure atomicity, and lifecycle/stale
  atomicity across all affected lanes.
- Every paste remaps event, note, grace-note, and marking identities again,
  even though fragment identities are already source-disjoint.
- Parts use `(track_ordinal, stave_ordinal, voice)` and map only referenced
  coordinates; never use unordered `TrackLane::stave_ids()` for mapping.
- Boundary handling is deterministic and directional: crossing events and
  markings are clipped, outgoing ties are severed where required, and pedal
  spans remain TrackLane/stave-scoped.
- Pitch is clef-independent. Paste copies `SpelledPitch` verbatim and must not
  adjust pitch, octave, or accidental or apply `clef_at_origin`.
- `MeasureMap` and `ClefLane` mutation is deferred to 8h. Paste therefore does
  not apply copied interior clef/key/time changes or rewrite destination
  signatures. The broader clipping/reconnection plan box stays unchecked
  until that work lands.

## Remaining roadmap

- **8g-iii — Node copy/paste:** duplicate a node with fresh ids for the node,
  its connectors, lanes, and every notation entity; remap intra-selection
  connector edges and drop edges leaving the selection. Never duplicate a
  stable UUID.
- **8h — Measure insert/delete + node-timeline edit commands:** add the domain
  mutation needed for an atomic cascade across every lane. This phase also owns
  per-measure time/key changes, clef changes (including copied interior clef
  application), and pickdown set/clear.
- **Phase 9 — Validation service:** incremental and complete validation with
  stable ids, severity, machine-readable codes, text, and deterministic
  diagnostics.
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

## Environment quirks

- Clang-tidy 18 is a pre-commit gate and uses the warm `build/tidy` tree. On
  macOS it needs the SDK sysroot flags documented in `AGENTS.md`.
- cpplint is at `/Users/adamshield/Library/Python/3.9/bin`; clang-format is at
  `/Library/Developer/CommandLineTools/usr/bin/clang-format`.
- Watch for out-of-band commits and verify HEAD before committing.
- Any `bind_output_event` call on a noexcept command path must be wrapped for
  allocation safety.
