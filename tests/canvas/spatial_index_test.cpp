// SPDX-License-Identifier: Apache-2.0

#include <graphscore/canvas/graphscore_canvas.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace {

using graphscore::BoundedInvalidation;
using graphscore::CanvasItemId;
using graphscore::CanvasItemKind;
using graphscore::CanvasScene;
using graphscore::CanvasSceneItem;
using graphscore::SparseSpatialIndex;
using graphscore::ViewportTransform;
using graphscore::WorldBounds;
using graphscore::WorldRect;

constexpr std::array kItemKinds{
    CanvasItemKind::kNode, CanvasItemKind::kLabel, CanvasItemKind::kControl,
    CanvasItemKind::kConnectorSegment, CanvasItemKind::kHitRegion};

[[nodiscard]] CanvasSceneItem item(CanvasItemKind kind, std::uint64_t value,
                                   double x, double y, double width = 10.0,
                                   double height = 10.0) {
  return CanvasSceneItem{CanvasItemId{kind, value},
                         WorldBounds{{x, y}, width, height}};
}

[[nodiscard]] bool covers(const WorldRect& rect, const WorldBounds& bounds) {
  return rect.left <= bounds.origin.x && rect.top <= bounds.origin.y &&
         rect.right >= bounds.origin.x + bounds.width &&
         rect.bottom >= bounds.origin.y + bounds.height;
}

class SpatialItemKindTest : public testing::TestWithParam<CanvasItemKind> {};

TEST_P(SpatialItemKindTest, InsertsUpdatesRemovesAndViewportCulls) {
  SparseSpatialIndex index;
  const auto         original = item(GetParam(), 41, -20.0, 20.0);
  ASSERT_TRUE(index.insert(original));
  EXPECT_FALSE(index.insert(original));

  const auto initial = index.query(WorldBounds{{-10.0, 0.0}, 30.0, 30.0});
  ASSERT_TRUE(initial);
  ASSERT_EQ(initial->items.size(), 1U);
  EXPECT_EQ(initial->items.front(), original);

  const WorldBounds moved{{70.0, 80.0}, 20.0, 5.0};
  ASSERT_TRUE(index.update(original.id, moved));
  const auto old_area = index.query(WorldBounds{{-20.0, 20.0}, 10.0, 10.0});
  ASSERT_TRUE(old_area);
  EXPECT_TRUE(old_area->items.empty());

  ViewportTransform transform;
  const auto        visible = index.query_viewport(transform, 100.0, 100.0);
  ASSERT_TRUE(visible);
  ASSERT_EQ(visible->items.size(), 1U);
  EXPECT_EQ(visible->items.front().id, original.id);

  ASSERT_TRUE(index.remove(original.id));
  EXPECT_FALSE(index.remove(original.id));
  EXPECT_EQ(index.size(), 0U);
}

INSTANTIATE_TEST_SUITE_P(AllFiveSceneClasses, SpatialItemKindTest,
                         testing::ValuesIn(kItemKinds));

TEST(SparseSpatialIndexTest, IncludesTouchingEdgesAndCrossingSegments) {
  SparseSpatialIndex index;
  ASSERT_TRUE(
      index.insert(item(CanvasItemKind::kNode, 1, 0.0, 0.0, 10.0, 10.0)));
  ASSERT_TRUE(index.insert(
      item(CanvasItemKind::kConnectorSegment, 2, -5.0, 4.0, 20.0, 2.0)));
  ASSERT_TRUE(
      index.insert(item(CanvasItemKind::kHitRegion, 3, 10.0, 10.0, 0.0, 0.0)));

  const auto result = index.query(WorldBounds{{10.0, 5.0}, 0.0, 5.0});
  ASSERT_TRUE(result);
  ASSERT_EQ(result->items.size(), 3U);
  EXPECT_EQ(result->items[0].id, (CanvasItemId{CanvasItemKind::kNode, 1}));
  EXPECT_EQ(result->items[1].id,
            (CanvasItemId{CanvasItemKind::kConnectorSegment, 2}));
  EXPECT_EQ(result->items[2].id, (CanvasItemId{CanvasItemKind::kHitRegion, 3}));
}

TEST(SparseSpatialIndexTest, OrdersByStableCompositeIdentity) {
  SparseSpatialIndex index;
  ASSERT_TRUE(index.insert(item(CanvasItemKind::kNode, 9, 0.0, 0.0)));
  ASSERT_TRUE(index.insert(item(CanvasItemKind::kLabel, 9, 0.0, 0.0)));
  ASSERT_TRUE(index.insert(item(CanvasItemKind::kNode, 8, 0.0, 0.0)));
  ASSERT_TRUE(index.insert(item(CanvasItemKind::kNode, 3, 0.0, 0.0)));
  ASSERT_TRUE(index.insert(item(CanvasItemKind::kControl, 1, 0.0, 0.0)));

  const auto result = index.query(WorldBounds{{0.0, 0.0}, 10.0, 10.0});
  ASSERT_TRUE(result);
  ASSERT_EQ(result->items.size(), 5U);
  EXPECT_EQ(result->items[0].id, (CanvasItemId{CanvasItemKind::kNode, 3}));
  EXPECT_EQ(result->items[1].id, (CanvasItemId{CanvasItemKind::kNode, 8}));
  EXPECT_EQ(result->items[2].id, (CanvasItemId{CanvasItemKind::kNode, 9}));
  EXPECT_EQ(result->items[3].id, (CanvasItemId{CanvasItemKind::kLabel, 9}));
  EXPECT_EQ(result->items[4].id, (CanvasItemId{CanvasItemKind::kControl, 1}));
}

TEST(SparseSpatialIndexTest, RejectsInvalidGeometryWithoutPartialMutation) {
  SparseSpatialIndex index;
  const auto         original = item(CanvasItemKind::kNode, 1, 2.0, 3.0);
  ASSERT_TRUE(index.insert(original));
  const double infinity = std::numeric_limits<double>::infinity();

  EXPECT_FALSE(index.insert(item(CanvasItemKind::kLabel, 2, infinity, 0.0)));
  EXPECT_FALSE(index.update(original.id, WorldBounds{{2.0, 3.0}, -1.0, 10.0}));
  EXPECT_FALSE(index.query(WorldBounds{{0.0, 0.0}, infinity, 1.0}));
  EXPECT_EQ(index.size(), 1U);
  EXPECT_EQ(index.find(original.id), original);
}

TEST(SparseSpatialIndexTest, HandlesSparseNegativeAndExtremeFiniteCoordinates) {
  SparseSpatialIndex index;
  constexpr double   kLarge = 1.0e200;
  ASSERT_TRUE(
      index.insert(item(CanvasItemKind::kNode, 1, -1.0e12, -1.0e12, 5.0, 5.0)));
  ASSERT_TRUE(
      index.insert(item(CanvasItemKind::kLabel, 2, kLarge, -kLarge, 0.0, 0.0)));

  const auto negative = index.query(WorldBounds{{-1.0e12, -1.0e12}, 5.0, 5.0});
  ASSERT_TRUE(negative);
  ASSERT_EQ(negative->items.size(), 1U);
  EXPECT_EQ(negative->items.front().id.value, 1U);
  const auto extreme = index.query(WorldBounds{{kLarge, -kLarge}, 0.0, 0.0});
  ASSERT_TRUE(extreme);
  ASSERT_EQ(extreme->items.size(), 1U);
  EXPECT_EQ(extreme->items.front().id.value, 2U);
}

TEST(ViewportCullingTest, ExpansionAndEdgesAreInclusiveWithoutDeletingItems) {
  SparseSpatialIndex index;
  ASSERT_TRUE(
      index.insert(item(CanvasItemKind::kControl, 1, 100.0, 50.0, 0.0, 0.0)));
  ASSERT_TRUE(
      index.insert(item(CanvasItemKind::kHitRegion, 2, 105.0, 50.0, 0.0, 0.0)));
  ViewportTransform transform;

  const auto exact = index.query_viewport(transform, 100.0, 100.0);
  ASSERT_TRUE(exact);
  ASSERT_EQ(exact->items.size(), 1U);
  const auto expanded = index.query_viewport(transform, 100.0, 100.0, 5.0);
  ASSERT_TRUE(expanded);
  EXPECT_EQ(expanded->items.size(), 2U);
  EXPECT_EQ(index.size(), 2U);

  EXPECT_FALSE(index.query_viewport(
      transform, std::numeric_limits<double>::quiet_NaN(), 100.0));
  EXPECT_FALSE(index.query_viewport(transform, 100.0, 100.0, -1.0));
  EXPECT_EQ(index.size(), 2U);
}

TEST(SparseSpatialIndexTest, ThousandItemQueryTestsRelevantCandidatesOnly) {
  SparseSpatialIndex index;
  for (std::uint64_t value = 0; value < 1000U; ++value) {
    ASSERT_TRUE(index.insert(item(CanvasItemKind::kNode, value,
                                  static_cast<double>(value) * 1000.0, 0.0)));
  }

  const auto result = index.query(WorldBounds{{500000.0, 0.0}, 20.0, 20.0});
  ASSERT_TRUE(result);
  ASSERT_EQ(result->items.size(), 1U);
  EXPECT_EQ(result->items.front().id.value, 500U);
  EXPECT_LT(result->statistics.candidates_tested, 10U);
  EXPECT_LT(result->statistics.nodes_visited, 30U);
}

TEST(ViewportCullingTest, LowZoomWideViewportRejectsDistantClusterAtRoot) {
  SparseSpatialIndex index;
  for (std::uint64_t value = 0; value < 1000U; ++value) {
    ASSERT_TRUE(index.insert(item(CanvasItemKind::kNode, value,
                                  static_cast<double>(value) * 7000.0,
                                  7000000.0, 10.0, 10.0)));
  }
  ViewportTransform transform;
  ASSERT_TRUE(transform.zoom_to(0.0001, {0.0, 0.0}));

  const auto result = index.query_viewport(transform, 800.0, 600.0);
  ASSERT_TRUE(result);
  EXPECT_TRUE(result->items.empty());
  EXPECT_EQ(result->statistics.candidates_tested, 0U);
  EXPECT_EQ(result->statistics.nodes_visited, 1U);
}

TEST(SparseSpatialIndexTest,
     LongThinConnectorsRetainShortAxisDiscriminationUnderAdversarialInsertion) {
  SparseSpatialIndex index;
  for (std::uint64_t insertion = 0; insertion < 1000U; ++insertion) {
    const std::uint64_t position =
        insertion % 2U == 0U ? insertion / 2U : 999U - insertion / 2U;
    ASSERT_TRUE(index.insert(
        item(CanvasItemKind::kConnectorSegment, position, -1000000.0,
             static_cast<double>(position) * 10.0, 2000000.0, 1.0)));
  }

  const auto result =
      index.query(WorldBounds{{-500000.0, 5000.25}, 1000000.0, 0.5});
  ASSERT_TRUE(result);
  ASSERT_EQ(result->items.size(), 1U);
  EXPECT_EQ(result->items.front().id.value, 500U);
  EXPECT_LT(result->statistics.candidates_tested, 10U);
  EXPECT_LT(result->statistics.nodes_visited, 80U);
}

TEST(SparseSpatialIndexTest, ExtremeCoordinateSpansRemainSpatiallyBounded) {
  SparseSpatialIndex index;
  constexpr double   kExtreme = 1.0e300;
  ASSERT_TRUE(index.insert(item(CanvasItemKind::kConnectorSegment, 1, -kExtreme,
                                -20.0, kExtreme, 1.0)));
  ASSERT_TRUE(index.insert(
      item(CanvasItemKind::kConnectorSegment, 2, 0.0, 20.0, kExtreme, 1.0)));
  for (std::uint64_t value = 0; value < 1000U; ++value) {
    ASSERT_TRUE(
        index.insert(item(CanvasItemKind::kNode, value + 10U, kExtreme,
                          1000.0 + static_cast<double>(value), 0.0, 0.0)));
  }

  const auto result =
      index.query(WorldBounds{{-kExtreme, -20.0}, kExtreme, 0.5});
  ASSERT_TRUE(result);
  ASSERT_EQ(result->items.size(), 1U);
  EXPECT_EQ(result->items.front().id.value, 1U);
  EXPECT_EQ(result->statistics.candidates_tested, 1U);
  EXPECT_LT(result->statistics.nodes_visited, 80U);
}

class InvalidationKindTest : public testing::TestWithParam<CanvasItemKind> {};

TEST_P(InvalidationKindTest, AddRemoveAndUpdateCoverOldAndNewGeometry) {
  BoundedInvalidation dirty(8);
  const auto          old_item = item(GetParam(), 4, -10.0, 20.0, 4.0, 5.0);
  const auto          new_item = item(GetParam(), 4, 100.0, 200.0, 6.0, 7.0);
  ASSERT_TRUE(dirty.invalidate_add(old_item));
  ASSERT_TRUE(dirty.invalidate_remove(old_item));
  ASSERT_TRUE(dirty.invalidate_update(old_item, new_item));

  bool old_covered = false;
  bool new_covered = false;
  for (const WorldRect& region : dirty.regions()) {
    old_covered = old_covered || covers(region, old_item.bounds);
    new_covered = new_covered || covers(region, new_item.bounds);
  }
  EXPECT_TRUE(old_covered);
  EXPECT_TRUE(new_covered);
}

INSTANTIATE_TEST_SUITE_P(AllFiveSceneClasses, InvalidationKindTest,
                         testing::ValuesIn(kItemKinds));

TEST(BoundedInvalidationTest, RepeatedDisjointUpdatesRespectCapWithoutLoss) {
  BoundedInvalidation      dirty(3);
  std::vector<WorldBounds> expected;
  for (std::uint64_t value = 0; value < 20U; ++value) {
    const auto old_item =
        item(CanvasItemKind::kNode, 1, static_cast<double>(value) * 200.0,
             -static_cast<double>(value) * 100.0, 2.0, 3.0);
    const auto new_item = item(CanvasItemKind::kNode, 1,
                               static_cast<double>(value) * 200.0 + 100.0,
                               -static_cast<double>(value) * 100.0, 2.0, 3.0);
    expected.push_back(old_item.bounds);
    expected.push_back(new_item.bounds);
    ASSERT_TRUE(dirty.invalidate_update(old_item, new_item));
    ASSERT_LE(dirty.regions().size(), dirty.region_cap());
  }
  for (const WorldBounds& bounds : expected) {
    bool covered = false;
    for (const WorldRect& region : dirty.regions()) {
      covered = covered || covers(region, bounds);
    }
    EXPECT_TRUE(covered);
  }
  for (const WorldRect& region : dirty.regions()) {
    EXPECT_TRUE(std::isfinite(region.left));
    EXPECT_TRUE(std::isfinite(region.top));
    EXPECT_TRUE(std::isfinite(region.right));
    EXPECT_TRUE(std::isfinite(region.bottom));
  }
}

TEST(CanvasSceneTest, InvalidUpdateIsAtomicForSceneAndDirtyState) {
  CanvasScene scene(4);
  const auto  original = item(CanvasItemKind::kNode, 7, 1.0, 2.0);
  ASSERT_TRUE(scene.insert(original));
  scene.clear_invalidation();

  EXPECT_FALSE(scene.update(
      original.id,
      WorldBounds{{std::numeric_limits<double>::infinity(), 0.0}, 1.0, 1.0}));
  EXPECT_EQ(scene.index().find(original.id), original);
  EXPECT_TRUE(scene.invalidation().regions().empty());
}

}  // namespace
