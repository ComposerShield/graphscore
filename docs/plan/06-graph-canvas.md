# Milestone 06: Infinite Graph Canvas

## Goal

Build the large-scale graph authoring surface with notation-filled nodes and tidy, directly editable adaptive-music connections.

## Dependencies

- [ ] Milestone 05 notation rendering and semantic accessibility.
- [ ] Milestone 02 graph/command model.
- [ ] Milestone 01 writer shell.

## Deliverables

### Infinite viewport

- [ ] Sparse effectively unbounded coordinates with stable transforms and no persisted page edge.
- [ ] Native trackpad two-finger pan and pinch zoom, centered around the gesture focal point.
- [ ] Mouse wheel pan, modifier-wheel zoom, middle-button drag pan, and keyboard pan/zoom centered on accessibility focus.
- [ ] Spatial indexing, viewport clipping, and bounded invalidation for nodes, labels, controls, connector segments, and hit regions.
- [ ] Full notation remains conceptually present at every zoom; do not replace it with semantic summaries.
- [ ] Permit raster/detail simplification only when the exact glyph detail is below perceptible resolution.

### Notation nodes

- [ ] Render every active global track and staff in every node, aligned to the common measure timeline.
- [ ] Node header includes name, custom color, freeform-notes affordance, validation state, tempo-lane affordance, and dedicated play button.
- [ ] Node geometry follows measure/track content and exposes clear resize/layout rules rather than arbitrary clipping.
- [ ] Dragging nodes updates attached connector endpoint legs continuously.
- [ ] New nodes inherit track structure plus the selected/source node's exact tempo value and beat unit at the end of its main region, or project defaults when no source is selected.
- [ ] Track add/remove updates all nodes; removed-track music remains archived and recoverable.

### Connector creation and semantics

- [ ] Create any number of named input/output ports with stable identity and accessible labels.
- [ ] One output attaches to at most one destination input.
- [ ] Author sequential versus vertical type and show redundant color plus line-pattern distinction.
- [ ] Provide connector inspector fields for event binding, priority, random weight, and name, plus linked node/event-listener fields for queue policy/capacity and validation diagnostics.
- [ ] Reject loops only where a specific invariant requires it; ordinary graph cycles remain valid.

### Orthogonal route editing

- [ ] Automatically compute obstacle-avoiding straight/Manhattan routes around node bounds.
- [ ] Render all 90-degree turns with consistent rounded corners.
- [ ] Hovering a segment presents a bidirectional cursor perpendicular to its movement axis.
- [ ] Dragging any segment inserts/moves/removes bends as needed while preserving orthogonality and minimum endpoint clearance.
- [ ] Preserve valid customized interior segments when either endpoint node moves; repair only endpoint legs and invalid collisions.
- [ ] Primary+Shift+R returns a selected connector to automatic routing.
- [ ] Delete removes a selected connector through the undoable command path.
- [ ] Keep route geometry writer-only and out of cooked exports.

### Selection and playback affordances

- [ ] Normal single click selects ports, paths, segments, nodes, controls, or notation for editing.
- [ ] Double-clicking a connection requests its playback action without changing single-click editing behavior.
- [ ] Add a small destination-end action circle that invokes the same queue/jump action and has a large enough accessible hit target.
- [ ] Enable playback actions only for outputs whose source is the active node; other paths remain editable and expose an accessible unavailable reason.
- [ ] Play actions are initially routed through controller interfaces and become audible in Milestones 08/09.
- [ ] Node play controls are keyboard/screen-reader operable and clearly distinct from selecting/dragging a node.

### Organization operations

- [ ] Multi-select, move, duplicate, copy/paste, and delete nodes with deterministic connector remapping.
- [ ] Search/focus nodes by name and UUID.
- [ ] Undo/redo a complete connector drag as one transaction.
- [ ] Retain custom colors and freeform notes in project persistence.

## Acceptance Criteria

- [ ] Pan, zoom, node drag, selection, and connector segment drag meet the 60 fps target on the representative 1,000-node fixture.
- [ ] Offscreen work is culled and interaction cost scales with visible/spatially relevant content rather than total graph size.
- [ ] Default routes avoid nodes, custom routes remain orthogonal with rounded corners, and route reset is deterministic.
- [ ] Node moves preserve manually positioned interior segments where geometrically valid.
- [ ] All graph operations are undoable and survive save/reopen.
- [ ] Sequential and vertical connections remain distinguishable without relying on color alone.
- [ ] Keyboard and screen-reader users can create, inspect, connect, reroute, reset, and delete connections.

## Test Focus

- [ ] Transform precision and pointer-centered zoom across extreme coordinates.
- [ ] Spatial-index insertion/removal/query and viewport-culling tests.
- [ ] Orthogonal routing goldens, obstacle cases, segment dragging, corner cleanup, endpoint moves, and reset.
- [ ] Hit testing at route crossings, rounded corners, ports, and action circles.
- [ ] UUID/edge remapping during node duplication and paste.
- [ ] Performance benchmarks with 1,000 notation nodes and adversarial connector density.
- [ ] Accessibility tree virtualization and focus retention during pan/zoom.
