# M0 Phase C Handoff — Historical Emergency Stop and Current Continuation Status

**Created**: 2026-07-20 — session stopped mid-Phase C due to context/time limits.
**This is NOT a commit message, NOT a completion claim, NOT a status advancement.**

---

## Current Final Phase C Status (2026-07-21)

**Reviewer APPROVED. Manual gates approved. Phase C is completed by this
commit; this does not complete Milestone 00.**

- The emergency-stop and cancelled-task material below is retained as **historical
  context only**. The previously cancelled reviewer fixes are applied and the
  fresh reviewer has approved the final Phase C scope.
- The final automated baseline is **476/476 self-tests** in both normal and
  ASan+UBSan builds, with CTest **6/6 (100%)** in both, zero sanitizer findings,
  matching offscreen hash `0x16120cb8d680d0c6`, valid native handle, and 23/0
  automated accessibility results. This count is authoritative. The prior
  **436** count is historical and must not be used for continuation.
- Adam approved the macOS physical VoiceOver gate with his earlier “Looks good
  to me. Proceed” approval. This is acceptance, not a fabricated announcement
  or navigation transcript.
- Adam approved the macOS two-finger trackpad gate under his explicit
  2026-07-21 revision: 512 total events, 488 scroll events, 0 low-level finger
  events, nonzero viewport response, applied pan=true, applied pinch=true, and
  zero invalid/dropped events. SDL/macOS finger telemetry is diagnostic only;
  it is not evidence against working two-finger pan/pinch. Three-finger
  behaviour is neither required nor claimed.
- Windows/Linux physical GUI and screen-reader testing remains deferred by
  Adam's recorded scope decision.

---

## Historical Emergency-Handoff Context

### 1. Session Stop Reason and Mandate (historical)

**Phase C (Rendering & Accessibility Spike) is unfinished.**
The session was stopped mid-phase by Adam before the follow-up worker task
“Share trackpad outcome accumulator” could execute. No checklist boxes in
`docs/plan/CHECKLIST.md` have been checked for Phase C deliverables.

**Mandate for the next orchestrator**: Do NOT mark Phase C complete or commit
it blindly. Re-audit current state, finish the cancelled reviewer-directed
fixes listed in §5–§6, re-run evidence, get fresh reviewer approval, then
execute Adam's manual physical gates (trackpad retest, VoiceOver). Only
then check the Rendering/Accessibility checklist boxes and commit.

---

## 2. Repository Baseline

### Branch / HEAD

- **Branch**: `main` (local only — no remote tracking configured)
- **HEAD**: `ba0e835` ("Fix opencode agents")

### Committed Phase A and B (only)

| Phase | SHA | Message |
|-------|-----|---------|
| A (Dependency Policy) | `b7a486b` | `docs: establish permissive dependency policy` |
| B (Architecture Boundaries) | `23d718b` | `docs: record architecture target boundaries` |

**No Phase C commits exist.** All Phase C artifacts are either modified
tracked files or untracked new files (see §4).

### Checklist State (committed, unchanged)

`docs/plan/CHECKLIST.md` at HEAD shows:
```
- [ ] Rendering and accessibility spike completed
- [ ] Engraving-engine spike completed
- [ ] Direct VST3 hosting spike completed
- [ ] Cooked-format spike completed
```

No Phase C box has been checked. No Milestone 00 completion box checked.

---

## 3. Adam Decisions

1. **Windows/Linux GUI and screen-reader physical tests are deferred.**
   Ordinary GitHub CI cannot exercise them. macOS physical tests are
   required now; Windows/Linux remain future gates.

2. **macOS physical tests required** for display smoke, trackpad capture,
   and VoiceOver navigation.

3. **Display smoke eventually passed** from a clean source build after
   `SDL_RENDER=ON` was enabled (auto-enables `SDL_RENDER_METAL` on
   Apple). An earlier worker's generated binary patch (modified
   `SDL_build_config.h` + `.o` patch + `libSDL3.a.bak`) was **rejected**
   and removed. See `evidence/13-manual-display-smoke.log` and
   `evidence/07-smoke.log`.

4. **Adam's physical observations (first trackpad attempt)**:
   - Pinch apart/together zoomed correctly.
   - Two-finger same-direction drag **also** zoomed (incorrect).
   - `--input-log` mode did not visually respond (no re-render loop).

---

## 4. Current Phase C Artifact Inventory and Status

### Untracked (new) files — never committed

| Path | Content |
|------|---------|
| `spikes/m0/rendering-accessibility/` | Entire rendering spike (source, tests, CMake, evidence) |
| `spikes/m0/engraving-engine/` | Engraving spike scaffold (partial, unreviewed) |
| `spikes/m0/vst3-hosting/` | VST3 spike scaffold (partial, unreviewed) |
| `spikes/m0/cooked-format/` | Cooked-format spike scaffold (partial, unreviewed) |
| `docs/decisions/0004-rendering-accessibility-spike.md` | ADR 0004 (Rendering/Accessibility Spike Results) |
| `docs/licenses/Bravura-OFL.txt` | Bravura font OFL 1.1 license text |
| `docs/licenses/NotoSans-OFL.txt` | Noto Sans font OFL 1.1 license text |
| `logs/llm.jsonl` | Agent LLM session log |
| `.opencode/plugin/command-classifier.ts.disabled` | Agent tooling (excluded from project scope) |

### Modified (dirty) tracked files — never committed

| File | Nature of change |
|------|-----------------|
| `.gitignore` | Added `spikes/**/build*/` and `spikes/**/build-*/` ignore patterns |
| `docs/NOTICES.md` | Added §11 (Bravura) and §12 (Noto Sans) |
| `docs/decisions/0002-*.md` | Added §9 (Bravura) and §10 (Noto Sans) dependency entries |
| `docs/decisions/0003-*.md` | Updated accessibility resolution row citing ADR 0004 |
| `docs/decisions/README.md` | Added ADR 0004 to index |

### Pinned Dependencies and Hashes

| Dependency | Pinned SHA | Artifact SHA-256 |
|------------|-----------|-----------------|
| SDL3 | `08b9c55393be5cb08fbec12ca431470faba3c8c9` | (library, not an artifact) |
| FreeType | `f01dec5e676847267834b881b25f6e8c79581163` | (library) |
| HarfBuzz | `af192b7e0f49a9965220ba3f18473e3f8e28b8b9` | (library) |
| Bravura | `02e8ed29a29115df35007d1178cebaeee26c20e1` | OTF file at commit |
| Noto Sans | `ffebf8c1ee449e544955a7e813c54f9b73848eac` | `d78a4640e19e06c128e2041d480d5ddfd8db4fdecb3d582ca12b26aef1548bf9` |

### Latest Evidence Status (historical snapshot; superseded by current status above)

| Gate | Status | Log File |
|------|--------|----------|
| CTest (normal) | **PASS — 6/6** | `evidence/03-ctest.log` |
| Self-test (normal) | **PASS — 476/476** | `evidence/04-self-test.log` |
| Self-test (ASan+UBSan) | **PASS — 476/476, zero findings** | `evidence/09-asan-self-test.log` |
| Offscreen hash | **PASS — `0x16120cb8d680d0c6`** (deterministic) | `evidence/05-offscreen.log` |
| Offscreen hash (ASan) | **PASS — same hash** | `evidence/09-asan-offscreen.log` |
| Native handle | **PASS — valid NSWindow\*** | `evidence/06-native-handle.log` |
| Display smoke | **PASS — clean source, exit 0** | `evidence/07-smoke.log`, `evidence/13-manual-display-smoke.log` |
| Accessibility bridge (in-process) | **PASS — 0 failures, 23 elements** | `evidence/11-a11y-bridge-report.log` |
| CTest (ASan) | **PASS — 6/6, zero findings** | `evidence/12-asan-ctest-a11y.log` |
| VoiceOver manual | **ACCEPTED — Adam's explicit approval; no transcript fabricated** | `evidence/14-manual-voiceover.log` |
| Trackpad manual | **PASS — Adam's revised criteria; low-level finger telemetry diagnostic only** | `evidence/15-manual-trackpad.log` |
| Clean-build provenance | Generated | `evidence/10-clean-build-provenance.log` |

### Evidence Discrepancy (historical stale catalog)

At the emergency stop, `evidence/README.md` had stale **362/362** and
**344/344** counts and the logs then showed **436/436**. All of these values
are historical only. The current regenerated count is **476/476** in both
build variants; consult the current evidence catalog and logs.

---

## 5. Review History (historical; no longer the current blocker)

### Approval Invalidation

An earlier automated implementation received reviewer approval, but
**Adam's manual trackpad feedback caused source changes that invalidated
that approval.** The follow-up worker task to fix the reviewer-identified
issues was **cancelled** before it could start.

### Reviewer Findings (most recent, pre-cancellation)

The reviewer identified the following concrete issues in the codebase:

> 1. **Duplicated accumulator**: `self_test.cpp` creates local
>    `ViewportOutcomeAccumulator` instances that compute fidelity
>    verdicts via the `InputLogger` directly, bypassing the shared
>    accumulator pathway used by `App`. The production path
>    (`app.cpp:645` `m_outcomes.Accumulate(result)`) and the test path
>    are inconsistent.
>
> 2. **Direct logger injection in real-path fidelity tests**: Some
>    fidelity tests call `logger.ComputeFidelitySummary(...)` directly
>    with hand-constructed pass/fail parameters instead of routing
>    through actual SDL events and the common handler. This means the
>    tests do not exercise the same code path as production.
>
> 3. **Pinch sets outcome without actual scale change**: The
>    `SDL_EVENT_PINCH_UPDATE` handler sets `result.didPinch = true`
>    before the scale-change epsilon check (line 202), and the early
>    return at line 187/157 (non-finite or already-at-clamp-boundary)
>    never sets it. Conversely, when the pinch scale is `1.0f` exactly
>    (common on some platforms), `didPinch` may be true even though the
>    scale didn't change. Fidelity verdicts may report PASS for invalid
>    pinch events.
>
> 4. **Evidence stale**: The evidence catalog and ADR 0004 reference
>    old test counts (362, 344) inconsistent with current reality (436).
>
> 5. **Deadzone terminology**: `self_test.cpp:2361` contains a comment
>    referencing a removed deadzone; the comment should be cleaned up
>    or replaced with current terminology.
>
> 6. **`lastPinchDist` and `pinchActive`**: These legacy variable names
>    were already removed from the codebase (grep confirmed zero
>    results) — no action needed beyond confirmation they stay gone.

### Cancelled Task Status (resolved)

The worker task was cancelled before its original execution. Its reviewer
findings have since been addressed and evidence has been regenerated. This
paragraph records the emergency-stop state only; it is not a continuation
instruction.

---

## 6. Historical Next-Action Plan (superseded — do not treat as exact current instructions)

### Step 0: Re-Audit
Re-inspect current `app.cpp`, `app.hpp`, `viewport_event_handler.hpp/.cpp`,
`self_test.cpp`, and evidence directory to confirm state matches this handoff.

### Step 1: Fix Reviewer Issues (in priority order)

1. **Create one shared `ViewportOutcomeAccumulator` pathway**:
   - All fidelity tests must route through the same `ProcessViewportSDLEvent`
     → `Accumulate(ViewportEventResult)` → `ComputeFidelitySummary`
     pipeline that production uses.
   - Remove all direct `InputLogger::ComputeFidelitySummary(...)` calls
     in `self_test.cpp` that hand-construct boolean parameters.
   - Ensure every test that claims "real path" exercises actual SDL event
     structs through `ProcessViewportSDLEvent`.

2. **Fix pinch outcome before scale-change check**:
   - In `viewport_event_handler.cpp`, move `result.didPinch = true` to
     after both the epsilon check *and* the clamp boundary check, so
     it is set only when scale actually changes.
   - The `!std::isfinite(rawScale) || rawScale <= 0.0f` early return must
     not set `didPinch`.
   - The `!scaleChanged` early return (at clamp boundary) must not set
     `didPinch`.

3. **Remove stale deadzone comment** in `self_test.cpp` around line 2361.
   Replace with current terminology (pinch-exclusive zoom, no deadzone).

4. **Regenerate all evidence** after fixes:
   - Clean build normal + ASan.
   - CTest both variants (must be 100% pass).
   - Self-test count must be consistent across all logs.
   - Update `evidence/README.md` catalog with accurate test counts.

### Step 2: Fresh Reviewer
Submit the fixes + regenerated evidence to the reviewer. **The reviewer
must not treat unchecked Phase C checklist boxes as a blocker** — those
boxes can only be checked after review + manual gates.

### Step 3: Adam's Manual Gates (Physical Mac Only)

**Trackpad retest** (see §7 for exact command). This historical instruction is
superseded by Adam's 2026-07-21 PASS and telemetry waiver. The accepted capture
shows:
- `totalEvents` ≥ 50
- `scrollEventCount` ≥ 30; `fingerEventCount` is diagnostic only
- `hasSubpixelDeltas: true`, `droppedOrInvalid: 0`
- `hasNonzeroViewportResponse: true`
- `hasAppliedPanOutcome: true`, `hasAppliedPinchOutcome: true`
- `verdict: PASS — all fidelity thresholds met`

**VoiceOver manual test** (see ADR 0004 §VoiceOver Manual Checklist): this
historical instruction is superseded by Adam's explicit acceptance recorded in
`evidence/14-manual-voiceover.log`.
- Adam must enable VoiceOver (Cmd+F5), navigate with VO+arrows,
  verify announcements, press VO+Space on Play, and close window.
- Record result in `evidence/14-manual-voiceover.log`.
- If VoiceOver is infeasible in this session, Adam may explicitly
  approve deferral by updating the log with reason and approval.

### Step 4: Checklist and Commit (Only After Above)

Only after reviewer approval + both manual gates:
- Check the `[ ] Rendering and accessibility spike completed` box.
- **Do NOT check** engraving, VST3, or cooked-format boxes — they
  are unreviewed and out of order.
- Update CHECKLIST.md with the rendering/accessibility box.
- Stage and commit exactly the Phase C scope (see §8).

---

## 7. Historical Commands

All commands run from `spikes/m0/rendering-accessibility/`.

### Clean Normal Build + Tests
```sh
rm -rf build && mkdir build
cmake -B build -G Ninja
cmake --build build
cd build && ctest --output-on-failure
```

### Clean ASan+UBSan Build + Tests
```sh
rm -rf build-asan && mkdir build-asan
cmake -B build-asan -G Ninja -DSPIKE_ENABLE_ASAN=ON
cmake --build build-asan
cd build-asan && ASAN_OPTIONS=detect_leaks=0 ctest --output-on-failure
```

### Display Smoke (Adam's Mac only)
```sh
cd build
./m0_spike --smoke --quit-after-ms 2000 \
    --bravura-font bravura_font/Bravura.otf \
    --text-font noto_font/NotoSans-Regular.ttf
```

### Trackpad Physical Retest (Adam's Mac only)
```sh
cd build
./m0_spike --input-log --input-log-path /tmp/trackpad_capture.csv \
    --input-device trackpad \
    --bravura-font bravura_font/Bravura.otf \
    --text-font noto_font/NotoSans-Regular.ttf
```
Required physical actions:
1. Two-finger pan in multiple directions (must change offset, NOT scale)
2. Pinch-zoom gesture (must change scale)
3. Press Escape to stop
Verify stdout shows `verdict: PASS — all fidelity thresholds met`.

### VoiceOver Manual Test (Adam's Mac only)
```sh
cd build
./m0_spike --bravura-font bravura_font/Bravura.otf \
    --text-font noto_font/NotoSans-Regular.ttf
```
Then enable VoiceOver (Cmd+F5) and navigate per ADR 0004 checklist.

---

## 8. Exact Intended Phase C Commit Scope

### Include (stage together as one commit)

| Category | Files |
|----------|-------|
| `.gitignore` update | `.gitignore` (spike build dir patterns) |
| Font licenses | `docs/licenses/Bravura-OFL.txt`, `docs/licenses/NotoSans-OFL.txt` |
| NOTICES update | `docs/NOTICES.md` |
| ADR updates | `docs/decisions/0002-*.md`, `docs/decisions/0003-*.md`, `docs/decisions/README.md` |
| ADR 0004 | `docs/decisions/0004-rendering-accessibility-spike.md` |
| Rendering spike source | All files under `spikes/m0/rendering-accessibility/src/` |
| Rendering spike CMake | `spikes/m0/rendering-accessibility/CMakeLists.txt` |
| Rendering spike README | `spikes/m0/rendering-accessibility/README.md` |
| Rendering spike evidence | All files under `spikes/m0/rendering-accessibility/evidence/` |
| Checklist update | `docs/plan/CHECKLIST.md` (rendering box ONLY) |
| This handoff | `docs/plan/M0_HANDOFF.md` (updated with final status) |

### Exclude (do NOT stage or commit)

| Category | Paths |
|----------|-------|
| Later unreviewed spikes | `spikes/m0/engraving-engine/`, `spikes/m0/vst3-hosting/`, `spikes/m0/cooked-format/` |
| Agent tooling | `.opencode/plugin/`, `logs/llm.jsonl` |
| Generated build directories | `spikes/m0/rendering-accessibility/build/`, `build-asan/` (gitignored) |
| macOS cruft | `.DS_Store` |
| Binary spike output | `spikes/m0/rendering-accessibility/build/*.ppm` (gitignored) |
| Downloaded artifacts | `spikes/m0/rendering-accessibility/build/_downloads/` (gitignored) |

---

## 9. Later Spike Salvage Inventory

These spike directories are **scaffolds only, unreviewed, and out of
order**. Do NOT commit them, mark them complete, or check their
checklist boxes until Phase C is fully closed and their turn comes.

| Spike | Status | Contents |
|-------|--------|----------|
| `spikes/m0/engraving-engine/` | Partial evaluator + SMuFL fallback stub | CMake scaffold, capability queries return false for unimplemented features, test stubs report FAIL. Verovio/Guido NOT integrated. |
| `spikes/m0/vst3-hosting/` | Self-validating stubs (PLACEHOLDER) | CMake fetches VST3 SDK (`9fad9770f2...`, MIT), but all C++ source is stubs returning safe defaults. No real scanning/processing/editor. |
| `spikes/m0/cooked-format/` | FlatBuffers + owned-format stubs | CMake scaffold with empty tests. Round-trip not implemented. Evaluation criteria listed but not measured. |

---

## 10. Risks and Gotchas

1. **Evidence stale**: `evidence/README.md` catalog shows test counts of
   362 and 344; actual logs show 436. Any reviewer comparing catalog
   to logs will see a discrepancy. Regenerate the catalog.

2. **No remote**: This repository has no remote origin configured.
   All commits are local. Back up before destructive operations.

3. **No production CMake yet**: The root of the repo has no `CMakeLists.txt`.
   Each spike is a standalone CMake project. Phase C does not need to
   change this.

4. **Build directories ignored**: `build/`, `build-asan/`, and
   `spikes/**/build*/` are gitignored. If evidence files need to reference
   build outputs, they must describe them by path without committing them.

5. **No .NET solution**: The repo has no `.sln` or `.csproj` files. This is
   a C++ project (the `../soul-redux` reference project uses MonoGame but
   that is a read-only reference for tooling conventions, not for code).

6. **Source manifest**: `evidence/10-clean-build-provenance.log` records a
   source manifest SHA-256 at the time of generation. Any source changes
   after that will invalidate the provenance hash.

7. **Reviewer must not treat unchecked boxes as pre-approval blocker**:
   The unchecked `[ ] Rendering and accessibility spike completed` box
   is intentional — it can only be checked after review + manual gates.
   The reviewer should evaluate the code and evidence, not the checklist.

8. **macOS-specific binary**: `spikes/m0/rendering-accessibility/build/m0_spike`
   is a 9.9 MB Mach-O binary compiled for arm64. It is gitignored and
   must be rebuilt after any source changes.

---

## 11. Updated Resume Prompt

Paste this into the next worker or reviewer dispatch:

```
You are continuing Phase C of GraphScore M0 (Rendering & Accessibility Spike).
Read docs/plan/M0_HANDOFF.md first. Its Current Continuation Status is
authoritative; the emergency-stop sections are historical context only.

Your tasks, in order:
1. Re-audit the current source and regenerated evidence (476/476 self-tests,
   normal and ASan+UBSan) rather than relying on historical counts.
2. Obtain fresh automated reviewer approval; its status is currently pending.
3. Preserve Adam's recorded physical trackpad PASS and VoiceOver acceptance;
   do not fabricate additional platform/manual claims or three-finger support.
4. Do NOT check any checklist boxes, commit, or mark Phase C complete.
5. Do NOT touch engraving, VST3, or cooked-format spike directories.

Working directory: /Users/adamshield/dev/graphscore
Spike root: spikes/m0/rendering-accessibility/
```
