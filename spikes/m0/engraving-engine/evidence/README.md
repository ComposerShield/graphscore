# Engraving Spike Evidence Catalog

Generated from clean `build` and `build-asan` directories on 2026-07-21; the
final source manifest was recorded at 2026-07-21T08:49:47Z
on macOS 15.7.7 arm64 (Apple M1 Pro). Tool versions and source hashes are in
`00-toolchain-and-provenance.log`.

| Evidence | Normal | ASan+UBSan |
| --- | --- | --- |
| Configure | `01-configure.log` | `06-asan-configure.log` |
| Build | `02-build.log` | `07-asan-build.log` |
| CTest | `03-ctest.log`: 1/1, 100% | `08-asan-ctest.log`: 1/1, 100% |
| GTest inventory | `04-gtest-inventory.log`: 18 tests | same executable inventory |
| Evaluator | `05-evaluator.log`: 15/15 criteria | `09-asan-evaluator.log`: 15/15 criteria |
| Measurement | `10-measurement.log` | `11-asan-measurement.log` |

`commands.md` is the exact reproducible sequence. All sanitizer commands used
`ASAN_OPTIONS=detect_leaks=0`; normal and ASan+UBSan clean strict-warning builds
and CTest passed 1/1, the evaluator passed 15/15, and no sanitizer diagnostics
were emitted. The 18-test GTest inventory includes hostile geometry/input,
narrow four-voice append, and evaluator-mutation tests.
The measurement timing is a local observation only: it demonstrates four
visited notes for the changed measure, not a production performance promise.
