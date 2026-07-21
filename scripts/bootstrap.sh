#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
#
# One-time developer setup. Points this clone's Git hooks at the tracked
# .githooks directory:
#
#   ./scripts/bootstrap.sh
#
# core.hooksPath is set with `git config --local`, so it is written to this
# repository's .git/config only. The developer's global Git configuration is
# never modified, and other clones on the same machine are unaffected.
#
# Re-running is safe and idempotent.

set -eu

repo_root=$(git rev-parse --show-toplevel)
cd "$repo_root"

if [ ! -d .githooks ]; then
  echo "bootstrap: .githooks/ is missing from this checkout." >&2
  exit 1
fi

# The executable bit is tracked in the index, but a checkout made with
# core.fileMode=false (or an archive export) can lose it.
chmod +x .githooks/pre-commit

git config --local core.hooksPath .githooks

echo "bootstrap: core.hooksPath -> .githooks (local to this clone)"

# Report, but do not install, the optional lint tools. Both are optional for
# building and testing; the hook skips whichever is absent and CI runs both
# regardless.
missing=""

command -v cpplint >/dev/null 2>&1 || missing="$missing\n  cpplint       pip install cpplint"

if ! command -v clang-format >/dev/null 2>&1 \
   && [ ! -x /Library/Developer/CommandLineTools/usr/bin/clang-format ]; then
  missing="$missing\n  clang-format  LLVM install, or Xcode Command Line Tools on macOS"
fi

if ! command -v clang-tidy >/dev/null 2>&1; then
  missing="$missing\n  clang-tidy    LLVM install (only needed for -DGRAPHSCORE_ENABLE_CLANG_TIDY=ON)"
fi

if [ -n "$missing" ]; then
  # shellcheck disable=SC2059
  printf "bootstrap: optional lint tools not found:$missing\n"
fi

echo "bootstrap: done. Next: cmake --preset debug && cmake --build --preset debug"
