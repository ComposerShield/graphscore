# Milestone 06: Infinite Graph Canvas

## Goal

Build the large-scale graph authoring surface with notation-filled nodes and tidy, directly editable adaptive-music connections.

## Dependencies

- [ ] M6-phase-1 Milestone 05 notation rendering and semantic accessibility.
- [ ] M6-phase-2 Milestone 02 graph/command model.
- [ ] M6-phase-3 Milestone 01 writer shell.

## Deliverables

### Infinite viewport

- [x] M6-phase-4 Sparse effectively unbounded coordinates with stable transforms and no persisted page edge.
- [x] M6-phase-5 Native trackpad two-finger pan and pinch zoom, centered around the gesture focal point.
- [x] M6-phase-6 Mouse wheel pan, modifier-wheel zoom, middle-button drag pan, and keyboard pan/zoom centered on accessibility focus.
- [ ] M6-phase-7 Spatial indexing, viewport clipping, and bounded invalidation for nodes, labels, controls, connector segments, and hit regions.
- [ ] M6-phase-8 Full notation remains conceptually present at every zoom; do not replace it with semantic summaries.
- [ ] M6-phase-9 Permit raster/detail simplification only when the exact glyph detail is below perceptible resolution.

### Notation nodes

- [ ] M6-phase-10 Render every active global track and staff in every node, aligned to the common measure timeline.
- [ ] M6-phase-11 Node header includes name, custom color, freeform-notes affordance, validation state, tempo-lane affordance, and dedicated play button.
- [ ] M6-phase-12 Node geometry follows measure/track content and exposes clear resize/layout rules rather than arbitrary clipping.
- [ ] M6-phase-13 Dragging nodes updates attached connector endpoint legs continuously.
- [ ] M6-phase-14 New nodes inherit track structure plus the selected/source node's exact tempo value and beat unit at the end of its main region, or project defaults when no source is selected.
- [ ] M6-phase-15 Track add/remove updates all nodes; removed-track music remains archived and recoverable.

### Connector creation and semantics

- [ ] M6-phase-16 Create any number of named input/output ports with stable identity and accessible labels.
- [ ] M6-phase-17 One output attaches to at most one destination input.
- [ ] M6-phase-18 Author sequential versus vertical type and show redundant color plus line-pattern distinction.
- [ ] M6-phase-19 Provide connector inspector fields for event binding, priority, random weight, and name, plus linked node/event-listener fields for queue policy/capacity and validation diagnostics.
- [ ] M6-phase-20 Reject loops only where a specific invariant requires it; ordinary graph cycles remain valid.

### Orthogonal route editing

- [ ] M6-phase-21 Automatically compute obstacle-avoiding straight/Manhattan routes around node bounds.
- [ ] M6-phase-22 Render all 90-degree turns with consistent rounded corners.
- [ ] M6-phase-23 Hovering a segment presents a bidirectional cursor perpendicular to its movement axis.
- [ ] M6-phase-24 Dragging any segment inserts/moves/removes bends as needed while preserving orthogonality and minimum endpoint clearance.
- [ ] M6-phase-25 Preserve valid customized interior segments when either endpoint node moves; repair only endpoint legs and invalid collisions.
- [ ] M6-phase-26 Primary+Shift+R returns a selected connector to automatic routing.
- [ ] M6-phase-27 Delete removes a selected connector through the undoable command path.
- [ ] M6-phase-28 Keep route geometry writer-only and out of cooked exports.

### Selection and playback affordances

- [ ] M6-phase-29 Normal single click selects ports, paths, segments, nodes, controls, or notation for editing.
- [ ] M6-phase-30 Double-clicking a connection requests its playback action without changing single-click editing behavior.
- [ ] M6-phase-31 Add a small destination-end action circle that invokes the same queue/jump action and has a large enough accessible hit target.
- [ ] M6-phase-32 Enable playback actions only for outputs whose source is the active node; other paths remain editable and expose an accessible unavailable reason.
- [ ] M6-phase-33 Play actions are initially routed through controller interfaces and become audible in Milestones 08/09.
- [ ] M6-phase-34 Node play controls are keyboard/screen-reader operable and clearly distinct from selecting/dragging a node.

### Organization operations

- [ ] M6-phase-35 Multi-select, move, duplicate, copy/paste, and delete nodes with deterministic connector remapping.
- [ ] M6-phase-36 Search/focus nodes by name and UUID.
- [ ] M6-phase-37 Undo/redo a complete connector drag as one transaction.
- [ ] M6-phase-38 Retain custom colors and freeform notes in project persistence.

## Acceptance Criteria

- [ ] M6-phase-39 Pan, zoom, node drag, selection, and connector segment drag meet the 60 fps target on the representative 1,000-node fixture.
- [ ] M6-phase-40 Offscreen work is culled and interaction cost scales with visible/spatially relevant content rather than total graph size.
- [ ] M6-phase-41 Default routes avoid nodes, custom routes remain orthogonal with rounded corners, and route reset is deterministic.
- [ ] M6-phase-42 Node moves preserve manually positioned interior segments where geometrically valid.
- [ ] M6-phase-43 All graph operations are undoable and survive save/reopen.
- [ ] M6-phase-44 Sequential and vertical connections remain distinguishable without relying on color alone.
- [ ] M6-phase-45 Keyboard and screen-reader users can create, inspect, connect, reroute, reset, and delete connections.

## Test Focus

- [ ] M6-phase-46 Transform precision and pointer-centered zoom across extreme coordinates.
- [ ] M6-phase-47 Spatial-index insertion/removal/query and viewport-culling tests.
- [ ] M6-phase-48 Orthogonal routing goldens, obstacle cases, segment dragging, corner cleanup, endpoint moves, and reset.
- [ ] M6-phase-49 Hit testing at route crossings, rounded corners, ports, and action circles.
- [ ] M6-phase-50 UUID/edge remapping during node duplication and paste.
- [ ] M6-phase-51 Performance benchmarks with 1,000 notation nodes and adversarial connector density.
- [ ] M6-phase-52 Accessibility tree virtualization and focus retention during pan/zoom.
