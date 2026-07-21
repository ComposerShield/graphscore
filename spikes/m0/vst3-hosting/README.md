> **DISPOSABLE SPIKE CODE.** Everything under `src/` is scratch and is
> deleted once the ADR is written. No tests, no sanitizers, no evidence
> catalog — see the Spike Rules in docs/plan/00-architecture-spikes.md.

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


## What this spike is

A single throwaway binary, `m0_vst3_spike`, that answers the M0 hosting
questions on macOS arm64 and prints what happened. It is not a library, it has
no tests, and it does not join the production CMake DAG.

## Build

```sh
cd spikes/m0/vst3-hosting
cmake -B build -G Ninja
cmake --build build
```

Fetches the VST3 SDK at `9fad9770f2ae8542ab1a548a68c1ad1ac690abe0` via
FetchContent (submodules `base`, `cmake`, `pluginterfaces`, `public.sdk`;
`vstgui4` is deliberately not fetched).

### CMake options that were actually required

| Setting | Why |
|---|---|
| `CMAKE_BUILD_TYPE` non-empty | `base/source/fdebug.h` `#error`s unless `DEVELOPMENT`/`RELEASE`/`_DEBUG`/`NDEBUG` is defined; the Ninja single-config default of empty fails the build. |
| `XCODE_VERSION` env var **and** CMake variable | `smtg_detect_xcode_version` shells out to `xcodebuild`, which does not exist under Command Line Tools. Setting only the env var is not enough — the SDK then compares an undefined regular variable against `9`. |
| `SMTG_ENABLE_VSTGUI_SUPPORT=OFF`, `SMTG_ADD_VSTGUI=OFF` | Keeps VSTGUI out of the dependency closure (license policy). Side effect: SDK example plugins have no editor. |
| `SMTG_ENABLE_VST3_PLUGIN_EXAMPLES=ON` | Provides the `adelay` effect and `mda-vst3` (instruments + effects) fixtures. |
| `SMTG_ENABLE_VST3_HOSTING_EXAMPLES=OFF` | Not needed; saves build time. |
| `SMTG_CREATE_PLUGIN_LINK=OFF` | Otherwise the build symlinks its example plugins into `~/Library/Audio/Plug-Ins/VST3`. |
| `SMTG_RUN_VST_VALIDATOR=OFF` | Otherwise every example plugin runs the validator at build time. |
| `GIT_SUBMODULES "base cmake pluginterfaces public.sdk"` | Avoids cloning `vstgui4`. |
| compile `plugprovider.cpp` + `module_mac.mm` into the host target | `sdk_hosting` does **not** contain the platform module loader or `PlugProvider`; every SDK host example compiles them itself. `module_mac.mm` needs `-fobjc-arc`. |

## Run

```sh
# full lifecycle on the SDK's own fixtures
./build/m0_vst3_spike --host build/VST3/Release/adelay.vst3
./build/m0_vst3_spike --host build/VST3/Release/mda-vst3.vst3 --class JX10

# out-of-process scan; @crash and @hang force the child to misbehave
./build/m0_vst3_spike --scan build/VST3/Release/adelay.vst3 \
  'build/VST3/Release/adelay.vst3@crash' \
  'build/VST3/Release/adelay.vst3@hang' --timeout 3

# native editor attach — OPENS A WINDOW, needs a human to look at it
./build/m0_vst3_spike --editor '/Library/Audio/Plug-Ins/VST3/AutoTune.vst3'
```

`--scan-one` is the child mode used internally by `--scan`.
