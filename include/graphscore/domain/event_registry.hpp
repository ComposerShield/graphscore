// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <graphscore/core/graphscore_core.hpp>

namespace graphscore {

// One registered event definition: a stable identity plus a unique,
// case-sensitive UTF-8 name. The event *state machine* (occurrence storage,
// arrival sequencing, first/latest/FIFO arbitration) is a later phase; this
// is only the registry of event definitions it will operate over.
struct EventDefinition {
  EventId     id;
  std::string name;

  [[nodiscard]] bool operator==(const EventDefinition&) const = default;
};

// Registry of a project's registered events, keyed by stable EventId with
// a unique, case-sensitive UTF-8 name enforced at the API.
class EventRegistry {
 public:
  // Fails if `name` is already registered. Comparison is case-sensitive, so
  // "Attack" and "attack" are distinct, independently valid names.
  [[nodiscard]] std::optional<EventId> add_event(std::string name);

  // Reversible-undo substrate mirroring add_event, but with a caller-
  // supplied id rather than a freshly minted one: RegisterEventCommand's
  // redo and RemoveEventCommand's undo (command headers) call this to
  // restore a snapshot taken before the event was removed, preserving its
  // original EventId rather than minting a fresh one. Fails, leaving the
  // registry unchanged, if `id` is already registered or if `name` is
  // already registered under a different id.
  [[nodiscard]] Result add_event_with_id(EventId id, std::string name);

  // Fails if `id` is unknown, or if `new_name` collides with a different
  // event's name.
  [[nodiscard]] Result rename_event(EventId id, std::string new_name);

  // Raw removal: does not unbind outputs or prune listeners referencing
  // `id`. Prefer Graph::remove_event (graph.hpp), which cascades first.
  [[nodiscard]] Result remove_event(EventId id);

  [[nodiscard]] const EventDefinition* find_by_id(EventId id) const;

  [[nodiscard]] const EventDefinition* find_by_name(
      const std::string& name) const;

  [[nodiscard]] std::size_t size() const noexcept { return events_.size(); }

  // Returns every definition in stable UUID order. Validation and
  // persistence need complete registry traversal without exposing either
  // internal hash table for mutation.
  [[nodiscard]] std::vector<EventDefinition> definitions() const;

 private:
  std::unordered_map<EventId, EventDefinition> events_;
  std::unordered_map<std::string, EventId>     name_to_id_;
};

}  // namespace graphscore
