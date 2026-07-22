// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/event_registry.hpp>

#include <string>
#include <utility>

namespace graphscore {

std::optional<EventId> EventRegistry::add_event(std::string name) {
  if (name_to_id_.contains(name))
    return std::nullopt;

  const EventId id = EventId::generate();
  name_to_id_.emplace(name, id);
  events_.emplace(id, EventDefinition{id, std::move(name)});
  return id;
}

Result EventRegistry::rename_event(EventId id, std::string new_name) {
  const auto it = events_.find(id);
  if (it == events_.end())
    return Result(ResultCode::kInvalidArgument);

  const auto collision = name_to_id_.find(new_name);
  if (collision != name_to_id_.end() && collision->second != id)
    return Result(ResultCode::kInvalidArgument);

  name_to_id_.erase(it->second.name);
  it->second.name                  = new_name;
  name_to_id_[std::move(new_name)] = id;
  return Result();
}

Result EventRegistry::remove_event(EventId id) {
  const auto it = events_.find(id);
  if (it == events_.end())
    return Result(ResultCode::kInvalidArgument);

  name_to_id_.erase(it->second.name);
  events_.erase(it);
  return Result();
}

const EventDefinition* EventRegistry::find_by_id(EventId id) const {
  const auto it = events_.find(id);
  return it == events_.end() ? nullptr : &it->second;
}

const EventDefinition* EventRegistry::find_by_name(
    const std::string& name) const {
  const auto it = name_to_id_.find(name);
  return it == name_to_id_.end() ? nullptr : find_by_id(it->second);
}

}  // namespace graphscore
