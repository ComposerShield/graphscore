// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace graphscore {

// A node's writer-only 2D position on the graph canvas. Unlike musical
// positions, this carries no structural meaning and no exact-arithmetic
// requirement, so a plain double pair is sufficient; it never participates
// in Rational-based comparisons.
struct GraphPosition {
  double x = 0.0;
  double y = 0.0;

  [[nodiscard]] bool operator==(const GraphPosition&) const = default;
};

}  // namespace graphscore
