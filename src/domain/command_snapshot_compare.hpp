// SPDX-License-Identifier: Apache-2.0
//
// Stale-context comparison for the noexcept undo/redo boundary.
// Not installed; included only by src/domain/* command .cpp files.

#pragma once

namespace graphscore {
namespace internal {

// Compares live content against an undo/redo snapshot, reporting a
// comparison that cannot complete as a mismatch instead of letting an
// exception cross a `noexcept` command boundary.
//
// VoiceEvent is a std::variant, and libstdc++ implements variant equality
// through std::get, whose bad_variant_access throw is defined inline in the
// header; libc++ routes the same throw through an out-of-line function, so
// only the Linux build sees a throwing expression here. A variant can only
// become valueless when a move throws, which a snapshot copy never does, so
// the comparison cannot actually fail in practice — and reporting "not
// equal" is the safe answer either way: callers treat a mismatch as stale
// context and reject the undo or redo without mutating the project.
template <typename T>
[[nodiscard]] inline bool snapshot_matches(const T& live,
                                           const T& snapshot) noexcept {
  try {
    return live == snapshot;
  } catch (...) {
    return false;
  }
}

}  // namespace internal
}  // namespace graphscore
