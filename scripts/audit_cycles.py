#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""No-cycles audit (ADR 0003 §7.6).

CMake already rejects cyclic ``target_link_libraries`` at configure time, so
this script exists as an independent cross-check rather than as the primary
defence: it derives the dependency graph from ``cmake --graphviz`` output —
CMake's own rendering of the generated buildsystem — instead of from
cmake/architecture_contract.cmake or the audit's target-graph dump. A bug in
either of those cannot hide a cycle from this audit.

It also enforces the layer ordering that CMake has no opinion about: a target
in layer N may not depend on a target in a layer above N, except for the
same-layer edges ADR 0003 §2.1 permits explicitly.

Run by the ``audit_architecture`` CMake target and by CI.
"""

import re
import subprocess
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

_NODE_RE = re.compile(r'^\s*"(node\d+)"\s*\[\s*label\s*=\s*"([^"]+)"')
_EDGE_RE = re.compile(r'^\s*"(node\d+)"\s*->\s*"(node\d+)"')


def generate_graphviz(source_dir, binary_dir):
    """Ask CMake to render the generated buildsystem as a Graphviz graph.

    Passes only the existing binary directory — no ``-S`` / ``-B`` pair — so
    CMake reads the already-resolved CMakeCache.txt and never regenerates the
    build system. On Windows, this avoids a Ninja ``failed recompaction:
    Permission denied`` error that a full ``cmake -S ... -B ...`` would
    trigger while the build is running (or shortly after it finishes).
    """
    dot_path = binary_dir / "graphscore_dependencies.dot"
    result = subprocess.run(
        ["cmake", f"--graphviz={dot_path}", str(binary_dir)],
        capture_output=True, text=True, check=False)

    if result.returncode != 0:
        print(
            "audit_cycles: `cmake --graphviz` failed:\n" + result.stderr,
            file=sys.stderr)
        return None

    if not dot_path.exists():
        print(
            f"audit_cycles: CMake reported success but wrote no graph at "
            f"{dot_path}.",
            file=sys.stderr)
        return None

    return dot_path


def parse_graphviz(dot_path):
    """Parse the Graphviz output into (id_graph, id_to_label).

    The graph is keyed by Graphviz **node id**, not by target label. CMake
    versions differ in whether a target can appear as more than one node —
    for instance once in the global graph and again inside a per-directory
    cluster. Collapsing nodes by label merges those duplicates, and an edge
    between two nodes that share a label then becomes an apparent
    self-dependency: a cycle that does not exist. Keeping ids distinct
    reports exactly the cycles CMake actually generated, and labels are
    resolved only when a violation is described.
    """
    id_to_label = {}
    id_graph = {}

    for line in dot_path.read_text(encoding="utf-8").splitlines():
        node_match = _NODE_RE.match(line)
        if node_match:
            node_id, label = node_match.group(1), node_match.group(2)
            id_to_label[node_id] = label
            id_graph.setdefault(node_id, set())
            continue

        edge_match = _EDGE_RE.match(line)
        if edge_match:
            source_id, target_id = edge_match.group(1), edge_match.group(2)
            if source_id == target_id:
                continue  # self-edge is a CMake version artefact, not a cycle
            id_graph.setdefault(source_id, set()).add(target_id)
            id_graph.setdefault(target_id, set())

    return id_graph, id_to_label


def label_graph(id_graph, id_to_label):
    """Collapse the id graph to {label: {label, ...}} for layer checking.

    Layer ordering is a property of targets, not of Graphviz nodes, so
    duplicate labels are merged here deliberately. Self-edges introduced by
    that merge are dropped: they are an artefact of the collapse, and cycle
    detection has already run on the id graph where they cannot occur.
    """
    graph = {}
    for source_id, target_ids in id_graph.items():
        source = id_to_label.get(source_id)
        if source is None:
            continue
        targets = graph.setdefault(source, set())
        for target_id in target_ids:
            target = id_to_label.get(target_id)
            if target is not None and target != source:
                targets.add(target)
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

    dot_path = generate_graphviz(source_dir, binary_dir)
    if dot_path is None:
        return 1

    id_graph, id_to_label = parse_graphviz(dot_path)
    graph = label_graph(id_graph, id_to_label)
    owned = {name for name in graph if name in LAYERS}

    if not owned:
        print(
            "audit_cycles found no GraphScore targets in the Graphviz "
            "output. This is an audit-wiring bug, not a clean result.",
            file=sys.stderr)
        return 1

    violations = []

    cycle = find_cycle(id_graph)
    if cycle:
        # Report labels, but keep the node ids alongside them: a cycle whose
        # labels repeat is the signature of the duplicate-node case, and
        # without the ids that report is impossible to act on.
        described = " -> ".join(
            f"{id_to_label.get(node, '?')} [{node}]" for node in cycle)
        violations.append(audit.Violation(
            "cmake/architecture_contract.cmake", None,
            "dependency cycle: " + described))

    check_layers(graph, violations)

    return audit.report(
        "audit_cycles", "ADR 0003 §7.6", violations, len(owned))


if __name__ == "__main__":
    sys.exit(main())
