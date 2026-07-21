# SPDX-License-Identifier: Apache-2.0
"""Shared helpers for the Python architecture audits (ADR 0003 §7).

The four audit scripts alongside this module are invoked by the
``audit_architecture`` CMake target and, independently, by CI. They share
argument parsing, the target-to-directory mapping, and violation reporting so
that a contributor sees the same report shape from every audit.

This module is deliberately dependency-free: standard library only, so the
audits run on a bare CI runner with no pip install step.
"""

import argparse
import pathlib
import re
import sys

# ---------------------------------------------------------------------------
# The subset of the ADR 0003 contract the Python audits need.
#
# cmake/architecture_contract.cmake is the source of truth for the target
# graph; parsing CMake from Python to rediscover it would add a second,
# subtly different parser. What the Python audits need instead is the mapping
# from target to source directories and the third-party header/type
# vocabulary, neither of which the CMake contract carries.
# ---------------------------------------------------------------------------

CLEAN_TARGETS = (
    "core",
    "c_abi",
    "domain",
    "cooked_format",
    "compiler",
    "persistence",
    "scheduler",
    "loader",
    "runtime_impl",
    "runtime",
)

WRITER_TARGETS = (
    "notation",
    "rendering",
    "accessibility",
    "canvas",
    "writer_shell",
    "audio_device",
    "midi_io",
    "vst3_host",
    "writer_audio",
    "accessibility_platform",
    "plugin_scanner_client",
)

# ADR 0003 §3.3. Public headers, private headers, and sources of these
# targets may not reference any third-party windowing, audio, VST3,
# accessibility, GPU, shaping, font, or vector-rendering dependency.
PROTECTED_TARGETS = CLEAN_TARGETS

# ADR 0003 §7.3 step 3. Include patterns that mark a third-party dependency.
FORBIDDEN_INCLUDE_PATTERNS = (
    (re.compile(r"^SDL3?/"), "SDL3"),
    (re.compile(r"^SDL[_.]"), "SDL3"),
    (re.compile(r"^ft2build\.h$"), "FreeType"),
    (re.compile(r"^freetype/"), "FreeType"),
    (re.compile(r"^hb(-[\w-]+)?\.h$"), "HarfBuzz"),
    (re.compile(r"^harfbuzz/"), "HarfBuzz"),
    (re.compile(r"^thorvg"), "ThorVG"),
    (re.compile(r"^miniaudio\.h$"), "miniaudio"),
    (re.compile(r"^portaudio\.h$"), "PortAudio"),
    (re.compile(r"^pa_[\w]+\.h$"), "PortAudio"),
    (re.compile(r"^(rtmidi/)?RtMidi\.h$"), "RtMidi"),
    (re.compile(r"^(public\.sdk|pluginterfaces|base)/"), "VST3 SDK"),
    (re.compile(r"^vst3"), "VST3 SDK"),
    (re.compile(r"^accesskit"), "AccessKit"),
    (re.compile(r"^gtest/"), "GoogleTest"),
    (re.compile(r"^gmock/"), "GoogleTest"),
)

# ADR 0003 §7.7 step 2. Type-name patterns that mark third-party API surface.
FORBIDDEN_TYPE_PATTERNS = (
    (re.compile(r"\bSDL_[A-Z]\w*"), "SDL3"),
    (re.compile(r"\bFT_[A-Z]\w*"), "FreeType"),
    (re.compile(r"\bhb_\w+_t\b"), "HarfBuzz"),
    (re.compile(r"\btvg::"), "ThorVG"),
    (re.compile(r"\bma_[a-z]\w*\b"), "miniaudio"),
    (re.compile(r"\bPa(Stream|Error|DeviceIndex|SampleFormat)\w*"), "PortAudio"),
    (re.compile(r"\bRtMidi\w*"), "RtMidi"),
    (re.compile(r"\bSteinberg::"), "VST3 SDK"),
    (re.compile(r"\bIPluginFactory\b"), "VST3 SDK"),
    (re.compile(r"\baccesskit_\w+"), "AccessKit"),
)

SOURCE_SUFFIXES = (".c", ".h", ".cpp", ".hpp")

_INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]')

# A // or /* */ comment, or a string literal. Stripped before type scanning so
# that a comment saying "no SDL_Window here" is not itself a violation.
_COMMENT_OR_STRING_RE = re.compile(
    r'//[^\n]*|/\*.*?\*/|"(?:[^"\\\n]|\\.)*"', re.DOTALL)


class Violation:
    """One boundary violation, reported with the location that caused it."""

    def __init__(self, path, line, message):
        self.path = path
        self.line = line
        self.message = message

    def __str__(self):
        location = f"{self.path}:{self.line}" if self.line else str(self.path)
        return f"{location}: {self.message}"


def parse_args(description):
    """Parse the argument set every audit is invoked with."""
    parser = argparse.ArgumentParser(description=description)
    parser.add_argument(
        "--source-dir", required=True, type=pathlib.Path,
        help="Repository root")
    parser.add_argument(
        "--binary-dir", required=True, type=pathlib.Path,
        help="CMake build tree containing the generated target graph")
    parser.add_argument(
        "--runtime-library", type=pathlib.Path,
        help="Path to the built graphscore_runtime shared library")
    return parser.parse_args()


def public_headers(source_dir, target):
    """Public headers of `target` — include/graphscore/<target>/**."""
    root = source_dir / "include" / "graphscore" / target
    if not root.is_dir():
        return []
    return sorted(p for p in root.rglob("*") if p.suffix in SOURCE_SUFFIXES)


def private_sources(source_dir, target):
    """Implementation sources and private headers — src/<target>/**."""
    root = source_dir / "src" / target
    if not root.is_dir():
        return []
    return sorted(p for p in root.rglob("*") if p.suffix in SOURCE_SUFFIXES)


def read_text(path):
    """Read a source file, tolerating non-UTF-8 bytes in comments."""
    return path.read_text(encoding="utf-8", errors="replace")


def includes(text):
    """Yield (line_number, included_path) for every #include in `text`."""
    for number, line in enumerate(text.splitlines(), start=1):
        match = _INCLUDE_RE.match(line)
        if match:
            yield number, match.group(1)


def strip_comments_and_strings(text):
    """Blank out comments and string literals, preserving line numbering.

    Type-name scanning runs over the result so that prose mentioning a
    third-party type — including the boundary comments this project is full
    of — is never reported as API surface.
    """
    def blank(match):
        return re.sub(r"[^\n]", " ", match.group(0))

    return _COMMENT_OR_STRING_RE.sub(blank, text)


def relative(source_dir, path):
    try:
        return path.relative_to(source_dir)
    except ValueError:
        return path


def report(audit_name, adr_section, violations, checked_count):
    """Print the audit result and return the process exit code."""
    if violations:
        print(
            f"{audit_name} ({adr_section}): "
            f"{len(violations)} boundary violation(s)\n",
            file=sys.stderr)
        for violation in violations:
            print(f"  - {violation}", file=sys.stderr)
        print(
            "\nEvery boundary is declared in ADR 0003 "
            "(docs/decisions/0003-architecture-target-dag.md). Crossing one "
            "requires an ADR amendment first.",
            file=sys.stderr)
        return 1

    print(f"{audit_name} ({adr_section}): clean, {checked_count} checked")
    return 0
