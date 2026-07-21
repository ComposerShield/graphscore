#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Header include boundary audit (ADR 0003 §7.3).

Scans three directories per target — public headers, private headers, and
implementation sources — and rejects third-party ``#include`` directives that
cross a declared boundary:

* Protected clean targets (§3.3) may not include a third-party header
  anywhere: not in public headers, not in private headers, not in .cpp files.
* Writer-only targets may include the third-party headers their target owns
  (§2.2), but only from .cpp files and private headers — never from a public
  header, where the include would leak to every consumer.

Run by the ``audit_architecture`` CMake target and by CI.
"""

import sys

import graphscore_audit as audit

# ADR 0003 §2.2. The third-party dependency each writer-only target owns, and
# may therefore include from its own implementation files. Every other target
# including that dependency is a violation regardless of file kind.
OWNED_DEPENDENCIES = {
    "rendering": ("FreeType", "HarfBuzz", "ThorVG"),
    "writer_shell": ("SDL3",),
    "audio_device": ("miniaudio", "PortAudio"),
    "midi_io": ("RtMidi",),
    "vst3_host": ("VST3 SDK",),
    # Zero external edges in M1; Phase C adds exactly one bridge.
    "accessibility_platform": (),
}


def classify(include_path):
    """Return the third-party dependency `include_path` belongs to, or None."""
    for pattern, dependency in audit.FORBIDDEN_INCLUDE_PATTERNS:
        if pattern.match(include_path):
            return dependency
    return None


def audit_target(source_dir, target, violations):
    """Check one target's public headers, private headers, and sources."""
    protected = target in audit.PROTECTED_TARGETS
    owned = OWNED_DEPENDENCIES.get(target, ())
    checked = 0

    for path in audit.public_headers(source_dir, target):
        checked += 1
        text = audit.read_text(path)
        for line, include_path in audit.includes(text):
            dependency = classify(include_path)
            if dependency is None:
                continue
            # A public header is the consumer-visible surface. Even a
            # dependency this target owns may not be included here — that is
            # exactly the leak §2.2 forbids.
            violations.append(audit.Violation(
                audit.relative(source_dir, path), line,
                f"public header of graphscore_{target} includes "
                f"<{include_path}> ({dependency}). Third-party headers are "
                f"private to the owning target's .cpp files (ADR 0003 §2.2)."))

    for path in audit.private_sources(source_dir, target):
        checked += 1
        text = audit.read_text(path)
        for line, include_path in audit.includes(text):
            dependency = classify(include_path)
            if dependency is None:
                continue

            if protected:
                violations.append(audit.Violation(
                    audit.relative(source_dir, path), line,
                    f"graphscore_{target} is a protected clean target and "
                    f"includes <{include_path}> ({dependency}). ADR 0003 "
                    f"§3.3 guarantees it builds with no such dependency."))
            elif dependency not in owned:
                permitted = ", ".join(owned) if owned else "none"
                violations.append(audit.Violation(
                    audit.relative(source_dir, path), line,
                    f"graphscore_{target} includes <{include_path}> "
                    f"({dependency}), which it does not own. Dependencies "
                    f"permitted for this target (ADR 0003 §2.2): "
                    f"{permitted}."))

    return checked


def main():
    args = audit.parse_args(__doc__)
    source_dir = args.source_dir.resolve()

    violations = []
    checked = 0

    for target in audit.CLEAN_TARGETS + audit.WRITER_TARGETS:
        checked += audit_target(source_dir, target, violations)

    if checked == 0:
        print(
            "audit_includes examined no files. This is an audit-wiring bug, "
            "not a clean result.",
            file=sys.stderr)
        return 1

    return audit.report("audit_includes", "ADR 0003 §7.3", violations, checked)


if __name__ == "__main__":
    sys.exit(main())
