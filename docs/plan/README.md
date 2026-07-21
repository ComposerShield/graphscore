# GraphScore Development Plan

## Product Vision

GraphScore is a graph-based adaptive music notation system. A composer authors conventional sheet music inside graph nodes, connects those nodes with sequential and immediate vertical transitions, auditions the result with VST3 instruments and effects, and exports a compact asset for a game-facing MIDI runtime.

The repository will produce two primary products:

- **GraphScore Writer**: a desktop notation and graph authoring application for macOS, Windows, and Linux.
- **GraphScore Runtime**: a C++23 implementation exposed through a stable C ABI as a dynamic library. The host supplies clocked audio blocks, sample-offset trigger events, and caller-owned MIDI output storage.

Milestones 00 through 11 target the first public `0.1.0` release. Milestone 12 is post-`0.1.0` work for general MIDI CC support.

## Product Boundaries

### Writer-only data and behavior

- Infinite graph viewport, node positions, colors, freeform node notes, selection, and connector routes.
- VST3 instrument and effect identities, opaque plugin state, audio settings, and audition mix.
- Plugin scanning, native plugin editors, latency compensation, metering, and audio output.
- Undo/redo, autosave/recovery, and one project per application window.

Writer-only data must never appear in a cooked runtime asset unless it is required to preserve stable runtime identity.

### Shared semantic behavior

- Tracks, staves, measures, notation, tempo curves, events, nodes, connectors, and transition rules.
- Deterministic conversion from notation and graph semantics to sample-offset MIDI 1.0 events.
- Validation and diagnostics shared by writer export and runtime asset loading.

### Runtime-only constraints

- No allocation, locks, blocking I/O, exceptions, or unbounded work in processing and trigger paths.
- Immutable cooked graph data after load.
- One realtime processing owner per runtime instance.
- Host allocator callbacks for non-realtime creation and loading.
- No VST3, GUI, editable project parsing, or audio rendering.

## Locked Product Decisions

### Licensing and dependencies

- GraphScore is licensed under Apache-2.0.
- The default complete build uses only dependencies compatible with permissive commercial reuse.
- Dependencies are included through CMake `FetchContent`, pinned to immutable commit hashes, and recorded in a license inventory.
- Build documentation supports `FetchContent` source overrides and populated caches for offline builds.
- Platform SDKs, installed VST3 plugins, and OS services are not vendored dependencies.
- Third-party types must not leak into the domain, runtime C ABI, or persistence schemas.

### Toolchain and quality

- C++23 and CMake, compiled with Clang or AppleClang on every platform.
- Clang `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wold-style-cast -Wnon-virtual-dtor -Wcast-align -Wunused -Wnull-dereference -Wdouble-promotion -Wformat=2 -Wimplicit-fallthrough -Werror` (or the clang-cl equivalent) for GraphScore-owned code.
- Prefer `constexpr`; otherwise use `const`; mutable state requires a demonstrated need.
- GTest is the unit test framework and is used extensively in every milestone.
- clang-format, clang-tidy, cpplint, ASan, UBSan, and TSan are required quality layers.
- A version-controlled `.githooks/pre-commit` runs cpplint. CI invokes the same lint target independently.
- Third-party warnings are isolated and are not promoted to GraphScore build failures.

### Supported systems

- macOS 14 or later on arm64 and x86-64.
- Windows 11 on x86-64 and arm64.
- Ubuntu 24.04-class Linux on x86-64 and arm64, with Wayland and X11/XWayland.
- GitHub Actions builds and tests macOS, Windows, and Linux on every pull request.
- Windows arm64 and Linux arm64 are build-only initially. Native test execution is required for macOS arm64/x86-64 and Windows/Linux x86-64 release paths.

### Graph and timeline

- A project has one designated start node; the writer can audition any node directly.
- The project has up to 64 globally configured tracks. Every node shows every active track.
- A track has one or more fixed pitched staves; standard pitched and grand-staff layouts are in scope.
- Every node owns a common measure timeline across all tracks and may contain at least 64 measures within the supported test envelope.
- Opening pickup measures are out of scope for `0.1.0`.
- A node contains at least one complete main-region measure and may end with one explicit partial **pickdown** region greater than zero and shorter than one complete measure under the time signature active at the boundary.
- A sequential destination starts when the pickdown starts. Pickdown notes continue concurrently on the source tempo curve, may contain new attacks, emit MIDI only, and survive later active-node transitions until naturally complete.
- The source tempo curve extends through the pickdown. Notes/ties crossing the transition boundary transfer their post-boundary logical ownership to the pickdown tail.
- If overlapping material attacks the same track/channel/pitch, emit note-off then the newer note-on; the newer attack owns the eventual note-off and the older logical note's later release is suppressed.
- Overlapping CC64 pedal spans combine as logical OR per track/channel; pedal-up emits only after the last active logical span releases.
- Export computes a finite upper bound for concurrent pickdown tails and rejects assets whose cycles/timing cannot be bounded.

### Tempo and meter

- Time signatures may change per measure and are shared by all tracks in a node.
- Tempo is a node-wide continuous lane anchored to musical positions.
- Tempo points carry an explicit beat unit and accept 10 through 400 BPM.
- Segments support step changes, linear interpolation, and smooth rounded curves.
- New nodes inherit the selected/source node's tempo and beat unit at the end of its main region, or the project default when no source exists.

### Connections and events

- Every named input/output connector has a stable UUID.
- Each output connector has at most one destination while editing and exactly one when enabled for export. It owns its type, event binding, priority, and random weight as applicable; queue policy belongs to the node/event listener shared by matching outputs.
- Sequential transitions occur at a node boundary. Matching persistent event intent wins, then a writer-only manually queued connector, then weighted random selection.
- Random groups use deterministic host-seeded randomness and must total exactly 100 percent; zero-weight outputs are not eligible.
- Registered events have stable UUIDs and unique case-sensitive UTF-8 names.
- Sequential event occurrences persist in a bounded per-event runtime queue until consumed. A node/event listener interprets matching occurrences as first-wins, latest-valid-wins by default, or FIFO.
- At a boundary, connector priority chooses among different queued events, newest candidate occurrence wins equal priority, and stable connector order is final. First-wins consumes the earliest occurrence and discards later duplicates accumulated before that boundary; latest-wins consumes the newest and discards older duplicates; FIFO consumes only the earliest.
- A full FIFO drops its oldest item and increments a diagnostic counter.
- Vertical events are sample-local and never persist.
- Vertical and sequential listeners for the same event on the same node are rejected.
- Multiple simultaneous vertical matches use explicit priority and then stable connector order.
- Cycles are valid. A node with no eligible sequential output stops playback.

### Vertical transitions

- A vertical transition jumps immediately at the event's sample offset to the same measure and exact rational offset within that measure in its destination.
- Source and destination must have the same main-region measure count and corresponding time signatures. Pickdowns are excluded from compatibility.
- The destination begins using its own tempo curve at the mapped position.
- Source-main notes currently owning MIDI pitches receive note-offs at the jump. Concurrent pickdown tails remain active. Target notes that began before the mapped position are not chased; target events beginning exactly at or after the jump are emitted normally.
- Only the active main node can route vertically; pickdown tails never route. At most one vertical transition may be selected from the node active at a given sample offset, so same-sample events cannot chain through a vertical cycle.

### Notation scope

- Up to four explicitly selected rhythmic voices per staff.
- Notes and rests from whole through sixty-fourth duration, single/double dots, chords, ties, and single-level arbitrary `N:M` tuplets.
- Grace notes steal time from the preceding note.
- Standard key signatures through seven sharps/flats, changed per measure and shared by all tracks in a node.
- Standard pitched clefs with changes at musical positions.
- Double flats and double sharps on individual notes.
- Dynamics, hairpins, slurs, accent, marcato, staccato, staccatissimo, tenuto, automatic beaming with manual break/join and stem overrides, and pedal markings.
- Dynamics/hairpins affect note-on velocity, slurs provide legato overlap, and articulations affect velocity/duration.
- Sustain pedal is the sole pre-general-CC exception and emits MIDI CC64.
- Transposing instruments, percussion, tablature, general repeats, lyrics, general MIDI CC, and plugin automation are outside `0.1.0`.

### Writer interaction

- Notes are entered from a duration/notation palette with a yellow preview before placement, a visible selected-note highlight after placement, and a short VST3 audition after insertion or pitch change.
- In the selected voice, clicking an existing event changes its duration or builds a chord rather than injecting an overlapping rhythm. Polyphony requires an explicit voice change.
- Shortening an event creates normalized automatic rests. `R` converts the entire selected note/chord event to an equal-duration rest.
- Selection is per notehead. Up/Down moves diatonically while preserving the accidental. `-` and `=` move through double-flat, flat, natural, sharp, and double-sharp.
- Delete removes the selected note and selects the prior onset in the same voice/staff.
- Primary means Command on macOS and Control on Windows/Linux. Primary+Up/Down selects the nearest note at the same time on the prior/next staff, wrapping at node edges.
- `2` through `8` add that diatonic interval above the selected note; Shift+`2` through Shift+`8` add below. The new note is selected and `1` is a no-op.
- A measure selector can select and copy/paste one complete measure on a staff/track or the aligned measure across chosen tracks.
- A dedicated selection tool can click-drag a contiguous musical-time range across one or more staves/voices, then cut/copy/paste that fragment independently of measure boundaries.
- Fragment paste shows a destination preview, changes only the destination time/staff scope, preserves valid voice rhythm through normalized rests, and is one undoable transaction.
- Range selection, node copy/paste, measure insert/delete, and keyboard step entry are required.
- Keyboard and screen-reader access are first-release requirements.

### Canvas and connector interaction

- The graph uses an effectively infinite sparse coordinate space.
- Trackpad two-finger pan and pinch zoom are first-class. Mouse wheel/modifier zoom, middle-button pan, and keyboard pan/zoom are also supported.
- Zoom is centered on the pointer or accessibility focus as appropriate.
- Full notation remains semantically present at every zoom. Offscreen culling and imperceptible raster simplification are allowed; semantic level-of-detail replacement is not.
- The writer targets 60 fps pan, zoom, node drag, and connector editing with a representative 1,000-node project.
- Connector routes are straight or orthogonal with any number of 90-degree bends and rounded corners.
- Default routing avoids node obstacles. Dragging any segment moves it orthogonally with a bidirectional cursor.
- Customized interior routes survive node moves where valid. Primary+Shift+R resets a selected route; Delete removes it.
- Sequential and vertical routes differ through both color and line pattern.
- A normal click selects/edits a connector. During playback, double-clicking or activating its destination-end action circle queues a sequential connector or immediately takes a vertical connector only when its source is the active node; otherwise the writer reports why the action is unavailable.
- Every node has a play button. Activating it reconciles current notes, clears tails/queues, and starts that node from its beginning.

### Writer playback and plugins

- Each track has exactly one VST3 instrument slot followed by zero or more ordered VST3 effect slots.
- Plugin state is embedded in the editable project and removed from cooked runtime data.
- Scanning runs in a helper process; validated plugins run in-process for playback.
- Missing plugins become silent placeholders without blocking editing/export.
- Native VST3 editors are supported. A GraphScore-owned accessible generic parameter view is provided where practical.
- Track audition supports mute, solo, gain, pan, plugin bypass/reorder, and metering.
- Reported plugin latency is compensated across tracks.
- Effect tails decay naturally; a panic action cuts notes and resets processing immediately.
- Transport includes play, pause, stop, node play, loop node, metronome, count-in, connector actions, and a searchable event simulator.
- Safe edits publish on the next audio block. Future notes/tempo/connections update, deleted sounding notes receive note-off, past events do not replay, and elapsed measure-structure changes remain visibly deferred until restart.

### Runtime API

- Stable C ABI over a C++23 implementation; no C++ standard-library types cross the boundary.
- Runtime loads a validated cooked asset from a caller-provided memory buffer.
- Process calls provide absolute sample position, sample rate/configuration, variable frame count up to a configured maximum, and sample-offset input events up to a configured maximum count.
- Output is caller-owned MIDI 1.0 event storage. Events contain sample offset, stable track UUID/index, fixed track MIDI channel, and message bytes.
- The host performs an off-thread capacity query using maximum block size and maximum input-event count. If caller output capacity is below that requirement, process emits nothing, advances no state, and returns the required capacity so the same block can be retried with preallocated storage.
- Transport includes start designated node, start node UUID, stop, reset, pause, and resume. Discontinuous clocks require an explicit reset/seek-state operation rather than implicit reconstruction.
- Host supplies the deterministic random seed.
- Realtime diagnostics use process status flags plus pollable/resettable atomic counters, never logging callbacks.
- Official Unity and Unreal integrations wrap the same dynamic library and do not add an audio renderer or plugin host.

## Milestone Roadmap

Progress is tracked in the source-controlled [execution checklist](CHECKLIST.md). Every milestone and major implementation/test step has its own task box; check a step only after all detailed bullets in its linked section are complete.

| Milestone | Outcome |
| --- | --- |
| [00](00-architecture-spikes.md) | Permissive architecture, rendering/engraving, VST3, and cooked-format risks retired |
| [01](01-toolchain-ci.md) | Reproducible C++23 foundation, quality gates, and three-platform CI |
| [02](02-domain-model.md) | Tested musical, graph, event, transition, command, identity, and playback semantics |
| [03](03-persistence-export.md) | Editable bundle, autosave/recovery, validation, and deterministic cooked assets |
| [04](04-runtime-foundation.md) | Hard-realtime C ABI and deterministic sample-accurate MIDI scheduler |
| [05](05-notation-editor.md) | Focused notation engraving, editing, keyboard, and accessibility workflows |
| [06](06-graph-canvas.md) | Infinite graph canvas, notation nodes, and editable orthogonal connectors |
| [07](07-adaptive-transitions.md) | Tempo curves, sequential/vertical routing, events, randomness, and pickdowns |
| [08](08-audio-vst.md) | Cross-platform writer audio, VST3 chains, audition mix, and transport |
| [09](09-writer-integration.md) | Live editing/playback, event simulation, document workflows, and polished composition loop |
| [10](10-hardening.md) | Accessibility, realtime, performance, compatibility, and resilience release gates |
| [11](11-engine-release.md) | Unity/Unreal integrations and unsigned `0.1.0` release archives |
| [12](12-midi-cc.md) | Post-`0.1.0` general MIDI CC authoring and runtime support |

## Cross-Milestone Definition Of Done

This section applies to **production milestones 01 through 12**. Milestone 00
produces disposable spikes and written decisions, and is governed instead by the
spike rules in [00-architecture-spikes.md](00-architecture-spikes.md). Applying
production gates to throwaway spike code is a known failure mode of this plan
and is explicitly out of scope.

Every production milestone must:

- Keep the three primary platform builds green with Clang.
- Add GTest coverage for new domain and runtime behavior before considering the behavior complete.
- Add malformed-input, boundary, deterministic replay, and concurrency tests where applicable.
- Keep GraphScore-owned targets warning-clean under warnings-as-errors, cpplint, clang-format, and clang-tidy.
- Run relevant sanitizer suites without findings.
- Update schemas, C API documentation, accessibility semantics, and user-facing behavior documentation with the implementation.
- Avoid hidden network dependencies, floating dependency revisions, and license-incompatible source.
- Meet realtime rules for every function reachable from processing.

## Planning Assumptions

- Milestones are dependency-ordered, not promises of calendar duration.
- A milestone may use feature branches or internal increments, but its acceptance gates must pass together.
- Architecture and dependency spikes may reject a candidate; the recorded decision and fallback are the deliverable. Spikes carry stated time boxes, and reaching a time box without a clear answer is itself an answer.
- The tested scale is a release contract for representative fixtures, not permission to add arbitrary hard-coded storage limits.
- `0.1.0` denotes a coherent first release, not API stability equivalent to `1.0.0`; nevertheless, cooked assets and the C ABI are versioned from their first public appearance.
