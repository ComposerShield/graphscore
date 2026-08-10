# GraphScore Execution Checklist

This source-controlled checklist tracks the phases and major steps in the milestone plan. A step box is checked when every detailed deliverable in its linked section is complete.

Progress short of that is recorded as an **indented sub-entry** beneath the step, checked in its own right, naming what landed and what remains. A step whose deliverables are partly delivered should show its completed increments rather than reading as untouched — an unchecked box with no sub-entries means no work has landed. Milestone 02's Phase 8 series is the worked example of the form.

Acceptance and test boxes must be checked before the milestone-complete box.

**Execution order:** milestones are listed below by identity, not by the order
they are worked. **Milestone 05 runs before 03 and 04** — see the Execution
Order section of [README.md](README.md#milestone-roadmap) for why, what it
blocks, and the ADR amendments it requires first.

## Milestone 00: Architecture And Risk Spikes

Spike rules in [00-architecture-spikes.md](00-architecture-spikes.md) apply.
The cross-milestone Definition Of Done does **not**. Each box below is one
ADR or one time-boxed investigation, not a review-and-evidence cycle.

- [ ] [Milestone 00 complete](00-architecture-spikes.md)
- [x] Permissive dependency policy recorded (ADR 0001, ADR 0002)
- [x] Architectural boundaries recorded (ADR 0003)
- [x] Rendering and accessibility decision recorded, macOS (ADR 0004)
- [x] Engraving-engine decision recorded (ADR 0005)
- [x] Cooked-format decision recorded (ADR 0006) — owned binary format
- [x] VST3 SDK license confirmed from upstream — MIT across all core
      submodules, incl. `pluginterfaces`; VSTGUI BSD-3-style and not required
- [x] Direct VST3 hosting spike completed, macOS arm64 (ADR 0007)
- [ ] Spike directories deleted or quarantined
- [ ] Exit decision approved

## Milestone 01: Toolchain And CI Foundation

- [x] [Milestone 01 complete](01-toolchain-ci.md)
- [x] Dependencies completed
- [x] Repository structure created
- [x] Git source control initialized and required project/planning files tracked
- [x] Root `AGENTS.md` created and validated
- [x] Commit-message model/vendor attribution prohibition documented in `AGENTS.md`
- [x] Tracked `CLAUDE.md` symlink to `AGENTS.md` created and validated
- [x] CMake and FetchContent foundation completed
- [x] Const-correctness policy implemented
- [x] Local quality gates and `.githooks/pre-commit` completed
- [x] GitHub Actions platform matrix completed
- [x] Skeleton writer/runtime artifacts completed
- [x] Acceptance criteria passed — first green CI run across the platform
      matrix; every criterion verified locally on macOS arm64 and the
      workflow committed
- [x] Test focus completed

## Milestone 02: Domain And Command Model

The parenthetical text under each Phase 8 box is a **historical log written
when that phase landed**, not a statement of current status. A note saying
"selection, clipboard, and measure ops still unchecked" describes what was
outstanding *at that phase's own completion*, and later boxes discharge it.
Read the checkbox, not the note. Everything through Phase 8h-iv and the
validation service is delivered; the three genuinely open items are the two
unchecked deliverable boxes below and the sign-off sections in
[02-domain-model.md](02-domain-model.md).

- [ ] [Milestone 02 complete](02-domain-model.md)
- [x] Dependencies completed
- [x] Identity and value types completed
- [x] Project and track model completed
- [x] Node timeline completed
- [x] Notation model completed
- [x] Graph model completed
- [x] Adaptive playback semantics specified
- [x] Normative playback specification completed
- [x] &nbsp;&nbsp;Phase 8a foundational command/history/transaction increment completed<br>&nbsp;&nbsp;&nbsp;&nbsp;(non-throwing `Command` protocol, standalone `CommandHistory`, atomic `CommandTransaction` with<br>&nbsp;&nbsp;&nbsp;&nbsp;best-effort rollback, three stable-ID proving commands; 734 tests. Remaining Phase 8<br>&nbsp;&nbsp;&nbsp;&nbsp;edit commands, selection, clipboard, clipping/remapping, and measure ops still unchecked.)
- [x] &nbsp;&nbsp;Phase 8b metadata/audition-mix command increment completed<br>&nbsp;&nbsp;&nbsp;&nbsp;(ten reversible non-structural commands: `SetProjectName`, `SetStartNode`, `SetProjectDynamic`,<br>&nbsp;&nbsp;&nbsp;&nbsp;`SetTrackGain`, `SetTrackPan`, `SetTrackMute`, `SetTrackSolo`, `SetNodeColor`, `SetNodeNotes`,<br>&nbsp;&nbsp;&nbsp;&nbsp;`SetNodePosition`; stable-ID lookup, noexcept/allocation-safe, deterministic replay; 798 tests.<br>&nbsp;&nbsp;&nbsp;&nbsp;Selection, clipboard, plugin-chain, structural, graph, timeline, and notation commands remain.)
- [x] &nbsp;&nbsp;Phase 8c reversible structural/config command increment completed<br>&nbsp;&nbsp;&nbsp;&nbsp;(fourteen commands wrapping only existing domain API, split 8c-i + 8c-ii: `ArchiveTrack`,<br>&nbsp;&nbsp;&nbsp;&nbsp;`RestoreTrack`, `SetOutputType`, `SetListenerPolicy`, `SetOutputPriority`/`Weight`/`ExportEnabled`,<br>&nbsp;&nbsp;&nbsp;&nbsp;`SetInputConnectorName`/`SetOutputConnectorName`, `Connect`, `Disconnect`, `BindOutputEvent`,<br>&nbsp;&nbsp;&nbsp;&nbsp;`SetCustomRoute`, `ResetRoute`; route-geometry and EventListener snapshot/restore; 896 tests.<br>&nbsp;&nbsp;&nbsp;&nbsp;Add/remove structural commands deferred to 8d — they need new domain restore-with-id/removal<br>&nbsp;&nbsp;&nbsp;&nbsp;API first; selection, clipboard, and measure ops still unchecked.)
- [x] &nbsp;&nbsp;Phase 8d reversible add/remove-of-graph-entities increment completed<br>&nbsp;&nbsp;&nbsp;&nbsp;(split 8d-i connectors, 8d-ii events, 8d-iii tracks, 8d-iv nodes; new restore-with-id domain<br>&nbsp;&nbsp;&nbsp;&nbsp;API — `restore_input`/`restore_output`, `add_event_with_id`, `add_track_with_id`/`hard_remove_track`/<br>&nbsp;&nbsp;&nbsp;&nbsp;`remove_lane`, `add_node_with_id`/`remove_node`/`restore_node` — plus `bind_output_event`<br>&nbsp;&nbsp;&nbsp;&nbsp;allocation hardening; full aggregate + cross-graph cascade snapshots; 976 tests. Remaining:<br>&nbsp;&nbsp;&nbsp;&nbsp;notation/tempo edit commands, selection, clipboard, cut/copy/paste, node copy/paste remapping,<br>&nbsp;&nbsp;&nbsp;&nbsp;and measure ops — 8e..f.)
- [x] &nbsp;&nbsp;Phase 8f-ii selection representation increment completed<br>&nbsp;&nbsp;&nbsp;&nbsp;(seven-arm homogeneous deduplicated `Selection` variant — notehead, chord, full-measure,<br>&nbsp;&nbsp;&nbsp;&nbsp;arbitrary range, node, connector, insertion caret — each carrying per-item scope; separate<br>&nbsp;&nbsp;&nbsp;&nbsp;deterministic `validate_selection` projector; archived/wrong/missing scope, notehead/chord kind,<br>&nbsp;&nbsp;&nbsp;&nbsp;main-only ordinals, pickdown range classification, local connector ownership, caret extents;<br>&nbsp;&nbsp;&nbsp;&nbsp;`VoiceContent::position_of_event` with ChordNote/GraceNote chains and dangling/cycle guards;<br>&nbsp;&nbsp;&nbsp;&nbsp;`TrackLane::total_length` deterministic max; 1335 tests. `route-segment` deferred to M06;<br>&nbsp;&nbsp;&nbsp;&nbsp;`staff-focus` dropped. Clipboard (8g) and measure ops (8h) remain.)
- [x] &nbsp;&nbsp;Phase 8g-i clipboard fragment + copy extraction increment completed<br>&nbsp;&nbsp;&nbsp;&nbsp;(`NotationFragment` validated value type — relative positions, ordinal staff/voice mapping,<br>&nbsp;&nbsp;&nbsp;&nbsp;no source UUIDs, contradictory-duplicate rejection; pure `extract_fragment` query over<br>&nbsp;&nbsp;&nbsp;&nbsp;full-measure and arbitrary-range selections; twelve locked boundary-clipping rules R1–R12;<br>&nbsp;&nbsp;&nbsp;&nbsp;empty/short source voices rest-filled to the span; 1400 tests. Paste/cut followed in 8g-ii;<br>&nbsp;&nbsp;&nbsp;&nbsp;node copy/paste 8g-iii remains.)
- [x] &nbsp;&nbsp;Phase 8g-ii cut + paste command increment completed<br>&nbsp;&nbsp;&nbsp;&nbsp;(`PasteFragmentCommand` and `CutFragmentCommand`; complete-measure/non-aligned full-span<br>&nbsp;&nbsp;&nbsp;&nbsp;replacement, exact undo/redo, fresh identity remapping, deterministic clipping, marking/pedal<br>&nbsp;&nbsp;&nbsp;&nbsp;handling, empty/no-stave and stale multi-lane atomicity; 78 clipboard-command tests, 1481 total.<br>&nbsp;&nbsp;&nbsp;&nbsp;Node copy/paste 8g-iii and measure/timeline operations 8h remain.)
- [x] &nbsp;&nbsp;Phase 8g-iii node duplication + identity remapping increment completed<br>&nbsp;&nbsp;&nbsp;&nbsp;(`DuplicateNodesCommand` — direct in-place duplication, not a fragment/paste pair; fresh<br>&nbsp;&nbsp;&nbsp;&nbsp;`NodeId`/`ConnectorId`/`NotationEntityId` throughout with a whole-project uniqueness probe over<br>&nbsp;&nbsp;&nbsp;&nbsp;active and archived lanes; id-free `NodeTimeline` copied verbatim; intra-selection edges remapped<br>&nbsp;&nbsp;&nbsp;&nbsp;including self-loops, edges leaving the selection dropped, non-selected nodes untouched;<br>&nbsp;&nbsp;&nbsp;&nbsp;automatic routes, preserved project-scoped event bindings, start node unaffected; id-for-id redo<br>&nbsp;&nbsp;&nbsp;&nbsp;and cascade-repairing undo rollback; 25 new cases, 1506 total. Measure/timeline operations 8h and<br>&nbsp;&nbsp;&nbsp;&nbsp;the validation service remain.)
- [x] &nbsp;&nbsp;Phase 8h-i measure/clef mutation primitives increment completed<br>&nbsp;&nbsp;&nbsp;&nbsp;(`MeasureMap::insert_measure` ×2 / `remove_measure` / `set_measure` and `ClefLane::remove_change` /<br>&nbsp;&nbsp;&nbsp;&nbsp;`move_change` / `set_change` — the containers had none; derived `starts_`/`total_length_` rebuilt<br>&nbsp;&nbsp;&nbsp;&nbsp;through the constructor's own `compute_derived_state`, never patched; every mutator atomic;<br>&nbsp;&nbsp;&nbsp;&nbsp;`remove_measure` refuses the last measure, discharging the container half of the ≥1-main-measure<br>&nbsp;&nbsp;&nbsp;&nbsp;invariant two `measure_count() - 1` call sites would underflow without; no ids, no mutable<br>&nbsp;&nbsp;&nbsp;&nbsp;`MeasureMap` accessor on `NodeTimeline`; 23 new cases, 1531 total. 8h-ii/iii/iv remain.)
- [x] &nbsp;&nbsp;Phase 8h-ii non-length-changing timeline command increment completed<br>&nbsp;&nbsp;&nbsp;&nbsp;(six reversible stable-ID `noexcept` commands for per-measure key signatures, clef<br>&nbsp;&nbsp;&nbsp;&nbsp;add/remove/move, and pickdown set/clear; narrow `NodeTimeline` entry points, exact whole-container<br>&nbsp;&nbsp;&nbsp;&nbsp;and optional tempo snapshots, stale lifecycle and failure atomicity; 21 new cases, 1552 total.<br>&nbsp;&nbsp;&nbsp;&nbsp;Measure cascade 8h-iii, paste context 8h-iv, and validation remain.)
- [x] &nbsp;&nbsp;Phase 8h-iii measure/time-signature cascade increment completed<br>&nbsp;&nbsp;&nbsp;&nbsp;(`InsertMeasureCommand`, `DeleteMeasureCommand`, `SetMeasureTimeSignatureCommand`; atomic whole-node<br>&nbsp;&nbsp;&nbsp;&nbsp;cascade across active/archived lanes, voices, timeline, pedal spans, and pickdown; deterministic<br>&nbsp;&nbsp;&nbsp;&nbsp;boundary clipping, tuplet rejection, beam splitting, and exact undo/redo; denominator limit 64;<br>&nbsp;&nbsp;&nbsp;&nbsp;47 new cases, 1599 total. Paste context 8h-iv and validation remain.)
- [x] &nbsp;&nbsp;Phase 8h-iv paste clef/signature context increment completed<br>&nbsp;&nbsp;&nbsp;&nbsp;(`PasteFragmentCommand` now consumes the fragment context it previously ignored: interior clef<br>&nbsp;&nbsp;&nbsp;&nbsp;changes applied and contained — prevailing clef re-asserted at `range_end`, stale in-range<br>&nbsp;&nbsp;&nbsp;&nbsp;destination changes wiped across the union of touched and clef-named staves — while a copied<br>&nbsp;&nbsp;&nbsp;&nbsp;time signature is a reject-on-mismatch gate, never applied, and a copied key signature is never<br>&nbsp;&nbsp;&nbsp;&nbsp;applied at all; `clef_at_origin` deliberately never applied. Narrow `create_clef_lane`/<br>&nbsp;&nbsp;&nbsp;&nbsp;`remove_clef_lane` make absent-lane creation reversible; `prepare_lane_restore`/`commit_lane_restore`<br>&nbsp;&nbsp;&nbsp;&nbsp;split removes the partial-commit window; 25 new cases, 1624 total. Phase 8h complete;<br>&nbsp;&nbsp;&nbsp;&nbsp;the validation service remains.)
- [ ] Command and selection model completed
- [x] Validation service completed<br>&nbsp;&nbsp;&nbsp;&nbsp;(public complete and scoped incremental validation; typed, context-scoped deterministic diagnostics;<br>&nbsp;&nbsp;&nbsp;&nbsp;cache replacement/prior-scope contract; all eight validation families with archived/orphan handling;<br>&nbsp;&nbsp;&nbsp;&nbsp;30 focused `ValidationService` cases, 1654 total; exact-tree debug, lint, all seven audits,<br>&nbsp;&nbsp;&nbsp;&nbsp;canonical clang-tidy 18, and ASan/UBSan reviewer-approved.)
- [ ] Acceptance criteria passed
- [ ] Test focus completed

## Milestone 03: Persistence And Runtime Export

- [ ] [Milestone 03 complete](03-persistence-export.md)
- [ ] Dependencies completed
- [ ] Editable project bundle completed
- [ ] Autosave and recovery completed
- [ ] Schema evolution rules completed
- [ ] Export pipeline completed
- [ ] Runtime loader boundary completed
- [ ] Acceptance criteria passed
- [ ] Test focus completed

## Milestone 04: Realtime Runtime Foundation

- [ ] [Milestone 04 complete](04-runtime-foundation.md)
- [ ] Dependencies completed
- [ ] Public C ABI completed
- [ ] Processing contract completed
- [ ] Capacity and diagnostics completed
- [ ] Same-sample phase order completed
- [ ] Core scheduling completed
- [ ] Realtime implementation rules completed
- [ ] Acceptance criteria passed
- [ ] Test focus completed

## Milestone 05: Notation Engraving And Editing

- [ ] [Milestone 05 complete](05-notation-editor.md)
- [x] Dependencies completed<br>&nbsp;&nbsp;&nbsp;&nbsp;(rendering-dependency bring-up: `cmake/ThorVG.cmake`, `cmake/FreeType.cmake`, `cmake/HarfBuzz.cmake`,<br>&nbsp;&nbsp;&nbsp;&nbsp;`cmake/Bravura.cmake` wired behind ADR 0002 §§A1–A7; SDL3 renderer options flipped `ON` with<br>&nbsp;&nbsp;&nbsp;&nbsp;per-platform derived-probe assertions; writer install rule shipping Bravura under a fixed name<br>&nbsp;&nbsp;&nbsp;&nbsp;plus an FTL §2-compliant notice set; ADR 0003 §2.3 test-target enforcement gap closed. M02's own<br>&nbsp;&nbsp;&nbsp;&nbsp;completion box remains open only for `route-segment` selection (deferred to M06) and drag-gesture<br>&nbsp;&nbsp;&nbsp;&nbsp;transaction grouping, which lands here — neither gates 05.)
- [x] Incremental engraving completed<br>&nbsp;&nbsp;&nbsp;&nbsp;(toolkit-neutral full active-track/stave engraving with retained hit regions; required glyphs,<br>&nbsp;&nbsp;&nbsp;&nbsp;markings, and four-voice scope; affected-measure/system engraving-fragment rebuilds preserve<br>&nbsp;&nbsp;&nbsp;&nbsp;unaffected identities and equal fresh layout; concurrency-safe FreeType/HarfBuzz/Bravura/ThorVG<br>&nbsp;&nbsp;&nbsp;&nbsp;backend with exact Bravura identity; bounded domain revision-delta journal with stale fallback<br>&nbsp;&nbsp;&nbsp;&nbsp;drives incremental invalidation. Option A bounds only engraving-fragment rebuilds; domain,<br>&nbsp;&nbsp;&nbsp;&nbsp;validation/index discovery, and full-layout assembly may scan/copy retained content. Reviewer-<br>&nbsp;&nbsp;&nbsp;&nbsp;approved exact tree: debug 1766/1766, full lint, all seven architecture audits, canonical<br>&nbsp;&nbsp;&nbsp;&nbsp;clang-tidy 18 writer-OFF plus writer-ON rendering, ASan/UBSan and TSan writer-OFF 1766 with 11<br>&nbsp;&nbsp;&nbsp;&nbsp;expected writer-backend skips, runtime-only 1766 with the same skips, and M05 focused 30/30 plus<br>&nbsp;&nbsp;&nbsp;&nbsp;concurrency at 20 repetitions.)
- [x] Note palette and pointer entry completed
- [x] &nbsp;&nbsp;Palette control state model completed<br>&nbsp;&nbsp;&nbsp;&nbsp;(toolkit-neutral `NotePaletteState`/`NotePaletteEntrySpec` in `graphscore_notation`, covering every<br>&nbsp;&nbsp;&nbsp;&nbsp;in-scope control — note value through sixty-fourth, dots, note-versus-rest, voice, tuplet,<br>&nbsp;&nbsp;&nbsp;&nbsp;articulations, dynamic, hairpin, tie, slur, pedal, beam override — reusing the existing domain/core<br>&nbsp;&nbsp;&nbsp;&nbsp;vocabulary rather than a parallel one; validated transitions return `std::nullopt` and never throw,<br>&nbsp;&nbsp;&nbsp;&nbsp;matching `Duration::create`/`Voice::create`; duration-articulation mutual exclusion and out-of-range<br>&nbsp;&nbsp;&nbsp;&nbsp;`Articulation` both rejected, the latter closing an out-of-range shift; `resolved_duration()` and<br>&nbsp;&nbsp;&nbsp;&nbsp;`next_entry_spec()` total by construction; `kAllArticulations`/`kArticulationCount` added to<br>&nbsp;&nbsp;&nbsp;&nbsp;`graphscore_core` as the single enumerator source of truth. Reviewer-approved exact tree: writer-ON<br>&nbsp;&nbsp;&nbsp;&nbsp;debug 1796/1796 with zero skips, warning-clean forced full recompile, full lint, all seven<br>&nbsp;&nbsp;&nbsp;&nbsp;architecture audits, zero clang-tidy 18 diagnostics tree-wide, ASan/UBSan. Applying an armed control<br>&nbsp;&nbsp;&nbsp;&nbsp;to the score is deferred: preview, click-to-change-duration, chord building, rest normalization, and<br>&nbsp;&nbsp;&nbsp;&nbsp;voice isolation remain here; tuplets and every marking land in Structural editing.)
- [x] &nbsp;&nbsp;Pointer-entry preview increment completed<br>&nbsp;&nbsp;&nbsp;&nbsp;(toolkit-neutral `NotationPreview`/`preview_note_entry` in `graphscore_notation` — a pure query resolving a<br>&nbsp;&nbsp;&nbsp;&nbsp;point to staff, natural diatonic staff step, and nearest existing onset in the armed voice, with its own<br>&nbsp;&nbsp;&nbsp;&nbsp;standalone command list for a later translucent raster pass, since `NotationCommand` has no color/alpha<br>&nbsp;&nbsp;&nbsp;&nbsp;and ADR 0003 bars a notation→rendering edge; new `spelled_pitch_at` inverse of `pitch_y`, and the<br>&nbsp;&nbsp;&nbsp;&nbsp;engraver's own `position_x` factored into a shared helper plus an exact `time_at_x` inverse so the preview<br>&nbsp;&nbsp;&nbsp;&nbsp;honors clef/key/time leading rather than a naive linear measure split; staff resolution bounded to a<br>&nbsp;&nbsp;&nbsp;&nbsp;six-staff-space ledger/marking lane matching `system_top_padding`, gating rests identically to notes.<br>&nbsp;&nbsp;&nbsp;&nbsp;Onsets snap only to existing event boundaries — no metric grid, no rest subdivision — so a whole-measure<br>&nbsp;&nbsp;&nbsp;&nbsp;rest offers exactly one onset. Reviewer-approved exact tree: debug 1818/1818 warning-clean, full lint, all<br>&nbsp;&nbsp;&nbsp;&nbsp;seven audits, zero clang-tidy 18 diagnostics on a forced reanalysis, ASan/UBSan 228/228, and a 132-case<br>&nbsp;&nbsp;&nbsp;&nbsp;clef×octave×letter sweep with zero mismatches. Applying the previewed entry to the score — click-to-change<br>&nbsp;&nbsp;&nbsp;&nbsp;duration, chord building, rest normalization, voice isolation, audition request — remains.)
- [x] &nbsp;&nbsp;Pointer-event/chord-building entry increment completed<br>&nbsp;&nbsp;&nbsp;&nbsp;(`make_note_entry_command` consumes palette duration/kind/selected voice plus preview onset/pitch;<br>&nbsp;&nbsp;&nbsp;&nbsp;returns reversible `SetEventCommand`; same-pitch duration replacement, rest↔note conversion, note→chord<br>&nbsp;&nbsp;&nbsp;&nbsp;and chord extension; five event-reference families remapped on top-level-ID changes with conservative<br>&nbsp;&nbsp;&nbsp;&nbsp;revision full-reset; pre-existing articulations/stem preserved; exact undo/redo. Reviewer-approved<br>&nbsp;&nbsp;&nbsp;&nbsp;debug 1855/1855, full lint, seven audits, clang-tidy 18 writer-OFF, ASan/UBSan writer-OFF 1844 pass +<br>&nbsp;&nbsp;&nbsp;&nbsp;11 expected writer-only skips; TSan N/A for synchronous caller-owned mutation. Remaining: automatic-rest<br>&nbsp;&nbsp;&nbsp;&nbsp;normalization checkbox, explicit voice-stream workflow, audition request.)
- [x] &nbsp;&nbsp;Automatic-rest normalization increment completed<br>&nbsp;&nbsp;&nbsp;&nbsp;(Shortening inserts exact idiomatic automatic rests through `VoiceContent::replace_event` and<br>&nbsp;&nbsp;&nbsp;&nbsp;preserves later event onsets; expansion consumes only contiguous following rests, preserves<br>&nbsp;&nbsp;&nbsp;&nbsp;split-rest identity, and rejects insufficient space atomically; `decompose_rest` corrected from<br>&nbsp;&nbsp;&nbsp;&nbsp;locally greedy dead-end to bounded exact minimal-rest decomposition with deterministic largest-<br>&nbsp;&nbsp;&nbsp;&nbsp;duration tie-breaking, a 64-rest limit, overflow-safe Rational scaling, non-dyadic rejection, and<br>&nbsp;&nbsp;&nbsp;&nbsp;cap-boundary behavior — all with exact undo/redo. Focused regressions in 5/128 and 17/128<br>&nbsp;&nbsp;&nbsp;&nbsp;greedy-dead-end cases, domain replacement, pointer command, cap/overflow/non-dyadic boundaries,<br>&nbsp;&nbsp;&nbsp;&nbsp;exact undo/redo, and deterministic property-style coverage. Reviewer-approved exact tree: debug<br>&nbsp;&nbsp;&nbsp;&nbsp;1882/1882; full lint; all seven architecture audits; canonical clang-tidy 18 writer-OFF 72/72;<br>&nbsp;&nbsp;&nbsp;&nbsp;ASan/UBSan 1882/1882 with 11 expected writer-rendering skips; TSan N/A (synchronous non-concurrent<br>&nbsp;&nbsp;&nbsp;&nbsp;domain editing); focused M05 family 65/65. Remaining in this phase: explicit voice-stream workflow<br>&nbsp;&nbsp;&nbsp;&nbsp;and audition preview request.)
- [x] &nbsp;&nbsp;Explicit voice-stream workflow increment completed<br>&nbsp;&nbsp;&nbsp;&nbsp;(Arming a voice has no domain effect; the first pointer entry into an entirely empty armed voice<br>&nbsp;&nbsp;&nbsp;&nbsp;returns one `CommandTransaction` composing a new reversible `CreateVoiceStreamCommand` with the<br>&nbsp;&nbsp;&nbsp;&nbsp;ordinary `SetEventCommand`, so one undo removes note and stream together and returns the voice to<br>&nbsp;&nbsp;&nbsp;&nbsp;zero events, redo id-for-id. New `decompose_measure_aligned_rests`/`..._rest_durations` decompose<br>&nbsp;&nbsp;&nbsp;&nbsp;each main-region measure independently and the pickdown as its own trailing group, so no automatic<br>&nbsp;&nbsp;&nbsp;&nbsp;rest crosses a barline — `normalize`'s single-gap decomposition would straddle one on a three-bar 3/4<br>&nbsp;&nbsp;&nbsp;&nbsp;node; preview and command share that one helper, proven by an equivalence test on 3/4 where the two<br>&nbsp;&nbsp;&nbsp;&nbsp;fills diverge rather than on 4/4 where they coincide. Empty armed voice previews as though rest-filled;<br>&nbsp;&nbsp;&nbsp;&nbsp;non-empty-but-incomplete deliberately still `std::nullopt`; `kRest` also materializes; non-empty path<br>&nbsp;&nbsp;&nbsp;&nbsp;behaviorally unchanged. Isolation holds through creation and rollback — a chained `SetEventCommand`<br>&nbsp;&nbsp;&nbsp;&nbsp;failure leaves the voice completely empty, not half-materialized. `decompose_rest` split into a<br>&nbsp;&nbsp;&nbsp;&nbsp;duration-only core so the preview no longer mints/discards a `Uuid` per rest per pointer move;<br>&nbsp;&nbsp;&nbsp;&nbsp;verified mechanically behavior-preserving (five mechanical line differences, DP table/tie-breaking/<br>&nbsp;&nbsp;&nbsp;&nbsp;64-term cap/overflow-safe scaling/non-dyadic rejection byte-identical, 13 `DecomposeRestTest` cases<br>&nbsp;&nbsp;&nbsp;&nbsp;and 13 call sites untouched). Reviewer-approved exact tree: debug 1911/1911 warning-clean on forced<br>&nbsp;&nbsp;&nbsp;&nbsp;full recompilation, full lint, all seven audits, clang-tidy 18 writer-OFF 76/76 zero diagnostics on<br>&nbsp;&nbsp;&nbsp;&nbsp;forced reanalysis, ASan/UBSan 1911/1911 with 11 expected writer-resource skips, TSan N/A. Remaining<br>&nbsp;&nbsp;&nbsp;&nbsp;in this phase: audition preview request.)
- [x] &nbsp;&nbsp;Note-audition preview-request increment completed<br>&nbsp;&nbsp;&nbsp;&nbsp;(`NoteAuditionRequest` — `TrackId`, ascending deduplicated `MidiPitch` set, one `MidiVelocity` — declared in<br>&nbsp;&nbsp;&nbsp;&nbsp;`graphscore_core`, not `graphscore_notation`, because ADR 0003 assigns note-preview audition to<br>&nbsp;&nbsp;&nbsp;&nbsp;`graphscore_writer_audio`, whose permitted edges exclude the producing layer; every field is already a core<br>&nbsp;&nbsp;&nbsp;&nbsp;type, so no new architecture edge either way. No duration field — M08's preview is short and *fixed*, an<br>&nbsp;&nbsp;&nbsp;&nbsp;audio-layer constant, not the notated duration. New pure query `audition_for_note_entry` mirrors<br>&nbsp;&nbsp;&nbsp;&nbsp;`make_note_entry_command`'s parameters and is evaluated pre-execution. Adding a pitch to a chord auditions<br>&nbsp;&nbsp;&nbsp;&nbsp;the whole resulting chord; pure duration changes, all `kRest` branches, and rejected clicks are silent.<br>&nbsp;&nbsp;&nbsp;&nbsp;Velocity is `velocity_for_dynamic(default_dynamic())` alone, emphasis/hairpin/context resolution<br>&nbsp;&nbsp;&nbsp;&nbsp;deliberately omitted per `playback_mapping.hpp`'s math-only scope. Enharmonic dedup is load-bearing:<br>&nbsp;&nbsp;&nbsp;&nbsp;C-sharp 4 onto a chord holding D-flat 4 is a real notated extension but one sounding pitch. Divergence is<br>&nbsp;&nbsp;&nbsp;&nbsp;structurally impossible — `make_note_entry_command` refactored onto one shared `resolve_note_entry`, verified<br>&nbsp;&nbsp;&nbsp;&nbsp;mechanically against `HEAD` with rejection-check order byte-for-byte preserved and all 71 pre-existing<br>&nbsp;&nbsp;&nbsp;&nbsp;entry/preview cases unmodified; one `sounding_pitches.empty()` predicate silences all five silent branches,<br>&nbsp;&nbsp;&nbsp;&nbsp;so a future branch fails safe. Nothing consumes the request; the pitch-edit trigger attaches with the Up/Down<br>&nbsp;&nbsp;&nbsp;&nbsp;and accidental commands below. Reviewer-approved exact tree: debug 1925/1925 warning-clean, full lint 340<br>&nbsp;&nbsp;&nbsp;&nbsp;files, all seven audits, clang-tidy 18 writer-OFF zero diagnostics on forced reanalysis, ASan/UBSan 1925/1925<br>&nbsp;&nbsp;&nbsp;&nbsp;with 11 expected writer-resource skips, release warning-clean under `NDEBUG`, TSan N/A. This closes the<br>&nbsp;&nbsp;&nbsp;&nbsp;"Note palette and pointer entry" phase — all six deliverables are now checked.)
- [ ] Selection and keyboard behavior completed
- [ ] Whole-measure selection and copy/paste completed
- [ ] Pointer-drag arbitrary-range selection and copy/paste completed
- [ ] Clipboard boundary/span and paste-preview behavior validated
- [ ] Structural editing completed
- [ ] Playback semantics in the editor model completed
- [ ] Accessibility completed
- [ ] Acceptance criteria passed
- [ ] Test focus completed

## Milestone 06: Infinite Graph Canvas

- [ ] [Milestone 06 complete](06-graph-canvas.md)
- [ ] Dependencies completed
- [ ] Infinite viewport completed
- [ ] Notation nodes completed
- [ ] Connector creation and semantics completed
- [ ] Orthogonal route editing completed
- [ ] Selection and playback affordances completed
- [ ] Organization operations completed
- [ ] Acceptance criteria passed
- [ ] Test focus completed

## Milestone 07: Adaptive Tempo And Transitions

- [ ] [Milestone 07 complete](07-adaptive-transitions.md)
- [ ] Dependencies completed
- [ ] Tempo lane completed
- [ ] Sequential routing completed
- [ ] Vertical routing completed
- [ ] Pickdown overlap completed
- [ ] Event and random APIs completed
- [ ] Writer graph feedback completed
- [ ] Acceptance criteria passed
- [ ] Test focus completed

## Milestone 08: Writer Audio And VST3 Hosting

- [ ] [Milestone 08 complete](08-audio-vst.md)
- [ ] Dependencies completed
- [ ] Audio devices and engine completed
- [ ] Track plugin chains completed
- [ ] Plugin scanning and compatibility completed
- [ ] In-process hosting question resolved (carried from ADR 0007)
- [ ] Plugin editors and parameters completed
- [ ] Audition mixer completed
- [ ] Transport and preview completed
- [ ] Acceptance criteria passed
- [ ] Test focus completed

## Milestone 09: Integrated Writer Workflow

- [ ] [Milestone 09 complete](09-writer-integration.md)
- [ ] Dependencies completed
- [ ] Document lifecycle completed
- [ ] Live playback snapshots completed
- [ ] Graph playback interaction completed
- [ ] Composition workflows completed
- [ ] Application preferences completed
- [ ] Acceptance criteria passed
- [ ] Test focus completed

## Milestone 10: Accessibility, Performance, And Hardening

- [ ] [Milestone 10 complete](10-hardening.md)
- [ ] Dependencies completed
- [ ] Accessibility completion audit passed
- [ ] Canvas and notation performance gates passed
- [ ] Realtime and determinism hardening passed
- [ ] Robustness and security hardening passed
- [ ] Platform compatibility gates passed
- [ ] Acceptance criteria passed
- [ ] Test focus completed

## Milestone 11: Engine Integrations And 0.1.0 Release

- [ ] [Milestone 11 complete](11-engine-release.md)
- [ ] Dependencies completed
- [ ] Unity integration completed
- [ ] Unreal integration completed
- [ ] SDK and examples completed
- [ ] Release automation completed
- [ ] User and integrator documentation completed
- [ ] Acceptance criteria passed
- [ ] Test focus completed
- [ ] `0.1.0` archives published

## Milestone 12: General MIDI CC

- [ ] [Milestone 12 complete](12-midi-cc.md)
- [ ] Dependencies completed
- [ ] Milestone-start questions resolved
- [ ] Proposed scope reconfirmed after `0.1.0` feedback
- [ ] General CC authoring completed
- [ ] General CC persistence/export completed
- [ ] General CC runtime behavior completed
- [ ] General CC accessibility completed
- [ ] Acceptance criteria passed
- [ ] Test focus completed
