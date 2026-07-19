# Milestone 05: Notation Engraving And Editing

## Goal

Provide a polished, directly editable focused notation system inside a node, with complete keyboard and screen-reader workflows.

## Dependencies

- [ ] Milestone 02 domain and command model.
- [ ] Rendering/engraving/accessibility decisions from Milestone 00.
- [ ] Milestone 01 writer shell.

## Deliverables

### Incremental engraving

- [ ] Lay out all active tracks and their one-or-more staves against the node's common measure timeline.
- [ ] Render staff systems, barlines, standard clefs, key/time signatures, notes, chords, rests, dots, accidentals through double, stems, flags, beams, ledger lines, ties, tuplets, and grace notes.
- [ ] Render dynamics, hairpins, slurs, accent, marcato, staccato, staccatissimo, tenuto, and pedal markings.
- [ ] Support up to four voices with conventional collision, stem, rest, and accidental placement.
- [ ] Re-layout only affected measures/systems after local edits while retaining stable hit-test identities.
- [ ] Keep layout and render commands toolkit-neutral and testable without a window.

### Note palette and pointer entry

- [ ] Palette controls for durations through sixty-fourth, dots, rests, voices, tuplets, ties/slurs, articulations, dynamics, and other in-scope markings.
- [ ] Show a yellow semitransparent note/rest preview at the candidate staff pitch and nearest valid onset before click.
- [ ] Clicking an existing rhythmic event in the selected voice changes its selected duration; clicking another pitch at the onset builds a chord.
- [ ] Replacing/shortening material creates normalized automatic rests rather than overlaps or implicit gaps.
- [ ] A different rhythmic stream is created only after the composer explicitly selects another voice.
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
- [ ] Edit ties, slurs, hairpins, pedal spans, beam breaks/joins, and stem overrides with clear invalid-target feedback.
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
