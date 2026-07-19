# Milestone 08: Writer Audio And VST3 Hosting

## Goal

Make composition audible through a permissively implemented cross-platform audio engine with one VST3 instrument and an ordered VST3 effect chain per track.

## Dependencies

- [ ] Milestone 07 complete adaptive scheduler.
- [ ] Milestone 00 VST3/audio platform decisions.
- [ ] Milestone 03 plugin-state persistence.

## Deliverables

### Audio devices and engine

- [ ] Enumerate/select output devices and configure supported sample rates and block sizes.
- [ ] Run the shared scheduler from the audio callback without UI-thread coupling.
- [ ] Support variable callback sizes within the configured maximum.
- [ ] Provide safe device loss/restart, sample-rate change, buffer-size change, and panic behavior.
- [ ] Keep device/platform types behind a narrow writer-only abstraction.

### Track plugin chains

- [ ] Exactly one VST3 instrument slot followed by zero or more ordered effect slots per active track.
- [ ] Add, remove, bypass, and reorder effects safely off the audio thread.
- [ ] Route the track's fixed-channel MIDI into its instrument and audio through each enabled effect.
- [ ] Persist opaque component/controller state in the project bundle.
- [ ] Use a silent placeholder when a plugin is missing while preserving identity, state, and chain position.
- [ ] Strip all chain data from cooked runtime assets.

### Plugin scanning and compatibility

- [ ] Discover standard and user-configured VST3 locations.
- [ ] Scan one candidate at a time in a helper process with timeout, crash/hang detection, metadata normalization, and blacklist/rescan controls.
- [ ] Never let a scan crash take down the writer or corrupt the project.
- [ ] Include redistributable GraphScore test instrument/effect fixtures built from the SDK for CI.
- [ ] Document platform/architecture compatibility and present clear diagnostics for wrong-architecture plugins.

### Plugin editors and parameters

- [ ] Open, resize, focus, close, and reopen native plugin editors using platform-native parent handles.
- [ ] Handle keyboard focus without breaking writer shortcuts outside the plugin editor.
- [ ] Provide an accessible generic parameter view where the VST3 exposes usable parameter metadata.
- [ ] Do not author or export parameter automation in `0.1.0`.

### Audition mixer

- [ ] Per-track mute, solo, gain, pan, and level metering.
- [ ] Automatic latency compensation aligns tracks using reported instrument/effect latency.
- [ ] Rebuild compensation safely when chain order, bypass, sample rate, or plugin-reported latency changes.
- [ ] Let reported effect tails decay naturally after stop/transition; panic sends note-offs and CC64-up for every held track/channel, clears logical ownership, and resets/silences chains immediately.
- [ ] Keep audition mix settings writer-only.

### Transport and preview

- [ ] Play, pause, stop, start designated node, dedicated node play, and loop-node audition.
- [ ] Metronome follows time-signature and tempo curves; count-in precedes playback without shifting authored graph time.
- [ ] Newly inserted or pitch-edited notes produce a short fixed preview through the relevant track chain.
- [ ] Transition/event controls connect to the same scheduler path used by runtime behavior.
- [ ] Reconcile all active notes when node play replaces existing playback and clear graph queues/tails as specified.

## Acceptance Criteria

- [ ] Writer playback is sample-accurate with runtime MIDI output for the same asset, seed, and event stream before plugin rendering.
- [ ] One instrument plus multiple effects can be scanned, loaded, reordered, bypassed, state-restored, edited, and played on all primary x86-64 platforms.
- [ ] The complete plugin scanner, lifecycle, audio, state, and native-editor fixture also passes natively on macOS arm64.
- [ ] Plugin scan crashes/hangs are contained and produce recoverable diagnostics.
- [ ] Missing plugins do not block open, edit, save, or export.
- [ ] Latency compensation aligns synthetic test impulses within one sample.
- [ ] Audio callback remains allocation/lock-free for GraphScore code and passes realtime stress tests at 44.1/48 kHz and 64-1024 variable frames.
- [ ] Native and generic plugin controls remain keyboard reachable; generic controls expose screen-reader semantics.

## Test Focus

- [ ] Synthetic instrument/effect lifecycle, state, buses, latency, tails, bypass, and reorder tests.
- [ ] Scanner helper crash, timeout, malformed metadata, duplicate plugin, and blacklist tests.
- [ ] Device restart and configuration-change tests using fake and available platform backends.
- [ ] MIDI-to-audio timing goldens, latency impulse alignment, natural-tail, and panic tests.
- [ ] Transport state machine, metronome/count-in against curved tempo, loop, and note preview tests.
- [ ] Audio-thread allocation/locking assertions plus ASan/UBSan and TSan off-realtime suites.
