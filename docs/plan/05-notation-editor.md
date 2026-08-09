# Milestone 05: Notation Engraving And Editing

## Goal

Provide a polished, directly editable focused notation system inside a node, with complete keyboard and screen-reader workflows.

## Dependencies

- [x] Milestone 02 domain and command model. **(Every domain primitive this
      milestone calls is delivered and tested: the `Selection` variant,
      `NotationFragment` with clipping rules R1–R12, `CutFragmentCommand`/
      `PasteFragmentCommand`, `DuplicateNodesCommand`, the measure/timeline
      cascade commands, and the validation service. Milestone 02's own
      completion box stays open for reasons that do not gate 05:
      `route-segment` selection is deferred to M06, and drag-gesture
      transaction grouping is a writer concern that lands here in 05/06.)**
- [x] Rendering/engraving/accessibility decisions from Milestone 00.
- [x] Milestone 01 writer shell.

## Deliverables

### Incremental engraving

- [x] Lay out all active tracks and their one-or-more staves against the node's common measure timeline.
- [x] Render staff systems, barlines, standard clefs, key/time signatures, notes, chords, rests, dots, accidentals through double, stems, flags, beams, ledger lines, ties, tuplets, and grace notes.
- [x] Render dynamics, hairpins, slurs, accent, marcato, staccato, staccatissimo, tenuto, and pedal markings.
- [x] Support up to four voices with conventional collision, stem, rest, and accidental placement.
- [x] Re-layout only affected measures/systems after local edits while retaining stable hit-test identities.
- [x] Keep layout and render commands toolkit-neutral and testable without a window.

### Note palette and pointer entry

- [x] Palette controls for durations through sixty-fourth, dots, rests, voices, tuplets, ties/slurs, articulations, dynamics, and other in-scope markings. **(Toolkit-neutral palette state model only: `NotePaletteState`/`NotePaletteEntrySpec` in `graphscore_notation`. Applying an armed control to the score is deliberately deferred — durations/dots/rests to the pointer-entry bullets below, voices likewise, tuplets and every marking to Structural editing.)**
- [x] Show a yellow semitransparent note/rest preview at the candidate staff pitch and nearest valid onset before click. **(Toolkit-neutral `NotationPreview`/`preview_note_entry` in `graphscore_notation`: a pure query resolving a point to a staff, a natural diatonic staff step, and the nearest existing event onset in the armed voice, plus the standalone preview geometry the palette would insert there. The yellow semitransparent appearance is deliberately not a property of these commands — `NotationCommand` has no color/alpha field and ADR 0003 forbids a `graphscore_notation` → `graphscore_rendering` edge, so the preview carries its own command list for a later, separate `rasterize_notation` pass with its own translucent `RasterOptions`. That second pass is wired when the writer shell composites, not here.)**
- [x] Clicking an existing rhythmic event in the selected voice changes its selected duration; clicking another pitch at the onset builds a chord. **(Toolkit-neutral `make_note_entry_command` consuming `NotePaletteEntrySpec` duration/kind and selected voice plus the preview-chosen onset/pitch; returns reversible `SetEventCommand` for `CommandHistory`; same-pitch duration replacement; rest↔note conversion; note→chord and chord extension; event identity preservation with exact undo/redo; all five event-reference families remapped on top-level-ID changes using conservative revision full-reset; pre-existing articulations and stem direction preserved while armed markings and plugin audition remain deferred. Reviewer-approved final evidence: debug 1855/1855, full lint, all seven architecture audits, canonical clang-tidy 18 writer-OFF, ASan/UBSan writer-OFF 1844 pass + 11 expected writer-only skips; TSan N/A for synchronous caller-owned mutation.)**
- [x] Replacing/shortening material creates normalized automatic rests rather than overlaps or implicit gaps. **(Automatic-rest normalization through reversible `SetEventCommand`/`VoiceContent::replace_event`: shortening inserts exact idiomatic automatic rests and preserves later event onsets; expansion consumes only contiguous following rests, preserves split-rest identity, and rejects insufficient space atomically; `decompose_rest` corrected from locally greedy dead-end to bounded exact minimal-rest decomposition with deterministic largest-duration tie-breaking, a 64-rest limit, overflow-safe Rational scaling, non-dyadic rejection, and cap-boundary behavior — all with exact undo/redo. Focused regressions include 5/128 and 17/128 greedy-dead-end cases, domain replacement, pointer command, cap/overflow/non-dyadic boundaries, exact undo/redo, and deterministic property-style complete/non-overlapping voice coverage. Reviewer-approved final evidence: debug 1882/1882; full lint; all seven architecture audits; canonical clang-tidy 18 writer-OFF 72/72; ASan/UBSan 1882/1882 with 11 expected writer-rendering skips; TSan N/A (synchronous non-concurrent domain editing); focused M05 family 65/65.)**
- [x] A different rhythmic stream is created only after the composer explicitly selects another voice. **(Toolkit-neutral explicit voice-stream workflow. Arming a voice has no domain effect on its own; the first pointer entry into an *entirely empty* armed voice returns one `CommandTransaction` composing a new reversible `CreateVoiceStreamCommand` with the ordinary `SetEventCommand`, so a single undo removes note and stream together and returns the voice to zero events, and redo is id-for-id. New `decompose_measure_aligned_rests` / `decompose_measure_aligned_rest_durations` in `graphscore_domain` decompose each main-region measure independently and the pickdown as its own trailing group, so no automatic rest crosses a barline — `VoiceContent::normalize`'s single-gap decomposition would emit a barline-straddling dotted-whole plus dotted-half on a three-bar 3/4 node. Preview and command consume that one helper, proven by an equivalence test on 3/4 where the measure-aligned and naive fills genuinely diverge rather than on 4/4 where they coincide. `preview_note_entry` treats an entirely empty armed voice as though already rest-filled and snaps to the hypothetical fill's onsets; a non-empty but incomplete voice deliberately keeps returning `std::nullopt`. `kRest` into an empty voice also materializes the stream. The non-empty entry path is behaviorally unchanged (still a bare `SetEventCommand`, asserted by `dynamic_cast`). Isolation holds through the creation path and its rollback: only the armed voice is ever written, and a chained `SetEventCommand` failure leaves the voice completely empty rather than half-materialized. `decompose_rest` was split into a duration-only core so the preview no longer mints and discards a `Uuid` per rest on every pointer move; the split was verified mechanically behavior-preserving — 91-line bodies differing only in signature, container type, one `push_back`, one return, and one comment word, with the DP table, largest-duration tie-breaking, 64-term cap, overflow-safe `Rational` scaling, and non-dyadic rejection all in the byte-identical region, all 13 pre-existing `DecomposeRestTest` cases intact and all 13 call sites unchanged. Reviewer-approved exact tree: debug 1911/1911 warning-clean on forced full recompilation; full lint; all seven architecture audits; canonical clang-tidy 18 writer-OFF 76/76 with zero diagnostics on a forced reanalysis; ASan/UBSan 1911/1911 with 11 expected writer-resource skips; TSan N/A (synchronous caller-owned domain mutation, no new threads/atomics/shared state).)**
- [ ] Newly inserted or pitch-edited notes issue a short preview request; actual plugin audition is connected in Milestone 08.

### Selection and keyboard behavior

- [ ] Select individual noteheads, whole chord events, rests, markings, ranges, and insertion carets through explicit hit regions.
- [ ] Add a dedicated selection tool whose pointer drag creates a contiguous musical-time selection across the intersected staves/voices rather than selecting engraving glyph bounds individually.
- [ ] Add measure hit targets/actions for selecting one complete measure on the focused staff/track and extending that aligned measure selection across additional tracks.
- [ ] Support Shift/keyboard range extension and accessible start/end/staff-scope controls that produce the same selection as pointer dragging.
- [ ] Up/Down moves a selected notehead one diatonic staff step and preserves its accidental.
- [ ] `-` and `=` step through double-flat, flat, natural, sharp, and double-sharp.
- [ ] Delete removes the selected notehead and selects the prior onset in the same voice/staff; when none exists it leaves an insertion caret at the deleted onset. Removing the last chord pitch leaves a normalized rest.
- [ ] `R` converts the entire selected note/chord event to an equal-duration rest.
- [ ] Primary is Command on macOS and Control on Windows/Linux. Primary+Up/Down moves to the prior/next staff, wraps within the node, and selects the same-voice note nearest the musical position, then the visually nearest note on a tie, or places a caret.
- [ ] `2` through `8` add a key-spelled diatonic interval above; Shift variants add below; the inserted notehead becomes selected and `1` remains a no-op.
- [ ] Add keyboard step entry, explicit voice shortcuts, duration actions, range extension, cut/copy/paste, and command-palette discoverability without conflicting with interval keys.
- [ ] Record a platform-normalized action table before UI binding, including physical/logical key behavior on non-US layouts, focus contexts, tie-breaking, and every no-selection fallback.

### Structural editing

- [ ] Insert/delete measures across every track and update signatures, clefs, tempo anchors, spans, selection, and rests atomically.
- [ ] Create and edit arbitrary single-level `N:M` tuplets without allowing nested tuplets.
- [ ] Edit articulations, dynamics, ties, slurs, hairpins, pedal spans, beam breaks/joins, and stem overrides with clear invalid-target feedback.
- [ ] Create the final pickdown through an explicit node-end duration setting and show the transition boundary distinctly.
- [ ] Copy/paste one or more complete selected measures to an explicitly chosen destination measure and staff/track scope.
- [ ] Cut/copy/paste arbitrary non-measure-aligned selections, including partial beats and multi-staff fragments, while preserving valid rhythm.
- [ ] Show a translucent paste preview at the destination caret/range before commit, including affected staves and duration.
- [ ] Default paste replaces only the corresponding destination time/staff/voice range, preserves all material outside it, fills uncovered time with normalized rests, and commits as one undoable transaction.
- [ ] Apply the domain-defined clipping/reconnection policy to ties, slurs, tuplets, hairpins, pedal spans, signatures, clefs, dynamics, and other boundary-crossing entities, with diagnostics when a fragment cannot be pasted validly.
- [ ] Range operations also include delete and diatonic/chromatic transpose while preserving valid rhythm.

### Playback semantics in the editor model

- [ ] Grace notes steal configured/default time from the preceding note.
- [ ] Dynamics use project-wide editable velocity defaults; hairpins interpolate note-on velocities.
- [ ] Slurs create legato overlap unless an explicit articulation overrides it.
- [ ] Articulations affect documented note velocity and/or duration.
- [ ] Pedal spans generate the explicit MIDI CC64 exception.

### Accessibility

- [ ] Expose node, track, staff, measure, voice, note/rest, marking, palette, and selection semantics rather than glyph primitives.
- [ ] Announce pitch spelling, sounding pitch, duration, voice, bar/beat, selected state, and available actions.
- [ ] Support complete note entry/editing without pointer input.
- [ ] Keep focus stable across incremental layout and expose offscreen musical elements through a virtualized semantic tree.
- [ ] Validate the workflow with VoiceOver, Narrator, and Orca on both Wayland and X11/XWayland.

## Acceptance Criteria

- [ ] Every in-scope notation entity can be created, selected, edited, deleted, copied, undone, redone, saved, and rendered.
- [ ] A composer can copy a complete measure to another measure and copy a pointer-dragged partial range to another valid musical position on the same or different compatible staves.
- [ ] Engraving remains deterministic for golden fixtures across supported platforms, allowing documented raster tolerance only at the backend boundary.
- [ ] Four voices, tuplets, grace notes, cross-measure ties, signatures, and markings survive complex measure edits without invalid references.
- [ ] The specified keyboard workflow is covered by automated command tests and manual platform shortcut checks.
- [ ] The action table defines a complete step-entry protocol and has keyboard-layout tests rather than relying on US key labels alone.
- [ ] A screen-reader user can navigate staves, identify notes, change pitch/duration/accidental/voice, and perform undo/redo.
- [ ] Editing a representative 64-track, 64-measure node remains responsive when only a small visible region changes.

## Test Focus

- [ ] Toolkit-neutral layout goldens and semantic geometry assertions.
- [ ] Hit-testing around dense chords, accidentals, overlapping voices, beams, and spans.
- [ ] Duration replacement, automatic-rest normalization, chord building, and explicit voice isolation.
- [ ] Keyboard command tables, wrapping staff focus, selection recovery, and interval spelling in every standard key.
- [ ] Round-trip edit/undo/redo tests for every notation command.
- [ ] Measure and arbitrary-range paste tests cover empty/occupied destinations, multiple voices/staves, partial beats, tuplets, boundary-crossing spans, UUID remapping, preview/commit equality, and undo/redo.
- [ ] Accessibility tree snapshots and action invocation tests.
- [ ] Property-style generated valid measures checked for non-overlap and complete voice duration.
