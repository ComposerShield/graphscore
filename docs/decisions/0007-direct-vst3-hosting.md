# ADR 0007: Direct VST3 Hosting

| Status | Author | Date |
|--------|--------|------|
| Accepted | M0 direct VST3 hosting spike | 2026-07-21 |

## Context

The plan requires the writer to host VST3 instruments and effects directly,
without a licensed framework. Milestone 00 could not exit until direct VST3
hosting had a credible implementation path, with native editor embedding the
principal unknown.

Environment: macOS 15.7.7 arm64, CMake 4.4.0, Ninja 1.13.2, Apple clang 17.0.0,
Command Line Tools only, no Xcode.app.

## Decision

**Direct VST3 hosting is viable. No licensed framework is required.**

The full lifecycle was exercised end to end: FetchContent integration,
instantiation, bus configuration, audio and MIDI processing, opaque state
round-trip, latency and tail query, out-of-process scanning with crash and
timeout survival, and native editor attach with human-observed render, resize,
and keyboard focus.

VSTGUI is disabled and stays out of the dependency closure. Licensing is
confirmed MIT across the root repository and all core submodules, including
`pluginterfaces`; see `spikes/m0/vst3-hosting/README.md`.

## What Was Verified

| Area | Result |
|---|---|
| FetchContent at pinned commit `9fad9770f2` | Clean-clone configure 18.8 s, build 32.9 s, 248 targets, exit 0 |
| Instrument lifecycle (`mda JX10`) | Bus setup, processing, MIDI note-on produced audio (peak 0.652), state round-trip byte-identical |
| Effect lifecycle (`adelay`) | 440 Hz tone passed, peak 0.250, byte-identical state round-trip |
| Commercial effect (AutoTune) | 24 params, latency 112 samples, 1241-byte state, byte-identical round-trip |
| Commercial instrument (Kontakt 8) | 4145 params, 32 stereo output buses, 16-ch event I/O, processes correctly |
| Out-of-process scanning | Parent survived deliberate SIGSEGV and timeout-kill; diagnostics emitted |
| Native editor | Human-observed PASS on render, resize, and keyboard focus |

The editor result is a physical gate recorded in
`spikes/m0/vst3-hosting/EDITOR-GATE.md` as direct human observation. The
automated host prints API return values and cannot see the screen.

Both commercial plugins were expected to fail, hang, or raise modal
authorization dialogs. Neither did. This exercises the real Milestone 08
requirement rather than an SDK-example approximation of it.

## Findings That Constrain Later Milestones

These are the reason the spike was worth running. Each is a downstream
constraint discovered before the milestone that depends on it.

| # | Finding | Affects |
|---|---|---|
| 1 | **Plugin state is not byte-deterministic.** Kontakt saved 3897 bytes, restored, then saved 3913 over an identical cycle. | M03. Persistence must not hash plugin state for change detection, assume stable bytes, or diff state blobs. Store opaque and compare by identity, not content. |
| 2 | **Commercial plugins install process-wide signal handlers.** AutoTune installs its own SIGSEGV/SIGABRT/SIGFPE/SIGBUS handlers into the host process on load. | M08. The plan scans out-of-process but hosts in-process for playback. This is a live argument for reconsidering in-process hosting, or at minimum entering M08 aware that plugin code takes over host crash handling. |
| 3 | **`getTailSamples()` commonly returns `kInfiniteTail` (0xFFFFFFFF).** Observed on both commercial plugins. | M08 and the scheduler. Tail cannot be treated as a bounded sample count; effect-tail decay needs an explicit policy. |
| 4 | **`IPlugView::onFocus()` is advisory.** AutoTune returned fail while keyboard focus demonstrably worked. | M08. Never treat a non-ok `onFocus` return as failure or use it to drive host focus logic. Focus is the host `NSWindow`'s job. |
| 5 | **`getSize()` before `attached()` is unreliable** (Kontakt returns kResultFalse / 0×0), and `IPlugFrame::resizeView` is called **re-entrantly from inside** `attached()`. | M08. The host frame must be fully functional before attach, not initialized after it. |
| 6 | **`canResize()` is the authoritative resize gate**, not `checkSizeConstraint()`, which returned "accepted" for Kontakt despite `canResize -> no`. | M08. |
| 7 | **`IEditController::getState` is frequently `kNotImplemented`**; `setComponentState` is the load-bearing path. | M08, M03. |
| 8 | **`sdk_hosting` is incomplete.** It contains neither the platform module loader (`module_mac.mm`) nor `plugprovider.cpp`; every SDK host example compiles them into its own target, and so must GraphScore. `module_mac.mm` requires `-fobjc-arc`. | M01, M08. |
| 9 | **Scan timeouts must be generous.** Kontakt took 3.07 s to scan out-of-process. A per-plugin timeout well above 5 s is required. | M08. |

## Build Integration Notes

Two SDK issues are hard failures on a plain Command Line Tools plus Ninja setup
and must be handled in Milestone 01 CI:

- `base/source/fdebug.h:52` `#error`s unless a build type is defined. Ninja's
  empty default `CMAKE_BUILD_TYPE` breaks the SDK build.
- `smtg_detect_xcode_version` runs `xcodebuild -version`, which fails without
  Xcode.app. The SDK guards detection on `ENV{XCODE_VERSION}` but compares the
  regular `XCODE_VERSION` variable, so **both** must be set.

Required options: `SMTG_ENABLE_VSTGUI_SUPPORT=OFF`, `SMTG_ADD_VSTGUI=OFF`,
`SMTG_CREATE_PLUGIN_LINK=OFF` (otherwise it symlinks examples into the user's
plugin directory), `SMTG_RUN_VST_VALIDATOR=OFF`, and explicit
`GIT_SUBMODULES "base cmake pluginterfaces public.sdk"` so `vstgui4/` stays
empty.

With VSTGUI off, SDK example plugins have **no editor** — `createView(kEditor)`
returns null. The editor path can only be exercised against real plugins. This
also means the generic-parameter-view fallback path is naturally exercised by
the mda examples.

## Open Risks

- **Windows, Linux/Wayland, and x86-64 are untested.** Moved to Milestone 08 by
  the Milestone 00 rescope.
- **Wayland native-editor embedding remains the highest residual platform risk
  in the plan.** Not addressed here. Fallback is the GraphScore-owned
  accessible generic parameter view.
- **Both commercial plugins were already authorized on the test machine.**
  Behavior on a fresh machine or in CI, where authorization prompts could block
  instantiation, is unknown. Milestone 08 scanning concern.

## Consequences

- Milestone 00's exit condition is satisfied on macOS arm64.
- `spikes/m0/vst3-hosting/` is disposable. Its code is retained only until
  Milestone 08 consumes the integration notes above, then deleted. The ADR and
  the editor gate record are the durable artifacts.
