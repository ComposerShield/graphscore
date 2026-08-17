# Milestone 08: Writer Audio And VST3 Hosting

## Goal

Make composition audible through a permissively implemented cross-platform audio engine with one VST3 instrument and an ordered VST3 effect chain per track.

## Dependencies

- [ ] M8-phase-1 Milestone 07 complete adaptive scheduler.
- [ ] M8-phase-2 Milestone 00 VST3/audio platform decisions.
- [ ] M8-phase-3 Milestone 03 plugin-state persistence.

## Deliverables

### Audio devices and engine

- [ ] M8-phase-4 Enumerate/select output devices and configure supported sample rates and block sizes.
- [ ] M8-phase-5 Run the shared scheduler from the audio callback without UI-thread coupling.
- [ ] M8-phase-6 Support variable callback sizes within the configured maximum.
- [ ] M8-phase-7 Provide safe device loss/restart, sample-rate change, buffer-size change, and panic behavior.
- [ ] M8-phase-8 Keep device/platform types behind a narrow writer-only abstraction.

### Track plugin chains

- [ ] M8-phase-9 Exactly one VST3 instrument slot followed by zero or more ordered effect slots per active track.
- [ ] M8-phase-10 Add, remove, bypass, and reorder effects safely off the audio thread.
- [ ] M8-phase-11 Route the track's fixed-channel MIDI into its instrument and audio through each enabled effect.
- [ ] M8-phase-12 Persist opaque component/controller state in the project bundle.
- [ ] M8-phase-13 Use a silent placeholder when a plugin is missing while preserving identity, state, and chain position.
- [ ] M8-phase-14 Strip all chain data from cooked runtime assets.

### Plugin scanning and compatibility

- [ ] M8-phase-15 Discover standard and user-configured VST3 locations.
- [ ] M8-phase-16 Scan one candidate at a time in a helper process with timeout, crash/hang detection, metadata normalization, and blacklist/rescan controls.
- [ ] M8-phase-17 Never let a scan crash take down the writer or corrupt the project.
- [ ] M8-phase-18 Use a per-plugin scan timeout well above 5 seconds. ADR 0007 measured
       Kontakt 8 at 3.07 s for a bare out-of-process scan.
- [ ] M8-phase-19 Include redistributable GraphScore test instrument/effect fixtures built from the SDK for CI.
- [ ] M8-phase-20 Document platform/architecture compatibility and present clear diagnostics for wrong-architecture plugins.

### Plugin editors and parameters

- [ ] M8-phase-21 Open, resize, focus, close, and reopen native plugin editors using platform-native parent handles.
- [ ] M8-phase-22 Handle keyboard focus without breaking writer shortcuts outside the plugin editor.

**Carried from M0 (ADR 0007), verified on macOS arm64:**

- [ ] M8-phase-23 Treat `canResize()` as the authoritative resize gate. `checkSizeConstraint()`
       returned accepted for a plugin reporting `canResize -> no`.
- [ ] M8-phase-24 Do not treat a non-ok `IPlugView::onFocus()` return as a focus failure. It
       is advisory; keyboard focus worked correctly while it returned fail.
- [ ] M8-phase-25 Make the host frame fully functional *before* `attached()`. `getSize()`
       before attach is unreliable, and `IPlugFrame::resizeView` is called
       re-entrantly from inside `attached()`.
- [ ] M8-phase-26 Prefer `setComponentState`; `IEditController::getState` is frequently
       `kNotImplemented`.
- [ ] M8-phase-27 Do not treat `getTailSamples()` as bounded. `kInfiniteTail` (0xFFFFFFFF)
       was returned by both commercial plugins tested.
- [ ] M8-phase-28 Provide a generic parameter view where the VST3 exposes usable parameter metadata.
- [ ] M8-phase-29 Do not author or export parameter automation in `0.1.0`.

### Open question carried from M0: in-process hosting

**ADR 0007 finding.** AutoTune installs its own process-wide SIGSEGV, SIGABRT,
SIGFPE, and SIGBUS handlers into the host process on load. The current plan
scans out-of-process but hosts in-process for playback, which means third-party
plugin code takes over host crash handling for the whole writer.

- [ ] M8-phase-30 Decide explicitly whether playback hosting stays in-process. Record the
       decision and its crash-containment story as an ADR before building the
       plugin chain.
- [ ] M8-phase-31 If hosting stays in-process, document what happens to autosave and
       recovery when a plugin handler intercepts a fatal signal.

### Audition mixer

- [ ] M8-phase-32 Per-track mute, solo, gain, pan, and level metering.
- [ ] M8-phase-33 Automatic latency compensation aligns tracks using reported instrument/effect latency.
- [ ] M8-phase-34 Rebuild compensation safely when chain order, bypass, sample rate, or plugin-reported latency changes.
- [ ] M8-phase-35 Let reported effect tails decay naturally after stop/transition; panic sends note-offs and CC64-up for every held track/channel, clears logical ownership, and resets/silences chains immediately.
- [ ] M8-phase-36 Keep audition mix settings writer-only.

### Transport and preview

- [ ] M8-phase-37 Play, pause, stop, start designated node, dedicated node play, and loop-node audition.
- [ ] M8-phase-38 Metronome follows time-signature and tempo curves; count-in precedes playback without shifting authored graph time.
- [ ] M8-phase-39 Newly inserted or pitch-edited notes produce a short fixed preview through the relevant track chain.
- [ ] M8-phase-40 Transition/event controls connect to the same scheduler path used by runtime behavior.
- [ ] M8-phase-41 Reconcile all active notes when node play replaces existing playback and clear graph queues/tails as specified.

## Acceptance Criteria

- [ ] M8-phase-42 Writer playback is sample-accurate with runtime MIDI output for the same asset, seed, and event stream before plugin rendering.
- [ ] M8-phase-43 One instrument plus multiple effects can be scanned, loaded, reordered, bypassed, state-restored, edited, and played on all primary x86-64 platforms.
- [ ] M8-phase-44 The complete plugin scanner, lifecycle, audio, state, and native-editor fixture also passes natively on macOS arm64.
- [ ] M8-phase-45 Plugin scan crashes/hangs are contained and produce recoverable diagnostics.
- [ ] M8-phase-46 Missing plugins do not block open, edit, save, or export.
- [ ] M8-phase-47 Latency compensation aligns synthetic test impulses within one sample.
- [ ] M8-phase-48 Audio callback remains allocation/lock-free for GraphScore code and passes realtime stress tests at 44.1/48 kHz and 64-1024 variable frames.
- [ ] M8-phase-49 Native and generic plugin controls remain keyboard reachable.

## Test Focus

- [ ] M8-phase-50 Synthetic instrument/effect lifecycle, state, buses, latency, tails, bypass, and reorder tests.
- [ ] M8-phase-51 Scanner helper crash, timeout, malformed metadata, duplicate plugin, and blacklist tests.
- [ ] M8-phase-52 Device restart and configuration-change tests using fake and available platform backends.
- [ ] M8-phase-53 MIDI-to-audio timing goldens, latency impulse alignment, natural-tail, and panic tests.
- [ ] M8-phase-54 Transport state machine, metronome/count-in against curved tempo, loop, and note preview tests.
- [ ] M8-phase-55 Audio-thread allocation/locking assertions plus ASan/UBSan and TSan off-realtime suites.
