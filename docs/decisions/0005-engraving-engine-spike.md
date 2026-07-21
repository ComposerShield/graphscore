# ADR 0005: Engraving-Engine Spike Direction

| Status | Author | Date |
|--------|--------|------|
| Accepted | M0 engraving-engine spike | 2026-07-21 |

## Context

GraphScore needs interactive notation layout that remains toolkit-neutral:
`graphscore_notation` may depend only toward core/domain and must emit retained
SMuFL glyph references plus abstract geometry/render commands (ADR 0003 §1,
Layer 6). The Apache-2.0 default build accepts only the permissive commercial
reuse closure defined by ADR 0001. GPL, AGPL, mandatory commercial licensing,
and LGPL candidates are not accepted for the default complete build.

This time-boxed spike evaluated known mature engines without fetching or
building a candidate that had already failed that policy gate. It then tested a
small owned semantic-layout proof, rather than attempting production engraving.

## Research Budget and Method

The review was checked on **2026-07-21** under a fixed **four engineer-hour**
budget: one hour each for four candidates. The method was source/license/API/
documentation triage at an immutable upstream commit: read the source-tree
license, public API, and documentation, classify the stated interaction model,
then apply the ADR 0001 default-build license gate. No copyleft source code was
integrated, fetched, compiled, or exercised. Interaction statements below are
therefore source/documentation triage, not empirical fixture failures.

## Candidate Evidence and Decision

Sources were checked on 2026-07-21. Immutable commits were resolved through the
authoritative upstream GitHub or GitLab tag/ref APIs; no rejected source was
made a GraphScore dependency or pin.

| Candidate | Version and immutable source | Source/license evidence | Result | Reason |
| --- | --- | --- | --- | --- |
| Verovio | 6.2.0, [`43f806031bfff2c64003fc8ddd9910820445f6ab`](https://github.com/rism-digital/verovio/commit/43f806031bfff2c64003fc8ddd9910820445f6ab) | [COPYING](https://raw.githubusercontent.com/rism-digital/verovio/43f806031bfff2c64003fc8ddd9910820445f6ab/COPYING), [COPYING.LESSER](https://raw.githubusercontent.com/rism-digital/verovio/43f806031bfff2c64003fc8ddd9910820445f6ab/COPYING.LESSER) | License-gate rejected | GPL-3.0/LGPL-3.0 fails ADR 0001. MEI-to-SVG/document output was source/documentation triage only; no low-latency API was run. |
| GuidoLib | 1.7.7, [`3eefd99ff13baa3887172c7cfc96c826c39dcd2a`](https://github.com/grame-cncm/guidolib/commit/3eefd99ff13baa3887172c7cfc96c826c39dcd2a) | [LICENSE](https://raw.githubusercontent.com/grame-cncm/guidolib/3eefd99ff13baa3887172c7cfc96c826c39dcd2a/LICENSE) | License-gate rejected | MPL-2.0 is outside ADR 0001. The notation-string/document model was source/documentation triage only, not run. |
| MuseScore | 4.7.4, [`7688c005ad963ba09ee715aae53dbc7e8c9d5ef8`](https://github.com/musescore/MuseScore/commit/7688c005ad963ba09ee715aae53dbc7e8c9d5ef8) | [LICENSE.txt](https://raw.githubusercontent.com/musescore/MuseScore/7688c005ad963ba09ee715aae53dbc7e8c9d5ef8/LICENSE.txt) | License-gate rejected | GPL-3.0 fails ADR 0001. Application architecture was source/documentation triage only; no embedding test ran. |
| LilyPond | 2.25.81, [`18cf09c1f24d22eeb0cdc187dec0af734d60cfc8`](https://gitlab.com/lilypond/lilypond/-/commit/18cf09c1f24d22eeb0cdc187dec0af734d60cfc8) | [COPYING](https://gitlab.com/lilypond/lilypond/-/raw/18cf09c1f24d22eeb0cdc187dec0af734d60cfc8/COPYING) | License-gate rejected | GPL-3.0 fails ADR 0001. Batch/compiler and page behaviour is source/documentation triage only, not an executed latency test. |

No candidate passed the policy gate, so no candidate capability score is
reported. This is intentional: inventing a runtime result for code that was
not built would be misleading. A future candidate must empirically demonstrate
bounded partial reflow, semantic retained output, and deterministic hit testing
through a GraphScore-owned adapter before it can pass the interaction gate.

## Owned Fallback Measurement

The standalone Apache-2.0 proof at `spikes/m0/engraving-engine/` has no UI,
SDL, font asset, or third-party type in its public model. Its only fetched
dependency is spike-only GTest 1.16.0, BSD 3-Clause, commit
`6910c9d9165801d8827d628cb72eb7ea9dd538c5`; it is isolated from owned warning
flags and recorded in `docs/NOTICES.md` with the exact license at
`docs/licenses/GoogleTest-BSD-3-Clause.txt`.

The evaluator has 15 non-empty criteria. It validates semantic identities and
roles, finite/bounded geometry, staff/stem/beam/flag/ledger proof geometry,
four simultaneous voice separation (including a bounded four-voice, three-note
fixture at width 128), 3:2 ratio semantics and timing versus 3:3, 3:2 versus
4:2 actual-count semantics and derived exact timing/positions, anchored grace timing, clef/key/time
changes, separate tie/slur curves, both hairpin directions, collision-free
stacked articulations, deterministic hit testing, unchanged-region incremental
stability with exact changed-measure work, and 128/384 width adaptation.
Mutation doubles prove that it rejects incorrect hairpin direction, normal
tuplet counts, actual tuplet metadata, and 4:2 timing that falsely retains 4:2
metadata, articulation geometry, grace anchor, and work output despite positive
capability flags. GTest also rejects malformed voice count,
`quiet_NaN()`/infinite/absurd/too-small widths, integer-extreme pitch, slur,
tuplet, non-finite hit point, malicious prior geometry, and uninitialized cases.

The checked hardware/date and exact normal/ASan results are in
`spikes/m0/engraving-engine/evidence/README.md` and its referenced logs. The
scale command builds 64 one-voice, four-eighth-note measures at 320 units, then
relayouts one changed measure 1,000 times. It must report four visited notes.
Its elapsed time is evidence for this fixture only, not a production latency,
interactive-frame-rate, or full-score scaling guarantee.

## Decision

Adopt the **owned semantic-layout fallback as the production direction**. This
does not commit GraphScore to the spike implementation or claim it is a mature
engraving engine. A production `graphscore_notation` target will preserve the
same core boundary: GraphScore-owned semantic input; retained IDs; positioned
SMuFL code points; abstract glyph/line/curve/clip commands; and toolkit-neutral
hit testing. Concrete font metrics and rendering stay behind private rendering
adapters as required by ADR 0003.

No SMuFL font is acquired or redistributed by this spike. Code points alone are
not a font asset and must not be entered in the dependency or font-license
inventory. A future selected font must be evaluated and noticed independently.

## Consequences and Follow-ups

- Move the proven types behind the Layer 6 `graphscore_notation` API in the
  appropriate production milestone; do not expose renderer/font/UI types.
- Replace the synthetic metrics with the abstract glyph-metrics interface and a
  private renderer implementation.
- Add production rhythmic normalization (the spike only applies representative
  tuplet ratios and a same-voice preceding-anchor grace rule), chords/rests,
  cross-measure spans, true invalidation, general collision handling, line/system
  breaking, accessibility semantics, and target platform performance budgets
  before claiming production engraving.
- Re-open candidate review only for an engine with a license that passes ADR
  0001 and demonstrable low-latency partial reflow. It must then receive a
  pinned, inventoried FetchContent integration and the same fixture suite.
