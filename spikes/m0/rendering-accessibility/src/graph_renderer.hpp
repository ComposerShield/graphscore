// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "types.hpp"
#include "render_list.hpp"
#include "font_manager.hpp"

namespace spike {

struct GraphNode {
    Rect bounds;     // in world coords
    Color fillColor;
    std::string label;
    float cornerRadius = 10.0f;
};

struct GraphConnector {
    Vec2 start;      // world coords
    Vec2 end;
    std::vector<Vec2> bends;
    Color color;
    float lineWidth = 3.0f;
    float cornerRadius = 8.0f;
};

class GraphRenderer {
public:
    explicit GraphRenderer(FontManager* fm) : m_fontManager(fm) {}

    void RenderNode(RenderList& rl, const GraphNode& node,
                     const Transform& xform);

    void RenderConnector(RenderList& rl, const GraphConnector& conn,
                          const Transform& xform);

    // Return a representative set of nodes and connectors
    static std::vector<GraphNode> DemoNodes();
    static std::vector<GraphConnector> DemoConnectors();

private:
    FontManager* m_fontManager;
};

} // namespace spike
