// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include <graphscore/core/graphscore_core.hpp>
#include <graphscore/domain/route_geometry.hpp>

namespace graphscore {

// A stable, named entry point on a node. Inputs carry no transition
// semantics of their own -- the transition type, event trigger, priority,
// and weight all live on the OutputConnector whose destination targets
// this input (see OutputConnector below).
class InputConnector {
 public:
  explicit InputConnector(std::string name);

  [[nodiscard]] ConnectorId id() const noexcept { return id_; }

  [[nodiscard]] const std::string& name() const noexcept { return name_; }

  void set_name(std::string name) { name_ = std::move(name); }

  [[nodiscard]] bool operator==(const InputConnector&) const = default;

 private:
  ConnectorId id_;
  std::string name_;
};

// Where an OutputConnector's single destination edge lands: a specific
// input connector on a specific node. Graph::connect (graph.hpp) validates
// that both exist before an OutputConnector ever holds one.
struct ConnectorDestination {
  NodeId      node;
  ConnectorId connector;

  [[nodiscard]] bool operator==(const ConnectorDestination&) const = default;
};

// Sequential transitions are taken at a node boundary among the current
// node's eligible sequential outputs; vertical transitions jump
// sample-locally the instant their bound event occurs (product decisions,
// docs/plan/README.md). Which OutputConnector fields below are meaningful
// depends on this type.
enum class ConnectorType : std::uint8_t {
  kSequential = 0,
  kVertical,
};

// A stable, named exit point on a node with at most one destination edge
// while editing (see destination()), plus the fields that will govern how
// playback selects among a node's eligible outputs. This is configuration
// storage only: the weighted-random selection algorithm and the
// cross-event arbitration/tie-breaking that actually consume these fields
// are later phases (see the TODOs below).
//
// event_binding() is the type-appropriate trigger: required in practice
// for ConnectorType::kVertical (there is no other way a vertical output
// becomes eligible) and optional for kSequential (an event-gated
// sequential output only becomes eligible once its event has a matching
// persisted occurrence; an unbound sequential output is always a
// candidate at the node boundary). priority() matters only for kVertical
// -- TODO(Phase 5b): cross-event arbitration/tie-breaking among
// simultaneously eligible vertical outputs. weight() matters only for
// kSequential -- TODO(Phase 6): weighted-random selection among eligible
// sequential outputs, 100%-total groups, and zero-weight ineligibility.
// Both fields stay stored regardless of the connector's current type, so
// retyping a connector never discards data entered under its previous
// type.
class OutputConnector {
 public:
  explicit OutputConnector(std::string   name,
                           ConnectorType type = ConnectorType::kSequential);

  [[nodiscard]] ConnectorId id() const noexcept { return id_; }

  [[nodiscard]] const std::string& name() const noexcept { return name_; }

  void set_name(std::string name) { name_ = std::move(name); }

  [[nodiscard]] ConnectorType type() const noexcept { return type_; }

  [[nodiscard]] std::optional<EventId> event_binding() const noexcept {
    return event_binding_;
  }

  [[nodiscard]] int priority() const noexcept { return priority_; }

  void set_priority(int priority) noexcept { priority_ = priority; }

  [[nodiscard]] double weight() const noexcept { return weight_; }

  void set_weight(double weight) noexcept { weight_ = weight; }

  // Whether this output must have exactly one destination to count toward
  // Graph::is_export_ready(). An output not yet enabled for export may be
  // left with zero destinations indefinitely while editing (product
  // decision: "Zero or one destination edge per output while editing;
  // enabled exportable outputs require exactly one").
  [[nodiscard]] bool export_enabled() const noexcept { return export_enabled_; }

  void set_export_enabled(bool enabled) noexcept { export_enabled_ = enabled; }

  [[nodiscard]] const std::optional<ConnectorDestination>& destination()
      const noexcept {
    return destination_;
  }

  // Raw mutator: performs no existence check on the destination node/
  // connector. Prefer Graph::connect/disconnect (graph.hpp), which
  // validate the target before calling this. Clearing the destination
  // (nullopt) also resets the route to automatic, since a customized
  // interior route is meaningless without an edge to route.
  void set_destination(
      std::optional<ConnectorDestination> destination) noexcept {
    destination_ = std::move(destination);
    if (!destination_.has_value())
      route_.reset_to_automatic();
  }

  // Unconditionally mutable, including on an output with no destination:
  // set_custom_route does not require destination().has_value(), so a
  // customized route can be recorded before an edge exists and is then
  // silently adopted by a later connect(). This asymmetry is accepted
  // (LOW, Phase 5a review); a future Graph/Node-level route mutator could
  // require a destination first if it becomes a real authoring hazard.
  [[nodiscard]] RouteGeometry& route() noexcept { return route_; }

  [[nodiscard]] const RouteGeometry& route() const noexcept { return route_; }

  [[nodiscard]] bool operator==(const OutputConnector&) const = default;

 private:
  friend class Node;

  // Raw mutator: performs no vertical/sequential clash check and does not
  // update the owning Node's EventListener::bound_type() for this
  // connector's bound event. Prefer Node::set_output_type (node.hpp),
  // which enforces both.
  void set_type(ConnectorType type) noexcept { type_ = type; }

  // Raw mutator: performs no registry validation and no vertical/
  // sequential clash check. Prefer Graph::bind_output_event (graph.hpp) or
  // Node::bind_output_event (node.hpp), which enforce both.
  void set_event_binding(std::optional<EventId> event) noexcept {
    event_binding_ = event;
  }

  ConnectorId                         id_;
  std::string                         name_;
  ConnectorType                       type_;
  std::optional<EventId>              event_binding_;
  int                                 priority_       = 0;
  double                              weight_         = 1.0;
  bool                                export_enabled_ = true;
  std::optional<ConnectorDestination> destination_;
  RouteGeometry                       route_;
};

}  // namespace graphscore
