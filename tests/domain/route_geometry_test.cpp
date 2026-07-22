// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>

#include <limits>
#include <optional>
#include <vector>

#include <graphscore/domain/graphscore_domain.hpp>

using graphscore::RouteGeometry;
using graphscore::RoutePoint;

TEST(RouteGeometryTest, DefaultsToAutomaticWithNoWaypoints) {
  RouteGeometry route;
  EXPECT_TRUE(route.is_automatic());
  EXPECT_TRUE(route.waypoints().empty());
}

TEST(RouteGeometryTest, SetCustomRouteAcceptsOrthogonalWaypoints) {
  RouteGeometry                 route;
  const std::vector<RoutePoint> waypoints = {
      {0.0, 0.0}, {10.0, 0.0}, {10.0, 5.0}, {20.0, 5.0}};

  EXPECT_TRUE(route.set_custom_route(waypoints).ok());
  EXPECT_FALSE(route.is_automatic());
  EXPECT_EQ(route.waypoints(), waypoints);
}

TEST(RouteGeometryTest, SetCustomRouteRejectsNonOrthogonalSegment) {
  RouteGeometry                 route;
  const std::vector<RoutePoint> waypoints = {
      {0.0, 0.0}, {10.0, 5.0}};  // Diagonal: shares neither x nor y.

  EXPECT_FALSE(route.set_custom_route(waypoints).ok());
  EXPECT_TRUE(route.is_automatic());
  EXPECT_TRUE(route.waypoints().empty());
}

TEST(RouteGeometryTest, SetCustomRouteRejectsZeroLengthSegment) {
  RouteGeometry                 route;
  const std::vector<RoutePoint> waypoints = {
      {0.0, 0.0}, {0.0, 0.0}, {5.0, 0.0}};  // Identical consecutive points.

  EXPECT_FALSE(route.set_custom_route(waypoints).ok());
  EXPECT_TRUE(route.is_automatic());
  EXPECT_TRUE(route.waypoints().empty());
}

TEST(RouteGeometryTest, SetCustomRouteRejectsNonFiniteCoordinate) {
  const double nan = std::numeric_limits<double>::quiet_NaN();

  RouteGeometry                 route;
  const std::vector<RoutePoint> waypoints = {{nan, 0.0}, {5.0, 0.0}};

  EXPECT_FALSE(route.set_custom_route(waypoints).ok());
  EXPECT_TRUE(route.is_automatic());
  EXPECT_TRUE(route.waypoints().empty());
}

TEST(RouteGeometryTest, EmptyOrSinglePointRouteIsTriviallyOrthogonal) {
  RouteGeometry route;
  EXPECT_TRUE(route.set_custom_route({}).ok());
  EXPECT_FALSE(route.is_automatic());

  RouteGeometry single;
  EXPECT_TRUE(single.set_custom_route({{1.0, 2.0}}).ok());
  EXPECT_FALSE(single.is_automatic());
}

TEST(RouteGeometryTest, ResetToAutomaticClearsCustomization) {
  RouteGeometry route;
  ASSERT_TRUE(route.set_custom_route({{0.0, 0.0}, {0.0, 5.0}}).ok());
  ASSERT_FALSE(route.is_automatic());

  route.reset_to_automatic();
  EXPECT_TRUE(route.is_automatic());
  EXPECT_TRUE(route.waypoints().empty());
}

TEST(RouteGeometryTest, ClearingOutputDestinationResetsItsRouteToAutomatic) {
  graphscore::OutputConnector output("Out");
  ASSERT_TRUE(output.route().set_custom_route({{0.0, 0.0}, {0.0, 5.0}}).ok());
  ASSERT_FALSE(output.route().is_automatic());

  output.set_destination(graphscore::ConnectorDestination{
      graphscore::NodeId::generate(), graphscore::ConnectorId::generate()});
  EXPECT_FALSE(output.route().is_automatic());

  output.set_destination(std::nullopt);
  EXPECT_TRUE(output.route().is_automatic());
}
