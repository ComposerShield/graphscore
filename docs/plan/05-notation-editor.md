# Milestone 05: Notation Engraving And Editing

## Goal

Provide a polished, directly editable focused notation system inside a node, with complete keyboard and screen-reader workflows.

## Dependencies

- [x] M5-phase-1 Milestone 02 domain and command model. **(Every domain primitive this
      milestone calls is delivered and tested: the `Selection` variant,
      `NotationFragment` with clipping rules R1–R12, `CutFragmentCommand`/
      `PasteFragmentCommand`, `DuplicateNodesCommand`, the measure/timeline
      cascade commands, and the validation service. Milestone 02's own
      completion box stays open for reasons that do not gate 05:
      `route-segment` selection is deferred to M06, and drag-gesture
      transaction grouping is a writer concern that lands here in 05/06.)**
- [x] M5-phase-2 Rendering/engraving/accessibility decisions from Milestone 00.
- [x] M5-phase-3 Milestone 01 writer shell.

## Deliverables

### Incremental engraving

- [x] M5-phase-4 Lay out all active tracks and their one-or-more staves against the node's common measure timeline.
- [x] M5-phase-5 Render staff systems, barlines, standard clefs, key/time signatures, notes, chords, rests, dots, accidentals through double, stems, flags, beams, ledger lines, ties, tuplets, and grace notes.
- [x] M5-phase-6 Render dynamics, hairpins, slurs, accent, marcato, staccato, staccatissimo, tenuto, and pedal markings.
- [x] M5-phase-7 Support up to four voices with conventional collision, stem, rest, and accidental placement.
- [x] M5-phase-8 Re-layout only affected measures/systems after local edits while retaining stable hit-test identities.
- [x] M5-phase-9 Keep layout and render commands toolkit-neutral and testable without a window.

### Note palette and pointer entry

- [x] M5-phase-10 Palette controls for durations through sixty-fourth, dots, rests, voices, tuplets, ties/slurs, articulations, dynamics, and other in-scope markings.
- [x] M5-phase-11 Show a yellow semitransparent note/rest preview at the candidate staff pitch and nearest valid onset before click.
- [x] M5-phase-12 Clicking an existing rhythmic event in the selected voice changes its selected duration; clicking another pitch at the onset builds a chord.
- [x] M5-phase-13 Replacing/shortening material creates normalized automatic rests rather than overlaps or implicit gaps.
- [x] M5-phase-14 A different rhythmic stream is created only after the composer explicitly selects another voice.
- [x] M5-phase-15 Newly inserted or pitch-edited notes issue a short preview request; actual plugin audition is connected in Milestone 08.

### Selection and keyboard behavior

- [x] M5-phase-16 Select individual noteheads, whole chord events, rests, markings, ranges, and insertion carets through explicit hit regions.
  - [x] M5-phase-16a Represent rests and markings in the domain selection model.
  - [x] M5-phase-16b Emit stem hit regions so a whole chord event has clickable geometry.
  - [x] M5-phase-16c Resolve a pointer position to a notehead, chord, or rest selection.
  - [x] M5-phase-16d Resolve a pointer position on blank staff area to an insertion caret.
  - [x] M5-phase-16e Resolve marking hit regions to dynamic, hairpin, slur, pedal span, articulation, tie, and tuplet selections.
  - [x] M5-phase-16f Emit a notehead-column hit region, below the noteheads' own priority, so a stemless whole-note chord can select its whole event.
  - [x] M5-phase-16g Resolve range selections through hit regions; the selection tool and measure targets below drive them.
  - [x] M5-phase-16h Resolve the hit-priority and voice-disambiguation gaps the resolvers above left open: a tie segment's fixed four-staff-space band shadows the articulation glyphs on a chord carrying both, and blankets a close-voiced stemless chord so the notehead-column affordance cannot reach a tied whole-note chord; two stemless chords in different voices at one onset emit overlapping equal-area columns that `hit_test` separates only by `Uuid` ordering, so a click can select the other voice's chord.
  - [x] M5-phase-16i Reconcile per-system tuplet-digit suppression with the domain's own run keying: the engraver suppresses against the first record in the system it is laying out, while the domain keys `kIncompleteTupletGroup` to the true global run start, so the two can disagree across a system break.
- [x] M5-phase-17 Add a dedicated selection tool whose pointer drag creates a contiguous musical-time selection across the intersected staves/voices rather than selecting engraving glyph bounds individually.
  - [x] M5-phase-17a Resolve a two-point drag to a contiguous musical-time range selection across the intersected staves and voices.
  - [x] M5-phase-17b Hold drag state and the active tool at the app layer, and wire pointer press/move/release to the resolver.
  - [x] M5-phase-17c Show a live selection extent during and after the drag.
- [x] M5-phase-18 Add measure hit targets/actions for selecting one complete measure on the focused staff/track and extending that aligned measure selection across additional tracks.
  - [x] M5-phase-18a Emit a per-staff measure hit region and resolve a pointer position on it to a one-measure full-measure selection on that staff/track.
  - [x] M5-phase-18b Extend an aligned measure selection across additional chosen tracks/staves.
- [x] M5-phase-19 Support Shift/keyboard range extension and accessible start/end/staff-scope controls that produce the same selection as pointer dragging.
  - [x] M5-phase-19a Resolve keyboard range extension and explicit start/end/staff-scope controls in the notation layer from musical coordinates, through the same resolution the pointer drag uses.
  - [x] M5-phase-19b Wire platform-neutral key events through the writer shell and app so Shift extension and the accessible controls drive those resolvers.
    - [x] M5-phase-19b-i Hold a keyboard-set committed range selection and expose score order in the notation layer.
    - [x] M5-phase-19b-ii Deliver platform-neutral key events carrying modifiers through the writer shell.
    - [x] M5-phase-19b-iii Interpret Shift extension and the accessible start/end/staff-scope controls in the app.
- [x] M5-phase-20 Up/Down moves a selected notehead one diatonic staff step and preserves its accidental.
- [x] M5-phase-21 `-` and `=` step through double-flat, flat, natural, sharp, and double-sharp.
- [x] M5-phase-22 Delete removes the selected notehead and selects the prior onset in the same voice/staff; when none exists it leaves an insertion caret at the deleted onset. Removing the last chord pitch leaves a normalized rest.
- [x] M5-phase-23 `R` converts the entire selected note/chord event to an equal-duration rest.
- [x] M5-phase-24 Primary is Command on macOS and Control on Windows/Linux. Primary+Up/Down moves to the prior/next staff, wraps within the node, and selects the same-voice note nearest the musical position, then the visually nearest note on a tie, or places a caret.
- [x] M5-phase-25 `2` through `8` add a key-spelled diatonic interval above; Shift variants add below; the inserted notehead becomes selected and `1` remains a no-op.
- [x] M5-phase-26 Record a platform-normalized action table before UI binding, including physical/logical key behavior on non-US layouts, focus contexts, tie-breaking, and every no-selection fallback.
- [x] M5-phase-27 Add keyboard step entry, explicit voice shortcuts, duration actions, range extension, cut/copy/paste, and command-palette discoverability without conflicting with interval keys.

#### Platform-normalized action table

The normative, implementation-ready action table for M5-phase-26 — platform
modifier mapping, physical/logical key identity on non-US layouts, focus
contexts and routing, precedence and tie-breaking, the complete binding set
including the M5-phase-27 additions, the step-entry cursor lifecycle and pitch
reference, the clipboard handoff, the command-palette discoverability route,
and the collision-free interval rule — lives in
[05-notation-editor-action-table.md](05-notation-editor-action-table.md).

### Structural editing

- [x] M5-phase-28 Insert/delete measures across every track and update signatures, clefs, tempo anchors, spans, selection, and rests atomically.
  - [x] M5-phase-28a Resolve an aligned measure selection to the domain insert/append/delete command and remap the selection the edit invalidates.
  - [x] M5-phase-28b Route the structural measure actions through the command palette and record them in the action table.
- [x] M5-phase-29 Create and edit arbitrary single-level `N:M` tuplets without allowing nested tuplets.
- [ ] M5-phase-30 Edit articulations, dynamics, ties, slurs, hairpins, pedal spans, beam breaks/joins, and stem overrides with clear invalid-target feedback.
- [ ] M5-phase-31 Create the final pickdown through an explicit node-end duration setting and show the transition boundary distinctly.
- [ ] M5-phase-32 Copy/paste one or more complete selected measures to an explicitly chosen destination measure and staff/track scope.
- [ ] M5-phase-33 Cut/copy/paste arbitrary non-measure-aligned selections, including partial beats and multi-staff fragments, while preserving valid rhythm.
- [ ] M5-phase-34 Show a translucent paste preview at the destination caret/range before commit, including affected staves and duration.
- [ ] M5-phase-35 Default paste replaces only the corresponding destination time/staff/voice range, preserves all material outside it, fills uncovered time with normalized rests, and commits as one undoable transaction.
- [ ] M5-phase-36 Apply the domain-defined clipping/reconnection policy to ties, slurs, tuplets, hairpins, pedal spans, signatures, clefs, dynamics, and other boundary-crossing entities, with diagnostics when a fragment cannot be pasted validly.
- [ ] M5-phase-37 Range operations also include delete and diatonic/chromatic transpose while preserving valid rhythm.

#### Carried obligation: step-entry cursor after a structural edit

A measure insert, append, or delete remaps the committed selection
(M5-phase-28a) but leaves the app-owned step-entry cursor's absolute
node-time position untouched, so after a structural edit the cursor can name
different music than before, or a position past the node's end. This
degrades safely today — the step-entry availability checks trial-execute
against a project copy and report the action unavailable rather than
corrupting anything — and the clipboard's cut path has had the identical
property since M5-phase-27. Whichever phase takes up step-entry cursor
revalidation owns remapping the cursor across both.

### Playback semantics in the editor model

- [ ] M5-phase-38 Grace notes steal configured/default time from the preceding note.
- [ ] M5-phase-39 Dynamics use project-wide editable velocity defaults; hairpins interpolate note-on velocities.
- [ ] M5-phase-40 Slurs create legato overlap unless an explicit articulation overrides it.
- [ ] M5-phase-41 Articulations affect documented note velocity and/or duration.
- [ ] M5-phase-42 Pedal spans generate the explicit MIDI CC64 exception.

### Accessibility

- [ ] M5-phase-43 Expose node, track, staff, measure, voice, note/rest, marking, palette, and selection semantics rather than glyph primitives.
- [ ] M5-phase-44 Announce pitch spelling, sounding pitch, duration, voice, bar/beat, selected state, and available actions.
- [ ] M5-phase-45 Support complete note entry/editing without pointer input.
- [ ] M5-phase-46 Keep focus stable across incremental layout and expose offscreen musical elements through a virtualized semantic tree.
- [ ] M5-phase-47 Validate the workflow with VoiceOver, Narrator, and Orca on both Wayland and X11/XWayland.

## Acceptance Criteria

- [ ] M5-phase-48 Every in-scope notation entity can be created, selected, edited, deleted, copied, undone, redone, saved, and rendered.
- [ ] M5-phase-49 A composer can copy a complete measure to another measure and copy a pointer-dragged partial range to another valid musical position on the same or different compatible staves.
- [ ] M5-phase-50 Engraving remains deterministic for golden fixtures across supported platforms, allowing documented raster tolerance only at the backend boundary.
- [ ] M5-phase-51 Four voices, tuplets, grace notes, cross-measure ties, signatures, and markings survive complex measure edits without invalid references.
- [ ] M5-phase-52 The specified keyboard workflow is covered by automated command tests and manual platform shortcut checks.
- [ ] M5-phase-53 The action table defines a complete step-entry protocol and has keyboard-layout tests rather than relying on US key labels alone.
- [ ] M5-phase-54 A screen-reader user can navigate staves, identify notes, change pitch/duration/accidental/voice, and perform undo/redo.
- [ ] M5-phase-55 Editing a representative 64-track, 64-measure node remains responsive when only a small visible region changes.

## Test Focus

- [ ] M5-phase-56 Toolkit-neutral layout goldens and semantic geometry assertions.
- [ ] M5-phase-57 Hit-testing around dense chords, accidentals, overlapping voices, beams, and spans.
- [ ] M5-phase-58 Duration replacement, automatic-rest normalization, chord building, and explicit voice isolation.
- [ ] M5-phase-59 Keyboard command tables, wrapping staff focus, selection recovery, and interval spelling in every standard key.
- [ ] M5-phase-60 Round-trip edit/undo/redo tests for every notation command.
- [ ] M5-phase-61 Measure and arbitrary-range paste tests cover empty/occupied destinations, multiple voices/staves, partial beats, tuplets, boundary-crossing spans, UUID remapping, preview/commit equality, and undo/redo.
- [ ] M5-phase-62 Accessibility tree snapshots and action invocation tests.
- [ ] M5-phase-63 Property-style generated valid measures checked for non-overlap and complete voice duration.
