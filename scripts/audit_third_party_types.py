#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Third-party type leakage audit (ADR 0003 §7.7).

Reads the public headers of every GraphScore target and rejects any reference
to a third-party type name — ``SDL_``, ``FT_``, ``hb_``, ``tvg::``, ``ma_``,
``PaStream``, ``RtMidi``, ``Steinberg::``, ``IPluginFactory``,
``accesskit_``.

This complements audit_includes.py: a public header can leak a third-party
type through a forward declaration without ever including the third-party
header, which the include audit alone would not see.

Comments and string literals are stripped before scanning, so the boundary
documentation in this repository — which names these types constantly — does
not report itself as a violation.

Run by the ``audit_architecture`` CMake target and by CI.
"""

import sys

import graphscore_audit as audit


def scan_public_header(source_dir, target, path, violations):
    """Flag third-party type references and includes in one public header."""
    text = audit.read_text(path)
    relative_path = audit.relative(source_dir, path)

    code = audit.strip_comments_and_strings(text)
    for number, line in enumerate(code.splitlines(), start=1):
        for pattern, dependency in audit.FORBIDDEN_TYPE_PATTERNS:
            match = pattern.search(line)
            if match:
                violations.append(audit.Violation(
                    relative_path, number,
                    f"public header of graphscore_{target} exposes the "
                    f"{dependency} type '{match.group(0)}'. Public headers "
                    f"expose only GraphScore-owned types (ADR 0003 §2.2)."))

    # Header pollution check (§7.7 step 4): a third-party #include in a
    # public header is a violation even when no third-party type is named.
    for number, include_path in audit.includes(text):
        for pattern, dependency in audit.FORBIDDEN_INCLUDE_PATTERNS:
            if pattern.match(include_path):
                violations.append(audit.Violation(
                    relative_path, number,
                    f"public header of graphscore_{target} includes "
                    f"<{include_path}> ({dependency}), polluting every "
                    f"consumer's include path (ADR 0003 §7.7)."))


def main():
    args = audit.parse_args(__doc__)
    source_dir = args.source_dir.resolve()

    violations = []
    checked = 0

    for target in audit.CLEAN_TARGETS + audit.WRITER_TARGETS:
        for path in audit.public_headers(source_dir, target):
            checked += 1
            scan_public_header(source_dir, target, path, violations)

    if checked == 0:
        print(
            "audit_third_party_types examined no public headers. This is an "
            "audit-wiring bug, not a clean result.",
            file=sys.stderr)
        return 1

    return audit.report(
        "audit_third_party_types", "ADR 0003 §7.7", violations, checked)


if __name__ == "__main__":
    sys.exit(main())
