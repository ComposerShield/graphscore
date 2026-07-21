// SPDX-License-Identifier: Apache-2.0

#include <graphscore/domain/graphscore_domain.hpp>

namespace graphscore {
namespace {
constexpr int kDomainVersion = 1;
}  // namespace

int domain_version() {
  return kDomainVersion;
}
}  // namespace graphscore
