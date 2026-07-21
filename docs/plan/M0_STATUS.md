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
| Cooked asset format | GraphScore owns its binary format; FlatBuffers rejected on allocator control and byte-determinism cost | ADR 0006 |
| VST3 SDK licensing | MIT across the root repo and all core submodules incl. `pluginterfaces`; VSTGUI BSD-3-style and not required | `spikes/m0/vst3-hosting/README.md` |

macOS physical VoiceOver and trackpad gates were run and accepted by Adam; both
are recorded in ADR 0004 and the rendering spike's evidence directory. Windows
and Linux screen-reader and physical-GUI verification are deferred to
Milestone 10 by recorded scope decision.

| Direct VST3 hosting | Viable on macOS arm64 with no licensed framework; native editor gate observed by Adam on render, resize, and keyboard focus | ADR 0007 |

## Remaining

1. Approve the M0 exit decision.
2. Delete `spikes/m0/vst3-hosting/` once Milestone 01 has the VST3 SDK
   building in CI. Retained deliberately: the CMake workarounds in ADR 0007
   are fiddly enough that working code is worth more than a clean tree.

## Findings Carried Forward

Recorded where the owning milestone will read them, not in a separate tracker.

| Finding | Owner | Location |
|---|---|---|
| Plugin state is not byte-deterministic (Kontakt 3897 -> 3913 bytes over an identical save/restore/save cycle). Never hash or diff it for change detection. | M03 | `03-persistence-export.md`, Editable project bundle |
| Commercial plugins install process-wide signal handlers into the host (AutoTune takes SIGSEGV/SIGABRT/SIGFPE/SIGBUS). Open question against the planned in-process playback hosting. | M08 | `08-audio-vst.md`, Open question carried from M0 |
| VST3 editor API quirks: `canResize` authoritative, `onFocus` advisory, re-entrant `resizeView` during `attached()`, `kInfiniteTail` common. | M08 | `08-audio-vst.md`, Plugin editors and parameters |
| Scan timeouts must exceed 5s per plugin. | M08 | `08-audio-vst.md`, Plugin scanning |
| SDK build workarounds: `fdebug.h` build-type `#error`, `XCODE_VERSION` double-set, incomplete `sdk_hosting`. | M01 | ADR 0007, Build Integration Notes |
| Commercial plugins were pre-authorized on the test machine; fresh-machine and CI behaviour unknown. | M08 | ADR 0007, Open Risks |
| Windows, Linux/Wayland, x86-64 VST3 untested. Wayland editor embedding is the highest residual platform risk. | M08 | ADR 0007, Open Risks |

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
