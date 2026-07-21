# Native Plugin Editor — Physical Gate Record

Human-observed gate. Adam at the machine, macOS 15.7.7 arm64. This file
records direct observation only; it is not a test result and nothing here
was produced by an automated check. The automated host prints API return
values but cannot see the screen.

## 2026-07-21 — PASS, 4 of 4 criteria observed

| Criterion | Plugin | Result |
|---|---|---|
| Instantiate + editor renders | AutoTune (effect) | **PASS** — "rendered perfectly" |
| Instantiate + editor renders | Kontakt 8 (instrument) | **PASS** — "rendered perfectly" |
| Editor resize | AutoTune | **PASS** — "resizes perfectly" |
| Editor keyboard focus | AutoTune | **PASS** — "gets keyboard focus correctly" |

Adam's wording, verbatim, across two observations:
"The first one was autotune, now its Kontakt. Both rendered perfectly."
"It resizes perfectly and it gets keyboard focus correctly."

Resize and focus were tested against AutoTune specifically. Adam observed
that Kontakt "does not resize well" and selected AutoTune as the fixture
because it "resizes the way one would expect." The API agrees: Kontakt
reports `canResize -> no`, AutoTune reports `canResize -> yes`. Kontakt's
resize behaviour is the plugin declining to resize, not a host defect.
Testing resize against a plugin known to support it means a failure would
have been attributable to GraphScore.

## Corresponding API output (AutoTune)

```
isPlatformTypeSupported(NSView) -> yes
getSize -> ok  752 x 600
setFrame -> ok
IPlugFrame::resizeView -> 752 x 600
attached(NSView) -> ok
checkSizeConstraint(902x720) -> accepted (adjusted to 902 x 720)
canResize -> yes
onSize(902x720) -> ok
setContentScaleFactor(2.0) -> unsupported
onFocus(true) -> fail
```

## Finding: `onFocus()` return value is not a focus signal

`onFocus(true)` returned **fail**, yet Adam observed keyboard focus
working correctly. `IPlugView::onFocus` is advisory in VST3; real keyboard
focus is the host `NSWindow`'s responsibility. Milestone 08 must not treat
a non-ok `onFocus` return as a focus failure or use it to drive host focus
logic.

## Caveat carried to Milestone 08

Both plugins were **already authorized on this machine** (AutoTune via
iLok, Kontakt 8 via Native Access). This observation says nothing about a
fresh machine or CI, where authorization prompts could block instantiation
entirely. This is a plugin scanning and compatibility concern for
Milestone 08, not an M0 blocker.

## Not covered

Windows, Linux/Wayland, and x86-64 are untested and unchanged as open
risks. Wayland native-editor embedding remains the highest residual
platform risk in the plan; the recorded fallback is the GraphScore-owned
accessible generic parameter view.
