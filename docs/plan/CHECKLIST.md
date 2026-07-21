# GraphScore Execution Checklist

This source-controlled checklist tracks the phases and major steps in the milestone plan. A step is complete only when every detailed deliverable in the linked section is complete. Acceptance and test boxes must be checked before the milestone-complete box.

## Milestone 00: Architecture And Risk Spikes

- [ ] [Milestone 00 complete](00-architecture-spikes.md)
- [x] Dependencies confirmed
- [x] Permissive dependency policy completed
- [x] Architectural boundaries recorded
- [x] Rendering and accessibility spike completed
- [ ] Engraving-engine spike completed
- [ ] Direct VST3 hosting spike completed
- [ ] Cooked-format spike completed
- [ ] Acceptance criteria passed
- [ ] Test and evidence requirements completed
- [ ] Exit decision approved

## Milestone 01: Toolchain And CI Foundation

- [ ] [Milestone 01 complete](01-toolchain-ci.md)
- [ ] Dependencies completed
- [ ] Repository structure created
- [ ] Git source control initialized and required project/planning files tracked
- [ ] Root `AGENTS.md` created and validated
- [ ] Commit-message model/vendor attribution prohibition documented in `AGENTS.md`
- [ ] Tracked `CLAUDE.md` symlink to `AGENTS.md` created and validated
- [ ] CMake and FetchContent foundation completed
- [ ] Const-correctness policy implemented
- [ ] Local quality gates and `.githooks/pre-commit` completed
- [ ] GitHub Actions platform matrix completed
- [ ] Skeleton writer/runtime artifacts completed
- [ ] Acceptance criteria passed
- [ ] Test focus completed

## Milestone 02: Domain And Command Model

- [ ] [Milestone 02 complete](02-domain-model.md)
- [ ] Dependencies completed
- [ ] Identity and value types completed
- [ ] Project and track model completed
- [ ] Node timeline completed
- [ ] Notation model completed
- [ ] Graph model completed
- [ ] Adaptive playback semantics specified
- [ ] Normative playback specification completed
- [ ] Command and selection model completed
- [ ] Validation service completed
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
- [ ] Dependencies completed
- [ ] Incremental engraving completed
- [ ] Note palette and pointer entry completed
- [ ] Selection and keyboard behavior completed
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
