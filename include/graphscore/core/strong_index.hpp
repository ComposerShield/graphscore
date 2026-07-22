// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <compare>
#include <cstdint>

namespace graphscore {

// Phantom-tagged, position-based index into a project-ordered collection.
// An index reflects presentation/iteration order and is regenerated freely;
// it is a distinct type from the corresponding StrongId, which is the
// stable, persistent identity of the same entity. The two are never
// interconvertible.
template <typename Tag>
class StrongIndex {
 public:
  constexpr StrongIndex() = default;

  constexpr explicit StrongIndex(std::uint32_t value) noexcept
      : value_(value) {}

  [[nodiscard]] constexpr std::uint32_t value() const noexcept {
    return value_;
  }

  [[nodiscard]] constexpr auto operator<=>(const StrongIndex&) const = default;

 private:
  std::uint32_t value_ = 0;
};

namespace detail {
struct TrackIndexTag {};

struct StaveIndexTag {};

struct NodeIndexTag {};

struct ConnectorIndexTag {};

struct EventIndexTag {};

struct NotationEntityIndexTag {};
}  // namespace detail

using TrackIndex          = StrongIndex<detail::TrackIndexTag>;
using StaveIndex          = StrongIndex<detail::StaveIndexTag>;
using NodeIndex           = StrongIndex<detail::NodeIndexTag>;
using ConnectorIndex      = StrongIndex<detail::ConnectorIndexTag>;
using EventIndex          = StrongIndex<detail::EventIndexTag>;
using NotationEntityIndex = StrongIndex<detail::NotationEntityIndexTag>;

}  // namespace graphscore
