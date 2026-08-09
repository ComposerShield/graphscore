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
chmod +x .githooks/pre-commit .githooks/pre-push

git config --local core.hooksPath .githooks

echo "bootstrap: core.hooksPath -> .githooks (local to this clone)"

# Report, but do not install, lint tools. cpplint is optional locally;
# clang-format 18 is required whenever the pre-commit hook sees staged C/C++.
missing=""

command -v cpplint >/dev/null 2>&1 || missing="$missing\n  cpplint       pip install cpplint"

clang_format_major() {
  "$1" --version 2>/dev/null \
    | sed -n 's/.*clang-format version \([0-9][0-9]*\)\(\..*\)\{0,1\}$/\1/p'
}

clang_format=""
for candidate in clang-format-18 \
  /opt/homebrew/opt/llvm@18/bin/clang-format \
  /usr/local/opt/llvm@18/bin/clang-format clang-format; do
  if command -v "$candidate" >/dev/null 2>&1; then
    candidate_path=$(command -v "$candidate")
    if [ "$(clang_format_major "$candidate_path")" = "18" ]; then
      clang_format=$candidate_path
      break
    fi
  fi
done

if [ -n "$clang_format" ]; then
  echo "bootstrap: clang-format 18 -> $clang_format"
else
  missing="$missing\n  clang-format 18  brew install llvm@18 | sudo apt install clang-format-18 | pip install clang-format==18.1.8 (required by pre-commit)"
fi

# The pre-push hook requires clang-tidy 18 specifically: CI pins that
# version, and another major version reports a different set of findings.
tidy_version=""
if command -v clang-tidy >/dev/null 2>&1; then
  tidy_version=$(clang-tidy --version 2>/dev/null | sed -n 's/.*version \([0-9]*\).*/\1/p')
fi
if [ "$tidy_version" != "18" ] && [ ! -x /opt/homebrew/opt/llvm@18/bin/clang-tidy ]; then
  missing="$missing\n  clang-tidy 18  brew install llvm@18 | apt install clang-tidy-18 | pip install clang-tidy==18.1.8 (pre-push hook)"
fi

if [ -n "$missing" ]; then
  # shellcheck disable=SC2059
  printf "bootstrap: lint tools not found:$missing\n"
fi

echo "bootstrap: done. Next: cmake --preset debug && cmake --build --preset debug"
