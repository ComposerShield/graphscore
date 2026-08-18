// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <optional>
#include <utility>
#include <vector>

#include <graphscore/core/result.hpp>
#include <graphscore/domain/command.hpp>
#include <graphscore/domain/graph_position.hpp>
#include <graphscore/domain/node.hpp>
#include <graphscore/domain/selection.hpp>

namespace graphscore {

// Duplicates every node named by `selection` within the same Project,
// reversibly, never duplicating a stable UUID anywhere in the project.
//
// Identity: each duplicate gets a fresh NodeId, a fresh ConnectorId for
// every one of its input and output connectors, and a fresh
// NotationEntityId for every top-level VoiceEvent, embedded ChordNote and
// GraceNote, and marking (dynamics, hairpins, slurs, beam overrides, grace
// groups, pedal spans) inside every duplicated TrackLane. NodeTimeline
// carries no ids and is copied verbatim by value (its MeasureMap, ClefLanes,
// and TempoLane are untouched); a node with no timeline stays without one.
// Lane TrackId/StaveId keys are preserved exactly -- tracks and staves are
// project-owned and are never duplicated, only the TrackLane values.
//
// Edges: an edge whose source node and destination node are both in the
// selection is remapped to the corresponding duplicate endpoints, including
// a self-loop (a node's output targeting one of its own inputs targets the
// duplicate's own input, not the source's). An edge leaving the selection
// (source selected, destination not) is dropped: the duplicate output has
// no destination. An edge entering the selection from outside is simply not
// reproduced -- inputs carry no destination of their own. No non-selected
// node is mutated in any way. A remapped edge's route is always automatic,
// never a copy of the source's custom RouteGeometry: absolute waypoints are
// meaningless under a position offset, and the duplicate's output is
// freshly minted with an automatic route -- the source's RouteGeometry is
// deliberately never copied onto it.
//
// Event bindings are preserved, not remapped: EventId is project-scoped, so
// a duplicated output bound to event E stays bound to the same E, with the
// duplicate's EventListener policy and capacity matching the source's.
//
// A duplicate never becomes the project start node; Project::start_node is
// unchanged by execute, undo, and redo. `name`, `color`, and `notes` are
// copied verbatim (no synthesized suffix -- this is the domain layer);
// `position` is the source position plus `offset` (default zero).
//
// The selection must validate diagnostic-free via validate_selection
// (selection.hpp) for the NodeSet arm; a selection naming a NodeId the
// project does not own fails kInvalidArgument, mutating nothing.
//
// Reversibility: ids are minted exactly once, on the first successful
// execute, and stored in `created_` -- a full Node snapshot per duplicate,
// in selection order. undo removes every node named in `created_`; redo
// re-inserts them, id-for-id identical to the first execute. Publishing
// (inserting or removing more than one Node from the Project) is not a
// single non-throwing operation the way a TrackLane assignment is, so a
// partial failure part-way through is rolled back to that direction's own
// target end state: a failed publish (execute or redo) removes every node
// the attempt itself inserted, since publishing never mutates a node
// outside the batch and the target state is none of them present; a failed
// unpublish (undo) instead removes every node in the batch that is still
// present -- including one that was never itself removed but was left
// cascade-damaged when Project::remove_node cleared another duplicate's
// destination (and reset that destination's route) on its way out -- and
// then restores every node in the batch verbatim from its own snapshot,
// since the target state is all of them present exactly as captured. If
// that rollback itself cannot complete, the command becomes permanently
// State::kFaulted rather than risk leaving the project in a state this
// command cannot account for.
class DuplicateNodesCommand : public Command {
 public:
  explicit DuplicateNodesCommand(NodeSet selection, GraphPosition offset = {})
      : selection_(std::move(selection)), offset_(offset) {}

  // Clipboard form: duplicates immutable node snapshots rather than resolving
  // live source ids. This lets paste remain valid after the copied nodes are
  // edited or deleted. Snapshots must be non-empty and have distinct NodeIds.
  explicit DuplicateNodesCommand(std::vector<Node> snapshots,
                                 GraphPosition     offset = {})
      : snapshots_(std::move(snapshots)), offset_(offset) {}

  Result execute(Project& project) noexcept override;
  Result undo(Project& project) noexcept override;
  Result redo(Project& project) noexcept override;

  [[nodiscard]] std::vector<NodeId> created_node_ids() const;

 private:
  std::optional<NodeSet> selection_;
  std::vector<Node>      snapshots_;
  GraphPosition          offset_;
  std::vector<Node>      created_;
  State                  state_ = State::kFresh;
};

}  // namespace graphscore
