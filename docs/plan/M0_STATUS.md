# Milestone 00 Status

**Last updated**: 2026-07-21

Scope and rules for M0 live in [00-architecture-spikes.md](00-architecture-spikes.md).
Progress boxes live in [CHECKLIST.md](CHECKLIST.md). This file records only
current state.

## Decided

| Question | Outcome | Record |
|---|---|---|
| Licensing and dependency policy | Apache-2.0, permissive-only default build | ADR 0001 |
| Writer dependency baseline | SDL3, FreeType, HarfBuzz, Bravura, Noto Sans, pinned to immutable commits | ADR 0002 |
| Architecture boundaries and dependency direction | Recorded target DAG | ADR 0003 |
| Rendering and accessibility | Viable on macOS; custom-drawn notation is screen-reader navigable | ADR 0004 |
| Engraving engine | No permissively licensed embeddable interactive engine exists; GraphScore owns its layout using SMuFL glyphs and a toolkit-neutral render list | ADR 0005 |

macOS physical VoiceOver and trackpad gates were run and accepted by Adam; both
are recorded in ADR 0004 and the rendering spike's evidence directory. Windows
and Linux screen-reader and physical-GUI verification are deferred to
Milestone 10 by recorded scope decision.

## Remaining

1. Write ADR 0006 selecting a cooked-format direction. Paper only, 2-hour box.
2. Confirm the VST3 SDK license from `LICENSE.txt` at the pinned commit. The
   scaffold claims MIT as of SDK 3.8.x, replacing the former GPLv3 + Steinberg
   proprietary dual license. This is unverified and load-bearing for all of
   Milestone 08 — stop and escalate if it does not hold.
3. Run the direct VST3 hosting spike on macOS arm64. One working day.
4. Delete or quarantine everything under `spikes/m0/`.
5. Approve the M0 exit decision.

## Repository Notes

- No remote origin is configured. All commits are local; back up before
  destructive operations.
- There is no root `CMakeLists.txt` yet. Each spike is a standalone CMake
  project. Production build setup is Milestone 01.
- `spikes/**/build*/` is gitignored.
- `spikes/m0/vst3-hosting/` and `spikes/m0/cooked-format/` are stub scaffolds
  only — self-validating placeholders with no real implementation behind them.
  Do not read their passing tests as evidence of anything.

## History

M0 originally ran four full spikes under production-grade gates — tests,
sanitizers, evidence catalogs, provenance hashes, and reviewer cycles on
disposable code. That consumed several days and did not increase confidence in
any decision. The milestone was rescoped on 2026-07-21 to one remaining spike
plus one paper ADR, and the cross-milestone Definition Of Done was narrowed to
production milestones only. Detailed history is in the git log.
