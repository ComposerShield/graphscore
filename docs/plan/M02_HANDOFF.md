# Milestone 02 (Domain And Command Model) — Orchestration Handoff

**Maintenance rule:** this is an active handoff, not a changelog. After every
completed phase, remove or compress completed detail so the file gets SMALLER,
not larger. Git history preserves old detail. It should contain only what the
next agent needs for the next task plus active downstream deferrals.

## Status: PHASE 8h-iii COMPLETE, REVIEWER-APPROVED

Phases 1–8h-iii are complete; **8h-iv is next**. The 8h-iii exact tree passed
1599/1599 debug tests, lint over 326 files, all seven architecture audits,
canonical clang-tidy 18, and ASan/UBSan 1599/1599. TSan is not applicable.

The four-way 8h split and the open 8h-iv decision live in
`02-domain-model.md` under the measure-operations box.

## Progress

| # | Section | State |
|---|---|---|
| 1–8h-iii | Domain model through measure/time-signature cascade | ✅ complete |
| 8h-iv | Paste applies copied clef/signature context | ⬜ next |
| 9 | Validation service | ⬜ |

The CHECKLIST.md “Command and selection model” box remains unchecked until 8h
finishes. Validation, Acceptance Criteria, Test Focus, and the top-level
Milestone 02 box also remain unchecked.

## Next-phase facts

- **8h-iv:** make `PasteFragmentCommand` apply the validated, sorted interior
  `NotationFragment::clef_changes()`, `stave_contexts()`, and
  `measure_contexts()` it currently ignores.
- **Open question for Adam:** applying a copied time signature can change
  `node_end()`, conflicting with paste's locked “paste never grows the node”
  rule. Resolve this before fixing 8h-iv behavior.
- Preserve clipboard identity and mapping rules: fragments retain no source ids;
  map by `(track_ordinal, stave_ordinal, voice)`, not unordered `stave_ids()`.
  Timeline containers are id-free. If signature application changes measure
  length, reuse whole-container/whole-node atomic snapshots and include active
  and archived lanes plus timeline/pickdown effects rather than per-edit inverses.

## Remaining roadmap

- **8h-iv** — apply copied interior clef/signature context after resolving the
  time-signature/paste-length question in `02-domain-model.md`.
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

- Clang-tidy 18 uses the warm `build/tidy` tree and needs the macOS SDK sysroot
  flags documented in `AGENTS.md`; verify HEAD before committing.
- No fault-injection allocator exists; OOM/`kFaulted` paths without a public seam
  remain inspection-verified. Any `bind_output_event` call on a `noexcept`
  command path must be wrapped for allocation safety.
