> **STUBS REMOVED 2026-07-21.** The previous contents were
> self-validating placeholder code that implemented nothing. This spike
> has not run yet; see docs/plan/00-architecture-spikes.md for its scope.
> The VST3 SDK license claim below was INDEPENDENTLY VERIFIED on
> 2026-07-21 against upstream sources; see the Verification Record.

# M0 Phase D — Direct VST3 Hosting Spike

## License Verification Record (2026-07-21)

Verified directly against upstream, not from secondary summaries.

| Component | Upstream repo | License |
|---|---|---|
| vst3sdk (root) | `steinbergmedia/vst3sdk` @ `9fad9770f2` | MIT (c) 2025 Steinberg |
| base | `steinbergmedia/vst3_base` | MIT |
| pluginterfaces | `steinbergmedia/vst3_pluginterfaces` | MIT |
| public.sdk | `steinbergmedia/vst3_public_sdk` | MIT |
| cmake | `steinbergmedia/vst3_cmake` | MIT |
| vstgui | `steinbergmedia/vstgui` | BSD-3-clause-style, (c) 2022 Steinberg |

The SDK is a multi-repo submodule layout; the root `LICENSE.txt` alone is not
sufficient evidence. `pluginterfaces` carried the historical GPLv3 + Steinberg
proprietary dual license and was the real risk — it is now MIT.

**VSTGUI is not required.** GraphScore hosts native plugin editors and provides
its own accessible generic parameter view (see plan README, Writer playback and
plugins). VSTGUI must remain disabled in the GraphScore build; its permissive
but distinct license then never enters the dependency closure.

**Conclusion**: the VST3 SDK passes the ADR 0001 default-build license gate.
Milestone 08 is not blocked by a licensing assumption.


## Purpose

Disposable quarantined spike that retires VST3 plugin hosting risks before
production architecture depends on them.

This spike does NOT join the production CMake DAG and will be deleted or
replaced after decisions are recorded.

## Dependencies

| Dependency | SHA | License | Status |
|------------|-----|---------|--------|
| VST3 SDK   | `9fad9770f2ae8542ab1a548a68c1ad1ac690abe0` | MIT | POLICY-CLEARED |

### VST3 SDK License Verification

The VST3 SDK was previously dual-licensed under GPLv3+ with a Steinberg
proprietary exception. **As of VST SDK 3.8.x, the license has changed to MIT.**

The LICENSE.txt in the repository root reads:

> MIT License
> Copyright (c) 2025, Steinberg Media Technologies GmbH
>
> Permission is hereby granted, free of charge, to any person obtaining a copy
> of this software and associated documentation files (the "Software"), to deal
> in the Software without restriction, including without limitation the rights
> to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
> copies of the Software...

Licensing under GPLv3 and the Steinberg proprietary license is no longer
available. The MIT license permits unrestricted use, modification, and
redistribution — including in commercial products — provided MIT terms
are followed.

**Repository:** https://github.com/steinbergmedia/vst3sdk

### Dependency Decision

The VST3 SDK is a git repository with submodules for each component
(`base`, `cmake`, `pluginterfaces`, `public.sdk`, `vstgui4`). This spike
fetches the superproject with recursive submodules via FetchContent and
links against the `sdk_hosting` target.

**Current status:** This spike is a scaffold with PLACEHOLDER implementations.
The CMake build infrastructure fetches and configures the VST3 SDK, but the
C++ source files contain stub implementations that return safe defaults.
Scanning, processing, state save/restore, latency/tail queries, and editor
attachment are planned capabilities — not yet implemented against the SDK.

**Why not a wrapper library?** JUCE, Tracktion Engine, and clap-wrapper
were considered but rejected for this spike:
- JUCE is GPLv3+ for non-commercial use; its module dependency graph is
  heavy for a spike that only needs the VST3 host interface.
- Tracktion Engine depends on JUCE and its licensing is similarly
  restrictive.
- clap-wrapper VST3 support is CLAP-host-centric; we need direct VST3
  control for diagnostic-level inspection (latency/tail/states).

The spike targets the VST3 SDK's `public.sdk/source/vst/hosting`
module, which provides `VST3::Hosting::Module` and `PluginFactory` —
the same module used by Steinberg's own validator. The current
implementation is a scaffold that compiles against the SDK but uses
stub logic in all host and scanner methods.

## Build

### Prerequisites

- CMake 3.25+
- Apple Clang 17+ (C++23), or Clang 19+
- Git (for FetchContent with submodules)
- macOS 14+ (tested on arm64)

### Configure and Build

```sh
cd spikes/m0/vst3-hosting
cmake -B build -G Ninja
cmake --build build
```

To override FetchContent source directories (offline/air-gapped):

```sh
cmake -B build -G Ninja \
  -DFETCHCONTENT_SOURCE_DIR_VST3SDK=/path/to/vst3sdk
```

### Run

```sh
cd build

# Scan a directory for VST3 plugins (helper-process, with timeout)
./m0_vst3_spike --scan "/Library/Audio/Plug-Ins/VST3"

# Instantiate and test one plugin
./m0_vst3_spike --test "/path/to/plugin.vst3"

# Run self-tests (no external plugins needed)
./m0_vst3_spike --self-test

# Smoke test with minimal round-trip
./m0_vst3_spike --smoke --quit-after-ms 2000
```

### Run Tests

```sh
cd build && ctest --output-on-failure
```

## Modes

| Mode | Flag | Description |
|------|------|-------------|
| Self-test | `--self-test` | SDK API surface checks (no native plugins) |
| Scan | `--scan <dir>` | Helper-process recursive directory scan with timeout/crash reporting |
| Test | `--test <path>` | Instantiate one plugin, process audio/MIDI, report metrics |
| Smoke | `--smoke --quit-after-ms N` | Full round-trip: scan, instantiate, process, save/restore state |

## Scope

The spike scaffolds the following VST3 hosting capabilities. All
implementations are currently PLACEHOLDER stubs that return safe
defaults. The list below describes the planned integration surface:

### Helper-Process Scanning (Safety Boundary) — PLACEHOLDER
- Forks (or spawns) a child process per `.vst3` bundle
- Child loads the module, queries factory info, and reports structured results
  back via IPC (pipe or shared memory, TBD)
- Parent imposes a timeout (default 30 s) — if child hangs, parent kills it
- Parent catches child crashes (signal/SEH) and reports them without crashing
  the scanner process
- Result: a JSON or CSV report of discovered plugins with metadata

### Single-Plugin Instantiation (x86-64 macOS) — PLACEHOLDER
- Instantiate one test **instrument** (e.g. a synthesizer plugin) via the
  VST3 host module
- Instantiate one test **effect** (e.g. a reverb/eq plugin)
- Each runs in its own process for crash isolation
- Report factory info: plugin name, vendor, version, category, I/O bus config

### Audio/MIDI Processing — PLACEHOLDER
- Provide a zero-filled audio buffer of configurable size and sample rate
- Send a MIDI note-on/note-off sequence to instruments
- Process audio blocks through effects
- Validate process call returns `kResultOk`
- Measure process call latency in microseconds

### State Save/Restore — PLACEHOLDER
- Call `IEditController::getState()` to capture opaque plugin state
- Write state to a binary blob on disk
- Destroy the plugin instance
- Re-instantiate from the same factory
- Call `IEditController::setState()` with the saved blob
- Verify re-instantiation does not crash and produces matching factory info

### Latency and Tail Queries — PLACEHOLDER
- Query `IAudioProcessor::getLatencySamples()` before and after processing
- Query `IAudioProcessor::getTailSamples()` for reverb/fade tails
- Verify queries return sensible values (non-negative integers)

### Native Editor Attachment — PLACEHOLDER
- Query `IEditController::createView()` to obtain a platform view pointer
- For a test plugin, attach the view to a temporary offscreen window (NSView*)
- Resize the view via `IPlugView::onSize()` and verify `checkSizeConstraint()`
- Set keyboard focus via `IPlugView::setFocus()`
- Destroy the view cleanly via `IPlugView::removed()` then `release()`

## Future Cross-Platform Instructions

### Windows (x86-64, build-only)

```sh
cmake -B build -G "Visual Studio 17 2025" -A x64
cmake --build build --config Release
```

Self-test mode should pass on GitHub-hosted Windows runners without audio
hardware. Scan and test modes require native VST3 plugin fixtures.

### Ubuntu 24.04 (x86-64)

```sh
cmake -B build -G Ninja
cmake --build build
```

Self-test mode should pass on GitHub-hosted Linux runners. Scan and test
modes require native VST3 plugin fixtures.

### CI Limitations

Standard GitHub-hosted CI runners do NOT have VST3 plugin fixtures
installed, so:
- `--self-test` is CI-safe (no native plugins needed).
- `--scan` and `--test` modes require VST3 bundles in known paths; these
  must be provisioned or mocked.
- Windows/Linux arm64 builds should compile successfully even when native
  plugin fixtures are unavailable.
- Native editor attachment modes require a display and may fail headless.

## Ownership

All source files in `src/` and `tests/` are GraphScore-owned and licensed
under Apache-2.0 (SPDX headers present). The VST3 SDK is fetched at build
time and is NOT owned by GraphScore.
