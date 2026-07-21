#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""No-cycles audit (ADR 0003 §7.6).

Derives the dependency graph from the configure-time target-graph dump that
cmake/ArchitectureAudit.cmake writes, rather than from ``cmake --graphviz``
output.  The dump is always up to date (it is written at configure time) and
never touches the Ninja build files, so it avoids the ``ninja: failed
recompaction: Permission denied`` error that ``cmake --graphviz`` can hit on
Windows while the build is running.

Enforces layer ordering: a target in layer N may not depend on a target in a
layer above N, except for the same-layer edges ADR 0003 §2.1 permits
explicitly.

Run by the ``audit_architecture`` CMake target and by CI.
"""

import re
import sys

import graphscore_audit as audit

# ADR 0003 §1. Layer 0 is the bottom of the graph.
LAYERS = {
    "graphscore_core": 0,
    "graphscore_c_abi": 0,
    "graphscore_domain": 1,
    "graphscore_cooked_format": 2,
    "graphscore_compiler": 2,
    "graphscore_persistence": 3,
    "graphscore_scheduler": 4,
    "graphscore_loader": 5,
    "graphscore_runtime_impl": 5,
    "graphscore_runtime": 5,
    "graphscore_notation": 6,
    "graphscore_rendering": 7,
    "graphscore_accessibility": 8,
    "graphscore_canvas": 8,
    "graphscore_writer_shell": 9,
    "graphscore_audio_device": 9,
    "graphscore_midi_io": 9,
    "graphscore_vst3_host": 9,
    "graphscore_writer_audio": 9,
    "graphscore_accessibility_platform": 9,
    "graphscore_plugin_scanner_client": 10,
    "graphscore_writer_app": 10,
    "graphscore_plugin_scanner": 11,
}

# ADR 0003 §2.1, "Same-layer permitted" column.
PERMITTED_SAME_LAYER_EDGES = {
    ("graphscore_compiler", "graphscore_cooked_format"),
    ("graphscore_runtime_impl", "graphscore_loader"),
    ("graphscore_runtime", "graphscore_runtime_impl"),
    ("graphscore_canvas", "graphscore_accessibility"),
    ("graphscore_vst3_host", "graphscore_audio_device"),
    ("graphscore_writer_audio", "graphscore_audio_device"),
    ("graphscore_writer_audio", "graphscore_vst3_host"),
    ("graphscore_writer_app", "graphscore_plugin_scanner_client"),
}

# Synthesised CMake targets that are not real libraries but appear as
# link-time requirements in the target-graph dump. They have no sources,
# no output, and no layer, so they are excluded from cycle and layer
# checking.
_IGNORE_GRAPHSCORE_TARGETS = frozenset({
    "graphscore_warnings",
})

_SET_FN_RE = re.compile(
    r'^\s*set\s*\(\s*GRAPHSCORE_GRAPH_(?:LINK|INTERFACE)_(\S+)\s+"([^"]*)"')


def parse_target_graph(target_graph_path):
    """Read the configure-time target-graph dump and return {label: {dep, ...}}.

    ``cmake/ArchitectureAudit.cmake`` writes ``graphscore_target_graph.cmake``
    at configure time.  This script reads it directly instead of invoking
    ``cmake --graphviz``, which on Windows would re-run the Ninja generator
    and collide with its file lock (``failed recompaction: Permission
    denied``).
    """
    graph = {}
    text = target_graph_path.read_text(encoding="utf-8")

    for line in text.splitlines():
        match = _SET_FN_RE.match(line)
        if not match:
            continue
        target = match.group(1)
        if target in _IGNORE_GRAPHSCORE_TARGETS:
            continue
        graph.setdefault(target, set())
        for dep in match.group(2).split(";"):
            dep = dep.strip()
            if not dep or dep == target:
                continue
            if dep in _IGNORE_GRAPHSCORE_TARGETS or dep.startswith("$<"):
                continue
            graph[target].add(dep)
            graph.setdefault(dep, set())

    return graph


def find_cycle(graph):
    """Return one cycle as a list of target names, or None if acyclic."""
    WHITE, GREY, BLACK = 0, 1, 2
    colour = {node: WHITE for node in graph}

    def visit(node, stack):
        colour[node] = GREY
        stack.append(node)
        for neighbour in sorted(graph.get(node, ())):
            if neighbour not in colour:
                continue
            if colour[neighbour] == GREY:
                # Back edge: the cycle is the stack from `neighbour` on.
                return stack[stack.index(neighbour):] + [neighbour]
            if colour[neighbour] == WHITE:
                cycle = visit(neighbour, stack)
                if cycle:
                    return cycle
        stack.pop()
        colour[node] = BLACK
        return None

    for node in sorted(graph):
        if colour[node] == WHITE:
            cycle = visit(node, [])
            if cycle:
                return cycle

    return None


def check_layers(graph, violations):
    """Reject any edge that points from a lower layer to a higher one."""
    for source, dependencies in sorted(graph.items()):
        source_layer = LAYERS.get(source)
        if source_layer is None:
            continue

        for dependency in sorted(dependencies):
            dependency_layer = LAYERS.get(dependency)
            if dependency_layer is None:
                continue

            if dependency_layer < source_layer:
                continue

            if dependency_layer == source_layer:
                if (source, dependency) in PERMITTED_SAME_LAYER_EDGES:
                    continue
                violations.append(audit.Violation(
                    "cmake/architecture_contract.cmake", None,
                    f"{source} -> {dependency} is a same-layer edge (layer "
                    f"{source_layer}) that ADR 0003 §2.1 does not permit."))
                continue

            violations.append(audit.Violation(
                "cmake/architecture_contract.cmake", None,
                f"{source} (layer {source_layer}) depends on {dependency} "
                f"(layer {dependency_layer}). A target may only depend on "
                f"lower layers (ADR 0003 §1)."))


def main():
    args = audit.parse_args(__doc__)
    source_dir = args.source_dir.resolve()
    binary_dir = args.binary_dir.resolve()

    target_graph_path = binary_dir / "graphscore_target_graph.cmake"
    if not target_graph_path.exists():
        print(
            f"audit_cycles: target-graph dump not found at "
            f"{target_graph_path} (reconfigure to generate it).",
            file=sys.stderr)
        return 1

    graph = parse_target_graph(target_graph_path)
    owned = {name for name in graph if name in LAYERS}

    if not owned:
        print(
            "audit_cycles found no GraphScore targets in the target-graph "
            "dump. This is an audit-wiring bug, not a clean result.",
            file=sys.stderr)
        return 1

    violations = []

    cycle = find_cycle(graph)
    if cycle:
        violations.append(audit.Violation(
            "cmake/architecture_contract.cmake", None,
            "dependency cycle: " + " -> ".join(cycle)))

    check_layers(graph, violations)

    return audit.report(
        "audit_cycles", "ADR 0003 §7.6", violations, len(owned))


if __name__ == "__main__":
    sys.exit(main())
