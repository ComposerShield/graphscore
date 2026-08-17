// SPDX-License-Identifier: Apache-2.0

#include <graphscore/canvas/graphscore_canvas.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace graphscore {
namespace {

constexpr int kNullNode = -1;

[[nodiscard]] bool valid_kind(CanvasItemKind kind) noexcept {
  switch (kind) {
    case CanvasItemKind::kNode:
    case CanvasItemKind::kLabel:
    case CanvasItemKind::kControl:
    case CanvasItemKind::kConnectorSegment:
    case CanvasItemKind::kHitRegion:
      return true;
  }
  return false;
}

[[nodiscard]] std::optional<WorldRect> to_rect(
    const WorldBounds& bounds) noexcept {
  if (!is_valid_world_bounds(bounds)) {
    return std::nullopt;
  }
  return WorldRect{bounds.origin.x, bounds.origin.y,
                   bounds.origin.x + bounds.width,
                   bounds.origin.y + bounds.height};
}

[[nodiscard]] bool intersects(const WorldRect& first,
                              const WorldRect& second) noexcept {
  return first.left <= second.right && second.left <= first.right &&
         first.top <= second.bottom && second.top <= first.bottom;
}

[[nodiscard]] WorldRect unite(const WorldRect& first,
                              const WorldRect& second) noexcept {
  return WorldRect{std::min(first.left, second.left),
                   std::min(first.top, second.top),
                   std::max(first.right, second.right),
                   std::max(first.bottom, second.bottom)};
}

[[nodiscard]] double normalized_perimeter(const WorldRect& rect,
                                          double           scale) noexcept {
  return (rect.right / scale - rect.left / scale) +
         (rect.bottom / scale - rect.top / scale);
}

[[nodiscard]] double comparison_scale(const WorldRect& first,
                                      const WorldRect& second) noexcept {
  return std::max({1.0, std::abs(first.left), std::abs(first.top),
                   std::abs(first.right), std::abs(first.bottom),
                   std::abs(second.left), std::abs(second.top),
                   std::abs(second.right), std::abs(second.bottom)});
}

}  // namespace

bool is_valid_world_bounds(const WorldBounds& bounds) noexcept {
  if (!std::isfinite(bounds.origin.x) || !std::isfinite(bounds.origin.y) ||
      !std::isfinite(bounds.width) || !std::isfinite(bounds.height) ||
      bounds.width < 0.0 || bounds.height < 0.0) {
    return false;
  }
  const double right  = bounds.origin.x + bounds.width;
  const double bottom = bounds.origin.y + bounds.height;
  return std::isfinite(right) && std::isfinite(bottom) &&
         (bounds.width == 0.0 || right != bounds.origin.x) &&
         (bounds.height == 0.0 || bottom != bounds.origin.y);
}

std::optional<WorldBounds> viewport_world_bounds(
    const ViewportTransform& transform, double viewport_width,
    double viewport_height, double expansion) noexcept {
  if (!std::isfinite(viewport_width) || !std::isfinite(viewport_height) ||
      !std::isfinite(expansion) || viewport_width < 0.0 ||
      viewport_height < 0.0 || expansion < 0.0) {
    return std::nullopt;
  }
  const double right  = viewport_width + expansion;
  const double bottom = viewport_height + expansion;
  if (!std::isfinite(right) || !std::isfinite(bottom)) {
    return std::nullopt;
  }
  const auto top_left =
      transform.to_world(ViewportPosition{-expansion, -expansion});
  const auto bottom_right = transform.to_world(ViewportPosition{right, bottom});
  if (!top_left || !bottom_right) {
    return std::nullopt;
  }
  const WorldBounds result{*top_left, bottom_right->x - top_left->x,
                           bottom_right->y - top_left->y};
  if (!is_valid_world_bounds(result)) {
    return std::nullopt;
  }
  return result;
}

class SparseSpatialIndex::Impl {
 public:
  struct Node {
    WorldRect                   bounds;
    std::optional<CanvasItemId> item_id;
    int                         parent = kNullNode;
    int                         left   = kNullNode;
    int                         right  = kNullNode;
    int                         height = 0;
    int                         next   = kNullNode;

    [[nodiscard]] bool is_leaf() const noexcept { return left == kNullNode; }
  };

  struct Record {
    CanvasSceneItem item;
    int             leaf = kNullNode;
  };

  std::map<CanvasItemId, Record> records;
  std::vector<Node>              nodes;
  int                            root      = kNullNode;
  int                            free_list = kNullNode;

  [[nodiscard]] int allocate_node() {
    if (free_list != kNullNode) {
      const int result = free_list;
      free_list        = nodes[static_cast<std::size_t>(result)].next;
      nodes[static_cast<std::size_t>(result)] = Node{};
      return result;
    }
    nodes.emplace_back();
    return static_cast<int>(nodes.size() - 1U);
  }

  void release_node(int index) noexcept {
    nodes[static_cast<std::size_t>(index)]      = Node{};
    nodes[static_cast<std::size_t>(index)].next = free_list;
    free_list                                   = index;
  }

  [[nodiscard]] double insertion_cost(int              index,
                                      const WorldRect& leaf_bounds) const {
    const WorldRect& bounds = nodes[static_cast<std::size_t>(index)].bounds;
    const double     scale  = comparison_scale(bounds, leaf_bounds);
    return normalized_perimeter(unite(bounds, leaf_bounds), scale) -
           normalized_perimeter(bounds, scale);
  }

  [[nodiscard]] int choose_sibling(const WorldRect& leaf_bounds) const {
    int index = root;
    while (!nodes[static_cast<std::size_t>(index)].is_leaf()) {
      const Node&  node       = nodes[static_cast<std::size_t>(index)];
      const double left_cost  = insertion_cost(node.left, leaf_bounds);
      const double right_cost = insertion_cost(node.right, leaf_bounds);
      if (left_cost != right_cost) {
        index = left_cost < right_cost ? node.left : node.right;
        continue;
      }
      const Node& left  = nodes[static_cast<std::size_t>(node.left)];
      const Node& right = nodes[static_cast<std::size_t>(node.right)];
      if (left.height != right.height) {
        index = left.height < right.height ? node.left : node.right;
      } else {
        index = std::min(node.left, node.right);
      }
    }
    return index;
  }

  void update_branch(int index) noexcept {
    Node&       node  = nodes[static_cast<std::size_t>(index)];
    const Node& left  = nodes[static_cast<std::size_t>(node.left)];
    const Node& right = nodes[static_cast<std::size_t>(node.right)];
    node.bounds       = unite(left.bounds, right.bounds);
    node.height       = 1 + std::max(left.height, right.height);
  }

  [[nodiscard]] int balance(int index) noexcept {
    Node& node = nodes[static_cast<std::size_t>(index)];
    if (node.is_leaf() || node.height < 2) {
      return index;
    }
    const int left_index  = node.left;
    const int right_index = node.right;
    Node&     left        = nodes[static_cast<std::size_t>(left_index)];
    Node&     right       = nodes[static_cast<std::size_t>(right_index)];
    const int difference  = right.height - left.height;
    if (difference > 1) {
      const int old_parent = node.parent;
      const int low        = right.left;
      const int high       = right.right;
      right.left           = index;
      right.parent         = old_parent;
      node.parent          = right_index;
      if (old_parent == kNullNode) {
        root = right_index;
      } else {
        Node& parent = nodes[static_cast<std::size_t>(old_parent)];
        (parent.left == index ? parent.left : parent.right) = right_index;
      }
      if (nodes[static_cast<std::size_t>(low)].height >
          nodes[static_cast<std::size_t>(high)].height) {
        right.right = low;
        node.right  = high;
      } else {
        right.right = high;
        node.right  = low;
      }
      nodes[static_cast<std::size_t>(node.right)].parent = index;
      update_branch(index);
      update_branch(right_index);
      return right_index;
    }
    if (difference < -1) {
      const int old_parent = node.parent;
      const int low        = left.left;
      const int high       = left.right;
      left.left            = index;
      left.parent          = old_parent;
      node.parent          = left_index;
      if (old_parent == kNullNode) {
        root = left_index;
      } else {
        Node& parent = nodes[static_cast<std::size_t>(old_parent)];
        (parent.left == index ? parent.left : parent.right) = left_index;
      }
      if (nodes[static_cast<std::size_t>(low)].height >
          nodes[static_cast<std::size_t>(high)].height) {
        left.right = low;
        node.left  = high;
      } else {
        left.right = high;
        node.left  = low;
      }
      nodes[static_cast<std::size_t>(node.left)].parent = index;
      update_branch(index);
      update_branch(left_index);
      return left_index;
    }
    return index;
  }

  void refit_from(int index) noexcept {
    while (index != kNullNode) {
      index = balance(index);
      update_branch(index);
      index = nodes[static_cast<std::size_t>(index)].parent;
    }
  }

  void insert_leaf(int leaf) {
    if (root == kNullNode) {
      root = leaf;
      return;
    }
    const int sibling =
        choose_sibling(nodes[static_cast<std::size_t>(leaf)].bounds);
    const int old_parent  = nodes[static_cast<std::size_t>(sibling)].parent;
    const int parent      = allocate_node();
    Node&     parent_node = nodes[static_cast<std::size_t>(parent)];
    parent_node.parent    = old_parent;
    parent_node.left      = sibling;
    parent_node.right     = leaf;
    parent_node.height    = nodes[static_cast<std::size_t>(sibling)].height + 1;
    parent_node.bounds = unite(nodes[static_cast<std::size_t>(sibling)].bounds,
                               nodes[static_cast<std::size_t>(leaf)].bounds);
    nodes[static_cast<std::size_t>(sibling)].parent = parent;
    nodes[static_cast<std::size_t>(leaf)].parent    = parent;
    if (old_parent == kNullNode) {
      root = parent;
    } else {
      Node& old_parent_node = nodes[static_cast<std::size_t>(old_parent)];
      (old_parent_node.left == sibling ? old_parent_node.left
                                       : old_parent_node.right) = parent;
      refit_from(old_parent);
    }
  }

  void detach_leaf(int leaf) noexcept {
    if (leaf == root) {
      root = kNullNode;
      return;
    }
    const int parent      = nodes[static_cast<std::size_t>(leaf)].parent;
    const int grandparent = nodes[static_cast<std::size_t>(parent)].parent;
    const int sibling     = nodes[static_cast<std::size_t>(parent)].left == leaf
                                ? nodes[static_cast<std::size_t>(parent)].right
                                : nodes[static_cast<std::size_t>(parent)].left;
    if (grandparent == kNullNode) {
      root                                            = sibling;
      nodes[static_cast<std::size_t>(sibling)].parent = kNullNode;
    } else {
      Node& grandparent_node = nodes[static_cast<std::size_t>(grandparent)];
      (grandparent_node.left == parent ? grandparent_node.left
                                       : grandparent_node.right) = sibling;
      nodes[static_cast<std::size_t>(sibling)].parent            = grandparent;
    }
    release_node(parent);
    nodes[static_cast<std::size_t>(leaf)].parent = kNullNode;
    if (grandparent != kNullNode) {
      refit_from(grandparent);
    }
  }
};

SparseSpatialIndex::SparseSpatialIndex() : impl_(std::make_unique<Impl>()) {}

SparseSpatialIndex::~SparseSpatialIndex()                             = default;
SparseSpatialIndex::SparseSpatialIndex(SparseSpatialIndex&&) noexcept = default;
SparseSpatialIndex& SparseSpatialIndex::operator=(
    SparseSpatialIndex&&) noexcept = default;

bool SparseSpatialIndex::insert(CanvasSceneItem item) {
  if (!valid_kind(item.id.kind) || !is_valid_world_bounds(item.bounds) ||
      impl_->records.contains(item.id)) {
    return false;
  }
  const int leaf = impl_->allocate_node();
  auto&     node = impl_->nodes[static_cast<std::size_t>(leaf)];
  node.bounds    = *to_rect(item.bounds);
  node.item_id   = item.id;
  impl_->insert_leaf(leaf);
  impl_->records.emplace(item.id, Impl::Record{item, leaf});
  return true;
}

bool SparseSpatialIndex::update(CanvasItemId id, WorldBounds bounds) {
  const auto found = impl_->records.find(id);
  if (found == impl_->records.end() || !is_valid_world_bounds(bounds)) {
    return false;
  }
  const int leaf = found->second.leaf;
  impl_->detach_leaf(leaf);
  impl_->nodes[static_cast<std::size_t>(leaf)].bounds = *to_rect(bounds);
  impl_->insert_leaf(leaf);
  found->second.item.bounds = bounds;
  return true;
}

bool SparseSpatialIndex::remove(CanvasItemId id) {
  const auto found = impl_->records.find(id);
  if (found == impl_->records.end()) {
    return false;
  }
  const int leaf = found->second.leaf;
  impl_->detach_leaf(leaf);
  impl_->release_node(leaf);
  impl_->records.erase(found);
  return true;
}

bool SparseSpatialIndex::contains(CanvasItemId id) const {
  return impl_->records.contains(id);
}

std::optional<CanvasSceneItem> SparseSpatialIndex::find(CanvasItemId id) const {
  const auto found = impl_->records.find(id);
  if (found == impl_->records.end()) {
    return std::nullopt;
  }
  return found->second.item;
}

std::size_t SparseSpatialIndex::size() const noexcept {
  return impl_->records.size();
}

std::optional<SpatialQueryResult> SparseSpatialIndex::query(
    WorldBounds bounds) const {
  const auto query_rect = to_rect(bounds);
  if (!query_rect) {
    return std::nullopt;
  }
  SpatialQueryResult result;
  std::vector<int>   pending;
  if (impl_->root != kNullNode) {
    pending.push_back(impl_->root);
  }
  while (!pending.empty()) {
    const int index = pending.back();
    pending.pop_back();
    const Impl::Node& node = impl_->nodes[static_cast<std::size_t>(index)];
    ++result.statistics.nodes_visited;
    if (node.is_leaf()) {
      ++result.statistics.candidates_tested;
      if (intersects(*query_rect, node.bounds)) {
        result.items.push_back(impl_->records.at(*node.item_id).item);
      }
      continue;
    }
    if (!intersects(*query_rect, node.bounds)) {
      continue;
    }
    pending.push_back(node.left);
    pending.push_back(node.right);
  }
  std::sort(result.items.begin(), result.items.end(),
            [](const CanvasSceneItem& first, const CanvasSceneItem& second) {
              return first.id < second.id;
            });
  return result;
}

std::optional<SpatialQueryResult> SparseSpatialIndex::query_viewport(
    const ViewportTransform& transform, double viewport_width,
    double viewport_height, double expansion) const {
  const auto bounds = viewport_world_bounds(transform, viewport_width,
                                            viewport_height, expansion);
  if (!bounds) {
    return std::nullopt;
  }
  return query(*bounds);
}

BoundedInvalidation::BoundedInvalidation(std::size_t region_cap)
    : region_cap_(std::max<std::size_t>(1U, region_cap)) {}

bool BoundedInvalidation::invalidate_add(const CanvasSceneItem& item) {
  return valid_kind(item.id.kind) && add_bounds(item.bounds);
}

bool BoundedInvalidation::invalidate_remove(const CanvasSceneItem& item) {
  return valid_kind(item.id.kind) && add_bounds(item.bounds);
}

bool BoundedInvalidation::invalidate_update(const CanvasSceneItem& old_item,
                                            const CanvasSceneItem& new_item) {
  if (old_item.id != new_item.id || !valid_kind(old_item.id.kind) ||
      !is_valid_world_bounds(old_item.bounds) ||
      !is_valid_world_bounds(new_item.bounds)) {
    return false;
  }
  const auto previous = regions_;
  if (!add_bounds(old_item.bounds) || !add_bounds(new_item.bounds)) {
    regions_ = previous;
    return false;
  }
  return true;
}

void BoundedInvalidation::clear() noexcept {
  regions_.clear();
}

const std::vector<WorldRect>& BoundedInvalidation::regions() const noexcept {
  return regions_;
}

std::size_t BoundedInvalidation::region_cap() const noexcept {
  return region_cap_;
}

bool BoundedInvalidation::add_bounds(const WorldBounds& bounds) {
  const auto rect = to_rect(bounds);
  if (!rect) {
    return false;
  }
  WorldRect merged = *rect;
  bool      merged_any;
  do {
    merged_any = false;
    for (auto existing = regions_.begin(); existing != regions_.end();) {
      if (intersects(merged, *existing)) {
        merged     = unite(merged, *existing);
        existing   = regions_.erase(existing);
        merged_any = true;
      } else {
        ++existing;
      }
    }
  } while (merged_any);
  regions_.push_back(merged);
  if (regions_.size() > region_cap_) {
    WorldRect collapsed = regions_.front();
    for (std::size_t index = 1; index < regions_.size(); ++index) {
      collapsed = unite(collapsed, regions_[index]);
    }
    regions_.assign(1U, collapsed);
  }
  std::sort(regions_.begin(), regions_.end(),
            [](const WorldRect& first, const WorldRect& second) {
              if (first.left != second.left) {
                return first.left < second.left;
              }
              if (first.top != second.top) {
                return first.top < second.top;
              }
              if (first.right != second.right) {
                return first.right < second.right;
              }
              return first.bottom < second.bottom;
            });
  return true;
}

CanvasScene::CanvasScene(std::size_t dirty_region_cap)
    : invalidation_(dirty_region_cap) {}

bool CanvasScene::insert(CanvasSceneItem item) {
  if (!index_.insert(item)) {
    return false;
  }
  return invalidation_.invalidate_add(item);
}

bool CanvasScene::update(CanvasItemId id, WorldBounds bounds) {
  const auto old_item = index_.find(id);
  if (!old_item || !is_valid_world_bounds(bounds)) {
    return false;
  }
  const CanvasSceneItem new_item{id, bounds};
  if (!index_.update(id, bounds)) {
    return false;
  }
  return invalidation_.invalidate_update(*old_item, new_item);
}

bool CanvasScene::remove(CanvasItemId id) {
  const auto item = index_.find(id);
  if (!item || !index_.remove(id)) {
    return false;
  }
  return invalidation_.invalidate_remove(*item);
}

const SparseSpatialIndex& CanvasScene::index() const noexcept {
  return index_;
}

const BoundedInvalidation& CanvasScene::invalidation() const noexcept {
  return invalidation_;
}

void CanvasScene::clear_invalidation() noexcept {
  invalidation_.clear();
}

}  // namespace graphscore
