// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <functional>
#include <string>

#include <graphscore/core/uuid.hpp>

namespace graphscore {

// Phantom-tagged wrapper around Uuid. Two StrongId instantiations with
// different Tag types are unrelated at compile time, so a TrackId can never
// be passed where a StaveId is expected, even though both wrap a Uuid.
template <typename Tag>
class StrongId {
 public:
  constexpr StrongId() = default;

  constexpr explicit StrongId(const Uuid& value) noexcept : value_(value) {}

  [[nodiscard]] constexpr const Uuid& value() const noexcept { return value_; }

  [[nodiscard]] std::string to_string() const { return value_.to_string(); }

  [[nodiscard]] bool operator==(const StrongId&) const = default;

  static StrongId generate() { return StrongId(Uuid::generate()); }

 private:
  Uuid value_;
};

namespace detail {
struct ProjectIdTag {};

struct TrackIdTag {};

struct StaveIdTag {};

struct NodeIdTag {};

struct ConnectorIdTag {};

struct EventIdTag {};

struct NotationEntityIdTag {};
}  // namespace detail

using ProjectId        = StrongId<detail::ProjectIdTag>;
using TrackId          = StrongId<detail::TrackIdTag>;
using StaveId          = StrongId<detail::StaveIdTag>;
using NodeId           = StrongId<detail::NodeIdTag>;
using ConnectorId      = StrongId<detail::ConnectorIdTag>;
using EventId          = StrongId<detail::EventIdTag>;
using NotationEntityId = StrongId<detail::NotationEntityIdTag>;

}  // namespace graphscore

namespace std {

template <typename Tag>
struct hash<graphscore::StrongId<Tag>> {
  [[nodiscard]] std::size_t operator()(
      const graphscore::StrongId<Tag>& id) const noexcept {
    std::size_t seed = 0;
    for (const auto byte : id.value().bytes()) {
      seed = seed * 131 + byte;
    }
    return seed;
  }
};

}  // namespace std
