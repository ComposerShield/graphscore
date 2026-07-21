# ADR 0006: Cooked Asset Format Direction

| Status | Author | Date |
|--------|--------|------|
| Accepted | M0 cooked-format decision | 2026-07-21 |

## Context

The cooked asset is the immutable binary the writer exports and the runtime
loads. It crosses a trust boundary: it ships inside other people's games and may
be modified, truncated, or deliberately crafted before reaching
`graphscore_loader`.

Four constraints from ADR 0003 and the plan README bound the choice:

1. **Zero allocation on load.** `graphscore_loader` takes a caller-provided
   immutable memory span and host allocator callbacks. Allocation is the host's
   decision, not the format's.
2. **Byte-for-byte deterministic export.** `graphscore_compiler` must produce
   identical output for semantically identical projects.
3. **Full bounds, offset, and reference validation on hostile input**, reported
   through structured C-compatible error codes.
4. **No third-party types in persistence schemas, the domain, or the C ABI.**

This decision was made on paper under a two-hour budget. Milestone 00 previously
carried it as a spike; the constraints above already discriminate between the
candidates, and Milestone 03 is where the format becomes real and where being
wrong is cheap to correct.

## Decision

**GraphScore owns its cooked binary format.** `graphscore_cooked_format`
implements the layout, reader/writer, magic and version constants, and
bounds/offset validation helpers, as already specified in ADR 0003.

No schema-generation dependency enters the runtime closure.

## Rationale Against the Evaluation Criteria

| Criterion | Outcome |
|---|---|
| Deterministic byte-for-byte export | Direct. We emit the bytes; there is no vtable deduplication, alignment, or padding ambiguity to pin down and test. |
| Versioning and unknown-field behavior | **Weakest area.** Must be designed explicitly, not inherited. See Risks. |
| Bounds/offset validation on hostile input | Explicit and auditable in one reviewable place, using our own error codes rather than a foreign verifier's success/failure bit. |
| Load-time allocation count and host allocators | Zero allocation is the default rather than something engineered around a library's own allocation behavior. |
| Compact representation of UUID/index maps, schedules, tempo curves, routing | Full control over packing. The compiler already resolves UUIDs to dense indices, so the format stores indices and side tables, not a general object graph. |
| Debuggability and generated-code warning hygiene | No generated code to keep warning-clean or isolate. **This is the weakest argument in favor** — the toolchain uses a targeted warning set rather than `-Weverything`, so generated code would have been a manageable irritation, not a running battle. Introspection must be built. See Risks. |

## Rejected Alternative: FlatBuffers

FlatBuffers is a credible, well-engineered choice and was rejected on fit, not
quality. What it offers is real and is being given up deliberately:

- Battle-tested schema evolution and unknown-field handling.
- A verifier written by people with prior adversarial exposure.
- A self-documenting `.fbs` schema and free asset dumps via `flatc`.

It was rejected because its verifier is an O(n) validation pass with its own
allocation behavior that does not route through host allocator callbacks;
byte-determinism requires pinning down vtable deduplication, alignment, and
padding; generated types in the persistence and runtime layers would carve an
explicit exception into the no-third-party-types rule; and `flatc` adds a
codegen step to the build graph for every consumer of the format.

The first three reasons are load-bearing. Warning hygiene and build-graph
complexity are secondary and would not on their own justify owning a format.

Under different constraints — particularly if host-controlled allocation or
strict byte-determinism were dropped — this decision would likely reverse.

## Accepted Risks and Mitigations

These are the real costs of the decision and are carried into Milestone 03.

| Risk | Mitigation |
|---|---|
| **We now own schema evolution and versioning**, the part that is routinely underestimated. | Design version negotiation and unknown-region skipping **before** the first format byte is written, not after the first migration is needed. A cooked asset is versioned from its first public appearance per the plan README. |
| **We now own the validator, and its bugs are security bugs** on attacker-controlled input in a library shipped inside third-party games. | Milestone 03 must treat the validator as adversarial-input code: malformed, truncated, overlapping-offset, cyclic-reference, and integer-overflow fixtures; fuzzing over the loader entry point; ASan/UBSan on the corpus. Validation must be total — no field is trusted before it is checked. |
| **No free introspection** for debugging malformed assets. | Build a minimal owned dump tool in Milestone 03 alongside the writer. It is small once the layout is fixed, and it doubles as a golden-file diffing aid for determinism tests. |

## Fallback

If Milestone 03 shows the owned format failing — most plausibly because
hand-written versioning or validation proves unsustainable — the fallback is
FlatBuffers confined to `graphscore_cooked_format` behind the existing owned
reader API, with generated types never crossing that boundary. ADR 0003 already
isolates every consumer behind that target, so the blast radius is one library,
and the accepted-risk exception to the no-third-party-types rule would be
recorded as a superseding ADR.

The trigger for reconsidering is explicit: a validation or migration defect
class that recurs after being fixed once.

## Consequences

- Milestone 00 writes no format code. This ADR is the deliverable.
- Milestone 03 owns layout design, version negotiation, the validator, the
  adversarial-input corpus, the determinism tests, and the dump tool.
- `spikes/m0/cooked-format/` is deleted; it contained stubs that implemented
  nothing and whose passing tests were not evidence.
