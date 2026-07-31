# Milestone 02 (Domain And Command Model) — Orchestration Handoff

**Maintenance rule:** this is an active handoff, not a changelog. After every
completed phase, remove or compress completed detail so the file gets SMALLER,
not larger. Git history preserves old detail. It should contain only what the
next agent needs for the next task plus active downstream deferrals.

**Status at this doc:** Phases 1–8g-i complete and committed. **8g-ii
(paste + cut commands) is next.**

- Tests at this increment: 1400, 100% pass (debug + ASan/UBSan clean)

## Milestone scope

Two CMake targets only: `graphscore_core` (Layer 0, pure value types) and
`graphscore_domain` (Layer 1, depends only on core). No new targets, no ADR or
`architecture_contract.cmake` changes.

## Tiered execution and commit policy

Three tiers (full policy in `AGENTS.md`):
- **Tier 1 — focused iteration:** configure only when needed; build affected
  target(s); run focused test binary/filter/regex; lint/format touched files.
  Used during implementation and every fix round.
- **Tier 2 — phase candidate:** worker runs full debug build (zero warnings),
  full `ctest --preset debug --output-on-failure`, full lint target once before
  handoff for review. Reviewers independently verify Tier 2 on the candidate.
- **Tier 3 — final exact-tree:** reviewer runs Tier 2 + all seven architecture
  audits + clang-tidy 18 in `build/tidy` + applicable sanitizer suite(s).
  Run once on the final approved tree after all findings are resolved.

Fix-round workers run **only** Tier 1 targeted regressions matching the
finding and affected target — not the full suite. Report exactly which focused
tests ran. The re-reviewer inspects the delta, runs relevant focused tests,
and defers Tier 3 until findings are resolved. Documentation-only changes
require diff/frontmatter validation, not a C++ sanitizer cycle.

**Commits:** one per phase/increment directly to `main`, never mention AI
assistance. Stage by explicit paths.

## Progress: phases and commits

| # | Section | State | Commit |
|---|---|---|---|
| 1 | Identity and value types | ✅ | `0d6693b` |
| 2 | Project and track model | ✅ | `511045f` |
| 3 | Node timeline | ✅ | `b330675` |
| 4a/4b | Notation model | ✅ | `af8ff75`, `99c1452` |
| 5a/5b | Graph model | ✅ | `87e4b92`, `e13d4b5` |
| 6a–6c | Adaptive playback semantics | ✅ | `c6be9a4` + later |
| 7a–7c | Normative playback specification | ✅ | `063f1af`, `dc7550a` |
| 8a | Command foundation | ✅ | (in-tree) |
| 8b | Metadata/audition-mix commands | ✅ | `ffc9f2c` |
| 8c-i/8c-ii | Structural/config commands (14) | ✅ | `54543c1`, `138458d` |
| 8d-i..iv | Add/remove graph entities | ✅ | `910d250`, `273e0b9`, `da7e0cb`, `fd83039` |
| 8e-i..iii | Notation + tempo edit commands | ✅ | `6f428ac`, later, `5b0d32e` |
| 8f-i | ChordNote + GraceNote identity | ✅ | `1b59fd1` |
| 8f-ii | Selection representation | ✅ | `2372330` |
| **8g-i** | **Clipboard fragment + copy extraction** | ✅ | this increment |
| 8g-ii | Paste + cut commands | ⬜ | — |
| 8g-iii | Node copy/paste id remapping | ⬜ | — |
| 8h | Measure insert/delete + node-timeline edit commands | ⬜ | — |
| 9 | Validation service | ⬜ | — |

CHECKLIST.md M02 boxes remaining: "Command and selection model" (still
unchecked — 8g-ii/8g-iii/8h outstanding), Validation service, Acceptance
criteria, Test focus, top-level "Milestone 02 complete".

## Clipboard API (completed — 8g-i); what 8g-ii builds on

`notation_fragment.hpp` — `NotationFragment` (validated `static create`,
private ctor) + `FragmentExtraction extract_fragment(const Project&, const
Selection&)`, a pure non-mutating query over `FullMeasureSet` and
`ArbitraryRangeSet`.

Facts 8g-ii must honor:
- **Every fragment id is already fresh and source-disjoint.** Paste must
  STILL remap to new ids, so pasting one fragment twice cannot collide.
- Parts are keyed `(track_ordinal, stave_ordinal, voice)`; ordinals resolve
  through `Track::index()` and `Track::layout().staves()`. Paste maps
  ordinals onto destination tracks/staves — never by stored id.
- Every part exactly tiles `[0, span_length())`; empty/short source voices
  are rest-filled. A destination write is therefore always a full-span
  replacement, not a sparse merge.
- Measure/clef context is **informational**. `MeasureMap` has no mutators
  until 8h, so paste must not rewrite destination signatures.
- `PedalSpan` is TrackLane/stave-scoped (not per-voice) and stored relative.
- Copy-side clipping is R1–R12 in the header. Paste owns the *reconnection*
  half: clipping/reconnection into an occupied destination range.
- Reuse `VoiceContent::insert_event`/`remove_event`/`replace_event` and
  `normalize` for destination rest normalization — they are transactional and
  fail atomically. Do not hand-roll rest math.

## Remaining roadmap: 8g-ii → 8g-iii → 8h → 9

- **8g-ii — Paste and cut commands:** reversible `PasteFragmentCommand` and
  cut, whole-container snapshot for undo (the 8d-iv/8e-i precedent), identity
  remapping to fresh ids, destination rest normalization, and **no music
  modified outside the destination range**. Cut = extract + delete-range in
  one transaction. Must handle paste into an occupied range and paste at a
  destination shorter than the fragment.
- **8g-iii — Node copy/paste:** duplicate a node with fresh ids for the node,
  its connectors, lanes, and every notation entity; intra-selection connector
  edges remapped, edges leaving the selection dropped. Never duplicates a
  stable UUID.
- **8h — Measure insert/delete + node-timeline edit commands:** `MeasureMap`
  has no mutator at all — needs new domain API + atomic cascade across every
  voice in every track's lane. Also owns (per Adam's post-8e-iii ruling) the
  three missing node-timeline command families: per-measure time/key-signature
  changes, clef changes (wrapping `ClefLane`), and pickdown set/clear
  (wrapping existing `set_pickdown`/`clear_pickdown` with optional snapshots).
  These close the "reversible commands for every … notation … edit" box.
- **Phase 9 — Validation service:** fast incremental + complete validation;
  diagnostics with stable ids/severity/code/text; validates rhythmic
  completeness, UUID uniqueness, references, track alignment, signature
  legality, graph edge integrity, event-name uniqueness, connector cardinality.
- **Finally:** verify Acceptance Criteria + Test Focus, check all remaining
  boxes, update CHECKLIST.md, summarize for Adam, **stop** (do not start M03).

## Active deferrals (relevant to remaining M02 work)

- **→ M06:** route-segment selection deferred out of M02. `RouteGeometry` stores
  only interior waypoints, endpoints are explicitly a rendering concern, and
  an automatic route stores no geometry at all. Any index-based segment
  reference is invalidated by `SetCustomRouteCommand`, `ResetRouteCommand`,
  and `disconnect` (which resets a customized route to automatic). M06 owns
  connector-segment drag; define the representation there, next to its only
  real consumer. The `02-domain-model.md` selection deliverable box stays
  unchecked for this reason.
- **→ M03:** connector order is semantically load-bearing (tier-3 arbitration
  depends on `Node::outputs()` insertion order, which was previously only
  cosmetic). Persistence must preserve and round-trip this order exactly — a
  serializer that writes connectors from a map or re-sorts by UUID would
  silently change playback. Warrants a dedicated round-trip test in M03.
- **→ M03:** `ChordNote::id` and `GraceNote::id` from 8f-i are load-bearing
  fields (same class of obligation as connector order). A serializer that
  drops or regenerates them silently breaks every notehead/grace-note
  selection and every clipboard identity remapping.
- **→ Phase 9:** deterministic ordering in the notation validator.
  `notation_validation.cpp` iterates `TrackLane::stave_ids()` from a
  `std::unordered_map`, so cross-stave diagnostic order is non-deterministic.
  Sort stave ids when the general ValidationService subsumes the focused
  validator.
- **→ Phase 9:** `EventStateMachine::clear_event` caller obligation is still
  unenforced — a caller who unbinds the last output for an event and forgets
  `clear_event` gets a stale occurrence resurrecting on rebind. The transport/
  edit layer should make the call automatic at the mutation site.
- **→ later (advisory):** orphan queue growth — `EventStateMachine::queues_`
  entries for removed nodes/events are never reclaimed (harmless since ids are
  UUIDs); a `prune(const Graph&)` would address it. A zero-capacity FIFO
  listener silently swallows every occurrence (Phase 9 diagnostic candidate).

## Environment quirks

- **clang-tidy is a pre-commit gate.** `.githooks/pre-commit` runs the
  clang-tidy 18 analysis (incrementally, reusing `build/tidy`) and blocks the
  commit. Canonical configure: `cmake --preset debug -B build/tidy
  -DGRAPHSCORE_BUILD_WRITER=OFF -DGRAPHSCORE_ENABLE_CLANG_TIDY=ON
  -DGRAPHSCORE_CLANG_TIDY_EXECUTABLE=/opt/homebrew/opt/llvm@18/bin/clang-tidy`.
  On macOS append `-DCMAKE_CXX_FLAGS="-isysroot $(xcrun --show-sdk-path)"
  -DCMAKE_C_FLAGS="-isysroot $(xcrun --show-sdk-path)"`. Warm `build/tidy`
  before committing so the hook sees "ninja: no work to do".
- **cpplint** is not on PATH; lives at
  `/Users/adamshield/Library/Python/3.9/bin`. Prepend before configure.
- **clang-format:** `/Library/Developer/CommandLineTools/usr/bin/clang-format`.
- **IDE/clangd false positives** (C++20 `<=>`, missing includes) — judge
  only by the clean cmake build.
- **Watch for Adam's out-of-band commits:** verify HEAD before committing.
- **Any `bind_output_event` call on a noexcept command path must be wrapped**
  for allocation safety (family-wide hardening precedent from `7297bca`).

## Conventions (locked, match exactly)

Flat `namespace graphscore` (no nested namespaces). Files snake_case; types
PascalCase; methods/members snake_case with trailing `_` on private members;
constants `kPascalCase`; `enum class` with `k`-prefixed enumerators. Every file
starts `// SPDX-License-Identifier: Apache-2.0`; headers use `#pragma once`.
Umbrella headers re-`#include` each new public header. New `.cpp` go in the
target's `CMakeLists.txt` source list; new tests in
`tests/<target>/CMakeLists.txt`. Validated value types use
`static std::optional<T> create(...)` with a private constructor — no silent
invalid construction. Musical positions/durations are exact `Rational` in
whole-note units, never floating point. Pure value types → `graphscore_core`;
notation/graph/model structures → `graphscore_domain`. `.clang-format` is
Google-based, 80 columns, 2-space indent, sorted includes.
