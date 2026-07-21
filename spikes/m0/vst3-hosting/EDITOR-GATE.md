# Native Plugin Editor — Physical Gate Record

Human-observed gate. Adam at the machine, macOS arm64. This file records
direct observation only; it is not a test result and nothing here was
produced by an automated check.

## 2026-07-21 — partial pass

| Criterion | Plugin | Result |
|---|---|---|
| Instantiate + editor renders | AutoTune (effect) | **PASS** — Adam: "rendered perfectly" |
| Instantiate + editor renders | Kontakt 8 (instrument) | **PASS** — Adam: "rendered perfectly" |
| Editor resize | — | **NOT TESTED** |
| Editor keyboard focus | — | **NOT TESTED** |

Adam's wording, verbatim: "The first one was autotune, now its Kontakt.
Both rendered perfectly." Resize and focus were not attempted before the
session moved on.

## Why the render pass matters more than expected

Both fixtures are copy-protected commercial plugins — AutoTune via iLok,
Kontakt 8 via Native Access. The spike brief anticipated these would fail,
hang, or throw modal authorization dialogs, and instructed that such
misbehavior be recorded as a finding rather than treated as a blocker.
They instead instantiated and rendered native editors successfully. A real
sampler and a real effect hosting correctly is the Milestone 08
requirement, not the SDK-example approximation of it.

## Caveat carried to Milestone 08

Both plugins were **already authorized on this machine**. This observation
says nothing about behavior on a fresh machine or in CI, where
authorization prompts could block instantiation entirely. This is a plugin
scanning and compatibility concern for Milestone 08, not an M0 blocker.

## Outstanding

Resize and keyboard focus remain unverified and must not be described as
passing. Resize is the higher-risk of the two: `IPlugView` size
negotiation is bidirectional, and a plugin may refuse a requested size or
demand its own via `checkSizeConstraint` / `onSize`. Failure typically
presents as clipped content, dead space, or snap-back on drag. Kontakt is
a resizable-UI plugin and is a good stress case for this.
