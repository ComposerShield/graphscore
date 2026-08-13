# GraphScore Execution Checklist

This source-controlled checklist is the one-page cross-milestone status index. A
step box is checked when every detailed deliverable in its linked section is
complete. Maintenance rules live in [AGENTS.md](../../AGENTS.md) under "Plan and
checklist maintenance".

**Execution order:** milestones are listed below by identity, not by the order
they are worked. **Milestone 05 runs before 03 and 04** — see the Execution
Order section of [README.md](README.md#milestone-roadmap) for why, what it
blocks, and the ADR amendments it requires first.

## Milestone 00: Architecture And Risk Spikes

Spike rules in [00-architecture-spikes.md](00-architecture-spikes.md) apply.
The cross-milestone Definition Of Done does **not**. Each box below is one
ADR or one time-boxed investigation, not a review-and-evidence cycle.

- [ ] [Milestone 00 complete](00-architecture-spikes.md)
- [x] Permissive dependency policy recorded (ADR 0001, ADR 0002)
- [x] Architectural boundaries recorded (ADR 0003)
- [x] Rendering and accessibility decision recorded, macOS (ADR 0004)
- [x] Engraving-engine decision recorded (ADR 0005)
- [x] Cooked-format decision recorded (ADR 0006) — owned binary format
- [x] VST3 SDK license confirmed from upstream — MIT across all core
      submodules, incl. `pluginterfaces`; VSTGUI BSD-3-style and not required
- [x] Direct VST3 hosting spike completed, macOS arm64 (ADR 0007)
- [ ] Spike directories deleted or quarantined
- [ ] Exit decision approved

## Milestone 01: Toolchain And CI Foundation

- [x] [Milestone 01 complete](01-toolchain-ci.md)
- [x] Dependencies completed
- [x] Repository structure created
- [x] Git source control initialized and required project/planning files tracked
- [x] Root `AGENTS.md` created and validated
- [x] Commit-message model/vendor attribution prohibition documented in `AGENTS.md`
- [x] Tracked `CLAUDE.md` symlink to `AGENTS.md` created and validated
- [x] CMake and FetchContent foundation completed
- [x] Const-correctness policy implemented
- [x] Local quality gates and `.githooks/pre-commit` completed
- [x] GitHub Actions platform matrix completed
- [x] Skeleton writer/runtime artifacts completed
- [x] Acceptance criteria passed — first green CI run across the platform
      matrix; every criterion verified locally on macOS arm64 and the
      workflow committed
- [x] Test focus completed

## Milestone 02: Domain And Command Model

- [ ] [Milestone 02 complete](02-domain-model.md)
- [x] Dependencies completed
- [x] Identity and value types completed
- [x] Project and track model completed
- [x] Node timeline completed
- [x] Notation model completed
- [x] Graph model completed
- [x] Adaptive playback semantics specified
- [x] Normative playback specification completed
- [x] &nbsp;&nbsp;Phase 8a foundational command/history/transaction increment completed
- [x] &nbsp;&nbsp;Phase 8b metadata/audition-mix command increment completed
- [x] &nbsp;&nbsp;Phase 8c reversible structural/config command increment completed
- [x] &nbsp;&nbsp;Phase 8d reversible add/remove-of-graph-entities increment completed
- [x] &nbsp;&nbsp;Phase 8f-ii selection representation increment completed
- [x] &nbsp;&nbsp;Phase 8g-i clipboard fragment + copy extraction increment completed
- [x] &nbsp;&nbsp;Phase 8g-ii cut + paste command increment completed
- [x] &nbsp;&nbsp;Phase 8g-iii node duplication + identity remapping increment completed
- [x] &nbsp;&nbsp;Phase 8h-i measure/clef mutation primitives increment completed
- [x] &nbsp;&nbsp;Phase 8h-ii non-length-changing timeline command increment completed
- [x] &nbsp;&nbsp;Phase 8h-iii measure/time-signature cascade increment completed
- [x] &nbsp;&nbsp;Phase 8h-iv paste clef/signature context increment completed
- [ ] Command and selection model completed
- [x] Validation service completed
- [ ] Acceptance criteria passed
- [ ] Test focus completed

## Milestone 03: Persistence And Runtime Export

- [ ] [Milestone 03 complete](03-persistence-export.md)
- [ ] Dependencies completed
- [ ] Editable project bundle completed
- [ ] Autosave and recovery completed
- [ ] Schema evolution rules completed
- [ ] Export pipeline completed
- [ ] Runtime loader boundary completed
- [ ] Acceptance criteria passed
- [ ] Test focus completed

## Milestone 04: Realtime Runtime Foundation

- [ ] [Milestone 04 complete](04-runtime-foundation.md)
- [ ] Dependencies completed
- [ ] Public C ABI completed
- [ ] Processing contract completed
- [ ] Capacity and diagnostics completed
- [ ] Same-sample phase order completed
- [ ] Core scheduling completed
- [ ] Realtime implementation rules completed
- [ ] Acceptance criteria passed
- [ ] Test focus completed

## Milestone 05: Notation Engraving And Editing

- [ ] [Milestone 05 complete](05-notation-editor.md)
- [x] Dependencies completed
- [x] Incremental engraving completed
- [x] Note palette and pointer entry completed
- [x] &nbsp;&nbsp;Palette control state model completed
- [x] &nbsp;&nbsp;Pointer-entry preview increment completed
- [x] &nbsp;&nbsp;Pointer-event/chord-building entry increment completed
- [x] &nbsp;&nbsp;Automatic-rest normalization increment completed
- [x] &nbsp;&nbsp;Explicit voice-stream workflow increment completed
- [x] &nbsp;&nbsp;Note-audition preview-request increment completed
- [ ] Selection and keyboard behavior completed
- [x] &nbsp;&nbsp;Rest/marking selection-model increment completed
- [x] &nbsp;&nbsp;Pointer selection-resolver increment completed
- [x] &nbsp;&nbsp;Marking selection-resolver increment completed
- [x] &nbsp;&nbsp;Pointer-drag range-resolver increment completed
- [x] &nbsp;&nbsp;Stemless notehead-column hit-region increment completed
- [x] &nbsp;&nbsp;Range-selection hit-region integration increment completed
- [x] &nbsp;&nbsp;Hit-priority and voice-disambiguation increment completed
- [x] &nbsp;&nbsp;Dedicated pointer-range selection-tool increment completed
- [x] &nbsp;&nbsp;Per-staff measure hit-region increment completed
- [x] &nbsp;&nbsp;Aligned multi-staff measure-selection increment completed
- [x] &nbsp;&nbsp;Keyboard range-extension and accessible selection-control increment completed
- [ ] Whole-measure selection and copy/paste completed
- [ ] Pointer-drag arbitrary-range selection and copy/paste completed
- [ ] Clipboard boundary/span and paste-preview behavior validated
- [ ] Structural editing completed
- [ ] Playback semantics in the editor model completed
- [ ] Accessibility completed
- [ ] Acceptance criteria passed
- [ ] Test focus completed

## Milestone 06: Infinite Graph Canvas

- [ ] [Milestone 06 complete](06-graph-canvas.md)
- [ ] Dependencies completed
- [ ] Infinite viewport completed
- [ ] Notation nodes completed
- [ ] Connector creation and semantics completed
- [ ] Orthogonal route editing completed
- [ ] Selection and playback affordances completed
- [ ] Organization operations completed
- [ ] Acceptance criteria passed
- [ ] Test focus completed

## Milestone 07: Adaptive Tempo And Transitions

- [ ] [Milestone 07 complete](07-adaptive-transitions.md)
- [ ] Dependencies completed
- [ ] Tempo lane completed
- [ ] Sequential routing completed
- [ ] Vertical routing completed
- [ ] Pickdown overlap completed
- [ ] Event and random APIs completed
- [ ] Writer graph feedback completed
- [ ] Acceptance criteria passed
- [ ] Test focus completed

## Milestone 08: Writer Audio And VST3 Hosting

- [ ] [Milestone 08 complete](08-audio-vst.md)
- [ ] Dependencies completed
- [ ] Audio devices and engine completed
- [ ] Track plugin chains completed
- [ ] Plugin scanning and compatibility completed
- [ ] In-process hosting question resolved (carried from ADR 0007)
- [ ] Plugin editors and parameters completed
- [ ] Audition mixer completed
- [ ] Transport and preview completed
- [ ] Acceptance criteria passed
- [ ] Test focus completed

## Milestone 09: Integrated Writer Workflow

- [ ] [Milestone 09 complete](09-writer-integration.md)
- [ ] Dependencies completed
- [ ] Document lifecycle completed
- [ ] Live playback snapshots completed
- [ ] Graph playback interaction completed
- [ ] Composition workflows completed
- [ ] Application preferences completed
- [ ] Acceptance criteria passed
- [ ] Test focus completed

## Milestone 10: Accessibility, Performance, And Hardening

- [ ] [Milestone 10 complete](10-hardening.md)
- [ ] Dependencies completed
- [ ] Accessibility completion audit passed
- [ ] Canvas and notation performance gates passed
- [ ] Realtime and determinism hardening passed
- [ ] Robustness and security hardening passed
- [ ] Platform compatibility gates passed
- [ ] Acceptance criteria passed
- [ ] Test focus completed

## Milestone 11: Engine Integrations And 0.1.0 Release

- [ ] [Milestone 11 complete](11-engine-release.md)
- [ ] Dependencies completed
- [ ] Unity integration completed
- [ ] Unreal integration completed
- [ ] SDK and examples completed
- [ ] Release automation completed
- [ ] User and integrator documentation completed
- [ ] Acceptance criteria passed
- [ ] Test focus completed
- [ ] `0.1.0` archives published

## Milestone 12: General MIDI CC

- [ ] [Milestone 12 complete](12-midi-cc.md)
- [ ] Dependencies completed
- [ ] Milestone-start questions resolved
- [ ] Proposed scope reconfirmed after `0.1.0` feedback
- [ ] General CC authoring completed
- [ ] General CC persistence/export completed
- [ ] General CC runtime behavior completed
- [ ] General CC accessibility completed
- [ ] Acceptance criteria passed
- [ ] Test focus completed
