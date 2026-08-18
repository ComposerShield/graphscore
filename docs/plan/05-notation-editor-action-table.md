# Milestone 05 — Platform-Normalized Action Table (M5-phase-26)

This document is the normative, implementation-ready keyboard/action binding
and focus-routing specification for Milestone 05's notation editing. It is the
source of truth that M5-phase-27's UI binding implements. It is planning, not a
changelog or an evidence dump: it resolves every deferred key question, and it
defines what M5-phase-27 must build. The checklist line `M5-phase-26` in
[05-notation-editor.md](05-notation-editor.md) is checked by the orchestrator
after this document is approved, not by this document.

## 1. Scope and architecture ownership

ADR 0003 assigns the three responsibilities this table touches, and nothing
below changes that assignment:

- `graphscore_writer_shell` owns **platform input translation** — SDL3
  physical scancode and modifier mask to platform-neutral `KeyCode` /
  `KeyModifiers` — and **platform clipboard translation**. This table is
  consumed by the shell only as a list of keys the shell must be able to
  identify; the shell performs no action routing.
- `graphscore_writer_app` (application assembly) owns **action routing**:
  mapping a `(KeyCode, KeyModifiers)` pair, plus the active focus context and
  tool, to a domain/notation action.
- `graphscore_notation` and `graphscore_domain` remain platform- and
  toolkit-neutral. Every action this table names is an already-delivered or
  already-specified domain/notation primitive (a `Command`, a selection
  resolver, or `NotePaletteState`); no platform or third-party type crosses
  their boundary.
- There is **no runtime impact**: nothing here reaches `graphscore_runtime`
  or any clean-layer target.

Explicitly **out of scope** here, and therefore not claimed by this table:
platform accessibility bridges and AT key routing (deferred indefinitely under
the product-scope decision in `README.md`), native plugin-editor focus handling
(M8), canvas bindings (M6), OS
clipboard interchange and clipboard persistence (a later phase — this table
defines only the in-memory handoff to `CutFragmentCommand` /
`PasteFragmentCommand`), and any toolbar/UI presentation beyond the key chords
themselves (the command palette's keyboard-routing contract is in scope, §11;
its visual presentation is not).

## 2. Modifier model

Four modifier bits are delivered by the shell (`KeyModifiers`): `shift`,
`control`, `alt`, `meta` (`meta` is Command on macOS, the Windows/Super key
elsewhere).

- **Primary** is resolved at application assembly, never baked into the shell:
  **Command (meta) on macOS, Control on Windows/Linux** (M5-phase-24;
  `PrimaryModifier` / `kPlatformPrimaryModifier` in `key_bindings.hpp`).
- **Alt** is **Option on macOS, Alt on Windows/Linux** (the shell maps
  `SDL_KMOD_ALT`, which macOS reports for Option).
- The **non-Primary modifier** — Control on macOS, Meta/Windows-key on
  Windows/Linux — carries **no binding** in this table. Chords using it are
  no-ops and are reserved for future milestones.

## 3. Exact modifier matching and tie-breaking

Every binding is an **exact chord**: the chord's own modifier is required and
every other modifier must be clear. This is the discipline M5-phase-20/21/23
already follow, generalized to all five chord classes.

The five chord classes are defined by **disjoint** conditions, so one key event
matches at most one class, and evaluation order is irrelevant. They are five
**disjoint bound classes, not a partition** — the modifier space is larger than
their union — and everything outside them is a single explicit **unbound
remainder**. This table is therefore a total function from `(chord class, key)`
to an action, with the remainder mapping every combination outside the five
classes to a no-op.

| Class | Condition |
|---|---|
| Unmodified | `shift=control=alt=meta=false` |
| Shift | `shift=true`, `control=alt=meta=false` |
| Primary | Primary set, `shift=false`, `alt=false`, non-Primary of `{control, meta}` clear |
| Shift+Primary | Primary set, `shift=true`, `alt=false`, non-Primary clear |
| Alt | `alt=true`, `shift=control=meta=false` |

**Numpad is not a sixth chord class.** Numpad keys are a **key family within
the Unmodified class**, distinguished by physical key identity
(`SDL_SCANCODE_KP_*`, §4), never by modifiers: an unmodified numpad key is an
Unmodified chord whose key is a numpad key, and a modified numpad key falls
into whichever of Shift/Primary/Alt (or a mixed) class its modifiers produce.
The numpad/non-numpad distinction partitions the Unmodified class's *key
space*, not the chord classes, and §7.1 (non-numpad) and §7.6 (numpad)
together exhaust the Unmodified class.

The **unbound remainder** — any other combination, including a mixed chord
(e.g. Shift+Alt) and the non-Primary modifier — is an **explicit no-op**
(unbound), not a sixth bound class.

Tie-breaking: because the table is a function, no `(chord, key)` pair is
ambiguous. Where one action is reachable from two keys (Backspace and Delete
both delete a notehead), they are two rows mapping to the one action, not an
ambiguity. `1` and `Shift+1` are both explicit no-ops (M5-phase-25).

## 4. Key identity: physical vs logical

A key press has two identity axes: **physical** (SDL physical scancode = USB
HID position, layout-independent) and **logical** (SDL keycode = the character
the active layout produces). This section resolves the question the writer
shell's header explicitly deferred to this table.

**Binding rule by key class:**

- **Physical** for every positional, navigation, editing, digit, and symbol
  key: arrows, Home, End, PageUp/PageDown (future), Backspace, Delete,
  Enter/Return, Escape, Tab, Space, top-row digits `0`–`9`, `-`, `=`, and all
  numpad keys. These are positional or layout-invariant, and their unmodified
  position is the canonical home.
- **Logical** for **letter mnemonics**: `R` (rest), `N` (step entry), `A`–`G`
  (pitch), `C`/`X`/`V` (copy/cut/paste), `Z` (undo/redo), `K` (command
  palette). A mnemonic must mean the same character on every layout.

**Rationale (non-US layouts):**

- Top-row digits must be physical: on AZERTY/QWERTZ the logical digits require
  Shift/AltGr, which would collide with the Shift = "interval below / range"
  semantics and force Shift for the most frequent operations. Physical
  identity also preserves the already-delivered M5-phase-25 interval digits
  byte-for-byte.
- Letter mnemonics must be logical: on AZERTY the physical key that is `R` on
  QWERTY is labelled `T`, so a physical `R` binding would force the composer
  to hunt for the US position of a mnemonic — the exact "relying on US key
  labels alone" failure M5-phase-53 forbids.
- `-` and `=` stay physical: they are symbol keys (not letters), their
  unmodified US position is canonical, and making them logical would force
  Shift/AltGr on AZERTY/QWERTZ, breaking the unmodified-chord discipline the
  accidental ladder depends on. This preserves the delivered M5-phase-21
  behaviour and its shell test.

**Shell consequence (implemented in M5-phase-27, not here):** the shell must
additionally surface a **logical key** for the letter-mnemonic bindings (deliver
the layout-mapped keycode alongside the physical scancode, or a platform-neutral
logical-letter classification), so the app binds letters by intended character.
Every physical binding in this table is already satisfied by the delivered
scancode translation.

## 5. Focus contexts and suppression

A single **focus owner** at application assembly routes each key event to
exactly one context. Deterministic precedence, first match wins:

1. **Modal dialog / blocking overlay** (future): consumes all keys except its
   own; notation suppressed.
2. **Command palette open**: consumes all character keys as filter/text input;
   only the palette's own keys are interpreted — Up/Down to navigate, Enter to
   run, Escape and `Primary+K` to dismiss (§11) — and every notation binding is
   suppressed. `Primary+K` is thus the toggle in the notation context and the
   close in the palette context; it never falls through to a notation binding
   or to the filter field.
3. **Text entry active** (future: node rename, dialog fields, etc.): character
   keys go to text entry; notation suppressed.
4. **Native plugin editor or foreign window has focus** (future, M8/M9):
   notation suppressed. The *policy* — never route a notation action into a
   foreign editor — is fixed here; the mechanism is M8/M9's.
5. **Graph canvas has focus** (future, M6): canvas bindings apply (M6 owns
   them); notation bindings suppressed.
6. **Notation editor** (default): the table in §7 applies.

Within context 6, the **active tool** gates the bindings (see §7's tool
column). This is the complete routing policy; later milestones bind
deterministically on top of it without re-deriving the order.

## 6. Key repeat policy

The shell currently delivers OS auto-repeat as an ordinary press (no repeat
flag). M5-phase-27 must surface `repeat` on the key event (or track key-up at
the app) to honour the two policies below, assigned per binding in §7:

- **repeat-safe** — auto-repeat re-fires the action per repeat. Used for
  idempotent actions (copy, voice arming) and for well-defined sequential
  actions (diatonic move, accidental step, staff step, range extension,
  interval add, undo/redo, step-entry commit).
- **repeat-once** — auto-repeat is suppressed; the action fires once per
  physical key-down until key-up. Used for toggles and for actions where rapid
  re-fire is a footgun (step-entry mode, command palette, paste, cut, and the
  dot cycle — holding the dot key must not cycle 0→1→2→0→1… uncontrollably).

## 7. Binding table

Tool column: **Both** = active in `kNoteEntry` and `kSelection`; **Selection**
= `kSelection` only; **Entry** = `kNoteEntry` only; **Any** = active regardless
of the current tool (clipboard, undo/redo, command palette, and `N` the tool
toggle).

Rows labelled (19b)–(25) record the delivered **actions and their key chords**
verbatim; rows labelled (27) are new. The **Tool** column is the gating phase
27 must ship, not a claim that phases 19b–25 already run in both tools: the
delivered `SelectionToolHandler::on_key_press` (M5-phase-19b-iii through -25)
gates every one of those actions on `kSelection` alone, and phase 27 is what
broadens their applicability to the `Both`/`Any` shown here while adding the
new rows. Nothing in this table claims otherwise.

"Fallback" is the behaviour when the precondition is unmet (no selection,
ineligible selection, empty history/clipboard, or out-of-mode); every fallback
is an explicit no-op unless a diagnostic is stated.

### 7.1 Unmodified chords (non-numpad keys)

Numpad keys are the numpad key family of the same Unmodified class and are
listed separately in §7.6.

| Key | Action (phase) | Tool | Precondition | Fallback | Repeat |
|---|---|---|---|---|---|
| Up | Move single notehead one diatonic step up (20) | Both | single `NoteheadSet` item | no-op | safe |
| Down | Move single notehead one diatonic step down (20) | Both | single `NoteheadSet` item | no-op | safe |
| `-` (physical) | Accidental ladder one rung down (21) | Both | single `NoteheadSet` item | no-op | safe |
| `=` (physical) | Accidental ladder one rung up (21) | Both | single `NoteheadSet` item | no-op | safe |
| Backspace | Delete selected notehead (22) | Both | single `NoteheadSet` item | no-op | safe |
| Delete | Delete selected notehead (22) | Both | single `NoteheadSet` item | no-op | safe |
| `R` (logical) | Convert selected note/chord to equal-duration rest (23) | Both | single `NoteheadSet` (Note/ChordNote, not GraceNote) or single `ChordSet` | no-op | safe |
| `1` (physical) | **No-op** (25) | Both | — | no-op | — |
| `2`–`8` (physical) | Add key-spelled diatonic interval **above** (25) | Both | single `NoteheadSet` (Note/ChordNote, not GraceNote) | no-op | safe |
| `N` (logical) | Toggle active tool between `kNoteEntry` and `kSelection` (27) | Any | — | — | once |
| `A`–`G` (logical) | Step-entry commit a note (27) | Entry | legal cursor (§8) | no-op; cursor unchanged (diagnostic, §8) | safe |

### 7.2 Shift chords

| Key | Action (phase) | Tool | Precondition | Fallback | Repeat |
|---|---|---|---|---|---|
| Shift+Left | Range: move focus edge one quarter note earlier (19b) | Selection | committed `ArbitraryRangeSet` | no-op | safe |
| Shift+Right | Range: move focus edge one quarter note later (19b) | Selection | committed `ArbitraryRangeSet` | no-op | safe |
| Shift+Up | Range: staff scope -1 staff (19b) | Selection | committed `ArbitraryRangeSet` | no-op | safe |
| Shift+Down | Range: staff scope +1 staff (19b) | Selection | committed `ArbitraryRangeSet` | no-op | safe |
| Shift+Home | Range: select to node start (19b) | Selection | committed `ArbitraryRangeSet` | no-op | safe |
| Shift+End | Range: select to node end (19b) | Selection | committed `ArbitraryRangeSet` | no-op | safe |
| Shift+`2`–`8` | Add key-spelled diatonic interval **below** (25) | Both | single `NoteheadSet` (Note/ChordNote, not GraceNote) | no-op | safe |
| Shift+`1` | **No-op** (25) | Both | — | no-op | — |

### 7.3 Primary chords

| Key | Action (phase) | Tool | Precondition | Fallback | Repeat |
|---|---|---|---|---|---|
| Primary+Up | Staff step to prior staff, wrapping (24) | Both | single `NoteheadSet`/`ChordSet`/`RestSet`/`InsertionCaretSet` | no-op | safe |
| Primary+Down | Staff step to next staff, wrapping (24) | Both | same as Primary+Up | no-op | safe |
| Primary+X | Cut selection to in-memory clipboard (27) | Any | single-measure `FullMeasureSet` or valid `ArbitraryRangeSet` | no-op + diagnostic; clipboard preserved (§10) | once |
| Primary+C | Copy selection to in-memory clipboard (27) | Any | contiguous one-or-more-measure `FullMeasureSet` or valid `ArbitraryRangeSet` | no-op + diagnostic; clipboard preserved (§10) | safe |
| Primary+V | Paste in-memory clipboard at caret/anchor (27) | Any | non-empty clipboard + derivable `PasteAnchor` (§10) | no-op + diagnostic (§10) | once |
| Primary+Z | Undo (27) | Any | `CommandHistory` (selection-independent) | no-op on empty undo stack | safe |
| Primary+K | Toggle command palette (27) | Any | — | — | once |

### 7.4 Shift+Primary chords

| Key | Action (phase) | Tool | Precondition | Fallback | Repeat |
|---|---|---|---|---|---|
| Shift+Primary+Z | Redo (27) | Any | `CommandHistory` | no-op on empty redo stack | safe |

`Primary+Y` is deliberately **not** bound: a single cross-platform redo
mnemonic (`Shift+Primary+Z`) is preferable to a Windows-only alias.

### 7.5 Alt chords

| Key | Action (phase) | Tool | Precondition | Fallback | Repeat |
|---|---|---|---|---|---|
| Alt+`1`–`4` (physical digits) | Arm Voice 1–4 (27) | Entry | — | — (palette-arming is selection-independent) | safe |
| Alt+Up | Step-entry octave reference up (27) | Entry | — | no-op | safe |
| Alt+Down | Step-entry octave reference down (27) | Entry | — | no-op | safe |

`Alt+<digit>` is the physical top-row digit plus the Alt modifier; it is a
distinct chord class from unmodified and Shift and therefore never collides
with M5-phase-25's interval digits. On macOS it is Option+digit; the shell
reads the physical scancode plus the modifier mask, so OS text-insertion of
Option+digit does not shadow the binding.

### 7.6 Numpad key family (Unmodified class, physical, Num Lock-independent)

| Key | Action (phase) | Tool | Precondition | Fallback | Repeat |
|---|---|---|---|---|---|
| KP_`1`–KP_`7` | Arm duration Whole..SixtyFourth (27) | Entry | — | — (palette-arming) | safe |
| KP_`0` | Step-entry commit a rest at the caret (27) | Entry | legal cursor (§8) | no-op; cursor unchanged (diagnostic, §8) | safe |
| KP_DECIMAL (`.`) | Cycle dots 0→1→2→0 (27) | Entry | — | — (palette-arming) | once |

Numpad bindings use the physical `SDL_SCANCODE_KP_*` scancodes, so they are
Num Lock-independent. They are Unmodified-class chords (no modifiers) that the
numpad key family distinguishes from the non-numpad Unmodified keys in §7.1 by
physical identity; the two families together exhaust the Unmodified class and
never overlap. Top-row `9` and `0` remain **unbound no-ops** (reserved);
step-entry rest is numpad `0`, distinct from the top-row key. The duration,
rest, and dot actions are all reachable without a numpad through the command
palette (§11).

### 7.7 Explicit no-ops and reservations

Unbound and reserved, all explicit no-ops: top-row `9` and `0`; `Shift+1`;
`Shift+<letter>`, `Shift+-`, and `Shift+=`; Control chords on macOS and Meta
chords on Windows/Linux (non-Primary); all mixed-modifier chords;
Enter/Return, Escape, Tab, and Space in the notation context (Escape dismisses
the palette in the palette context only).

### 7.8 Accessible controls are not key chords

M5-phase-19b's accessible start/end/staff-scope controls are **semantic
actions** exposed through the accessibility tree (M5-phase-43 onward) and
invoked by assistive technology, not key chords. They are outside this key
table; the range-extension chords in §7.2 and the step-entry protocol in §8
drive the same underlying notation resolvers.

## 8. Step-entry protocol and cursor lifecycle (normative)

Step entry is the keyboard modality of the note-entry tool
(`ActiveTool::kNoteEntry`). `N` toggles into that tool from `kSelection`, and
`N` again returns to `kSelection`; this is the keyboard route that resolves the
note-entry tool being the eventual (not yet wired) startup default, so the
composer can reach note entry from the keyboard alone. Step-entry keys
(`A`–`G`, numpad, KP_`0`, Alt+`1`–`4`, Alt+Up/Down) are live only while
`kNoteEntry` is active; range-extension chords are live only while `kSelection`
is active; the notehead/event-editing chords remain live in both.

### 8.1 The step-entry cursor is app-owned, not the committed Selection

The committed `Selection` is a variant holding exactly one homogeneous set
(`graphscore/domain/selection.hpp`); it cannot simultaneously name a selected
notehead and an insertion caret. Step entry therefore does **not** write the
committed Selection. The application assembly layer (`graphscore_writer_app`)
owns a distinct **step-entry cursor** value

  `(node, track, stave, voice, position)`

plus a **pitch reference** (`previous_pitch`, `octave_offset`; §8.3). Both
exist only while `kNoteEntry` is active. The committed Selection keeps its
`kSelection` meaning: after a successful commit it is a single-item
`NoteheadSet` naming the just-entered notehead (so `-`/`=`, Up/Down, and
`2`–`8`/Shift intervals operate on it), while the cursor advances independently
as the next commit's target. The selected notehead and the cursor are distinct
state owned at different layers, and both may be live at once — this replaces
the earlier wording that a selected notehead and an advanced `InsertionCaretSet`
coexist, which a one-arm Selection cannot express.

### 8.2 Deterministic cursor initialization

On entering `kNoteEntry` (via `N`, via the pointer note-entry palette, or via
the future startup default), the cursor is initialized from the current
committed Selection, first match wins:

1. Single-item `InsertionCaretSet` — the item's `(node, track, stave, voice,
   position)` verbatim.
2. Single-item `NoteheadSet`, `ChordSet`, or `RestSet` — the item's
   `(node, track, stave, voice)` and the addressed event's own onset.
3. `ArbitraryRangeSet` — the first item in stored order gives
   `(node, track, stave, voice)` and `span.start`.
4. `FullMeasureSet` — the first item gives `(node, track, stave)`, the armed
   voice (`palette.voice()`), and `measure_start(measure_index)`.
5. `MarkingSet` / `NodeSet` / `ConnectorSet` — none of these names a note-entry
   voice and position of its own (a marking names an event or a stave-scoped
   span rather than a caret, and a node/connector names no stave at all);
   treated as no selection (the rule below).
6. No committed Selection (or an ineligible arm) — the node's first staff in
   score order (`score_ordered_staves`), the armed voice (`palette.voice()`),
   and **position 0**.

In every case the position is then **snapped to the nearest legal caret
boundary** in the target voice — position 0, an exact event boundary, or
`TrackLane::total_length()` — ties resolving to the earlier position. If the
node has no timeline, or the resolved track/stave is archived or absent, no
cursor is created and every step-entry key is an inert no-op with a diagnostic
(§8.5). Rule 6 is the deterministic blank-state answer: a fresh node with no
selection starts step entry at the first staff, the armed voice, position 0 —
never at an unspecified "current caret".

### 8.3 Pitch source and reference (deterministic)

- **Letters.** `A`–`G` (logical, unmodified) name a pitch class, always spelled
  **natural** — no key-signature or accidental inference — matching pointer
  entry's "a click selects a diatonic staff position, never an accidental."
- **Previous pitch.** The **previously entered pitch** is the last pitch this
  step-entry session committed (`A`–`G`, or the inserted notehead of a `2`–`8`
  interval built on a step-entry notehead). It is app-owned, scoped to the
  cursor, and discarded with it (§8.4).
- **Nearest-octave rule.** The octave of the next `A`–`G` is the octave that
  minimizes the absolute distance to the reference pitch, measured in the
  engraver's own `diatonic_index` (letter+octave) units rather than MIDI
  semitones, so the choice is spelling-stable. One octave is exactly seven
  diatonic steps — an odd number — so the distances to the two candidate
  octaves flanking the reference can never be equal: the nearest octave is
  always **unique**, and there is no tie to break. (A "tritone" tie would only
  exist under a semitone metric, which this rule does not use.)
- **No previous pitch.** With no previous pitch the reference is the
  **middle staff line under the active clef at the cursor** — the letter/octave
  the engraver's `clef_middle_line` table names (treble B4, bass D3, alto C4,
  tenor A3). The active clef is `ClefLane::clef_at(cursor.position)` on the
  cursor's stave, falling back to the stave's default clef when it has no clef
  lane yet (the same lookup the engraver performs).
- **Octave offset (Alt+Up/Down).** `octave_offset` is a signed integer held in
  `[-8, +8]`, set by Alt+Up/Down while `kNoteEntry` is active and applied to
  the reference before the nearest-octave rule. Alt+Up/Down are **checked,
  saturating** single-octave steps: Alt+Up sets `octave_offset =
  min(octave_offset + 1, +8)` and Alt+Down sets `octave_offset =
  max(octave_offset - 1, -8)`. The bounds are finite and symmetric, so holding
  a key can never overflow the accumulator or wrap; at a bound the chord is a
  no-op (the offset stays at the bound, matching §7.5). The bound ±8 is far
  outside any reachable SpelledPitch octave, so the per-commit range check
  (§8.5) governs actual spellings. It is **single-shot**: the next
  **successful** letter commit consumes it and it resets to 0 — the committed
  pitch already incorporates the offset, so leaving it set would double-apply.
  Its effect is bounded by the same SpelledPitch octave-range check as every
  other step-entry pitch (§8.5): a letter whose resulting spelling leaves the
  valid octaves — or fails the sounding-MIDI conversion — is rejected
  atomically, never clamped. On rejection the offset is **not** consumed: the
  failed commit leaves the pitch reference, `octave_offset` included, exactly
  as it was (§8.5).
- **Reference invalidation.** `previous_pitch` resets to none, and
  `octave_offset` to 0, when the cursor's **staff** changes (Primary+Up/Down or
  a pointer note-entry click onto a different staff), when the cursor's
  **voice** changes (Alt+`1`–`4` or a pointer click into a different armed
  voice), when the **tool** exits (§8.4), and on **undo** of the commit that
  produced the current `previous_pitch`. Accidental steps (`-`/`=`) do **not**
  change `previous_pitch` — it stores the natural spelling, and the accidental
  belongs to the selected notehead, not to the reference. Pointer note entry
  (a `kNoteEntry` click) updates `previous_pitch` to the inserted pitch and
  repositions the cursor to the click onset.

### 8.4 Lifecycle under transitions

- **Tool exit** (`kNoteEntry` → `kSelection`): discard the cursor and pitch
  reference. The committed Selection (the just-entered notehead, if any)
  persists.
- **Tool entry**: initialize per §8.2.
- **Pointer note entry** (`kNoteEntry` click): reposition the cursor to the
  click onset and update `previous_pitch` (§8.3); `octave_offset` resets to 0.
- **Pointer selection** (`kSelection`): changes the committed Selection only;
  the cursor is unaffected until the next tool entry re-derives it.
- **Staff step** (Primary+Up/Down): in `kNoteEntry` the cursor moves to the
  same voice and position on the prior/next staff, and
  `previous_pitch`/`octave_offset` reset because the staff changed; the
  committed Selection is re-derived on the target staff by the same
  nearest-note/caret resolution the `kSelection` staff step uses
  (`selection_after_staff_step`). In `kSelection` the delivered
  committed-selection behavior is unchanged.
- **Voice change** (Alt+`1`–`4`): change the cursor's voice to the armed voice
  and reset `previous_pitch`/`octave_offset`.
- **Node/track/stave invalidation** (archived track, removed stave, node
  changed): discard the cursor and re-derive it on the next use (§8.2);
  step-entry keys are inert no-ops with a diagnostic until then.

### 8.5 Commit, advance, and rejection

- **Armed state.** Duration is armed by KP_`1`–KP_`7` (Whole..SixtyFourth),
  dots by KP_DECIMAL (0→1→2→0); voice by Alt+`1`–`4`; the default is quarter
  note, no dots, voice 1. A note is committed with `A`–`G`; a rest of the armed
  duration with KP_`0`. There is no persistent note/rest toggle — the commit
  key chooses.
- **Placement.** Each commit uses the same placement semantics as pointer note
  entry (`make_note_entry_command`): at an existing onset in a non-empty voice
  it replaces that event's duration or builds a chord; in an empty voice it
  materializes the measure-aligned rest stream first. A note commit leaves the
  committed Selection as a single-item `NoteheadSet` naming the inserted
  notehead; a rest commit leaves a single-item `RestSet` naming the inserted
  rest. No accidental is applied at commit time — the delivered `-`/`=` ladder
  (M5-phase-21) steps the selected notehead's accidental afterward.
- **Advance.** After each successful **base** commit (a note `A`–`G`, or a
  rest KP_`0`) the cursor advances by the armed duration's resolved length and
  snaps to the nearest legal caret boundary, ties resolving earlier. **Chord
  tones do not advance the cursor**: after a base note is committed (and
  selected), `2`–`8` / Shift+`2`–`8` add diatonic intervals at the same onset
  and the cursor stays put.
- **Node end.** If the advance reaches or passes `TrackLane::total_length()`
  the cursor clamps to `total_length()`, which is itself a legal caret. A
  commit there is still subject to the placement rule: a non-empty voice has no
  event boundary at node end and an empty voice's measure-aligned fill has no
  onset there, so `make_note_entry_command` returns nullptr and the commit is
  rejected — step entry never grows the node; extending material past node end
  is structural editing (M5-phase-28) or pointer entry into a fill the composer
  first creates.
- **Rejection (atomic, uniform diagnostic policy).** A commit is rejected
  **atomically** — no project mutation; the cursor and the entire pitch
  reference (`previous_pitch` *and* `octave_offset`) are left unchanged; and a
  diagnostic surfaced to the composer — when there is no cursor, no legal
  caret, the pitch spelling leaves the SpelledPitch/MIDI range, the armed voice
  has no event boundary at the caret and no fill can be produced, the node has
  no timeline, or the active-clef lookup has no clef. A rejected commit
  consumes nothing: `octave_offset` is single-shot only against a
  **successful** commit (§8.3), so a failed commit leaves the pending octave
  adjustment in place for the composer to act on. This mirrors
  `make_note_entry_command`'s nullptr returns and the audition helpers'
  nullopt returns. The §7 step-entry rows' fallback column states exactly this
  — "no-op; cursor unchanged (diagnostic, §8)" — so there is one diagnostic
  policy, not a silent no-op here and a diagnostic elsewhere.

## 9. Collision-free rule (interval keys preserved)

M5-phase-25 reserves the unmodified top-row digits for interval entry, and
this table preserves that reservation exactly:

- Unmodified `2`–`8` = interval **above** (unchanged).
- Shift+`2`–`8` = interval **below** (unchanged).
- Unmodified `1` = **no-op** (unchanged); `Shift+1` is also a no-op.

Therefore **no duration action, voice shortcut, or any other binding may use
the unmodified or Shift-modified top-row digits `1`–`8`.** Durations are on the
numpad (§7.6); voices are on Alt+top-row `1`–`4` (§7.5), a distinct exact
chord class. Top-row `9`/`0` remain reserved. Any future binding must pass this
same check.

## 10. Clipboard routing (copy/cut/paste handoff)

This table owns only the in-memory handoff to `CutFragmentCommand` /
`PasteFragmentCommand` (`extract_fragment` for copy). OS clipboard interchange
and clipboard persistence are later milestones (§1). The in-memory clipboard is
a single-slot value holding at most one `NotationFragment`; it is app-owned,
not part of `CommandHistory`, so undo/redo of a cut neither copies nor clears
it — a fragment already on the clipboard survives the undo of the cut that
produced it.

### 10.1 Copy and cut

- **Eligible selections.** Copy accepts exactly the arms `extract_fragment`
  whitelists: `FullMeasureSet`, including a contiguous multi-measure set, and
  `ArbitraryRangeSet`, each satisfying its own preconditions (single node;
  one shared measure range or one shared span). Cut accepts the same
  `ArbitraryRangeSet` selections but only a single-measure `FullMeasureSet`;
  multi-measure cut is outside M5-phase-32. Every other arm — notehead, chord,
  rest, marking, node, connector, caret — is an ineligible copy/cut target and
  is a no-op with a diagnostic. (Copying a run of rests is expressed as the
  `ArbitraryRangeSet` covering them.)
- **Copy (`Primary+C`)** is a pure extraction: on success the extracted
  `NotationFragment` **replaces** the in-memory clipboard and the project is
  unchanged; on failure the clipboard is **preserved** (left exactly as it was)
  and a diagnostic is reported. A contiguous multi-measure `FullMeasureSet`
  copies its complete measure span.
- **Cut (`Primary+X`)** is extraction plus range-clearing in one reversible
  command: on success the command's `fragment()` **replaces** the clipboard,
  the selection's range is replaced by normalized rests, the command is pushed
  to `CommandHistory`, and the committed Selection becomes a **single-item
  `InsertionCaretSet` at the cut start** — `{node, track, stave}` from the cut
  selection's **first item in stored order** (deterministic because all items
  share one node and one span/measure), `voice` from that `ArbitraryRangeItem`
  (or the armed voice `palette.voice()` for a `FullMeasureSet`, whose items
  carry no voice), and `position` = `span.start` (or
  `measure_start(measure_index)` for a `FullMeasureSet`) — so an immediate
  `Primary+V` derives its paste anchor from that caret deterministically
  (§10.2). This is the one mandated post-cut state; it is never "cleared" or
  optional. On failure — including an extraction or a straddling-tuplet
  rejection — the project, the clipboard, and the committed Selection are all
  left **unchanged** (preserved) and a diagnostic is reported;
  `CutFragmentCommand::execute` is atomic.
- Cut is **repeat-once** and copy **repeat-safe** (§7.3): repeating a cut must
  not re-fire the destructive range-clearing, while repeating a copy merely
  re-extracts and re-replaces the same fragment.

### 10.2 Paste-anchor derivation

`Primary+V` derives a `PasteAnchor { node, track, stave, position }` from the
current committed Selection, first match wins; the single-slot clipboard must
already hold a fragment. The anchor carries no voice (a fragment's voice is
preserved exactly at the destination).

1. Single-item `InsertionCaretSet` — `{node, track, stave, position}` verbatim.
2. Single-item `NoteheadSet`, `ChordSet`, or `RestSet` — the addressed event's
   own onset as `position`.
3. `ArbitraryRangeSet` — the first item (stored order) gives `{node, track,
   stave}` and `span.start` (all items share one span, so `span.start` is
   unambiguous).
4. `FullMeasureSet` — the first item gives `{node, track, stave}` and
   `measure_start(measure_index)` (all items share one measure index).

Rejections (no-op with a diagnostic, clipboard untouched):

- **Multi-item tie / ambiguity.** A multi-item `NoteheadSet`, `ChordSet`,
  `RestSet`, `MarkingSet`, or `InsertionCaretSet` names more than one position
  and is rejected — a paste anchor is a single position. An
  `ArbitraryRangeSet`/`FullMeasureSet` whose items span more than one node is
  rejected (matching `extract_fragment`'s single-node rule); their first-item
  rule above is deterministic because all items share one span/measure.
- **Stale or no target.** No committed Selection, a `MarkingSet`/`NodeSet`/
  `ConnectorSet` arm (a marking has no musical-time position of its own to
  anchor a paste, matching `extract_fragment`'s reasoning), or a selection
  whose `(node, track, stave)` no longer resolves to a live, active track/stave
  is no target — rejected with a diagnostic.
- **Empty clipboard.** A paste with an empty clipboard is a no-op with a
  diagnostic (there is nothing to paste, so nothing to route).
- **Out-of-range position.** An anchor whose `position` is negative or whose
  pasted range would exceed `node_end()` is rejected by `PasteFragmentCommand`
  (`kInvalidArgument`) with a diagnostic and the project unchanged — the
  derivation above never produces such a position from a valid selection, but
  the failure is defined anyway.

A successful paste leaves the clipboard intact (paste does not consume the
fragment).

### 10.3 Tuplet structural actions (M5-phase-29)

Tuplets are chord-less command-palette actions and parameterized app actions;
they do not consume top-row digits, so the `1`–`8` interval bindings remain
unchanged.

- **Create or change to triplet** applies `3:2` in one action. An exact,
  non-empty `ArbitraryRangeSet` containing complete contiguous events on one
  staff and voice creates a group. A single selected tuplet `MarkingSet`
  changes that complete group to `3:2` without changing its identity or bounds.
- **Tuplet ratio (`N:M`)...** requests played/normal integer input from the
  platform presentation or assistive-technology client. The toolkit-neutral
  app exposes the corresponding parameterized action; the palette row itself
  clearly requests input and never guesses a ratio. Validated arbitrary ratios
  use the same create/change target rules as the triplet action.
- **Remove tuplet** requires one selected tuplet marking and removes the ratio
  from that complete stable group with fixed bounds.

Partial-event ranges, mixed staff/voice ranges, stale selections, trivial or
zero ratios, and any range already containing tuplet membership are rejected
with a diagnostic. Every mutation is one undoable command and availability is
computed by constructing the same notation-layer command execution uses.
Single-click/step note entry never creates tuplet membership: it cannot know a
complete group, so tuplets are applied only through these range actions.

Engraving omits `M` for the conventional simple-subdivision family where `M`
is the greatest power of two strictly below `N` (`3:2`, `5:4`, `7:4`, `9:8`,
...). Every other ratio is explicit `N:M`; for example, `10:9` is printed
`10:9`.

### 10.4 Marking style actions (M5-phase-30)

Articulations, stem overrides, dynamics, hairpins, pedal spans, ties, slurs,
and beam overrides are chord-less command-palette actions. They consume no key
at all — neither a letter, which `A`–`G` step entry and `N`/`R` already own,
nor a digit, which the `2`–`8` interval bindings own — so §7's binding table
is unchanged by them and the reservations in §7.6/§7.7 stand.

- **Apply articulation** (one row per articulation) requires one selected
  note or chord event and adds that articulation to it. **Change
  articulation** (one row per articulation) and **remove articulation**
  require one selected articulation marking. A duplicate articulation, and a
  duration articulation conflicting with one the event already carries, are
  rejected with a diagnostic rather than silently normalized.
- **Stem direction: auto/up/down** requires one selected note or chord event.
  Auto is the model's explicit representation of clearing a manual override,
  so it is a normal edit rather than a separate clear action; setting the
  direction the event already carries is rejected.
- **Apply dynamic** (one row per dynamic, `ppp` through `fff`) requires one
  selected note or chord event and anchors a point dynamic to it; a selected
  chord notehead resolves to its own chord, because a dynamic is anchored to a
  top-level event. **Change dynamic** (one row per dynamic) and **remove
  dynamic** require one selected dynamic marking. Change preserves the
  marking's identity and its anchor, so the selection survives the edit;
  changing to the value already set is rejected.
- **Apply crescendo / apply diminuendo** require an exact, non-empty
  `ArbitraryRangeSet` of complete contiguous events on one staff and one
  voice, carrying at least two events; the range's first and last events
  become the hairpin's endpoints. A partial event at either end and a
  multi-staff or multi-voice range share the diagnostic `requires a range of
  complete events on one staff and voice`; a single-event range is rejected
  with `requires a range of at least two events`. **Change hairpin to
  crescendo/diminuendo** and **remove hairpin** require one selected hairpin
  marking; change preserves the marking's identity and both endpoints.
- **Apply pedal span** requires a range on one staff. Pedal is scoped per
  stave rather than per voice, so a range covering several voices of one
  stave is a legitimate pedal target even though it is not a legitimate
  hairpin one, and the span's bounds are the union of the range's own bounds,
  validated against the node's own timeline before any command is built.
  **Remove pedal span** requires one selected pedal span marking. Both pedal
  actions are available only when every voice on the addressed staff has
  complete rhythm; unrelated staves on the track do not gate the edit.
  Otherwise they report `requires complete rhythm in every voice on the
  staff`. There is no change action: a pedal span carries no style
  attribute, only endpoints, which a later endpoint-drag gesture owns.
- **Apply tie** and **remove tie** require exactly one selected notehead, not a
  chord/event, rest, range, or tie marking. Apply requires the immediately
  following event in the same voice to sound the selected pitch and rejects an
  existing tie; remove requires that notehead to carry a tie. The selected
  notehead survives both edits.
- **Apply slur** requires an exact, non-empty `ArbitraryRangeSet` of complete
  contiguous events on one staff and one voice. It requires at least two
  events and sounding note/chord endpoints, then creates one slur from the
  first event to the last. Partial-event, mixed staff/voice, stale, rest/grace
  endpoint, and duplicate endpoint-pair targets are rejected. **Remove slur**
  requires exactly one selected slur marking and clears that now-stale marking
  selection. There is no change action because a slur has no style attribute;
  endpoint dragging is future work.
- **Apply beam break / apply beam join** require an exact, non-empty
  `ArbitraryRangeSet` of at least two complete contiguous beamable events on
  one staff and one voice. The ordered selected events define every adjacent
  pair governed by the override. Applying the kind already present on that
  exact range is rejected; applying the opposite kind replaces it while
  preserving its identity and precedence among overlapping overrides.
  **Remove beam override** removes the override on that exact range. Beam
  editing remains range-based: beam overrides have no marking selection or hit
  geometry, and the range selection survives apply, replacement, removal,
  undo, and redo.

Stale selections, wrong selection arms, grace notes (which carry no
event-anchored marking of their own), and edits that would change nothing are
rejected with a diagnostic and no mutation. Every mutation is one undoable
command, and availability is computed by constructing the same notation-layer
command execution uses, so a row is enabled exactly when running it would
succeed.

### 10.5 Pickdown actions (M5-phase-31)

The pickdown actions are chord-less command-palette rows. They consume no key
and leave §7's binding table unchanged.

- **Pickdown duration (node end)...**
  (`PaletteCommandId::kSetPickdownDuration`) has an empty chord hint and is
  always available as a request for parameter input. Running it calls
  `request_pickdown_duration_entry()`; the platform presentation or
  assistive-technology client then supplies a `Rational` duration to
  `apply_pickdown_duration`. A valid duration is greater than zero and strictly
  shorter than the final main-region measure. Applying it executes one
  reversible `SetPickdownCommand` and refreshes the final measure/system through
  one transactional measure-structure invalidation. The request row's
  availability does not promise that applying the supplied value will succeed:
  unusable history, a missing current-node timeline, an invalid duration or
  tempo-lane result, and refresh or commit failure each reject the apply with a
  diagnostic and no partial edit.
- **Clear pickdown** (`PaletteCommandId::kClearPickdown`) has an empty chord
  hint and executes one reversible `ClearPickdownCommand`. It is available only
  when command history is usable and the current editor node has a timeline
  carrying a pickdown. Otherwise it is disabled with `command history is
  unavailable` or `no pickdown to clear`, as applicable.

Both actions address the current editor node (`layout_.node_id`) rather than
the committed `Selection`; neither requires a musical selection. They add no
key binding.

### 10.6 Range delete and transpose (M5-phase-37)

The range-edit actions are chord-less palette actions and are available only
with a committed arbitrary musical range in the selection tool. **Delete
selected range** replaces the addressed time/staff/voice scopes with
normalized rests using the same boundary reconnection policy as cut. **Range
transpose diatonically up/down** moves noteheads one staff step while keeping
their accidentals; **range transpose chromatically up/down** moves one
semitone and chooses a deterministic valid enharmonic spelling. Both
transpose forms preserve rhythm and identities, and move connected ties as a
unit. An action whose pitch result is out of range, whose selection is stale or
incoherent, or whose range contains no sounding note is unavailable and does
not mutate the project. Each successful action is one undoable transaction.

## 11. Command palette (complete normative route)

The command palette is the universal keyboard route to every
action in this table, and it is what makes the numpad-only bindings
(duration/rest/dots, §7.6) reachable on keyboards without a numpad.

- **Toggle and routing.** `Primary+K` toggles the palette open/closed in every
  tool (Any, §7.3). While open, the palette is focus context 2 (§5), above the
  notation context. The palette interprets `Primary+K` as *close*, so the same
  chord is the toggle in the notation context and the close in the palette
  context — coherent with §5 and never swallowed as filter text.
- **Inventory.** The palette enumerates exactly the actions in §7, one row per
  binding (Backspace and Delete are one row naming one action); the
  accessible range controls of §7.8; the three chord-less structural
  measure-editing actions of M5-phase-28 — insert a measure before the
  selected measure, append a measure at the node's own end, and delete the
  selected measure; M5-phase-29's three tuplet rows in §10.3; and
  M5-phase-30's marking style rows in §10.4 — apply/change articulation (one
  row per articulation), remove articulation, the three stem-direction rows,
  apply/change dynamic (one row per dynamic), remove dynamic, apply
  crescendo/diminuendo, change hairpin to crescendo/diminuendo, remove
  hairpin, apply pedal span, remove pedal span, apply/remove tie,
  apply/remove slur, apply beam break, apply beam join, and remove beam
  override; and M5-phase-31's two pickdown rows in §10.5 — request a node-end
  pickdown duration and clear the current pickdown; and M5-phase-37's five
  range-edit rows in §10.6 — delete the selected range, or transpose it one
  diatonic step or semitone up/down. Every one of those chord-less rows — the
  structural, tuplet, marking-style, pickdown, and range-edit rows alike —
  carries an **empty chord hint**: they are deliberately **not** bound
  to any key chord (no row in §7 names them), so §7's `(chord, key)` space is
  unchanged by their addition and §12's claim that this space is exhaustively
  enumerable still holds. Each row carries a stable **name**, its **chord
  hint** (empty for the chord-less accessible controls, structural actions,
  tuplet actions, marking-style actions, pickdown actions, and range-edit
  actions), a one-line
  description, and a live **availability** state.
- **Availability.** A row's availability is derived from the same precondition
  and fallback logic as its chord row, for a row that has one: "Copy" is
  available for a valid contiguous one-or-more-measure `FullMeasureSet` or
  valid `ArbitraryRangeSet`; "Cut" accepts the same ranges but only a
  single-measure `FullMeasureSet`; "Paste" when the clipboard is non-empty and
  a `PasteAnchor` can be derived (§10); the duration/rest/dots rows are
  available exactly when
  `kNoteEntry` is active (they are Entry-tool actions). A chord-less row's
  availability is its own precondition: each structural measure-editing row
  is available exactly when the operation it names would succeed, so "Delete
  measure" is unavailable on a node carrying its only measure while the two
  insert rows remain available. An unavailable row is shown disabled with the
  reason from its fallback.
- **Execution.** Running a row performs the identical action with the identical
  fallback as its chord, including the tool gate: running an Entry-tool action
  while `kSelection` is active is the same no-op as pressing its chord (the
  palette never switches tools or performs a broader operation than the chord).
  A chord-less row performs its own action under its own precondition; like
  the clipboard and history rows of §7.3, the structural measure-editing,
  tuplet, marking-style, and pickdown rows are ungated by tool. Range-edit
  rows are selection-tool gated. Their own
  selection or current-node preconditions apply rather than the active tool.
- **Search.** The filter matches the name and description, case-insensitively;
  filtering never changes an action's availability or chord hint.

This makes the palette the complete route M5-phase-52/53 exercise: every chord
in §7 is reachable by name without any specific physical key, so a numpad-less
keyboard reaches duration/rest/dots and every other action through `Primary+K`
plus its searchable name. The numpad bindings in §7.6 remain an accelerator for
keyboards that have them; they are not a requirement for reachability.

## 12. Determinism and testability

The table is written so that M5-phase-53's keyboard-layout tests can assert
behaviour without US key labels: physical bindings are asserted against raw
SDL scancodes, logical letter bindings against the layout-mapped keycode, and
each row's fallback and repeat policy is a single observable outcome. The five
modifier chord classes in §3 are five **disjoint bound classes plus an explicit
unbound remainder** — the five classes do not by themselves partition the
modifier space, but together with the remainder they cover it — and the
numpad/non-numpad key families partition the Unmodified class's key space by
physical identity (§3, §4), so a test can enumerate the full `(chord, key)`
space and assert the one action or no-op each cell maps to: the five bound
classes' cells **and the remainder cells (asserted as no-ops)**. Every cell is
covered exactly once, with no cell reachable through two classes or two
families.

## 13. Manual platform shortcut check

M5-phase-52 complements the automated command and shell tests with this native
keyboard smoke check. Run it in a normal writer window on macOS, Windows, and
Linux using the platform's native keyboard input path. The check is against
observable edits, selection, and suppression; it does not replace the
exhaustive layout matrix owned by M5-phase-53.

| Check | macOS chord | Windows/Linux chord | Expected result |
|---|---|---|---|
| Primary mapping | Command+Up/Down | Control+Up/Down | Staff focus moves; Control on macOS and Meta on Windows/Linux do nothing. |
| Clipboard | Command+C/X/V | Control+C/X/V | Copy is non-destructive, cut clears the selected range, and paste replaces only the destination range. |
| History | Command+Z / Shift+Command+Z | Control+Z / Shift+Control+Z | The edit is undone and redone once per press. |
| Palette focus | Command+K | Control+K | The palette takes keyboard focus; notation-editing keys are suppressed until Escape or the same chord closes it. |
| Voice and octave | Option+1..4 / Option+Up/Down | Alt+1..4 / Alt+Up/Down | In note entry, the armed voice or octave reference changes; the chords do nothing in selection mode. |
| Physical keys | `2`..`8`, Shift+`2`..`8`, `-`, `=`, numpad | same | Interval direction, accidental stepping, duration, rest, and dot actions follow physical key positions. |
| Logical letters | `A`..`G`, `N`, `R` | same | Letter mnemonics follow the active layout's produced character rather than the US physical position. |
| Exact modifiers | representative mixed and non-Primary chords | same | Extra or non-Primary modifiers do not trigger a notation action. |

Use a keyboard with a numpad where available and repeat the duration/rest/dot
rows through the searchable command palette on a numpad-less keyboard. For the
logical-letter row, switch to one installed non-US layout and verify one moved
letter mnemonic; exhaustive key/layout combinations remain M5-phase-53.
