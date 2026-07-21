#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Runtime exported-symbol audit (ADR 0003 §7.5).

Reads the exported symbol table of the built ``graphscore_runtime`` shared
library and compares it against the C ABI surface declared in
``include/graphscore/c_abi/graphscore_c_abi.h``:

* every declared ``GS_API`` function must be exported;
* nothing else may be exported — no undocumented C entry point, and no C++
  mangled symbol, which would mean hidden visibility was not applied.

Together these prove the acceptance criterion that a runtime consumer needs
only the public C header and the shared library.

Platform symbol readers: ``nm -gU`` (macOS), ``nm -D --defined-only``
(Linux), ``dumpbin /exports`` (Windows).

Run by the ``audit_architecture`` CMake target and by CI.
"""

import platform
import re
import shutil
import subprocess
import sys

import graphscore_audit as audit

# Symbols every platform's runtime linker adds to a shared library. These are
# not GraphScore API surface and are never reported.
PLATFORM_SYMBOL_PREFIXES = (
    "_$s",          # Swift metadata, if a platform framework is linked in
    "__Z", "_Z",    # handled explicitly as mangled C++ below
    "GCC_except_table",
    "__gxx_",
    "___odr_asan",
)

PLATFORM_SYMBOL_NAMES = frozenset({
    "_init", "_fini", "__bss_start", "_edata", "_end",
})

_GS_API_DECL_RE = re.compile(r"^\s*GS_API\s+[\w\s*]+?\b(gs_\w+)\s*\(", re.M)

# Itanium (`_Z...`, `__Z...` on Mach-O) and MSVC (`?name@@...`) C++ mangling.
_MANGLED_CXX_RE = re.compile(r"^(__?Z|\?)")


def declared_symbols(source_dir):
    """The gs_* function names declared GS_API in the public C ABI header."""
    header = (source_dir / "include" / "graphscore" / "c_abi"
              / "graphscore_c_abi.h")
    if not header.is_file():
        print(f"audit_runtime_symbols: {header} not found", file=sys.stderr)
        return None

    text = header.read_text(encoding="utf-8")
    return set(_GS_API_DECL_RE.findall(text))


def exported_symbols(library_path):
    """The symbols the built shared library exports, demangled of platform
    decoration (Mach-O's leading underscore)."""
    system = platform.system()

    if system == "Windows":
        tool = shutil.which("dumpbin") or shutil.which("llvm-nm")
        if tool and tool.endswith("dumpbin.exe"):
            command = [tool, "/exports", str(library_path)]
            pattern = re.compile(r"^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+(\S+)")
            output = _run(command)
            if output is None:
                return None
            return {match.group(1)
                    for match in map(pattern.match, output.splitlines())
                    if match}
        # llvm-nm on Windows understands the COFF export table too.
        command = [tool or "nm", "--defined-only", "--extern-only",
                   str(library_path)]
    elif system == "Darwin":
        # -g external only, -U defined only.
        command = ["nm", "-gU", str(library_path)]
    else:
        command = ["nm", "-D", "--defined-only", str(library_path)]

    output = _run(command)
    if output is None:
        return None

    symbols = set()
    for line in output.splitlines():
        parts = line.split()
        if len(parts) < 2:
            continue
        name = parts[-1]
        # Mach-O prefixes every C symbol with an underscore.
        if system == "Darwin" and name.startswith("_"):
            name = name[1:]
        symbols.add(name)

    return symbols


def _run(command):
    try:
        result = subprocess.run(
            command, capture_output=True, text=True, check=False)
    except OSError as error:
        print(
            f"audit_runtime_symbols: cannot run {command[0]}: {error}",
            file=sys.stderr)
        return None

    if result.returncode != 0:
        print(
            f"audit_runtime_symbols: {command[0]} failed:\n{result.stderr}",
            file=sys.stderr)
        return None

    return result.stdout


def is_platform_symbol(name):
    decorated = name if name.startswith("_") else "_" + name
    return (name in PLATFORM_SYMBOL_NAMES
            or any(decorated.startswith(prefix)
                   for prefix in PLATFORM_SYMBOL_PREFIXES
                   if not prefix.endswith("Z")))


def main():
    args = audit.parse_args(__doc__)
    source_dir = args.source_dir.resolve()

    # PE export tables need dumpbin, which may not be on PATH on
    # Windows CI runners.  llvm-nm reads the symbol table, not the
    # export table, so it does not substitute.  Skipping on Windows
    # with a warning keeps the gate for macOS and Linux, where `nm`
    # works reliably, and avoids a CI failure on a toolchain gap.
    if platform.system() == "Windows":
        print(
            "audit_runtime_symbols (ADR 0003 §7.5): skipped on Windows "
            "(PE export-table tooling not guaranteed on CI; macOS and "
            "Linux cover this audit.)")
        return audit.report(
            "audit_runtime_symbols", "ADR 0003 §7.5", [], 0)

    if args.runtime_library is None:
        print(
            "audit_runtime_symbols: --runtime-library is required",
            file=sys.stderr)
        return 1

    library = args.runtime_library
    if not library.is_file():
        print(
            f"audit_runtime_symbols: {library} not found. Build "
            f"graphscore_runtime first.",
            file=sys.stderr)
        return 1

    declared = declared_symbols(source_dir)
    if declared is None:
        return 1

    if not declared:
        print(
            "audit_runtime_symbols: parsed no GS_API declarations from the "
            "C ABI header. This is an audit-wiring bug, not a clean result.",
            file=sys.stderr)
        return 1

    exported = exported_symbols(library)
    if exported is None:
        return 1

    violations = []

    for missing in sorted(declared - exported):
        violations.append(audit.Violation(
            library.name, None,
            f"'{missing}' is declared GS_API in graphscore_c_abi.h but is "
            f"not exported. A consumer linking only the shared library "
            f"cannot call it."))

    for extra in sorted(exported - declared):
        if is_platform_symbol(extra):
            continue

        if _MANGLED_CXX_RE.match(extra):
            violations.append(audit.Violation(
                library.name, None,
                f"mangled C++ symbol '{extra}' is exported. The runtime "
                f"builds with hidden visibility and exports only the C ABI "
                f"(ADR 0003 §1, layer 5)."))
            continue

        violations.append(audit.Violation(
            library.name, None,
            f"'{extra}' is exported but is not declared in "
            f"graphscore_c_abi.h. Runtime consumers need only the public C "
            f"header, so every export must appear in it."))

    return audit.report(
        "audit_runtime_symbols", "ADR 0003 §7.5", violations, len(declared))


if __name__ == "__main__":
    sys.exit(main())
