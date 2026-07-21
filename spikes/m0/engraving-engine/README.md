# M0 Engraving-Engine Spike

## Outcome

**Decision: adopt an owned semantic-layout direction for production design.**
No reviewed mature candidate passed GraphScore's default-build license gate.
This small risk spike is not a production engraver; it proves only a
toolkit-neutral, retained semantic output direction.

### Finite research budget and method

The review was checked on **2026-07-21** with a fixed **four engineer-hour**
budget: one hour each for source/license/API/documentation triage of four
candidates. Each review read the upstream source-tree license and public API
or documentation at the immutable commit below, then classified the interaction
model. No copyleft candidate was fetched, compiled, vendored, or integrated:
license rejection ended evaluation before executable fixtures. Consequently,
all interaction observations for rejected candidates are **source/documentation
triage**, not empirical direct-manipulation or page-behaviour test results.

| Candidate | Reviewed immutable source | License source | Result and bounded rationale |
| --- | --- | --- | --- |
| Verovio 6.2.0 | [`43f806031bfff2c64003fc8ddd9910820445f6ab`](https://github.com/rism-digital/verovio/commit/43f806031bfff2c64003fc8ddd9910820445f6ab) | [COPYING](https://raw.githubusercontent.com/rism-digital/verovio/43f806031bfff2c64003fc8ddd9910820445f6ab/COPYING), [COPYING.LESSER](https://raw.githubusercontent.com/rism-digital/verovio/43f806031bfff2c64003fc8ddd9910820445f6ab/COPYING.LESSER) | **License-gate rejected:** GPL-3.0/LGPL-3.0. Source/documentation triage describes MEI-to-SVG/document output; no low-latency measure API was empirically tested. |
| GuidoLib 1.7.7 | [`3eefd99ff13baa3887172c7cfc96c826c39dcd2a`](https://github.com/grame-cncm/guidolib/commit/3eefd99ff13baa3887172c7cfc96c826c39dcd2a) | [LICENSE](https://raw.githubusercontent.com/grame-cncm/guidolib/3eefd99ff13baa3887172c7cfc96c826c39dcd2a/LICENSE) | **License-gate rejected:** MPL-2.0 is outside ADR 0001's default set. Its Guido notation/document model was only source/documentation triaged, not run. |
| MuseScore 4.7.4 | [`7688c005ad963ba09ee715aae53dbc7e8c9d5ef8`](https://github.com/musescore/MuseScore/commit/7688c005ad963ba09ee715aae53dbc7e8c9d5ef8) | [LICENSE.txt](https://raw.githubusercontent.com/musescore/MuseScore/7688c005ad963ba09ee715aae53dbc7e8c9d5ef8/LICENSE.txt) | **License-gate rejected:** GPL-3.0. Its application/project architecture was source/documentation triaged; no embedding or interaction test was run. |
| LilyPond 2.25.81 | [`18cf09c1f24d22eeb0cdc187dec0af734d60cfc8`](https://gitlab.com/lilypond/lilypond/-/commit/18cf09c1f24d22eeb0cdc187dec0af734d60cfc8) | [COPYING](https://gitlab.com/lilypond/lilypond/-/raw/18cf09c1f24d22eeb0cdc187dec0af734d60cfc8/COPYING) | **License-gate rejected:** GPL-3.0. Batch/compiler and page-oriented behaviour is a source/documentation finding only, not an executed benchmark. |
| Owned SMuFL fallback 0.3.1-spike | GraphScore-owned `src/` | Apache-2.0 repository license | Selected direction: executable measure-local output and evaluator coverage, with no third-party engraving closure. |

Immutable IDs were resolved from the upstream GitHub/GitLab tag/ref APIs on the
checked date. The matrix is not a claim that rejected candidates failed this
spike's executable fixture suite. Exact rationale and production follow-ups are
in [ADR 0005](../../../docs/decisions/0005-engraving-engine-spike.md).

### Adoption criteria

A candidate passes only if all are true: (1) its complete default-build closure
passes ADR 0001; (2) it exposes bounded measure-local relayout without page or
serialization round trips; (3) it passes every one of the 15 evaluator
   criteria, including semantic retained output and hit ordering; (4) it accepts
the 128/384 width fixtures; and (5) its public adapter exposes only
GraphScore-owned types. A policy failure is an immediate rejection; an
interaction or fixture failure is also a rejection. The owned proof is adopted
because it passes the executable criteria and has no external engraving closure.

## What the owned proof does

`SmuflFallbackEngraver` accepts GraphScore-owned data only and emits retained
`LayoutElement` records:

- SMuFL code points for clefs, noteheads, accidentals, time signatures,
  articulations, flags, and both actual/normal tuplet numbers;
- abstract glyph, line, and quadratic-curve geometry for staff lines, stems,
  beams, flags, ledger lines, ties, slurs, tuplets, hairpins, and barlines;
- stable semantic IDs/roles, deterministic hit testing, bounds, and per-layout
  work counters; and
- independent measure output with an appended x-origin. Existing measure
  output is not mutated by later layout calls.

The evaluator exercises one- and four-voice fixtures, including four voices with
three timed notes at width 128; 3:2 versus 3:3 and 3:2 versus 4:2 tuplets whose
derived normalized timing and positions differ; grace notes anchored after a preceding note;
clef/key/time changes; distinct tie/slur curves; both hairpin directions;
articulations; hit testing; 128 and 384 unit widths; and malformed input.
Layouts reserve voice columns before timeline positions, reject widths that
cannot reserve the header, columns, and representative timeline extent, and
validate every emitted x coordinate against the declared layout width. Capability
queries are only preconditions. Mutation doubles deliberately corrupt hairpin
direction, normal tuplet counts, actual tuplet metadata, or 4:2 timing while
retaining 4:2 metadata, articulation bounds, grace timing, and reported
incremental work; the evaluator rejects each output-path error.

## Build and verify

Run from this directory. GTest 1.16.0 is the sole fetched dependency, pinned
at immutable commit `6910c9d9165801d8827d628cb72eb7ea9dd538c5` under the
BSD 3-Clause license. `FETCHCONTENT_SOURCE_DIR_GOOGLETEST` supports an offline
source override; a populated cache can be used with
`-DFETCHCONTENT_FULLY_DISCONNECTED=ON`.

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
./build/engraving_spike_evaluator
./build/engraving_spike_evaluator --measure

cmake -S . -B build-asan -G Ninja -DSPIKE_ENABLE_ASAN=ON
cmake --build build-asan
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build-asan --output-on-failure
ASAN_OPTIONS=detect_leaks=0 ./build-asan/engraving_spike_evaluator
ASAN_OPTIONS=detect_leaks=0 ./build-asan/engraving_spike_evaluator --measure
```

GraphScore-owned targets use C++23 and Clang/AppleClang `-Weverything -Werror`.
GTest is built separately and its headers are consumed as `SYSTEM`. The
`SPIKE_ENABLE_ASAN` option adds ASan and UBSan only when Clang-family compilers
support them.

## Evidence and measurement

`evidence/` contains the configure/build/CTest/evaluator/measurement logs for
normal and ASan+UBSan clean builds, plus commands, machine metadata, source
provenance, and a catalog. The performance fixture is deliberately narrow:
64 measures, one voice, four eighth notes, 320-unit width, followed by 1,000
layouts of one changed measure. Its work counter must visit exactly four notes;
elapsed wall time is observational hardware evidence, **not** a production
latency or frame-rate claim.

## Limits and follow-up

This deliberately lacks real font metrics, optical spacing, rhythmic duration
normalization, cross-measure/system spans, chords, rests, general collision
avoidance beyond its bounded representative voice columns, pagination, line
breaking, accessibility exposure, and rendering.
Tuplet timing is only a representative duration ratio. A grace note must follow
an anchored note in the same voice; it borrows at most half that anchor's
duration for this proof, and an unanchored grace event is rejected. These are
truthful spike rules, not production rhythmic semantics. It does not ship a
font: SMuFL code points are references only, so it makes no font-license claim.
Production should move the owned interfaces to `graphscore_notation`, obtain
glyph metrics through the abstract interface from ADR 0003, add real
invalidation/collision/rhythm rules, and test the supported score envelope on
all target platforms.
