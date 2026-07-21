# ADR 0003: Architecture Target DAG and Layer Boundaries

| Status | Author | Date |
|--------|--------|------|
| Accepted | Phase B architecture | 2026-07-19 |

## Context

Milestone 00 Phase B requires defining named CMake targets, their dependency
direction, and hard isolation rules that prevent writer-only libraries from
entering runtime, scheduler, domain, persistence, compiler, or C ABI
compilation closures. The target graph serves as the mechanical architecture
contract for all downstream milestones and must be comprehensive enough that
M1 can implement it without inventing dependencies.

This ADR references:

- [ADR 0001](0001-permissive-dependency-policy.md) — permissive licensing and
  dependency acceptance checklist.
- [ADR 0002](0002-default-writer-dependency-baseline.md) — pinned dependency SHAs
  and their policy/provisional/excluded statuses.

## Decision

### 1. Target Inventory by Layer

Each target name follows the `graphscore_<component>` convention. Layer 0 is
at the bottom of the dependency graph; higher-numbered layers depend on lower
ones. A target in layer N may depend on targets in layers < N, or within the
same layer only where explicitly permitted in §2.

**The Public Dependencies and Private Dependencies columns describe only
GraphScore-internal edges.** Every external (third-party) dependency is
recorded exclusively in §2.2. No external type or library is a column entry
in the inventory tables below.

#### Layer 0 — Core Primitives

| Target | Kind | Public Dependencies | Private Dependencies | Responsibility |
|--------|------|---------------------|-----------------------|----------------|
| `graphscore_core` | STATIC | _(none)_ | _(none)_ | Platform-independent value types: UUID wrappers, exact-rational position/duration, MIDI pitch/channel/velocity, accidental, clef, key/time signature, voice, tempo, validated error enums. No domain logic. No third-party types. |
| `graphscore_c_abi` | INTERFACE | _(none)_ | _(none)_ | Pure C header (no C++ in header): opaque handles (`gs_asset_t`, `gs_player_t`), fixed-width integer types (`uint32_t`, `uint64_t`), sized/versioned structs, allocator callback types using explicit `uint64_t` byte counts and alignment (`gs_alloc_fn`), lifetime and error conventions (`gs_result_t`), exported-symbol macros (`GS_API`). No exceptions, no C++ standard library types, no third-party types, no platform types. |

#### Layer 1 — Domain

| Target | Kind | Public Dependencies | Private Dependencies | Responsibility |
|--------|------|---------------------|-----------------------|----------------|
| `graphscore_domain` | STATIC | `graphscore_core` | _(none)_ | Complete domain model: strong-identity wrappers, project/track/node/notation/graph/event models, reversible commands, transaction grouping, selection/clipboard model, incremental/complete validation service, normative playback-semantics specification (tempo curves, articulation mappings, event state machines, pickdown rules, MIDI ownership arbitration, vertical/sequential routing rules). Opaque plugin identity types and raw-state-byte storage interfaces — no VST3 types. No UI, no audio device, no VST3 SDK, no persistence, no rendering. |

#### Layer 2 — Cooked Format & Compilation (Clean)

| Target | Kind | Public Dependencies | Private Dependencies | Responsibility |
|--------|------|---------------------|-----------------------|----------------|
| `graphscore_cooked_format` | STATIC | `graphscore_core` | _(none)_ | Cooked asset data-structure definitions: compact schedule types, index tables, event lookup, transition tables, tail-slot layouts, binary format reader/writer, version negotiation, magic/version constants, bounds/offset validation helpers. No domain types. No runtime I/O. |
| `graphscore_compiler` | STATIC | `graphscore_domain`, `graphscore_cooked_format` | _(none)_ | Domain-to-compact compiler: validates the complete project for export correctness, compiles domain model into immutable compact scheduler input snapshots, resolves UUID→index maps, precomputes note schedules/tempo integration data/transition tables/tail-slot capacities, rejects unbounded overlaps and invalid vertical compatibility. Produces byte-for-byte deterministic compact output for semantically identical projects. Consumed by `graphscore_persistence` for export and by `graphscore_writer_audio` for live snapshot publication. Remains clean of UI, audio-device, VST3 SDK, rendering, and platform types. Snapshot compilation is designed to run off the realtime callback. |

Within-layer dependency: `graphscore_compiler` → `graphscore_cooked_format` is explicitly permitted.

#### Layer 3 — Persistence (Clean)

| Target | Kind | Public Dependencies | Private Dependencies | Responsibility |
|--------|------|---------------------|-----------------------|----------------|
| `graphscore_persistence` | STATIC | `graphscore_domain`, `graphscore_cooked_format`, `graphscore_compiler` | _(none)_ | Editable project bundle (ZIP-like), autosave/recovery journal, schema evolution/migration, atomic save, malformed-input detection, save-model round trips. Delegates domain→compact compilation to `graphscore_compiler` for the export pipeline. Stores opaque plugin state blobs as raw bytes with owned identifiers — never includes or depends on VST3 SDK types. |

#### Layer 4 — Scheduler (Clean)

| Target | Kind | Public Dependencies | Private Dependencies | Responsibility |
|--------|------|---------------------|-----------------------|----------------|
| `graphscore_scheduler` | STATIC | `graphscore_core`, `graphscore_cooked_format` | _(none)_ | **Single deterministic scheduler implementation.** Operates on cooked/compact schedule data (not domain types). Consumed by writer audition path and runtime. Implements: tempo integration (step/linear/smooth), note/chord/rest/tie/grace/dynamics/hairpin/articulation/slur/CC64 scheduling, same-sample phase ordering, vertical/sequential routing, pickdown tail management, event queue processing, weighted-random PRNG, capacity preflight, MIDI-ownership arbitration. Golden vectors independently verify semantics rather than duplicating implementation. No UI, no audio device, no VST3. |

#### Layer 5 — Runtime

| Target | Kind | Public Dependencies | Private Dependencies | Responsibility |
|--------|------|---------------------|-----------------------|----------------|
| `graphscore_loader` | STATIC | `graphscore_core`, `graphscore_c_abi`, `graphscore_cooked_format` | _(none)_ | Cooked-asset loader from caller-provided immutable memory span. Host allocator callbacks. Magic/version/bounds/offset/reference validation. Structured C-compatible error codes. |
| `graphscore_runtime_impl` | STATIC | `graphscore_core`, `graphscore_c_abi`, `graphscore_scheduler`, `graphscore_loader` | _(none)_ | C++23 runtime implementation: processing contract (absolute sample position, variable frame count, ordered input events), capacity query, transactional undersized-output/no-state-advance retry, realtime diagnostic counters, pause/resume/stop/reset lifecycle, preallocated queues, immutable asset storage, host-seeded PRNG, one-owner processing thread rules. No allocation, locks, exceptions, logging, I/O, or unbounded work in processing path. |
| `graphscore_runtime` | SHARED | `graphscore_c_abi` | `graphscore_runtime_impl` | Dynamic library with hidden visibility and explicitly exported C ABI symbols. Entry points: create, destroy, load, capacity query, start (designated/UUID), pause, resume, stop, reset, process, diagnostics poll/reset, version query. Link-time dependency for native hosts; dynamic load for Unity P/Invoke and Unreal plugin. |

Within-layer dependencies: `graphscore_runtime_impl` → `graphscore_loader` and `graphscore_runtime` → `graphscore_runtime_impl` (PRIVATE) are explicitly permitted.

#### Layer 6 — Notation (Toolkit-Neutral)

| Target | Kind | Public Dependencies | Private Dependencies | Responsibility |
|--------|------|---------------------|-----------------------|----------------|
| `graphscore_notation` | STATIC | `graphscore_domain`, `graphscore_core` | _(none)_ | Toolkit-neutral notation layout, hit testing, and abstract render-command production. Produces retained lists of positioned glyph references (SMuFL code points), staff/metric coordinates, stem/beam/flag/tie/slur/tuplet/hairpin/pedal paths, and clip regions as abstract geometry commands. Defines abstract glyph-metrics interface (bounding boxes, advance widths, kerning); concrete implementation lives in `graphscore_rendering`. No FreeType, no HarfBuzz, no ThorVG, no GPU API, no platform windowing. |

#### Layer 7 — Rendering Backend (Writer-Only)

| Target | Kind | Public Dependencies | Private Dependencies | Responsibility |
|--------|------|---------------------|-----------------------|----------------|
| `graphscore_rendering` | STATIC | `graphscore_notation`, `graphscore_core` | _(none)_ | Backend rasterization and text shaping. Implements abstract glyph-metrics interface via FreeType, text shaping via HarfBuzz, and vector rendering via either ThorVG adapter or owned render-list/tessellation fallback. All third-party rendering types (FreeType `FT_Face`, HarfBuzz `hb_font_t`, ThorVG `tvg::Canvas`) are **private to implementation files** and never appear in public headers. ThorVG is PROVISIONAL (ADR 0002 §3a); owned render-list/tessellation is the explicit Phase C fallback (§3b). Public interface exposes only GraphScore-owned types. Platform windowing (SDL3 GPU surface, native windows) is NOT here — that belongs to `graphscore_writer_shell`. |

#### Layer 8 — Accessibility & Canvas (Writer-Only)

| Target | Kind | Public Dependencies | Private Dependencies | Responsibility |
|--------|------|---------------------|-----------------------|----------------|
| `graphscore_accessibility` | STATIC | `graphscore_domain`, `graphscore_notation`, `graphscore_core` | _(none)_ | GraphScore-owned semantic accessibility interface: tree nodes (node, track, staff, measure, voice, note/rest, marking, connector, palette, transport), roles, names, values, states, actions, focus management, and virtualized offscreen-element exposure. Agnostic to the platform bridge choice — defines only GraphScore-owned types. |
| `graphscore_canvas` | STATIC | `graphscore_domain`, `graphscore_notation`, `graphscore_core`, `graphscore_accessibility` | _(none)_ | Infinite graph canvas: sparse spatial index, viewport transforms (pan, zoom), notation node placement and resizing, orthogonal connector route computation/editing (straight/Manhattan, obstacle avoidance, rounded corners, segment dragging, custom-route preservation), hit testing at ports/paths/segments/action circles, node copy/paste/delete with UUID remapping, search-by-name/UUID. Depends on `graphscore_notation` for node content layout; does NOT depend on `graphscore_rendering` (render commands are produced here, consumed by rendering at shell assembly time). |

Within-layer dependency: `graphscore_canvas` → `graphscore_accessibility` is explicitly permitted.

#### Layer 9 — Writer Platform (Writer-Only)

| Target | Kind | Public Dependencies | Private Dependencies | Responsibility |
|--------|------|---------------------|-----------------------|----------------|
| `graphscore_writer_shell` | STATIC | `graphscore_core`, `graphscore_canvas`, `graphscore_notation`, `graphscore_rendering`, `graphscore_accessibility` | _(none)_ | Platform shell via SDL3 (PROVISIONAL, ADR 0002 §1): native window creation, input event collection (keyboard, mouse, trackpad, touch), clipboard, drag-and-drop, high-DPI support, event-loop integration. Wires canvas/notation render commands through rendering backend to display surfaces. Does NOT own audio or MIDI devices. |
| `graphscore_audio_device` | STATIC | `graphscore_core` | _(none)_ | Audio device abstraction: enumeration, selection, sample rate/block size configuration, callback-driven I/O. Wraps miniaudio (PROVISIONAL, ADR 0002 §6a) with PortAudio fallback (§6b). Platform device types and third-party audio types are private to implementation. |
| `graphscore_midi_io` | STATIC | `graphscore_core` | _(none)_ | External MIDI port I/O: enumeration, port open/close, send/receive. Wraps RtMidi (PROVISIONAL, ADR 0002 §7a). Not used by runtime (runtime produces in-memory MIDI events via scheduler). |
| `graphscore_vst3_host` | STATIC | `graphscore_domain`, `graphscore_audio_device`, `graphscore_core` | _(none)_ | VST3 SDK integration (DEFERRED to M0 Phase D, ADR 0002 §8): plugin factory, component/controller lifecycle, audio/MIDI processing, opaque state save/restore, latency/tail queries, native editor parent-handle attachment. All VST3 SDK types are private. Depends on `graphscore_domain` only for plugin identity model and opaque-state storage identifiers — not for notation or graph types. |
| `graphscore_writer_audio` | STATIC | `graphscore_core`, `graphscore_domain`, `graphscore_scheduler`, `graphscore_audio_device`, `graphscore_vst3_host`, `graphscore_compiler` | _(none)_ | Writer audio engine: track plugin chains (one instrument + ordered effects per track), audition mixer (mute, solo, gain, pan, metering), latency compensation, panic action, metronome/count-in, note-preview insertion audition, transport state machine (play, pause, stop, node play, loop). Uses `graphscore_compiler` to build immutable compact scheduler snapshots off the audio thread and publishes atomically at block boundaries. Runs the shared `graphscore_scheduler` from the audio callback consuming published snapshots. |
| `graphscore_accessibility_platform` | STATIC | `graphscore_accessibility` | _(none in M1; deferred to Phase C)_ | **M1 placeholder/facade.** In M1 this target compiles as an owned STATIC library with zero private external bridge edges. Its backend is a no-op/unavailable implementation sufficient to pass compile and machine audit. After Phase C records an implementation selection, it will depend privately on exactly one of: AccessKit (accesskit-c, EXCLUDED from default, ADR 0002 §2) or native OS accessibility services (NSAccessibility on macOS, UI Automation on Windows, AT-SPI2 on Linux). Native OS frameworks/services are system-provided and are not vendored third-party CMake targets. All third-party/OS accessibility types are confined to `.cpp` files. No leakage into clean layers. Linked only by `graphscore_writer_app` at assembly time. |

Within-layer dependencies explicitly permitted:
- `graphscore_vst3_host` → `graphscore_audio_device`
- `graphscore_writer_audio` → `graphscore_audio_device`
- `graphscore_writer_audio` → `graphscore_vst3_host`

#### Layer 10 — Writer Application Assembly (Writer-Only)

| Target | Kind | Public Dependencies | Private Dependencies | Responsibility |
|--------|------|---------------------|-----------------------|----------------|
| `graphscore_plugin_scanner_client` | STATIC | `graphscore_core`, `graphscore_domain` | `graphscore_writer_shell` | In-process scanner client and orchestration: owns launching the scanner helper executable, timeout/kill enforcement, crash and hang detection, protocol validation (IPC/file-based), result parsing and normalization, blacklist management. Communicates with the separate `graphscore_plugin_scanner` executable via IPC without linking it. Depends privately on `graphscore_writer_shell` only for platform process-launch abstraction (never linking the scanner executable or linking VST3 SDK types). |
| `graphscore_writer_app` | EXECUTABLE | `graphscore_writer_shell`, `graphscore_writer_audio`, `graphscore_plugin_scanner_client`, `graphscore_canvas`, `graphscore_rendering`, `graphscore_notation`, `graphscore_accessibility`, `graphscore_accessibility_platform`, `graphscore_persistence`, `graphscore_compiler`, `graphscore_domain`, `graphscore_scheduler`, `graphscore_core` | _(none)_ | Writer application entry point, application-level wiring (assemble shell+audio+canvas+rendering+accessibility+persistence), document lifecycle (new/open/save/save-as/close/recent), project-window management, preference persistence, export UI, event simulator. All platform, UI, and plugin dependencies are reachable only through this target and its direct dependencies — no lower-layer target may reach them. Does NOT link `graphscore_plugin_scanner` (separate executable) nor any VST3 SDK types directly; plugin hosting is through `graphscore_vst3_host` and scanning orchestration through `graphscore_plugin_scanner_client`. |

Within-layer dependency: `graphscore_writer_app` → `graphscore_plugin_scanner_client` is explicitly permitted.

#### Layer 11 — Plugin Scanner Executable (Writer-Only)

| Target | Kind | Public Dependencies | Private Dependencies | Responsibility |
|--------|------|---------------------|-----------------------|----------------|
| `graphscore_plugin_scanner` | EXECUTABLE | `graphscore_core` | `graphscore_vst3_host` | Standalone helper process. Scans one VST3 candidate at a time with timeout, crash/hang containment, metadata normalization, and blacklist output. Depends privately on `graphscore_vst3_host` for VST3 SDK access. Never linked into writer or runtime process — always launched as a separate OS process by `graphscore_plugin_scanner_client`. Communicates results via IPC/file to the client. |

#### Engine Wrappers (External Consumers, Not GraphScore CMake Targets)

| Consumer | Relationship | Dependency | Notes |
|----------|-------------|------------|-------|
| Unity integration | C# P/Invoke facade | `graphscore_runtime` shared library + `graphscore_c_abi` header | Dynamic load (`[DllImport]`). No compile-time dependency on any GraphScore CMake target. Managed allocation avoided in audio callback after init. |
| Unreal integration | Native C++ plugin | `graphscore_runtime` shared library + `graphscore_c_abi` header | Dynamic link (normal platform linker). No UObject/game-thread access from realtime callback. |

Both engine wrappers consume only the C ABI. Neither includes, links, or depends on any writer target.

---

### 2. Permitted-Edge Matrix

Only explicitly listed edges are permitted. Any dependency not listed is
forbidden. "PUBLIC" means the dependency appears in public headers of the
depender. "PRIVATE" means the dependency is confined to `.cpp` implementation
files and is not exposed through public headers.

#### 2.1 Complete Permitted Dependency List

| Target | Permitted PUBLIC Dependencies | Permitted PRIVATE Dependencies | Same-Layer Permission |
|---|---|---|---|
| `graphscore_core` | _(none)_ | _(none)_ | — |
| `graphscore_c_abi` | _(none)_ | _(none)_ | — |
| `graphscore_domain` | `graphscore_core` | _(none)_ | — |
| `graphscore_cooked_format` | `graphscore_core` | _(none)_ | — |
| `graphscore_compiler` | `graphscore_domain`, `graphscore_cooked_format` | _(none)_ | Same-layer to `cooked_format` permitted |
| `graphscore_persistence` | `graphscore_domain`, `graphscore_cooked_format`, `graphscore_compiler` | _(none)_ | — |
| `graphscore_scheduler` | `graphscore_core`, `graphscore_cooked_format` | _(none)_ | — |
| `graphscore_loader` | `graphscore_core`, `graphscore_c_abi`, `graphscore_cooked_format` | _(none)_ | — |
| `graphscore_runtime_impl` | `graphscore_core`, `graphscore_c_abi`, `graphscore_scheduler`, `graphscore_loader` | _(none)_ | Same-layer to `loader` permitted |
| `graphscore_runtime` | `graphscore_c_abi` | `graphscore_runtime_impl` | Same-layer to `runtime_impl` permitted |
| `graphscore_notation` | `graphscore_domain`, `graphscore_core` | _(none)_ | — |
| `graphscore_rendering` | `graphscore_notation`, `graphscore_core` | _(none)_ | — |
| `graphscore_accessibility` | `graphscore_domain`, `graphscore_notation`, `graphscore_core` | _(none)_ | — |
| `graphscore_canvas` | `graphscore_domain`, `graphscore_notation`, `graphscore_core`, `graphscore_accessibility` | _(none)_ | Same-layer to `accessibility` permitted |
| `graphscore_writer_shell` | `graphscore_core`, `graphscore_canvas`, `graphscore_notation`, `graphscore_rendering`, `graphscore_accessibility` | _(none)_ | — |
| `graphscore_audio_device` | `graphscore_core` | _(none)_ | — |
| `graphscore_midi_io` | `graphscore_core` | _(none)_ | — |
| `graphscore_vst3_host` | `graphscore_domain`, `graphscore_audio_device`, `graphscore_core` | _(none)_ | Same-layer to `audio_device` permitted |
| `graphscore_writer_audio` | `graphscore_core`, `graphscore_domain`, `graphscore_scheduler`, `graphscore_audio_device`, `graphscore_vst3_host`, `graphscore_compiler` | _(none)_ | Same-layer to `audio_device`, `vst3_host` permitted |
| `graphscore_accessibility_platform` | `graphscore_accessibility` | _(none in M1; Phase C adds one private bridge edge)_ | — |
| `graphscore_plugin_scanner_client` | `graphscore_core`, `graphscore_domain` | `graphscore_writer_shell` | — |
| `graphscore_writer_app` | `graphscore_writer_shell`, `graphscore_writer_audio`, `graphscore_plugin_scanner_client`, `graphscore_canvas`, `graphscore_rendering`, `graphscore_notation`, `graphscore_accessibility`, `graphscore_accessibility_platform`, `graphscore_persistence`, `graphscore_compiler`, `graphscore_domain`, `graphscore_scheduler`, `graphscore_core` | _(none)_ | Same-layer to `plugin_scanner_client` permitted |
| `graphscore_plugin_scanner` | `graphscore_core` | `graphscore_vst3_host` | — |

#### 2.2 External Private Edge Table

The following table maps every permitted external third-party dependency to the
exact GraphScore target that privately consumes it. External types must never
appear in any GraphScore-owned public header. Every edge is PRIVATE (confined to
`.cpp` implementation files) and reflects the status recorded in ADR 0002.

| GraphScore Target | External Private Dependency | ADR 0002 Status | Fallback / Alternative | Private-Confined Types |
|---|---|---|---|---|
| `graphscore_rendering` | FreeType | POLICY-CLEARED (§5) | — | `FT_Face`, `FT_Library`, all FreeType types |
| `graphscore_rendering` | HarfBuzz | PROVISIONAL (§4) | — | `hb_font_t`, `hb_buffer_t`, all HarfBuzz types |
| `graphscore_rendering` | ThorVG | PROVISIONAL, excluded from default (§3a) | Owned render-list/tessellation (§3b) | `tvg::Canvas`, `tvg::Shape`, all ThorVG types |
| `graphscore_writer_shell` | SDL3 | PROVISIONAL (§1) | — | `SDL_Window`, `SDL_Renderer`, all SDL3 types |
| `graphscore_audio_device` | miniaudio | PROVISIONAL (§6a) | PortAudio (§6b) | `ma_device`, `ma_context`, all `ma_*` types |
| `graphscore_audio_device` | PortAudio | PROVISIONAL fallback (§6b) | Activated only if miniaudio fails Phase C gate | `PaStream`, all `Pa*` types |
| `graphscore_midi_io` | RtMidi | PROVISIONAL (§7a) | — | `RtMidiIn`, `RtMidiOut`, all RtMidi types |
| `graphscore_vst3_host` | VST3 SDK | DEFERRED (§8) | — | `Steinberg::IPluginFactory`, `Steinberg::Vst::IComponent`, all VST3/Steinberg types |
| `graphscore_accessibility_platform` | _(none in M1; after Phase C selection:) AccessKit (accesskit-c) or native OS accessibility services | M1: zero external edges. Phase C: EXCLUDED (AccessKit, §2) or native OS (§2) | Native OS accessibility services: NSAccessibility (macOS), UI Automation (Windows), AT-SPI2 (Linux) | M1: no private-confined types. Phase C: `accesskit_node`, `accesskit_tree_update` (if AccessKit chosen); or OS-specific types (if native chosen) |

No external type is ever PUBLIC in a GraphScore target. The type-leakage audit
in §7.3 mechanically enforces this.

#### 2.3 Test Framework Edge (DEFERRED to M1)

ADR 0002 does not contain a GTest pin or evaluation. No current production
GraphScore target has a `GTest` edge. M1 must:

1. Pin an exact GTest SHA, evaluate its license under the ADR 0001 checklist,
   and record the decision in a dependency ADR or ADR 0002 amendment.
2. Define exact named test-executable CMake targets (e.g. `graphscore_core_test`,
   `graphscore_domain_test`, etc.) with their precise permitted edges.
3. Amend §2.1 and §2.2 of this ADR to add each test target's internal and
   external permitted edges, then regenerate the machine audit allowlists.

Until M1 completes these steps, GTest is **DEFERRED** and no test target is in
the produced DAG. The machine audit scripts (§7) must reject any undeclared
`GTest` linkage as a violation.

---

### 3. Forbidden-Edge / Transitive-Closure Proof

#### 3.1 Runtime Closure — Guaranteed Isolation

For every target reachable from `graphscore_runtime` via `target_link_libraries`
(recursively, public and private), the following writer-only targets must never
appear:

| Forbidden Target | Reason |
|---|---|
| `graphscore_domain` | Domain model is forbidden and absent from the runtime closure. `graphscore_scheduler` operates only on cooked-format data, never on domain types. `graphscore_runtime` has zero compile-time or link-time dependency on `graphscore_domain`. |
| `graphscore_compiler` | Domain compilation belongs to writer |
| `graphscore_persistence` | Persistence belongs to writer |
| `graphscore_notation` | Notation layout (writer domain, though toolkit-neutral) |
| `graphscore_rendering` | Contains FreeType, HarfBuzz, ThorVG/owned-renderer |
| `graphscore_accessibility` | Semantic accessibility interface |
| `graphscore_accessibility_platform` | Platform accessibility bridge (writer-only) |
| `graphscore_canvas` | Graph canvas, viewport, connector editing |
| `graphscore_writer_shell` | SDL3 platform shell (windowing, input) |
| `graphscore_audio_device` | miniaudio/PortAudio wrapper |
| `graphscore_midi_io` | RtMidi wrapper |
| `graphscore_vst3_host` | VST3 SDK, plugin hosting |
| `graphscore_writer_audio` | Writer audio engine, mixer, transport |
| `graphscore_plugin_scanner_client` | Scanner client/orchestration |
| `graphscore_writer_app` | Writer application |
| `graphscore_plugin_scanner` | Plugin scanner helper |

**Proof by reachability** (target → direct deps):

```
graphscore_runtime
├── graphscore_runtime_impl
│   ├── graphscore_core
│   ├── graphscore_scheduler
│   │   ├── graphscore_core
│   │   └── graphscore_cooked_format
│   │       └── graphscore_core
│   ├── graphscore_loader
│   │   ├── graphscore_core
│   │   ├── graphscore_c_abi
│   │   └── graphscore_cooked_format
│   └── graphscore_c_abi
└── graphscore_c_abi
```

**Downstream transitive closure** from `graphscore_runtime` (6 targets, excluding the root):
`{graphscore_runtime_impl, graphscore_core, graphscore_scheduler,
  graphscore_cooked_format, graphscore_loader, graphscore_c_abi}`.

None of the forbidden targets appear. ✓

#### 3.2 Clean-Target Closure — Guaranteed Isolation

| Target | Forbidden To Depend On |
|---|---|
| `graphscore_domain` | All targets in layers 6-11, plus `graphscore_persistence`, `graphscore_compiler`, `graphscore_cooked_format`, `graphscore_c_abi`, `graphscore_loader`, `graphscore_runtime_impl`, `graphscore_runtime` |
| `graphscore_cooked_format` | All targets in layers 6-11, plus `graphscore_domain`, `graphscore_compiler`, `graphscore_persistence`, `graphscore_scheduler`, `graphscore_loader`, `graphscore_runtime_impl`, `graphscore_runtime`, `graphscore_c_abi` |
| `graphscore_compiler` | All targets in layers 6-11, plus `graphscore_persistence`, `graphscore_scheduler`, `graphscore_loader`, `graphscore_runtime_impl`, `graphscore_runtime`, `graphscore_c_abi` |
| `graphscore_scheduler` | All targets in layers 6-11, plus `graphscore_domain`, `graphscore_compiler`, `graphscore_persistence`, `graphscore_loader`, `graphscore_runtime_impl`, `graphscore_runtime`, `graphscore_c_abi` |
| `graphscore_persistence` | All targets in layers 6-11, plus `graphscore_scheduler`, `graphscore_loader`, `graphscore_runtime_impl`, `graphscore_runtime`, `graphscore_c_abi` |

All five closures are clean of UI, audio-device, VST3 SDK, platform shell,
accessibility bridge, rendering backend, and scanner. ✓

#### 3.3 UI/Audio-Device/VST3 Clean Guarantee

The targets `graphscore_domain`, `graphscore_compiler`, `graphscore_scheduler`,
`graphscore_persistence`, `graphscore_cooked_format`, `graphscore_loader`,
`graphscore_runtime_impl`, `graphscore_runtime`, `graphscore_c_abi`, and
`graphscore_core` are guaranteed to build without link-time or compile-time
dependency on:

- Any windowing/input library (SDL3, Cocoa, Win32, X11, Wayland)
- Any audio device library (miniaudio, PortAudio, CoreAudio, WASAPI, ALSA, PulseAudio)
- Any VST3 SDK header or library
- Any accessibility platform bridge (AccessKit, NSAccessibility, UI Automation, AT-SPI2)
- Any GPU/rendering API (Metal, Vulkan, OpenGL, Direct3D, SDL3 GPU)
- Any text shaping or font rasterization library (HarfBuzz, FreeType, CoreText, DirectWrite)
- Any vector rendering library (ThorVG)
- `graphscore_plugin_scanner`
- `graphscore_plugin_scanner_client`
- `graphscore_writer_shell`, `graphscore_writer_audio`, `graphscore_writer_app`

`graphscore_persistence` may store opaque plugin state as raw bytes
(`std::vector<std::byte>`) and owned plugin identifiers (`std::string`), but may
never `#include` a VST3 header or link a VST3 library. ✓

---

### 4. Key Architecture Decisions

#### 4.1 Scheduler Sharing

One GraphScore-owned deterministic scheduler implementation
(`graphscore_scheduler`) is consumed by:

- **Writer audition**: `graphscore_writer_audio` uses `graphscore_compiler` to
  build immutable compact scheduler snapshots from the domain model off the
  audio thread, then publishes them atomically at block boundaries. The scheduler
  operates on cooked/compact input structures — it does not accept domain model
  types directly.
- **Runtime**: `graphscore_runtime_impl` depends on `graphscore_scheduler`.
  The runtime loads pre-cooked assets via `graphscore_loader` and feeds them
  directly to the scheduler.

The scheduler is a single implementation, not duplicated. Golden vectors
(static precomputed MIDI traces for representative assets) independently verify
semantics: both writer and runtime tests compare their output against the same
golden vectors to prove behavioral equivalence.

#### 4.2 Compilation Off the Realtime Path

`graphscore_compiler` validates domain projects and compiles them into immutable
compact scheduler inputs. This is a potentially expensive operation (validation,
UUID→index resolution, schedule precomputation, capacity analysis) and must
never run from the audio callback. Two consumption paths:

- **Export**: `graphscore_persistence` invokes the compiler during user-requested
  export to produce a cooked asset for runtime consumption.
- **Live snapshot publication**: `graphscore_writer_audio` invokes the compiler
  off-thread after domain edits, then atomically publishes the resulting compact
  snapshot for the scheduler. The audio callback only reads the published
  snapshot; compilation never blocks the realtime path.

`graphscore_compiler` depends only on `graphscore_domain` and
`graphscore_cooked_format`. It is transitively free of UI, audio-device, VST3,
and all writer-only types.

#### 4.3 C ABI Ownership and Type Rules

`graphscore_c_abi` is a pure C header with the following hard rules:

- All types are C99-compatible: `struct`, `union`, `enum`, `typedef`, and
  integer/pointer types.
- No `class`, `template`, `namespace`, `constexpr`, `noexcept`, `std::*`, or
  C++ linkage.
- Handles are opaque: `typedef struct gs_asset_s gs_asset_t;` — the struct is
  never defined in the header.
- All integer types are fixed-width (`uint32_t`, `uint64_t`, `int32_t`).
  `size_t` is never used in the C ABI.
- All structs carry an explicit `uint32_t size` field as their first member and a
  `uint32_t version` field as the second member for forward-compatible extension.
- Allocator callbacks use explicit `uint64_t` for byte counts and alignment:
  `typedef void* (*gs_alloc_fn)(void* context, uint64_t size_bytes, uint64_t alignment);`
  Internal C++ implementations must use checked narrowing conversion to
  `size_t` before forwarding to platform allocators, or document paths where
  the managed size is guaranteed to fit within the platform `size_t` range.
- No exception propagation: all functions return `gs_result_t` (error code enum).
- No platform headers (`<windows.h>`, `<CoreFoundation/...>`, `<xcb/...>`).
- No third-party types.
- Exported symbols are explicitly annotated with `GS_API` and all other symbols
  are hidden (compiler `-fvisibility=hidden` or equivalent).

#### 4.4 Process Boundaries

- **Plugin scanner**: `graphscore_plugin_scanner` is a standalone executable,
  never linked into the writer process. `graphscore_plugin_scanner_client` owns
  launching the helper, timeout/kill enforcement, crash/hang detection, protocol
  validation, and result consumption. The client depends privately on
  `graphscore_writer_shell` for platform process-launch abstraction but never
  links the `graphscore_plugin_scanner` executable or VST3 SDK types. A scanner
  crash or hang must not take down the writer.

  `graphscore_plugin_scanner` has zero direct external edges in §2.2. Its only
  direct GraphScore edges are `graphscore_core` (PUBLIC) and
  `graphscore_vst3_host` (PRIVATE). VST3 SDK types are reached **transitively**
  through the private `graphscore_vst3_host` link. The scanner must not declare a
  direct `VST3 SDK` target dependency; the M1 direct-edge audit (§7.1) must
  reject any such edge.
- **VST3 hosting**: Occurs only in the writer process through
  `graphscore_vst3_host`. Runtime has no plugin loading, scanning, or hosting
  capability.
- **Installed plugins**: VST3 plugins installed on the host system are
  discovered and loaded at runtime by the writer. They are not vendored, not
  FetchContent-fetched, and not part of the build system.
- **Unity/Unreal wrappers**: Consume only `graphscore_runtime` shared library
  via the C ABI. Unity uses P/Invoke (`[DllImport]`) for dynamic load. Unreal
  uses normal platform dynamic linking. Neither wrapper links any GraphScore
  CMake target other than the runtime shared library; the C ABI header is
  vendored as a source file in the wrapper package, not as a CMake dependency.
  Neither wrapper includes an audio renderer or plugin host.

#### 4.5 Accessibility

`graphscore_accessibility` defines a GraphScore-owned semantic interface:
abstract tree nodes with roles, names, values, states, and actions. It is agnostic
to the eventual platform bridge choice.

`graphscore_accessibility_platform` (Layer 9) is a writer-only STATIC target
that:

- In M1 compiles as an owned placeholder/facade with **zero private external
  bridge edges**. Its backend is a no-op/unavailable implementation sufficient
  to pass compile and the machine audit.
- Depends publicly on `graphscore_accessibility` (GraphScore-owned types only).
- After Phase C records the bridge selection, depends privately on exactly one
  of: AccessKit (accesskit-c, EXCLUDED, ADR 0002 §2) or native OS accessibility
  services (NSAccessibility on macOS, UI Automation on Windows, AT-SPI2 on
  Linux). Native OS frameworks/services are system-provided and are not vendored
  third-party CMake targets — the machine audit must not require a vendored
  target entry for them.
- Is linked only by `graphscore_writer_app` at assembly time.
- Never leaks third-party or OS accessibility types into `graphscore_accessibility`
  or any lower-layer target.

The semantic interface target exists now. The platform bridge target is defined
as a compileable placeholder in M1 and receives its private bridge edge in
Phase C.

#### 4.6 Rendering Target Structure

`graphscore_notation` produces abstract render commands ("draw glyph G at
position (x,y) with scale s", "draw filled path P with color C", "set clip
region R"). It depends on an abstract glyph-metrics interface for layout
computation but does not import FreeType, HarfBuzz, or ThorVG headers.

`graphscore_rendering` implements the glyph-metrics interface using FreeType,
text shaping using HarfBuzz, and rasterization using either:

- **ThorVG** (PROVISIONAL, ADR 0002 §3a): requires Meson/Ninja build-tool
  pinning and a CMake adapter. If ThorVG meets all Phase C gates, it is the
  primary backend.
- **Owned render-list/tessellation** (explicitly adopted fallback, ADR 0002
  §3b): GraphScore-owned code that tessellates path commands into triangle
  meshes and emits vertex/index buffers. No third-party renderer dependency.

Both paths are optionally compiled behind CMake options
(`GRAPHSCORE_RENDER_THORVG=ON|OFF`). Neither leaks into `graphscore_notation`,
`graphscore_domain`, or `graphscore_runtime`.

#### 4.7 Audio/MIDI Target Structure

Consistent with ADR 0002 statuses:

| Target | ADR 0002 Status | Notes |
|---|---|---|
| `graphscore_audio_device` | PROVISIONAL (miniaudio primary, PortAudio fallback) | Wraps the selected backend behind a GraphScore-owned abstraction |
| `graphscore_midi_io` | PROVISIONAL (RtMidi) | External MIDI port I/O only; not used by runtime |
| `graphscore_vst3_host` | DEFERRED (VST3 SDK, Phase D) | Writer-only plugin hosting |

All three are writer-only. `graphscore_audio_device` and `graphscore_midi_io`
depend only on `graphscore_core` and the third-party library — they have no
dependency on domain, notation, or canvas. `graphscore_vst3_host` depends on
`graphscore_domain` only for the plugin identity model and opaque-state
identifiers (not for notation or graph types).

#### 4.8 Responsibility Ownership Audit

Every Phase B plan area has an owning target:

| Responsibility | Owning Target(s) |
|---|---|
| Core value types (UUID, rational, MIDI, clef, key) | `graphscore_core` |
| Project, track, node, notation, graph, event models | `graphscore_domain` |
| Reversible commands, transaction grouping | `graphscore_domain` |
| Selection model, clipboard model | `graphscore_domain` |
| Validation service (incremental + complete) | `graphscore_domain` |
| Normative playback semantics specification | `graphscore_domain` |
| Opaque plugin identity and state-byte types | `graphscore_domain` |
| Cooked binary format definitions | `graphscore_cooked_format` |
| Domain→compact compilation, export validation, snapshot building | `graphscore_compiler` |
| Editable project persistence, autosave, schema migration | `graphscore_persistence` |
| Deterministic MIDI scheduler, tempo integration, pickdown logic, PRNG | `graphscore_scheduler` |
| Cooked-asset loader (memory span, host allocators) | `graphscore_loader` |
| C ABI header, opaque handles, versioned structs | `graphscore_c_abi` |
| Runtime processing engine, capacity, diagnostics | `graphscore_runtime_impl` |
| Runtime shared library, symbol exports | `graphscore_runtime` |
| Toolkit-neutral notation layout, hit testing, render commands | `graphscore_notation` |
| Glyph rasterization (FreeType), text shaping (HarfBuzz), vector rendering (ThorVG/owned) | `graphscore_rendering` |
| Semantic accessibility tree interface | `graphscore_accessibility` |
| Platform accessibility bridge (AccessKit or native OS) | `graphscore_accessibility_platform` |
| Infinite graph canvas, viewport, connector routing, node editing | `graphscore_canvas` |
| Platform shell (SDL3), windowing, input, clipboard, DPI | `graphscore_writer_shell` |
| Audio device abstraction (miniaudio/PortAudio) | `graphscore_audio_device` |
| External MIDI I/O (RtMidi) | `graphscore_midi_io` |
| VST3 plugin hosting, audio/MIDI processing, native editors | `graphscore_vst3_host` |
| Writer audio engine, mixer, transport, snapshot publication | `graphscore_writer_audio` |
| Scanner client, helper process orchestration | `graphscore_plugin_scanner_client` |
| Scanner helper executable (standalone) | `graphscore_plugin_scanner` |
| Application entry point, cross-cutting assembly, document lifecycle | `graphscore_writer_app` |
| Game engine wrappers (Unity, Unreal) | External consumers of `graphscore_runtime` via C ABI |

---

### 5. Direct-Edge Adjacency List

The permitted-edge matrices in §2 are the **canonical, machine-auditable
specification**. This section reproduces every direct GraphScore-internal edge
from §2.1 as a concise adjacency list. No edge shown here is missing from §2.1;
no edge in §2.1 is missing here. External (third-party) edges are in §2.2.

```
graphscore_core            (no deps)
graphscore_c_abi           (no deps)

graphscore_domain          → graphscore_core

graphscore_cooked_format   → graphscore_core
graphscore_compiler        → graphscore_domain, graphscore_cooked_format

graphscore_persistence     → graphscore_domain, graphscore_cooked_format, graphscore_compiler

graphscore_scheduler       → graphscore_core, graphscore_cooked_format

graphscore_loader          → graphscore_core, graphscore_c_abi, graphscore_cooked_format
graphscore_runtime_impl    → graphscore_core, graphscore_c_abi, graphscore_scheduler, graphscore_loader
graphscore_runtime         → graphscore_c_abi (PUBLIC), graphscore_runtime_impl (PRIVATE)

graphscore_notation        → graphscore_domain, graphscore_core

graphscore_rendering       → graphscore_notation, graphscore_core

graphscore_accessibility   → graphscore_domain, graphscore_notation, graphscore_core
graphscore_canvas          → graphscore_domain, graphscore_notation, graphscore_core, graphscore_accessibility

graphscore_writer_shell    → graphscore_core, graphscore_canvas, graphscore_notation,
                             graphscore_rendering, graphscore_accessibility
graphscore_audio_device    → graphscore_core
graphscore_midi_io         → graphscore_core
graphscore_vst3_host       → graphscore_domain, graphscore_audio_device, graphscore_core
graphscore_writer_audio    → graphscore_core, graphscore_domain, graphscore_scheduler,
                             graphscore_audio_device, graphscore_vst3_host, graphscore_compiler
graphscore_accessibility_platform
                           → graphscore_accessibility (M1: no private edges)

graphscore_plugin_scanner_client
                           → graphscore_core, graphscore_domain (PUBLIC),
                             graphscore_writer_shell (PRIVATE)
graphscore_writer_app      → graphscore_writer_shell, graphscore_writer_audio,
                             graphscore_plugin_scanner_client, graphscore_canvas,
                             graphscore_rendering, graphscore_notation,
                             graphscore_accessibility, graphscore_accessibility_platform,
                             graphscore_persistence, graphscore_compiler,
                             graphscore_domain, graphscore_scheduler, graphscore_core

graphscore_plugin_scanner  → graphscore_core (PUBLIC), graphscore_vst3_host (PRIVATE)
```

**Runtime closure** (targets transitively reachable from `graphscore_runtime`):

```
graphscore_runtime → graphscore_runtime_impl → graphscore_loader
                   → graphscore_scheduler → graphscore_cooked_format → graphscore_core
                   → graphscore_c_abi
```

**Root-inclusive closure**: 7 targets (6 downstream +
`graphscore_runtime` root). Zero edges into layers 6-11. The runtime has no
link or include dependency on domain, compiler, persistence, notation,
rendering, or any writer-only target.

**Layer inventory** (from §1, for reference):

| Layer | Targets |
|-------|---------|
| 0 | `graphscore_core`, `graphscore_c_abi` |
| 1 | `graphscore_domain` |
| 2 | `graphscore_cooked_format`, `graphscore_compiler` |
| 3 | `graphscore_persistence` |
| 4 | `graphscore_scheduler` |
| 5 | `graphscore_loader`, `graphscore_runtime_impl`, `graphscore_runtime` |
| 6 | `graphscore_notation` |
| 7 | `graphscore_rendering` |
| 8 | `graphscore_accessibility`, `graphscore_canvas` |
| 9 | `graphscore_writer_shell`, `graphscore_audio_device`, `graphscore_midi_io`, `graphscore_vst3_host`, `graphscore_writer_audio`, `graphscore_accessibility_platform` |
| 10 | `graphscore_plugin_scanner_client`, `graphscore_writer_app` |
| 11 | `graphscore_plugin_scanner` |

---

### 6. Phase B Plan Area Mapping

| Phase B Plan Item | Primary Targets |
|---|---|
| 1. Domain and command model | `graphscore_core` (value types), `graphscore_domain` (identity, project/track/node/notation/graph/event models, commands, selection, clipboard, validation, playback semantics specification) |
| 2. Notation layout, hit testing, toolkit-neutral drawing commands | `graphscore_notation` (abstract layout, hit testing, render-command production), `graphscore_rendering` (concrete glyph metrics via FreeType, text shaping via HarfBuzz, rasterization via ThorVG or owned fallback) |
| 3. Adaptive playback scheduler and MIDI model | `graphscore_scheduler` (single deterministic implementation, tempo integration, MIDI output, event/transition/pickdown logic), `graphscore_domain` (normative playback specification) |
| 4. Runtime C ABI and cooked-asset loader | `graphscore_c_abi` (pure C header), `graphscore_cooked_format` (binary format definitions), `graphscore_compiler` (domain→compact compilation), `graphscore_loader` (load from memory span), `graphscore_runtime_impl` (processing engine), `graphscore_runtime` (shared library) |
| 5. Writer platform shell, accessibility bridge, audio devices, VST3 adapter | `graphscore_writer_shell` (SDL3), `graphscore_accessibility` (semantic interface), `graphscore_accessibility_platform` (platform bridge, Phase C), `graphscore_audio_device` (miniaudio/PortAudio), `graphscore_midi_io` (RtMidi), `graphscore_vst3_host` (VST3 SDK), `graphscore_writer_audio` (mixer/transport), `graphscore_writer_app` (application) |
| 6. Plugin scanner helper and game-engine wrappers | `graphscore_plugin_scanner_client` (scanner orchestration), `graphscore_plugin_scanner` (standalone helper process); Unity/Unreal wrappers (external, consume `graphscore_runtime` via C ABI) |

---

### 7. Mechanically Checkable Architecture Enforcement (M1 Implementation)

The following enforcement mechanisms are specified now. File paths and scripts
are M1 implementation work — they do not exist yet and must be created during
M1. All paths are relative to the repository root.

#### 7.1 Recursive CMake Link-Closure Audit

`cmake/audit_link_closure.cmake` — a CMake script that:

1. Defines the set of forbidden targets per audited target (as specified in
   §3.2 and §3.3 of this ADR).
2. For each audited target, recursively walks **both** `INTERFACE_LINK_LIBRARIES`
   and `LINK_LIBRARIES` properties (the full effective link closure, including
   transitive links inherited through public and private dependencies).
3. Reports any forbidden target or undeclared internal GraphScore target in the
   transitive closure as a `message(FATAL_ERROR ...)`.

Additionally, `cmake/audit_permitted_edges.cmake` performs the reverse check:

1. Defines the complete permitted internal edge set from §2.1 of this ADR and
   the complete permitted external edge set from §2.2.
2. Walks every GraphScore-owned target's direct `LINK_LIBRARIES` and
   `INTERFACE_LINK_LIBRARIES`.
3. Rejects any dependency edge — internal or external — that is not explicitly
   listed in the permitted sets. Every `target_link_libraries` call in the
   project must match the declared edges or the audit fails.

**M1 audit accommodations:**
- `graphscore_accessibility_platform` has zero external edges in M1; the audit
  must pass with no private bridge linkage.
- After Phase C, exactly one private bridge edge is permitted. Native OS
  accessibility frameworks/services (NSAccessibility, UI Automation, AT-SPI2)
  are system-provided services, not vendored CMake targets; the audit allowlist
  must treat them as permitted without requiring a vendored target name.
- Test targets are DEFERRED to M1 (§2.3). Until M1 defines named test targets
  and amends this ADR, the audit must reject any undeclared `GTest` linkage.

Invoked as a custom CMake target `audit_architecture` that runs after
configuration.

#### 7.2 Protected-Target Transitive Closure Checks

`cmake/audit_transitive_closure.cmake` — a CMake script that:

1. For each protected target listed in §3.3, recursively collects the full
   transitive link closure from both `INTERFACE_LINK_LIBRARIES` and
   `LINK_LIBRARIES`.
2. Verifies that no forbidden third-party library target (SDL3, FreeType,
   HarfBuzz, ThorVG, miniaudio, PortAudio, RtMidi, VST3 SDK, accesskit-c)
   appears in the closure.
3. Reports any violation as `message(FATAL_ERROR ...)`. In M1,
   `graphscore_accessibility_platform` has zero external edges and must
   not appear with any forbidden library in its closure.

This ensures that even if a future developer adds an undeclared private link
that is not caught by `audit_permitted_edges` (because it wasn't declared in
inventory), the transitive closure audit catches it.

#### 7.3 Header Include Boundary Audit

`scripts/audit_includes.py` — a Python script that:

1. Reads a configuration file listing permitted and forbidden `#include`
   patterns per target.
2. For each audited target, scans **three directories**:
   a. Public headers — directories listed in `INTERFACE_INCLUDE_DIRECTORIES`
      or `PUBLIC_HEADER` properties (the effective include path for consumers).
   b. Private headers — implementation headers in `src/` directories of the
      owning target.
   c. Source files — all `.cpp`/`.c` files in the target's source directories.
3. For public headers of protected clean targets (§3.3), flags **any**
   `#include` of a forbidden third-party header — e.g., `<vst3/...>`,
   `<SDL3/...>`, `<ft2build.h>`, `<hb.h>`, `<thorvg/...>`, `<miniaudio.h>`,
   `<portaudio.h>`, `<rtmidi/...>`, `<accesskit/...>` — as a `FATAL_ERROR`.
4. For private headers and source files of protected clean targets, similarly
   flags any forbidden third-party include.
5. For writer-only targets, verifies that third-party includes appear only in
   `.cpp`/private-header files, never in public headers (external type leakage
   check — see also §7.6).
6. Returns non-zero exit code on violation.

Run as a CI step and optionally as a pre-commit hook.

#### 7.4 Pure-C ABI Consumer Compile

`tests/abi/c_consumer/CMakeLists.txt` — a standalone CMake subdirectory that:

1. Defines a C executable target with:
   ```cmake
   add_executable(gs_c_consumer test_c_consumer.c)
   set_target_properties(gs_c_consumer PROPERTIES
     C_STANDARD 99
     C_STANDARD_REQUIRED ON
     C_EXTENSIONS OFF
   )
   target_link_libraries(gs_c_consumer PRIVATE graphscore_runtime)
   ```
2. The `test_c_consumer.c` translation unit:
   - Includes only `graphscore/graphscore_c_abi.h`.
   - Uses every type (handle, struct, enum, callback) at compile time.
   - Asserts at compile time that `sizeof(gs_allocator_t)` and `sizeof(gs_result_t)`
     match the expected fixed sizes.
   - Calls the version query function at runtime.
3. Compiles with a C compiler (not C++) via the `C_STANDARD 99` CMake property.
   No compiler-specific flags (`-pedantic -std=c99`) are used — portability is
   achieved through the CMake target property.

Compiled as part of CI. Failure means the C ABI header has C++ leakage or
platform-dependent type sizes.

#### 7.5 Runtime Exported-Symbol Audit

`scripts/audit_runtime_symbols.py` — a Python script that:

1. Reads the runtime shared library symbol table (`nm -gU` on macOS,
   `dumpbin /exports` on Windows, `nm -D --defined-only` on Linux).
2. Compares exported symbols against the declared C ABI surface from
   `graphscore_c_abi.h`.
3. Flags any undocumented export or C++ mangled symbol.
4. Also verifies that no symbol from a writer-only target appears.

Run as a CI step.

#### 7.6 No-Cycles Check

CMake built-in: `cmake --graphviz` output is parsed by `scripts/audit_cycles.py`
to detect any directed cycle in the target dependency graph. CMake also rejects
cyclic `target_link_libraries` at configure time, but the script provides an
independent cross-check.

#### 7.7 Third-Party Type Leakage Check

`scripts/audit_third_party_types.py` — a Python script that:

1. Reads the public headers of all GraphScore targets.
2. Scans for known third-party type patterns: `hb_`, `FT_`, `tvg::`, `SDL_`,
   `IPluginFactory`, `Steinberg::`, `ma_`, `PaStream`, `RtMidi`,
   `accesskit_`.
3. Flags any match as a violation.
4. Additionally scans public headers of protected clean targets for any
   `#include` of third-party headers, even if the types are not directly
   referenced (header pollution check).

Run as a CI step.

#### 7.8 CMake Consumer Test

`tests/cmake/consumer_test/` — a standalone CMake project that:

1. Uses `find_package(graphscore_runtime)` or `add_subdirectory` to import
   only the runtime target.
2. Links an executable against `graphscore_runtime`.
3. Asserts that attempting to link `graphscore_writer_app` or any writer target
   fails (configure-time or link-time).
4. Compiles and runs a minimal smoke test calling the C ABI version function.

---

### 8. Unresolved / Provisional Implementation Choices

These are recorded without weakening the boundary guarantees above. M1/M2/Phase C
resolution does not alter the forbidden-edge matrix.

| Item | Status | Resolution Milestone |
|---|---|---|
| ThorVG vs. owned render-list | PROVISIONAL (ThorVG requires Meson/Ninja pins and adapter; owned fallback explicitly adopted if ThorVG fails) | Phase C rendering spike |
| AccessKit vs. platform-native accessibility bridge | **Phase C resolved**: platform-native accessibility APIs selected (ADR 0004 §2). AccessKit remains EXCLUDED (ADR 0002 §2). `graphscore_accessibility_platform` depends privately on NSAccessibility (macOS), UI Automation (Windows, future), AT-SPI2 (Linux, future). Native OS frameworks are system-provided, not vendored CMake targets. macOS bridge implemented and tested; Windows/Linux mappings documented but deferred to physical-device testing. | Phase C accessibility spike |
| VST3 SDK SHA and adapter | DEFERRED to M0 Phase D | M0 Phase D |
| miniaudio confirmation (M1 compile gate + Phase C device gate) | PROVISIONAL | M1 build gate + Phase C device gate |
| PortAudio activation if miniaudio fails | PROVISIONAL fallback | Phase C if needed |
| RtMidi ALSA dependency resolution | PROVISIONAL | M1 build gate |
| SDL3 remaining unaudited defaults | PROVISIONAL (listed in ADR 0002 §1, M1 audit required) | M1 configure-cache pass |
| HarfBuzz CMake adapter and FreeType isolation verification | PROVISIONAL (adapter + `get_target_property` check specified in ADR 0002 §4) | M1 |
| Exact IPC protocol for plugin scanner | Specification deferred | M8 (plugin scanner implementation) |
| Notation glyph-metrics interface details | Abstract interface declared; FreeType implementation deferred | M5 (notation engraving) |
| `graphscore_notation` dep on `graphscore_rendering` for glyph metrics | Not listed as a permitted edge. Notation uses an abstract interface; concrete impl is injected at assembly time by `graphscore_writer_app`. Notation does NOT link or `#include` rendering. | M5 |
| Engine wrapper package structure | Consume C ABI; package structure defined in M11 | M11 |

---

## Consequences

- M1 must implement exactly the target graph defined in §1 with no additional
  edges and no missing targets. Deviations require an ADR amendment.
- The eight audit scripts/specs in §7 must be implemented during M1 and run in
  CI. They are the mechanical enforcement of the boundaries recorded here.
- The DAG in §5 is a simplified dependency-flow view; the permitted-edge
  matrices in §2 are the canonical, machine-auditable specification. Targets
  in layer N may not depend on targets in layers > N (except within-layer
  edges explicitly permitted in §2).
- `graphscore_writer_app` is the sole assembly point that links across all
  writer layers. It does **not** link `graphscore_plugin_scanner` (separate
  executable). Any other target attempting to pull in writer-only dependencies
  is a build error enforced by the link-closure audit.
- Runtime consumers (Unity, Unreal, plain C hosts) need only the
  `graphscore_runtime` shared library and `graphscore_c_abi` header. The CMake
  consumer test (§7.8) proves this mechanically.
- `graphscore_scheduler` is exactly one library. Writer and runtime tests
  compare against the same golden vectors. Duplicate scheduler implementations
  are a violation of this ADR.
- `graphscore_compiler` runs off the realtime callback. Its compiled compact
  snapshots are consumed atomically by the scheduler in `graphscore_writer_audio`
  and the export pipeline in `graphscore_persistence`. It never pulls in UI,
  audio, or VST3 types.
- Phase C may resolve ThorVG/AccessKit choices, implement
  `graphscore_accessibility_platform`, and add rendering backend alternatives
  without modifying the forbidden-edge matrix. The boundaries are designed to
  accommodate these provisional decisions.
