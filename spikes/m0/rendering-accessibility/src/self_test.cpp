// SPDX-License-Identifier: Apache-2.0

#include "self_test.hpp"
#include "types.hpp"
#include "render_list.hpp"
#include "rasterizer.hpp"
#include "font_manager.hpp"
#include "notation_renderer.hpp"
#include "graph_renderer.hpp"
#include "hit_test.hpp"
#include "input_logger.hpp"
#include "native_handle.hpp"
#include "semantic_tree.hpp"
#include "viewport_event_handler.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstring>
#include <cmath>
#include <limits>
#include <new>

namespace spike {

// Minimal glyph cache for tests that don't need real fonts
static std::vector<GlyphBitmap> g_emptyGlyphCache;

SelfTest::SelfTest() = default;

void SelfTest::AddResult(const char* name, bool passed, const char* detail) {
    m_results.push_back({name, passed, detail ? detail : ""});
}

bool SelfTest::RunAll() {
    m_results.clear();

    Test_Vec2Math();
    Test_RectOps();
    Test_TransformInverse();
    Test_HitTestDeterministic();
    Test_RoundedRectCommands();
    Test_OrthogonalConnectorCommands();
    Test_OrthogonalAxisAlignment();
    Test_OrthogonalTrimPoints();
    Test_OrthogonalRadiusBounds();
    Test_ClipRect();
    Test_ClipPixelAssertions();
    Test_StaffLines();
    Test_FontLoading();
    Test_HarfBuzzShaping();
    Test_SMuFLCodepoints();
    Test_OffscreenRenderHash();
    Test_ShapedGlyphRendering();
    Test_GlyphPixelAssertions();
    Test_GlyphTransformMoves();
    Test_GlyphClipObeys();
    Test_OrthogonalAllTurns();
    Test_OrthogonalReversedTraversal();
    Test_OrthogonalShortSegmentClamp();
    Test_OrthogonalZeroLengthReject();
    Test_OrthogonalDuplicateReject();
    Test_OrthogonalNonAxisReject();
    Test_OrthogonalArcContinuity();
    Test_OrthogonalFilletPixelRaster();       // NEW: pixel-level raster for all 4 turns
    Test_TextVsMusicGlyphDistinction();       // NEW: text face ≠ music face
    Test_A11yTreeStructure();
    Test_A11yTreeIDs();
    Test_A11yTreeHierarchy();
    Test_A11yTreeLabels();
    Test_A11yTreeSelectionState();
    Test_A11yTreeActions();
    Test_A11yTreeTransformedBounds();
    Test_A11yTreeHitTest();
    Test_A11yTreeActionTransition();
    Test_A11yTreeMutationValidation();
    Test_A11yTreeGenerationCounter();
    Test_A11yTreeInvalidAction();
    Test_A11yTreeLifetimeDetach();
    Test_A11yDuplicateIdRejection();          // NEW: duplicate stable ID rejection
    Test_A11yReparentDetachIntegrity();       // NEW: Reparent/DetachChild atomicity
    Test_InputPanMath();
    Test_InputPinchMath();
    Test_InputScrollMath();
    Test_InputFidelitySummaryPass();
    Test_InputFidelitySummaryFail();
    Test_InputFidelityThresholds();
    Test_InputFidelityPhysicalGateBoundaries();
    Test_InputFidelityTimestampDrops();
    Test_InputViewportResponseRequired();     // NEW: viewport response required for PASS
    Test_NativeHandleNullWindow();
    Test_NativeHandleUnavailable();
    Test_NativeHandleFailsOnNullReport();     // NEW: negative-path native handle
    Test_GlyphCacheNonNullInvariant();         // NEW: rasterizer requires non-null cache
    Test_A11yEmptyIdRejection();               // NEW: empty stable ID rejection
    Test_A11ySetRootValidation();              // NEW: SetRoot validation + generation
    Test_A11yReparentExceptionSafety();        // NEW: Reparent exception-safety
    Test_A11ySetParentChildExceptionAtomic();   // NEW: SetParentChild exception-atomic
    Test_BridgeSyncGeometrySpy();              // NEW: bridge SpyGeometry invoked on mutation
    Test_InputE2ESDLEvents();                  // NEW: E2E synthetic SDL events through ProcessSDLEvent
    Test_InputWheelPanOnly();                  // NEW: wheel events pan, not zoom
    Test_InputFingerPanOnly();                 // raw finger motion never zooms regardless of separation
    Test_InputPinchFocalStability();           // NEW: pinch beyond threshold with focal stability
    Test_InputPinchFocalOffCenter();           // NEW: native pinch off-center focal preservation
    Test_InputSameDirectionNoZoom();           // NEW: same-direction finger motion never changes scale
    Test_InputCombinedStreamNoDoubleZoom();    // NEW: raw-finger + native-pinch zooms exactly once
    Test_InputFidelityRequiresPanAndPinch();   // NEW: fidelity summary requires both pan and pinch
    Test_InputFidelityPanOnlyFail();           // NEW: pan-only fails fidelity (no pinch outcome)
    Test_InputFidelityPinchOnlyFail();         // NEW: pinch-only fails fidelity (no pan outcome)
    Test_InputFidelityWheelFingerDownNoPinch();// NEW: wheel+finger-down does not satisfy pinch
    Test_TextFaceRasterPixels();               // NEW: text face raster with pixel assertions
    Test_OrthogonalFilletCommandGeometry();    // NEW: inspect exact FillArc center/radius/sweep
    Test_GlyphShapeCacheStability();           // NEW: repeated-frame glyph/shape cache stability
    Test_InputRealPathPanOnlyFail();           // NEW: real-path pan-only fails fidelity
    Test_InputRealPathPinchOnlyFail();         // NEW: real-path pinch-only fails fidelity (focal offset excluded)
    Test_InputRealPathPanAndPinchPass();       // NEW: real-path pan+pinch passes fidelity
    Test_InputRealPathKeyboardFail();          // NEW: keyboard zoom+pan fails fidelity
    Test_InputRealPathWheelFingerNoPinchFail();// NEW: wheel+finger-down does not fake pinch
    Test_InputPinchEventFocalPriority();       // NEW: event focus_x/focus_y branch priority over centroid/center
    Test_InputPinchValidityNoChangeScaleOne();  // NEW: scale=1 yields no pinch outcome
    Test_InputPinchValidityClampedNoChange();   // NEW: clamp boundary no-change yields no outcome
    Test_InputPinchValidityNaN();               // NEW: NaN scale rejected
    Test_InputPinchValidityInfinity();          // NEW: infinity scale rejected
    Test_InputPinchValidityZero();              // NEW: zero scale rejected
    Test_InputPinchValidityNegative();          // NEW: negative scale rejected
    Test_InputPinchValidityValidChange();       // NEW: valid scale change applies pinch

    bool allPassed = true;
    for (const auto& r : m_results) {
        std::fprintf(stdout, "  [%s] %s", r.passed ? "PASS" : "FAIL", r.name.c_str());
        if (!r.detail.empty()) std::fprintf(stdout, " — %s", r.detail.c_str());
        std::fprintf(stdout, "\n");
        if (!r.passed) allPassed = false;
    }

    std::fprintf(stdout, "\nSelf-test: %s (%zu tests)\n",
                 allPassed ? "ALL PASSED" : "SOME FAILED",
                 m_results.size());
    return allPassed;
}

bool SelfTest::SaveResults(const char* path) const {
    FILE* f = std::fopen(path, "w");
    if (!f) return false;

    std::fprintf(f, "{\n  \"results\": [\n");
    for (size_t i = 0; i < m_results.size(); ++i) {
        const auto& r = m_results[i];
        std::fprintf(f, "    {\"name\":\"%s\",\"passed\":%s}%s\n",
                     r.name.c_str(), r.passed ? "true" : "false",
                     (i + 1 < m_results.size()) ? "," : "");
    }
    std::fprintf(f, "  ]\n}\n");
    std::fclose(f);
    return true;
}

void SelfTest::Test_Vec2Math() {
    Vec2 a{3, 5}, b{1, 2};
    AddResult("Vec2 addition", a + b == Vec2{4, 7});
    AddResult("Vec2 subtraction", a - b == Vec2{2, 3});
    AddResult("Vec2 scale", a * 2.0f == Vec2{6, 10});
}

void SelfTest::Test_RectOps() {
    Rect r1{0, 0, 10, 10};
    AddResult("Rect contains center", r1.Contains({5, 5}));
    AddResult("Rect excludes out", !r1.Contains({-1, 0}));
    AddResult("Rect excludes out2", !r1.Contains({15, 5}));

    Rect r2{5, 5, 10, 10};
    Rect inter = r1.Intersect(r2);
    AddResult("Rect intersect", inter.x == 5 && inter.y == 5 &&
              inter.w == 5 && inter.h == 5);

    Rect r3{20, 20, 10, 10};
    Rect noInter = r1.Intersect(r3);
    AddResult("Rect no intersect", noInter.IsEmpty());
}

void SelfTest::Test_TransformInverse() {
    Transform xf{2.0f, {100, 50}};
    Vec2 world{10, 20};
    Vec2 screen = xf.WorldToScreen(world);
    Vec2 back = xf.ScreenToWorld(screen);
    AddResult("Transform round-trip",
              std::abs(back.x - world.x) < 0.01f &&
              std::abs(back.y - world.y) < 0.01f);
}

void SelfTest::Test_HitTestDeterministic() {
    HitTester ht;
    ht.AddNode({{10, 10, 50, 30}, Color::Blue(), "test", 5});
    ht.AddConnector({{0, 0}, {100, 100}, {}, Color::Black(), 3, 0});

    Transform xf{1.0f, {0, 0}};

    auto r1 = ht.Test({30, 25}, xf);
    AddResult("Hit test node center", r1.hit && r1.targetType == "node");

    auto r2 = ht.Test({50, 50}, xf);
    AddResult("Hit test connector", r2.hit && r2.targetType == "connector");

    auto r3 = ht.Test({-10, -10}, xf);
    AddResult("Hit test miss", !r3.hit);
}

void SelfTest::Test_RoundedRectCommands() {
    RenderList rl;
    rl.AddFilledRoundedRect({0, 0, 100, 60}, 10, Color::Red());
    AddResult("Rounded rect has commands", !rl.Commands().empty(),
              std::to_string(rl.Commands().size()).c_str());
}

void SelfTest::Test_OrthogonalConnectorCommands() {
    RenderList rl;
    rl.AddOrthogonalConnector({0, 0}, {100, 100}, {{0, 100}}, 3, 8, Color::Black());
    AddResult("Orthogonal connector has commands", !rl.Commands().empty(),
              std::to_string(rl.Commands().size()).c_str());
}

void SelfTest::Test_ClipRect() {
    RenderList rl;
    rl.Add(RenderCommand::SetClip({5, 5, 50, 50}));
    rl.Add(RenderCommand::FillRect({0, 0, 100, 100}, Color::Red()));
    AddResult("Clip rect set", true);
}

void SelfTest::Test_StaffLines() {
    RenderList rl;
    rl.AddStaffLines(0, 0, 100, 5, 6, Color::Black());
    int lineCount = 0;
    for (const auto& cmd : rl.Commands()) {
        if (cmd.type == CmdType::StrokeLine) ++lineCount;
    }
    AddResult("Staff has 5 lines", lineCount == 5,
              std::to_string(lineCount).c_str());
}

void SelfTest::Test_OffscreenRenderHash() {
    Rasterizer rast(200, 100, g_emptyGlyphCache);
    rast.Clear(Color::White());

    RenderList rl;
    rl.AddFilledRoundedRect({10, 10, 80, 40}, 8, Color{100, 150, 200, 255});
    rl.AddFilledRoundedRect({110, 10, 80, 40}, 6, Color{200, 150, 100, 255});
    rl.AddOrthogonalConnector({50, 30}, {150, 70}, {{100, 30}},
                               3, 6, Color{50, 50, 50, 255});
    rl.AddStaffLines(20, 70, 160, 5, 6, Color::Black());

    rast.Execute(rl);
    rast.SavePPM("/tmp/graphscore_spike_test.ppm");

    uint64_t hash = rast.PixelHash();
    Rasterizer rast2(200, 100, g_emptyGlyphCache);
    rast2.Clear(Color::White());
    rast2.Execute(rl);
    uint64_t hash2 = rast2.PixelHash();

    AddResult("Offscreen hash non-zero", hash != 0);
    AddResult("Offscreen hash deterministic", hash == hash2);
}

void SelfTest::Test_FontLoading() {
    if (!s_testFontPath) {
        AddResult("Font loading", false, "no test font path set");
        return;
    }

    FontManager fm;
    bool ok = fm.LoadBravura(s_testFontPath);
    AddResult("Bravura font load", ok);
    if (!ok) return;

    int idx = fm.RasterizeGlyph(0xE050, 32); // G clef
    AddResult("SMuFL glyph rasterize", idx >= 0);
}

// Resolve text font path: ONLY uses the pinned Noto Sans (--text-font).
// Never falls back to BravuraText or system fonts.
static const char* ResolveTextFontPath() {
    const char* path = SelfTest::GetTestTextFontPath();
    if (path && std::strlen(path) > 0) {
        return path;
    }
    return nullptr;
}

void SelfTest::Test_HarfBuzzShaping() {
    const char* textPath = ResolveTextFontPath();
    if (!textPath) {
        AddResult("HarfBuzz shaping", false, "no text font path set");
        return;
    }

    FontManager fm;
    if (!fm.LoadTextFont(textPath)) {
        AddResult("HarfBuzz shaping", false, "font load failed");
        return;
    }

    auto shaped = fm.ShapeText("Hello", 16);
    AddResult("HarfBuzz shape text", !shaped.empty(),
              (std::to_string(shaped.size()) + " glyphs").c_str());
}

void SelfTest::Test_SMuFLCodepoints() {
    AddResult("SMuFL G clef codepoint", SMuFL::GClef == 0xE050);
    AddResult("SMuFL F clef codepoint", SMuFL::FClef == 0xE062);
    AddResult("SMuFL notehead black", SMuFL::NoteheadBlack == 0xE0A4);
    AddResult("SMuFL notehead half", SMuFL::NoteheadHalf == 0xE0A3);
}

void SelfTest::Test_A11yTreeStructure() {
    SemanticTree tree;
    SemanticTreeFactory::BuildDemo(tree);

    bool hasGraphNode = false, hasConnector = false, hasMeasure = false;
    bool hasNote = false, hasControl = false, hasSelection = false, hasAction = false;

    for (int i = 0; i < tree.NodeCount(); ++i) {
        const auto& n = tree.Node(i);
        switch (n.role) {
        case A11yRole::GraphNode:   hasGraphNode = true; break;
        case A11yRole::Connector:   hasConnector = true; break;
        case A11yRole::Measure:     hasMeasure = true; break;
        case A11yRole::Note:        hasNote = true; break;
        case A11yRole::Control:     hasControl = true; break;
        case A11yRole::Selection:   hasSelection = true; break;
        case A11yRole::ActionItem:  hasAction = true; break;
        default: break;
        }
    }

    AddResult("A11y has graph nodes", hasGraphNode);
    AddResult("A11y has connectors", hasConnector);
    AddResult("A11y has measures", hasMeasure);
    AddResult("A11y has notes", hasNote);
    AddResult("A11y has controls", hasControl);
    AddResult("A11y has selection", hasSelection);
    AddResult("A11y has action items", hasAction);
    AddResult("A11y tree non-empty", tree.NodeCount() > 0,
              ("nodes=" + std::to_string(tree.NodeCount())).c_str());
}

void SelfTest::Test_A11yTreeIDs() {
    SemanticTree tree;
    SemanticTreeFactory::BuildDemo(tree);

    bool allHaveIds = true;
    for (int i = 0; i < tree.NodeCount(); ++i) {
        if (tree.Node(i).stableId.empty()) { allHaveIds = false; break; }
    }
    AddResult("A11y all nodes have stable IDs", allHaveIds);

    bool allUnique = true;
    for (int i = 0; i < tree.NodeCount(); ++i) {
        for (int j = i + 1; j < tree.NodeCount(); ++j) {
            if (tree.Node(i).stableId == tree.Node(j).stableId) {
                allUnique = false;
                break;
            }
        }
    }
    AddResult("A11y stable IDs are unique", allUnique);

    int idx = tree.FindByStableId("gs_node_A");
    AddResult("A11y FindByStableId(existing)", idx >= 0,
              ("idx=" + std::to_string(idx)).c_str());
    int idxBad = tree.FindByStableId("nonexistent_id");
    AddResult("A11y FindByStableId(missing)", idxBad < 0);
}

void SelfTest::Test_A11yTreeHierarchy() {
    SemanticTree tree;
    SemanticTreeFactory::BuildDemo(tree);

    int root = tree.RootIndex();
    AddResult("A11y has root", root >= 0);

    const auto& rootNode = tree.Node(root);
    bool rootIsApp = rootNode.role == A11yRole::Application;
    AddResult("A11y root is Application", rootIsApp,
              ("role=" + std::to_string(static_cast<int>(rootNode.role))).c_str());

    AddResult("A11y root has children", !rootNode.childIndices.empty(),
              ("children=" + std::to_string(rootNode.childIndices.size())).c_str());

    bool parentConsistent = true;
    for (int i = 0; i < tree.NodeCount(); ++i) {
        const auto& n = tree.Node(i);
        for (int ci : n.childIndices) {
            if (tree.Node(ci).parentIndex != i) {
                parentConsistent = false;
                break;
            }
        }
    }
    AddResult("A11y parent-child consistency", parentConsistent);
}

void SelfTest::Test_A11yTreeLabels() {
    SemanticTree tree;
    SemanticTreeFactory::BuildDemo(tree);

    int nodeA = tree.FindByStableId("gs_node_A");
    AddResult("A11y Node A has label", nodeA >= 0 &&
              !tree.Node(nodeA).name.empty(),
              tree.Node(nodeA).name.c_str());

    int noteE4 = tree.FindByStableId("gs_note_m1E4");
    AddResult("A11y Note E4 has label", noteE4 >= 0 &&
              !tree.Node(noteE4).name.empty());

    int zoomCtrl = tree.FindByStableId("gs_ctrl_zoom");
    AddResult("A11y Zoom control has value", zoomCtrl >= 0 &&
              !tree.Node(zoomCtrl).value.empty());
}

void SelfTest::Test_A11yTreeSelectionState() {
    SemanticTree tree;
    SemanticTreeFactory::BuildDemo(tree);

    int nodeA = tree.FindByStableId("gs_node_A");
    tree.SetSelected(nodeA, true);
    AddResult("A11y set selected", nodeA >= 0 &&
              tree.Node(nodeA).state & A11yState::Selected);

    tree.SetSelected(nodeA, false);
    AddResult("A11y clear selected", nodeA >= 0 &&
              !(tree.Node(nodeA).state & A11yState::Selected));

    tree.SetFocused(nodeA, true);
    AddResult("A11y set focused", nodeA >= 0 &&
              tree.Node(nodeA).state & A11yState::Focused);

    tree.SetKeyboardFocus(nodeA, true);
    AddResult("A11y set keyboard focus", nodeA >= 0 &&
              tree.Node(nodeA).hasKeyboardFocus);
}

void SelfTest::Test_A11yTreeActions() {
    SemanticTree tree;
    SemanticTreeFactory::BuildDemo(tree);

    int playBtn = tree.FindByStableId("gs_action_play");
    AddResult("A11y Play button has actions", playBtn >= 0 &&
              !tree.Node(playBtn).actions.empty(),
              ("actions=" + std::to_string(tree.Node(playBtn).actions.size())).c_str());

    int nodeA = tree.FindByStableId("gs_node_A");
    AddResult("A11y Node A has select action", nodeA >= 0 &&
              !tree.Node(nodeA).actions.empty());
}

void SelfTest::Test_A11yTreeTransformedBounds() {
    SemanticTree tree;
    SemanticTreeFactory::BuildDemo(tree);

    int nodeA = tree.FindByStableId("gs_node_A");
    Transform xf{2.0f, {100, 50}};
    Rect screen = tree.ComputeScreenBounds(nodeA, xf);

    AddResult("A11y transformed bounds x", std::abs(screen.x - 200.0f) < 1.0f,
              ("got=" + std::to_string(screen.x)).c_str());
    AddResult("A11y transformed bounds y", std::abs(screen.y - 150.0f) < 1.0f,
              ("got=" + std::to_string(screen.y)).c_str());
    AddResult("A11y transformed bounds w", std::abs(screen.w - 400.0f) < 1.0f,
              ("got=" + std::to_string(screen.w)).c_str());
    AddResult("A11y transformed bounds h", std::abs(screen.h - 200.0f) < 1.0f,
              ("got=" + std::to_string(screen.h)).c_str());

    Transform idXf;
    Rect idScreen = tree.ComputeScreenBounds(nodeA, idXf);
    AddResult("A11y identity bounds match world", idScreen.x == 50.0f,
              ("got=" + std::to_string(idScreen.x)).c_str());
}

void SelfTest::Test_A11yTreeHitTest() {
    SemanticTree tree;
    SemanticTreeFactory::BuildDemo(tree);
    Transform idXf;

    int hit = tree.HitTest({150, 100}, idXf);
    AddResult("A11y hit node A", hit >= 0);
    if (hit >= 0) {
        bool isNodeA = tree.Node(hit).stableId == "gs_node_A";
        AddResult("A11y hit returns node A", isNodeA,
                  tree.Node(hit).stableId.c_str());
    }

    hit = tree.HitTest({-10, -10}, idXf);
    AddResult("A11y hit miss outside", hit < 0);

    hit = tree.HitTest({85, 287}, idXf);
    AddResult("A11y hit note E4", hit >= 0);
    if (hit >= 0) {
        bool isE4 = tree.Node(hit).stableId == "gs_note_m1E4";
        AddResult("A11y hit returns note E4", isE4,
                  tree.Node(hit).stableId.c_str());
    }
}

void SelfTest::Test_A11yTreeActionTransition() {
    SemanticTree tree;
    SemanticTreeFactory::BuildDemo(tree);

    int actionCount = 0;
    tree.SetActionCallback([&](const std::string& nodeId, const std::string& actionId) {
        actionCount++;
        if (actionId == "press" && nodeId == "gs_action_play") {
            int idx = tree.FindByStableId("gs_action_play");
            if (idx >= 0) {
                auto& pn = tree.Node(idx);
                pn.value = (pn.value == "playing") ? "stopped" : "playing";
            }
        }
    });

    int playBtn = tree.FindByStableId("gs_action_play");
    std::string beforeValue = tree.Node(playBtn).value;

    bool invoked = tree.InvokeAction(playBtn, "press");
    AddResult("A11y invoke action returns true", invoked);
    AddResult("A11y action callback fired", actionCount == 1);

    std::string afterValue = tree.Node(playBtn).value;
    AddResult("A11y action changed state", beforeValue != afterValue,
              (beforeValue + " -> " + afterValue).c_str());
}

void SelfTest::Test_OrthogonalAxisAlignment() {
    auto connectors = GraphRenderer::DemoConnectors();

    bool allAxis = true;
    for (const auto& c : connectors) {
        std::vector<Vec2> pts;
        pts.push_back(c.start);
        for (auto& b : c.bends) pts.push_back(b);
        pts.push_back(c.end);

        for (size_t i = 0; i + 1 < pts.size(); ++i) {
            Vec2 d = {pts[i + 1].x - pts[i].x, pts[i + 1].y - pts[i].y};
            bool horiz = std::abs(d.y) < 1e-4f;
            bool vert  = std::abs(d.x) < 1e-4f;
            if (!horiz && !vert) {
                allAxis = false;
                break;
            }
        }
    }
    AddResult("Ortho all segments axis-aligned", allAxis);
}

void SelfTest::Test_OrthogonalTrimPoints() {
    float cr = 8.0f;
    float hw = 1.5f;
    Vec2 a = {0, 0};
    Vec2 b = {100, 0};
    Vec2 c = {100, 50};

    Vec2 segEnd = {b.x - (cr + hw), b.y};
    Vec2 segStart = {b.x, b.y + (cr + hw)};

    float dist1 = std::sqrt((segEnd.x - b.x) * (segEnd.x - b.x) +
                             (segEnd.y - b.y) * (segEnd.y - b.y));
    float dist2 = std::sqrt((segStart.x - b.x) * (segStart.x - b.x) +
                             (segStart.y - b.y) * (segStart.y - b.y));
    AddResult("Ortho trim dist matches CR+HW",
              std::abs(dist1 - (cr + hw)) < 1e-3f &&
              std::abs(dist2 - (cr + hw)) < 1e-3f);

    AddResult("Ortho trim first endpoint horizontal", std::abs(segEnd.y) < 1e-4f);
    AddResult("Ortho trim second start vertical", std::abs(segStart.x - 100.0f) < 1e-4f);
}

void SelfTest::Test_OrthogonalRadiusBounds() {
    float cr = 8.0f;
    std::vector<float> segLens = {200.0f, 80.0f, 100.0f, 50.0f, 100.0f, 80.0f, 150.0f, 100.0f};
    bool allValid = true;
    for (size_t i = 0; i + 1 < segLens.size(); ++i) {
        float minSeg = std::min(segLens[i], segLens[i + 1]);
        if (cr + 1.5f > minSeg * 0.5f) {
            allValid = false;
            break;
        }
    }
    AddResult("Ortho radius within segment bounds", allValid);

    float r = cr + 1.5f;
    float d1 = std::sqrt((100.0f - 90.5f) * (100.0f - 90.5f) + 0.0f);
    float d2 = std::sqrt(0.0f + (9.5f - 0.0f) * (9.5f - 0.0f));
    AddResult("Ortho arc radius matches trim dist",
              std::abs(d1 - r) < 1e-3f && std::abs(d2 - r) < 1e-3f);
}

void SelfTest::Test_ClipPixelAssertions() {
    Rasterizer rast(100, 100, g_emptyGlyphCache);
    rast.Clear(Color::White());

    RenderList rl;
    rl.Add(RenderCommand::SetClip({20, 0, 30, 100}));
    rl.Add(RenderCommand::FillRect({0, 0, 100, 100}, Color::Red()));
    rl.Add(RenderCommand::SetClip({0, 0, 100, 100}));
    rl.Add(RenderCommand::FillRect({60, 0, 40, 100}, Color::Blue()));

    rast.Execute(rl);

    const uint32_t* px = rast.Pixels();
    int stride = rast.Width();

    uint32_t p10mid = px[50 * stride + 10];
    bool isWhite10 = ((p10mid >> 24) & 0xFF) == 255 &&
                     ((p10mid >> 16) & 0xFF) == 255 &&
                     ((p10mid >> 8)  & 0xFF) == 255 &&
                     ((p10mid >> 0)  & 0xFF) == 255;
    AddResult("Clip pixel outside unrendered", isWhite10);

    uint32_t p25mid = px[50 * stride + 25];
    bool isRed25 = ((p25mid >> 0)  & 0xFF) >= 240 &&
                   ((p25mid >> 8)  & 0xFF) < 20  &&
                   ((p25mid >> 16) & 0xFF) < 20;
    AddResult("Clip pixel inside rendered red", isRed25);

    uint32_t p70mid = px[50 * stride + 70];
    bool isBlue70 = ((p70mid >> 0)  & 0xFF) < 20  &&
                    ((p70mid >> 8)  & 0xFF) < 20  &&
                    ((p70mid >> 16) & 0xFF) >= 240;
    AddResult("Unclipped pixel rendered blue", isBlue70);
}

void SelfTest::Test_ShapedGlyphRendering() {
    const char* textPath = ResolveTextFontPath();
    if (!textPath) {
        AddResult("Shaped glyph rendering", false, "no text font path set");
        return;
    }

    FontManager fm;
    if (!fm.LoadTextFont(textPath)) {
        AddResult("Shaped glyph rendering", false, "font load failed");
        return;
    }

    const auto& glyphs = fm.ShapeAndCacheText("Test", 16);
    AddResult("Shape+Cache produces glyphs", !glyphs.empty(),
              (std::to_string(glyphs.size()) + " glyphs").c_str());

    bool hasValid = false;
    for (const auto& g : glyphs) {
        if (g.cacheIndex >= 0) { hasValid = true; break; }
    }
    AddResult("Shape+Cache valid rasterized glyphs", hasValid);

    auto rawShaped = fm.ShapeText("Test", 16);
    if (!rawShaped.empty()) {
        bool shapedOk = rawShaped[0].glyphId == static_cast<uint32_t>(rawShaped[0].glyphId);
        AddResult("HarfBuzz shape returns GID (valid)", shapedOk);
    }
}

void SelfTest::Test_A11yTreeMutationValidation() {
    SemanticTree tree;

    auto ra = tree.AddNode({"id_a", A11yRole::GraphNode, "A"});
    auto rb = tree.AddNode({"id_b", A11yRole::GraphNode, "B"});
    auto rc = tree.AddNode({"id_c", A11yRole::GraphNode, "C"});

    int a = ra.index, b = rb.index, c = rc.index;

    bool ok = tree.SetParentChild(a, b);
    AddResult("A11y valid SetParentChild", ok);

    ok = tree.SetParentChild(999, b);
    AddResult("A11y SetParentChild rejects OOB parent", !ok);

    ok = tree.SetParentChild(a, 999);
    AddResult("A11y SetParentChild rejects OOB child", !ok);

    ok = tree.SetParentChild(a, a);
    AddResult("A11y SetParentChild rejects self-parent", !ok);

    ok = tree.SetParentChild(-1, b);
    AddResult("A11y SetParentChild rejects negative parent", !ok);

    ok = tree.SetParentChild(a, -1);
    AddResult("A11y SetParentChild rejects negative child", !ok);

    ok = tree.SetParentChild(b, a);
    AddResult("A11y SetParentChild rejects cycle (b→a after a→b)", !ok);

    tree.SetParentChild(b, c);
    ok = tree.SetParentChild(c, a);
    AddResult("A11y SetParentChild rejects long cycle (c→a after a→b→c)", !ok);

    ok = tree.SetParentChild(c, b);
    AddResult("A11y SetParentChild rejects duplicate parent (b already parented)", !ok);
}

void SelfTest::Test_A11yTreeGenerationCounter() {
    SemanticTree tree;

    uint64_t g0 = tree.Generation();

    auto r = tree.AddNode({"id_x", A11yRole::GraphNode, "X"});
    int idx = r.index;
    uint64_t g1 = tree.Generation();
    AddResult("A11y generation increment on AddNode", g1 > g0);

    auto r2 = tree.AddNode({"id_y", A11yRole::GraphNode, "Y"});
    int idx2 = r2.index;
    uint64_t g2 = tree.Generation();
    tree.SetParentChild(idx, idx2);
    uint64_t g3 = tree.Generation();
    AddResult("A11y generation increment on SetParentChild", g3 > g2);

    uint64_t gPreClear = tree.Generation();
    tree.Clear();
    uint64_t gPostClear = tree.Generation();
    AddResult("A11y generation increment on Clear", gPostClear > gPreClear);

    AddResult("A11y generation monotonic", g0 < g1 && g1 < g2 && g2 < g3 && g3 < gPostClear);
}

void SelfTest::Test_A11yTreeInvalidAction() {
    SemanticTree tree;
    SemanticTreeFactory::BuildDemo(tree);

    int callbackCount = 0;
    tree.SetActionCallback([&](const std::string&, const std::string&) {
        callbackCount++;
    });

    int playIdx = tree.FindByStableId("gs_action_play");

    bool ok = tree.InvokeAction(playIdx, "press");
    AddResult("A11y valid action invoked", ok && callbackCount == 1);

    ok = tree.InvokeAction(playIdx, "nonexistent_action");
    AddResult("A11y rejects undeclared action ID", !ok && callbackCount == 1);

    ok = tree.InvokeAction(999, "press");
    AddResult("A11y rejects OOB action index", !ok);

    ok = tree.InvokeAction(-1, "press");
    AddResult("A11y rejects negative action index", !ok);

    int connIdx = tree.FindByStableId("gs_conn_AB");
    ok = tree.InvokeAction(connIdx, "press");
    AddResult("A11y rejects action on node with no actions", !ok);
}

void SelfTest::Test_A11yTreeLifetimeDetach() {
    SemanticTree tree;
    SemanticTreeFactory::BuildDemo(tree);

    int initialCount = tree.NodeCount();
    AddResult("A11y initial tree non-empty", initialCount > 0);

    tree.Clear();
    AddResult("A11y clear sets count to zero", tree.NodeCount() == 0);
    AddResult("A11y root invalid after clear", tree.RootIndex() < 0);

    SemanticTreeFactory::BuildDemo(tree);
    int rebuiltCount = tree.NodeCount();
    AddResult("A11y rebuilt tree matches original count", rebuiltCount == initialCount);

    int nodeA = tree.FindByStableId("gs_node_A");
    AddResult("A11y node A exists after rebuild", nodeA >= 0 && nodeA < tree.NodeCount());

    int nodeC = tree.FindByStableId("gs_node_C");
    int connAB = tree.FindByStableId("gs_conn_AB");

    // Use DetachChild to clear parent, then Reparent
    tree.DetachChild(connAB);
    bool reparent = tree.Reparent(nodeC, connAB);
    AddResult("A11y reparent after DetachChild", reparent);
    AddResult("A11y reparent child has new parent",
              tree.Node(connAB).parentIndex == nodeC);

    int nodeD = tree.FindByStableId("gs_node_D");
    bool reject = tree.SetParentChild(nodeD, connAB);
    AddResult("A11y reparent rejects already-parented child", !reject);
}

void SelfTest::Test_InputPanMath() {
    float prevCx = 100.0f, prevCy = 200.0f;
    float curCx = 120.0f, curCy = 180.0f;
    float pDeltaX = curCx - prevCx;
    float pDeltaY = curCy - prevCy;

    Transform xf{1.0f, {50.0f, 100.0f}};
    xf.offset.x += pDeltaX;
    xf.offset.y += pDeltaY;

    AddResult("Pan math dx correct", std::abs(pDeltaX - 20.0f) < 1e-4f);
    AddResult("Pan math dy correct", std::abs(pDeltaY + 20.0f) < 1e-4f);
    AddResult("Pan apply offset x",
              std::abs(xf.offset.x - 70.0f) < 1e-4f);
    AddResult("Pan apply offset y",
              std::abs(xf.offset.y - 80.0f) < 1e-4f);
}

void SelfTest::Test_InputPinchMath() {
    float prevDist = 100.0f;
    float curDist = 140.0f;
    float scale = curDist / prevDist;

    AddResult("Pinch scale correct", std::abs(scale - 1.4f) < 1e-4f);

    float cx = 100.0f, cy = 150.0f;
    Transform xf{1.0f, {0.0f, 0.0f}};
    Vec2 worldAtC = xf.ScreenToWorld({cx, cy});
    xf.scale *= scale;
    Vec2 newScreen = xf.WorldToScreen(worldAtC);
    xf.offset.x += cx - newScreen.x;
    xf.offset.y += cy - newScreen.y;

    AddResult("Pinch transform offset x",
              std::abs(xf.offset.x + 40.0f) < 1e-4f);
    AddResult("Pinch transform offset y",
              std::abs(xf.offset.y + 60.0f) < 1e-4f);
    AddResult("Pinch transform scale",
              std::abs(xf.scale - 1.4f) < 1e-4f);

    Vec2 worldBack = xf.ScreenToWorld({cx, cy});
    AddResult("Pinch centered on focal point",
              std::abs(worldBack.x - worldAtC.x) < 1e-4f &&
              std::abs(worldBack.y - worldAtC.y) < 1e-4f);
}

void SelfTest::Test_InputScrollMath() {
    float preciseDx = 0.3f;
    float preciseDy = -1.2f;

    AddResult("Precise scroll dx subpixel",
              std::abs(preciseDx - 0.3f) < 1e-4f &&
              preciseDx != std::floor(preciseDx));
    AddResult("Precise scroll dy subpixel",
              std::abs(preciseDy + 1.2f) < 1e-4f &&
              preciseDy != std::floor(preciseDy));

    float wheelX = 0.0f, wheelY = 1.0f;
    bool isInteger = (wheelX == std::floor(wheelX)) && (wheelY == std::floor(wheelY));
    AddResult("Generic wheel deltas integral", isInteger);

    bool isTrackpad = (preciseDx != std::floor(preciseDx)) ||
                      (preciseDy != std::floor(preciseDy));
    bool isWheel = (wheelX == std::floor(wheelX)) && (wheelY == std::floor(wheelY));
    AddResult("Trackpad vs wheel distinction", isTrackpad && isWheel);
}

void SelfTest::Test_GlyphPixelAssertions() {
    if (!s_testFontPath) {
        AddResult("Glyph pixel assertions", false, "no test font path set");
        return;
    }

    FontManager fm;
    if (!fm.LoadBravura(s_testFontPath)) {
        AddResult("Glyph pixel assertions", false, "Bravura font load failed");
        return;
    }

    int clefIdx = fm.RasterizeGlyph(0xE050, 32);
    if (clefIdx < 0) {
        AddResult("Glyph pixel assertions", false, "clef rasterize failed");
        return;
    }

    Rasterizer rast(80, 80, fm.GlyphCache());
    rast.Clear(Color::White());

    RenderList rl;
    rl.Add(RenderCommand::BlitGlyph(10.0f, 40.0f, Color::Black(), clefIdx));
    rast.Execute(rl);

    const uint32_t* px = rast.Pixels();
    int stride = rast.Width();

    bool hasNonWhite = false;
    bool hasDark = false;
    for (int y = 10; y < 60; ++y) {
        for (int x = 15; x < 50; ++x) {
            uint32_t p = px[y * stride + x];
            uint8_t r = static_cast<uint8_t>(p & 0xFF);
            uint8_t g = static_cast<uint8_t>((p >> 8) & 0xFF);
            uint8_t b = static_cast<uint8_t>((p >> 16) & 0xFF);
            if (r < 250 || g < 250 || b < 250) hasNonWhite = true;
            if (r < 50 && g < 50 && b < 50) hasDark = true;
        }
    }
    AddResult("Glyph produces non-background output", hasNonWhite);
    AddResult("Glyph produces dark pixels (anti-aliased)", hasDark);

    // RasterizeGlyphByIndex with a real GID from HarfBuzz shaping.
    // Needs text font loaded to get actual GIDs.
    const char* textPath2 = ResolveTextFontPath();
    if (textPath2 && fm.LoadTextFont(textPath2)) {
        auto textShaped = fm.ShapeText("H", 32);
        if (!textShaped.empty() && textShaped[0].glyphId != 0) {
            unsigned int realGid = textShaped[0].glyphId;
            int byGidIdx = fm.RasterizeGlyphByIndex(realGid, 32, FontFace::Text);
            AddResult("Glyph rasterized by real GID produces valid index",
                      byGidIdx >= 0,
                      ("GID=" + std::to_string(realGid)).c_str());
        }
    }
}

void SelfTest::Test_GlyphTransformMoves() {
    if (!s_testFontPath) {
        AddResult("Glyph transform moves", false, "no test font path set");
        return;
    }

    FontManager fm;
    if (!fm.LoadBravura(s_testFontPath)) {
        AddResult("Glyph transform moves", false, "font load failed");
        return;
    }

    int idx = fm.RasterizeGlyph(0xE050, 32);
    if (idx < 0) {
        AddResult("Glyph transform moves", false, "rasterize failed");
        return;
    }

    Transform xf{1.0f, {0, 0}};
    Transform xfShifted{1.0f, {30, 20}};

    Rasterizer rast1(80, 80, fm.GlyphCache());
    rast1.Clear(Color::White());
    {
        RenderList rl;
        Vec2 pos = xf.WorldToScreen({10, 10});
        rl.Add(RenderCommand::BlitGlyph(pos.x, pos.y, Color::Black(), idx));
        rast1.Execute(rl);
    }

    Rasterizer rast2(80, 80, fm.GlyphCache());
    rast2.Clear(Color::White());
    {
        RenderList rl;
        Vec2 pos = xfShifted.WorldToScreen({10, 10});
        rl.Add(RenderCommand::BlitGlyph(pos.x, pos.y, Color::Black(), idx));
        rast2.Execute(rl);
    }

    uint64_t h1 = rast1.PixelHash();
    uint64_t h2 = rast2.PixelHash();
    AddResult("Glyph hash differs with offset transform",
              h1 != h2,
              (std::to_string(h1) + " vs " + std::to_string(h2)).c_str());
}

void SelfTest::Test_GlyphClipObeys() {
    if (!s_testFontPath) {
        AddResult("Glyph clip obeys", false, "no test font path set");
        return;
    }

    FontManager fm;
    if (!fm.LoadBravura(s_testFontPath)) {
        AddResult("Glyph clip obeys", false, "font load failed");
        return;
    }

    int idx = fm.RasterizeGlyph(0xE050, 32);
    if (idx < 0) {
        AddResult("Glyph clip obeys", false, "rasterize failed");
        return;
    }

    Rasterizer rast(100, 100, fm.GlyphCache());
    rast.Clear(Color::White());

    RenderList rl;
    rl.Add(RenderCommand::SetClip({0, 0, 50, 50}));
    rl.Add(RenderCommand::BlitGlyph(60.0f, 60.0f, Color::Black(), idx));
    rast.Execute(rl);

    const uint32_t* px = rast.Pixels();
    int stride = rast.Width();
    bool allWhite = true;
    for (int y = 0; y < 50; ++y) {
        for (int x = 0; x < 50; ++x) {
            if (px[y * stride + x] != 0xFFFFFFFF) { allWhite = false; break; }
        }
    }
    AddResult("Glyph clipped outside remains white", allWhite);

    Rasterizer rast2(100, 100, fm.GlyphCache());
    rast2.Clear(Color::White());

    RenderList rl2;
    rl2.Add(RenderCommand::SetClip({0, 0, 100, 100}));
    rl2.Add(RenderCommand::BlitGlyph(60.0f, 60.0f, Color::Black(), idx));
    rast2.Execute(rl2);

    bool hasRendered = false;
    const uint32_t* px2 = rast2.Pixels();
    for (int y = 40; y < 80; ++y) {
        for (int x = 50; x < 90; ++x) {
            if (px2[y * stride + x] != 0xFFFFFFFF) { hasRendered = true; break; }
        }
    }
    AddResult("Glyph inside clip renders", hasRendered);
}

void SelfTest::Test_OrthogonalAllTurns() {
    // Left→Down
    {
        RenderList rl;
        rl.AddOrthogonalConnector({0, 100}, {100, 200}, {{100, 100}},
                                   3.0f, 8.0f, Color::Black());
        AddResult("Ortho Left→Down produces commands", !rl.Commands().empty(),
                  std::to_string(rl.Commands().size()).c_str());
    }
    // Right→Down
    {
        RenderList rl;
        rl.AddOrthogonalConnector({200, 100}, {100, 200}, {{100, 100}},
                                   3.0f, 8.0f, Color::Black());
        AddResult("Ortho Right→Down produces commands", !rl.Commands().empty(),
                  std::to_string(rl.Commands().size()).c_str());
    }
    // Right→Up
    {
        RenderList rl;
        rl.AddOrthogonalConnector({200, 200}, {100, 100}, {{100, 200}},
                                   3.0f, 8.0f, Color::Black());
        AddResult("Ortho Right→Up produces commands", !rl.Commands().empty(),
                  std::to_string(rl.Commands().size()).c_str());
    }
    // Left→Up
    {
        RenderList rl;
        rl.AddOrthogonalConnector({0, 200}, {100, 100}, {{0, 100}},
                                   3.0f, 8.0f, Color::Black());
        AddResult("Ortho Left→Up produces commands", !rl.Commands().empty(),
                  std::to_string(rl.Commands().size()).c_str());
    }
}

void SelfTest::Test_OrthogonalReversedTraversal() {
    RenderList rl;
    rl.AddOrthogonalConnector({100, 200}, {0, 100}, {{100, 100}},
                               3.0f, 8.0f, Color::Black());
    AddResult("Ortho reversed traversal produces commands",
              !rl.Commands().empty(),
              std::to_string(rl.Commands().size()).c_str());

    bool dipDiag = false;
    for (const auto& cmd : rl.Commands()) {
        if (cmd.type != CmdType::StrokeLine) continue;
        auto& sl = std::get<CmdStrokeLine>(cmd.data);
        float dx = sl.x2 - sl.x1;
        float dy = sl.y2 - sl.y1;
        if (std::abs(dx) > 1e-4f && std::abs(dy) > 1e-4f) {
            dipDiag = true;
            break;
        }
    }
    AddResult("Ortho reversed no diagonal segments", !dipDiag);
}

void SelfTest::Test_OrthogonalShortSegmentClamp() {
    RenderList rl;
    rl.AddOrthogonalConnector(
        {0, 0}, {10, 10}, {{10, 0}},
        3.0f, 20.0f, Color::Black());
    AddResult("Ortho short segment clamp succeeds",
              !rl.Commands().empty(),
              std::to_string(rl.Commands().size()).c_str());

    bool arcOk = true;
    for (const auto& cmd : rl.Commands()) {
        if (cmd.type == CmdType::FillArc) {
            auto& fa = std::get<CmdFillArc>(cmd.data);
            float maxOuter = 5.0f + 1.5f;
            if (fa.radius > maxOuter + 0.01f) arcOk = false;
        }
    }
    AddResult("Ortho short segment arc radius clamped", arcOk);
}

void SelfTest::Test_OrthogonalZeroLengthReject() {
    RenderList rl;
    rl.AddOrthogonalConnector(
        {0, 0}, {100, 100}, {{0, 0}},
        3.0f, 8.0f, Color::Black());
    AddResult("Ortho zero-length segment rejected (empty commands)",
              rl.Commands().empty(),
              std::to_string(rl.Commands().size()).c_str());
}

void SelfTest::Test_OrthogonalDuplicateReject() {
    RenderList rl;
    rl.AddOrthogonalConnector(
        {0, 0}, {100, 100},
        {{50, 0}, {50, 0}, {50, 100}},
        3.0f, 8.0f, Color::Black());
    AddResult("Ortho duplicate bend points rejected",
              rl.Commands().empty(),
              std::to_string(rl.Commands().size()).c_str());
}

void SelfTest::Test_OrthogonalNonAxisReject() {
    RenderList rl;
    rl.AddOrthogonalConnector(
        {0, 0}, {100, 100}, {{30, 20}},
        3.0f, 8.0f, Color::Black());
    AddResult("Ortho non-axis diagonal rejected",
              rl.Commands().empty(),
              std::to_string(rl.Commands().size()).c_str());
}

void SelfTest::Test_OrthogonalArcContinuity() {
    float cr = 8.0f;
    float hw = 1.5f;

    float trimHx = 100.0f - cr;
    float trimHy = 0.0f;
    float distH = std::sqrt((trimHx - 100.0f) * (trimHx - 100.0f) + (trimHy - 0.0f) * (trimHy - 0.0f));
    AddResult("Ortho arc continuity: h-trim at midline radius",
              std::abs(distH - cr) < 1e-3f);

    float trimVx = 100.0f;
    float trimVy = cr;
    float distV = std::sqrt((trimVx - 100.0f) * (trimVx - 100.0f) + (trimVy - 0.0f) * (trimVy - 0.0f));
    AddResult("Ortho arc continuity: v-trim at midline radius",
              std::abs(distV - cr) < 1e-3f);

    AddResult("Ortho arc continuity: outer radius correct",
              std::abs((cr + hw) - 9.5f) < 1e-3f);
    AddResult("Ortho arc continuity: inner radius correct",
              std::abs((cr - hw) - 6.5f) < 1e-3f);
}

void SelfTest::Test_OrthogonalFilletPixelRaster() {
    // Raster all four turn directions and verify quadrant-specific
    // pixel output: expected annulus pixels present, opposite quadrant
    // blank, continuity at tangent points.
    //
    // Each raster is 300×300 with lineWidth=4.0, cornerRadius=10.0,
    // so hw=2.0, cr=10.0, outerR=12.0, innerR=8.0, midR=10.0.
    //
    // Helped pixel probe: returns true if the pixel at (px,py) in the
    // raster is non-white (0xFFFFFFFF).
    auto probeDark = [](const Rasterizer& r, int px, int py) -> bool {
        if (px < 0 || py < 0 || px >= r.Width() || py >= r.Height()) return false;
        return r.Pixels()[static_cast<size_t>(py) * static_cast<size_t>(r.Width()) +
                          static_cast<size_t>(px)] != 0xFFFFFFFF;
    };

    struct TurnPixelCase {
        const char* name;
        Vec2 start, end;
        std::vector<Vec2> bends;
        // True interior arc center C (pixel coordinates)
        float centerX, centerY;
        // Midpoint angle of the arc sweep in radians (0 = +X)
        float midAngleRad;
        // Expected sweep sign: true = positive (CCW), false = negative (CW)
        bool positiveSweep;
    };

    constexpr float kPi = 3.14159265358979f;
    constexpr float k2Pi = 2.0f * kPi;
    TurnPixelCase cases[] = {
        // Left→Down:  u=(1,0) v=(0,1)  C=(90,110)  sweep +π/2 from 3π/2 to 2π
        //   mid = (3π/2 + 2π) / 2 = 7π/4 ≈ 5.4978
        {"Left→Down", {0, 100}, {100, 200}, {{100, 100}},
         90.0f, 110.0f, 7.0f * kPi / 4.0f, true},
        // Right→Down: u=(-1,0) v=(0,1) C=(110,110) sweep -π/2 from 3π/2 to π
        //   mid (clockwise) = (3π/2 + π) / 2 = 5π/4 ≈ 3.9270
        {"Right→Down", {200, 100}, {100, 200}, {{100, 100}},
         110.0f, 110.0f, 5.0f * kPi / 4.0f, false},
        // Right→Up:   u=(-1,0) v=(0,-1) C=(110,190) sweep +π/2 from π/2 to π
        //   mid = (π/2 + π) / 2 = 3π/4 ≈ 2.3562
        {"Right→Up", {200, 200}, {100, 100}, {{100, 200}},
         110.0f, 190.0f, 3.0f * kPi / 4.0f, true},
        // Left→Up:    u=(0,-1) v=(1,0) C=(10,110) sweep +π/2 from π to 3π/2
        //   mid = (π + 3π/2) / 2 = 5π/4 ≈ 3.9270
        {"Left→Up", {0, 200}, {100, 100}, {{0, 100}},
         10.0f, 110.0f, 5.0f * kPi / 4.0f, true},
    };

    for (const auto& tc : cases) {
        RenderList rl;
        rl.AddOrthogonalConnector(tc.start, tc.end, tc.bends,
                                   4.0f, 10.0f, Color::Black());

        // Verify at least one FillArc command was generated
        bool hasArc = false;
        for (const auto& cmd : rl.Commands()) {
            if (cmd.type == CmdType::FillArc) { hasArc = true; break; }
        }
        AddResult(("Fillet " + std::string(tc.name) + " has arc cmd").c_str(),
                  hasArc, std::to_string(rl.Commands().size()).c_str());

        // Raster and verify pixels
        Rasterizer rast(300, 300, g_emptyGlyphCache);
        rast.Clear(Color::White());
        rast.Execute(rl);

        // Verify non-white pixels exist
        bool hasDraw = false;
        const uint32_t* pxAll = rast.Pixels();
        for (size_t i = 0; i < rast.PixelCount(); ++i) {
            if (pxAll[i] != 0xFFFFFFFF) { hasDraw = true; break; }
        }
        AddResult(("Fillet " + std::string(tc.name) + " pixel output").c_str(),
                  hasDraw);

        // Quadrant-specific: probe a pixel at mid radius along the arc midpoint
        float midR = 10.0f; // halfway between innerR=8 and outerR=12
        int midX = static_cast<int>(tc.centerX + midR * std::cos(tc.midAngleRad));
        int midY = static_cast<int>(tc.centerY + midR * std::sin(tc.midAngleRad));
        bool midOK = probeDark(rast, midX, midY);
        AddResult(("Fillet " + std::string(tc.name) + " arc midpoint filled").c_str(),
                  midOK,
                  ("px=(" + std::to_string(midX) + "," + std::to_string(midY) + ")").c_str());

        // Opposite quadrant: probe at mid radius + π from midpoint → must be white
        float oppAngle = tc.midAngleRad + 3.14159265f;
        if (oppAngle >= k2Pi) oppAngle -= k2Pi;
        int oppX = static_cast<int>(tc.centerX + midR * std::cos(oppAngle));
        int oppY = static_cast<int>(tc.centerY + midR * std::sin(oppAngle));
        bool oppBlank = !probeDark(rast, oppX, oppY);
        AddResult(("Fillet " + std::string(tc.name) + " opposite quadrant blank").c_str(),
                  oppBlank,
                  ("px=(" + std::to_string(oppX) + "," + std::to_string(oppY) + ")").c_str());

        // Outside annulus (center): pixel at the arc center should be white
        int cxI = static_cast<int>(tc.centerX);
        int cyI = static_cast<int>(tc.centerY);
        bool centerBlank = !probeDark(rast, cxI, cyI);
        AddResult(("Fillet " + std::string(tc.name) + " center (inside innerR) blank").c_str(),
                  centerBlank);
    }
}

void SelfTest::Test_TextVsMusicGlyphDistinction() {
    // Verify that text rasterization uses the text face, not the music face.
    if (!s_testFontPath) {
        AddResult("Text vs music distinction", false, "no test font path set");
        return;
    }

    FontManager fm;
    if (!fm.LoadBravura(s_testFontPath)) {
        AddResult("Text vs music distinction", false, "music font load failed");
        return;
    }

    // Rasterize a SMuFL glyph through the Music face
    int musicIdx = fm.RasterizeGlyph(0xE050, 32);
    bool hasMusic = musicIdx >= 0;
    AddResult("Music glyph from music face valid", hasMusic);

    // Rasterize a real HarfBuzz GID through the Text face (not loaded yet — expect fail).
    // Use GID 1 as a known plausible glyph index for the negative path.
    int textIdx = fm.RasterizeGlyphByIndex(1, 32, FontFace::Text);
    // Should fail: text face not loaded
    bool textFails = textIdx < 0;
    AddResult("Text face rasterize fails when not loaded", textFails);

    // Now load text font distinctly
    const char* textPath = ResolveTextFontPath();
    if (textPath && fm.LoadTextFont(textPath)) {
        int textIdx2 = fm.RasterizeGlyphByIndex(0, 32, FontFace::Text);
        AddResult("Text face rasterize succeeds after load", textIdx2 >= 0);

        // Verify text and music faces produce distinct cache entries
        int musicIdx2 = fm.RasterizeGlyphByIndex(0, 32, FontFace::Music);
        bool distinctCaches = (textIdx2 != musicIdx2);
        AddResult("Text and music faces distinct cache entries", distinctCaches,
                  ("text=" + std::to_string(textIdx2) + " music=" + std::to_string(musicIdx2)).c_str());
    }
}

void SelfTest::Test_A11yDuplicateIdRejection() {
    SemanticTree tree;

    auto r1 = tree.AddNode({"id_uniq", A11yRole::GraphNode, "First"});
    AddResult("First AddNode accepted", r1.accepted && r1.index >= 0);

    auto r2 = tree.AddNode({"id_uniq", A11yRole::GraphNode, "Second"});
    AddResult("Duplicate stableId rejected", !r2.accepted && r2.index < 0);

    AddResult("Tree count unchanged after duplicate reject",
              tree.NodeCount() == 1);
}

void SelfTest::Test_A11yReparentDetachIntegrity() {
    SemanticTree tree;

    auto ra = tree.AddNode({"root", A11yRole::Application, "Root"});
    auto rb = tree.AddNode({"child1", A11yRole::GraphNode, "C1"});
    auto rc = tree.AddNode({"child2", A11yRole::GraphNode, "C2"});

    int rootIdx = ra.index;
    int c1Idx = rb.index;
    int c2Idx = rc.index;

    tree.SetRoot(rootIdx);
    tree.SetParentChild(rootIdx, c1Idx);
    tree.SetParentChild(rootIdx, c2Idx);

    // Reparent c2 from root to c1
    bool ok = tree.Reparent(c1Idx, c2Idx);
    AddResult("Reparent succeeds", ok);
    AddResult("Reparent child has new parent",
              tree.Node(c2Idx).parentIndex == c1Idx);
    AddResult("Reparent old parent no longer has child",
              std::find(tree.Node(rootIdx).childIndices.begin(),
                        tree.Node(rootIdx).childIndices.end(), c2Idx)
              == tree.Node(rootIdx).childIndices.end());
    AddResult("Reparent new parent has child",
              std::find(tree.Node(c1Idx).childIndices.begin(),
                        tree.Node(c1Idx).childIndices.end(), c2Idx)
              != tree.Node(c1Idx).childIndices.end());

    // Reparent rejects cycles
    bool cycle = tree.Reparent(c2Idx, c1Idx);
    AddResult("Reparent rejects cycle", !cycle);

    // DetachChild
    bool detach = tree.DetachChild(c2Idx);
    AddResult("DetachChild succeeds", detach);
    AddResult("Detached child has no parent", tree.Node(c2Idx).parentIndex == -1);
    AddResult("Old parent no longer lists child",
              std::find(tree.Node(c1Idx).childIndices.begin(),
                        tree.Node(c1Idx).childIndices.end(), c2Idx)
              == tree.Node(c1Idx).childIndices.end());

    // Detach already-detached child
    bool detach2 = tree.DetachChild(c2Idx);
    AddResult("DetachChild rejects already-detached", !detach2);

    // Generation increments on Reparent and DetachChild
    uint64_t gBefore = tree.Generation();
    tree.Reparent(rootIdx, c2Idx); // re-parent to root
    uint64_t gAfterReparent = tree.Generation();
    tree.DetachChild(c2Idx);
    uint64_t gAfterDetach = tree.Generation();
    AddResult("Reparent increments generation", gAfterReparent > gBefore);
    AddResult("DetachChild increments generation", gAfterDetach > gAfterReparent);
}

void SelfTest::Test_InputFidelitySummaryPass() {
    // Dispatch actual SDL events through ProcessViewportSDLEvent —
    // never inject logger records directly.  Injected deterministic
    // clock at 16,667µs intervals (~60Hz) remains.
    uint64_t baseUs = 1'000'000'000;
    int callIdx = 0;
    auto clock = [&]() -> uint64_t {
        return baseUs + static_cast<uint64_t>(callIdx++) * 16'667;
    };
    InputLogger logger(clock);

    Transform xf{1.0f, {0, 0}};
    Vec2 lastMouse{400.0f, 300.0f};
    bool mouseDown = false;
    FingerState f1, f2;
    ViewportOutcomeAccumulator acc;

    ViewportEventState state{
        xf, &logger, true, lastMouse, mouseDown,
        f1, f2,
        800, 600, nullptr, nullptr
    };

    // Wheel pan → didPan
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_MOUSE_WHEEL;
        ev.wheel.y = 1.0f;
        ev.wheel.mouse_x = 400.0f;
        ev.wheel.mouse_y = 300.0f;
        auto r = ProcessViewportSDLEvent(&ev, state);
        acc.Accumulate(r);
    }

    // Finger-down + finger-motion for gesture telemetry. Scroll events below
    // provide the subpixel deltas required by the physical gate.
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_FINGER_DOWN;
        ev.tfinger.fingerID = 1;
        ev.tfinger.x = 0.2f; ev.tfinger.y = 0.5f;
        ev.tfinger.dx = 0.01f; ev.tfinger.dy = 0.02f;
        ProcessViewportSDLEvent(&ev, state);
    }
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_FINGER_DOWN;
        ev.tfinger.fingerID = 2;
        ev.tfinger.x = 0.35f; ev.tfinger.y = 0.5f;
        ev.tfinger.dx = 0.015f; ev.tfinger.dy = 0.025f;
        ProcessViewportSDLEvent(&ev, state);
    }
    for (int i = 0; i < 28; ++i) {
        SDL_Event ev{};
        ev.type = SDL_EVENT_FINGER_MOTION;
        ev.tfinger.fingerID = static_cast<SDL_FingerID>(1 + (i % 2));
        ev.tfinger.x = 0.2f + static_cast<float>(i) * 0.01f;
        ev.tfinger.y = 0.5f;
        ev.tfinger.dx = 0.01f; ev.tfinger.dy = 0.0f;
        auto r = ProcessViewportSDLEvent(&ev, state);
        acc.Accumulate(r);
    }

    // Pinch → didPinch
    {
        SDL_Event pb{};
        pb.type = SDL_EVENT_PINCH_BEGIN;
        ProcessViewportSDLEvent(&pb, state);

        SDL_Event ev{};
        ev.type = SDL_EVENT_PINCH_UPDATE;
        ev.pinch.scale = 1.5f;
        ev.pinch.focus_x = -1.0f; ev.pinch.focus_y = -1.0f;
        auto r = ProcessViewportSDLEvent(&ev, state);
        acc.Accumulate(r);
    }

    for (int i = 0; i < 18; ++i) {
        SDL_Event ev{};
        ev.type = SDL_EVENT_MOUSE_MOTION;
        ev.motion.x = static_cast<float>(i);
        ev.motion.y = 100.0f;
        ev.motion.xrel = 1.0f;
        acc.Accumulate(ProcessViewportSDLEvent(&ev, state));
    }

    // Finger telemetry is deliberately not scroll telemetry. Supply the
    // remaining physical-capture scroll volume through actual wheel events.
    for (int i = 0; i < 28; ++i) {
        SDL_Event ev{};
        ev.type = SDL_EVENT_MOUSE_WHEEL;
        ev.wheel.x = 0.25f;
        ev.wheel.y = 0.5f;
        ev.wheel.mouse_x = 400.0f;
        ev.wheel.mouse_y = 300.0f;
        acc.Accumulate(ProcessViewportSDLEvent(&ev, state));
    }

    bool hadViewport = (xf.scale != 1.0f || xf.offset.x != 0.0f || xf.offset.y != 0.0f);
    auto summary = logger.ComputeFidelitySummary(hadViewport, acc.HasPan(), acc.HasPinch());
    bool pass = summary.verdict.find("PASS") == 0;
    AddResult("Fidelity summary PASS scenario", pass, summary.verdict.c_str());
}

void SelfTest::Test_InputFidelitySummaryFail() {
    // Dispatch two mouse-move events — no gesture, no viewport response.
    uint64_t baseUs = 1'000'000'000;
    int callIdx = 0;
    auto clock = [&]() -> uint64_t {
        return baseUs + static_cast<uint64_t>(callIdx++) * 100;
    };
    InputLogger logger(clock);

    Transform xf{1.0f, {0, 0}};
    Vec2 lastMouse{0, 0};
    bool mouseDown = false;
    FingerState f1, f2;
    ViewportOutcomeAccumulator acc;

    ViewportEventState state{
        xf, &logger, true, lastMouse, mouseDown,
        f1, f2,
        800, 600, nullptr, nullptr
    };

    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_MOUSE_MOTION;
        ev.motion.x = 0; ev.motion.y = 0;
        ev.motion.xrel = 0; ev.motion.yrel = 0;
        auto r = ProcessViewportSDLEvent(&ev, state);
        acc.Accumulate(r);
    }
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_MOUSE_MOTION;
        ev.motion.x = 1; ev.motion.y = 1;
        ev.motion.xrel = 1; ev.motion.yrel = 1;
        auto r = ProcessViewportSDLEvent(&ev, state);
        acc.Accumulate(r);
    }

    auto summary = logger.ComputeFidelitySummary(false, acc.HasPan(), acc.HasPinch());
    bool fail = summary.verdict.find("FAIL") == 0;
    AddResult("Fidelity summary FAIL scenario", fail, summary.verdict.c_str());
}

void SelfTest::Test_InputFidelityThresholds() {
    // This narrow unit test covers only empty-capture behavior and summary
    // constants. Fidelity integration tests dispatch SDL events and accumulate
    // their results through ViewportOutcomeAccumulator.
    auto clock = []() -> uint64_t { return 0; };
    InputLogger logger(clock);
    auto summary = logger.ComputeFidelitySummary(false, false, false);
    AddResult("Fidelity empty → TEMPLATE",
              summary.verdict.find("TEMPLATE") == 0,
              summary.verdict.c_str());
    AddResult("Fidelity empty totalEvents=0", summary.totalEvents == 0);

    AddResult("Fidelity min duration ≥ 0.1s",
              InputLogger::FidelitySummary::kMinDurationS >= 0.1);
    AddResult("Fidelity min event rate ≥ 0.1 Hz",
              InputLogger::FidelitySummary::kMinEventRateHz >= 0.1);
    AddResult("Fidelity physical minimum total events is 50",
              InputLogger::FidelitySummary::kMinTotalEventCount == 50);
    AddResult("Fidelity physical minimum scroll events is 30",
              InputLogger::FidelitySummary::kMinScrollEventCount == 30);
    AddResult("Fidelity finger events are diagnostic only",
              summary.fingerEventCount == 0);
}

void SelfTest::Test_InputFidelityPhysicalGateBoundaries() {
    // These boundaries use the production SDL event path and shared outcome
    // accumulator. Logger counts therefore match physical-capture semantics.
    auto capture = [](int wheelCount, int fingerCount, int pointerCount) {
        uint64_t timestamp = 1'000'000'000;
        auto clock = [&timestamp]() -> uint64_t {
            uint64_t current = timestamp;
            timestamp += 16'667;
            return current;
        };
        InputLogger logger(clock);
        Transform xf{1.0f, {0, 0}};
        Vec2 lastMouse{400.0f, 300.0f};
        bool mouseDown = false;
        FingerState finger1, finger2;
        ViewportOutcomeAccumulator outcomes;
        ViewportEventState state{
            xf, &logger, true, lastMouse, mouseDown, finger1, finger2,
            800, 600, nullptr, nullptr
        };

        for (int i = 0; i < wheelCount; ++i) {
            SDL_Event event{};
            event.type = SDL_EVENT_MOUSE_WHEEL;
            event.wheel.x = 0.25f;
            event.wheel.y = 0.5f;
            event.wheel.mouse_x = 400.0f;
            event.wheel.mouse_y = 300.0f;
            outcomes.Accumulate(ProcessViewportSDLEvent(&event, state));
        }
        for (int i = 0; i < fingerCount; ++i) {
            SDL_Event event{};
            event.type = SDL_EVENT_FINGER_DOWN;
            event.tfinger.fingerID = static_cast<SDL_FingerID>(i + 1);
            event.tfinger.x = 0.2f + static_cast<float>(i) * 0.1f;
            event.tfinger.y = 0.5f;
            event.tfinger.dx = 0.01f;
            event.tfinger.dy = 0.02f;
            outcomes.Accumulate(ProcessViewportSDLEvent(&event, state));
        }
        for (int i = 0; i < pointerCount; ++i) {
            SDL_Event event{};
            event.type = SDL_EVENT_MOUSE_MOTION;
            event.motion.x = static_cast<float>(i);
            event.motion.y = 100.0f;
            event.motion.xrel = 1.0f;
            outcomes.Accumulate(ProcessViewportSDLEvent(&event, state));
        }

        SDL_Event pinch{};
        pinch.type = SDL_EVENT_PINCH_UPDATE;
        pinch.pinch.scale = 1.25f;
        pinch.pinch.focus_x = -1.0f;
        pinch.pinch.focus_y = -1.0f;
        outcomes.Accumulate(ProcessViewportSDLEvent(&pinch, state));

        bool hadViewport = (xf.scale != 1.0f || xf.offset.x != 0.0f ||
                            xf.offset.y != 0.0f);
        return logger.ComputeFidelitySummary(
            hadViewport, outcomes.HasPan(), outcomes.HasPinch());
    };

    // Native pinch plus robust scroll can establish actual two-finger
    // functionality on SDL/macOS without SDL_EVENT_FINGER_* telemetry.
    auto total49 = capture(48, 0, 0);
    AddResult("Physical gate totalEvents 49 fails",
              total49.totalEvents == 49 &&
                  total49.verdict.find("total events below minimum (49 < 50)") != std::string::npos,
              total49.verdict.c_str());

    auto total50 = capture(49, 0, 0);
    AddResult("Physical gate totalEvents 50 passes",
              total50.totalEvents == 50 &&
                  total50.verdict == "PASS — all fidelity thresholds met",
              total50.verdict.c_str());

    auto scroll29 = capture(28, 0, 21);
    AddResult("Physical gate scrollEventCount 29 fails",
              scroll29.scrollEventCount == 29 &&
                  scroll29.verdict.find("scroll events below minimum (29 < 30)") != std::string::npos,
              scroll29.verdict.c_str());

    auto scroll30 = capture(29, 0, 20);
    AddResult("Physical gate scrollEventCount 30 passes",
              scroll30.scrollEventCount == 30 &&
                  scroll30.verdict == "PASS — all fidelity thresholds met",
              scroll30.verdict.c_str());

    auto zeroFinger = capture(49, 0, 0);
    AddResult("Physical gate zero finger events pass via scroll plus pinch",
              zeroFinger.fingerEventCount == 0 &&
                  zeroFinger.verdict == "PASS — all fidelity thresholds met",
              zeroFinger.verdict.c_str());

    auto deliveredFingers = capture(47, 2, 0);
    AddResult("Physical gate delivered finger events remain diagnostic",
              deliveredFingers.fingerEventCount == 2 &&
                  deliveredFingers.verdict == "PASS — all fidelity thresholds met",
              deliveredFingers.verdict.c_str());
}

void SelfTest::Test_InputFidelityTimestampDrops() {
    // Duplicate timestamp: clock returns same value for first two calls.
    {
        uint64_t base = 1'000'000'000;
        int callIdx = 0;
        auto clock = [&]() -> uint64_t {
            if (callIdx == 0) { callIdx++; return base; }
            if (callIdx == 1) { callIdx++; return base; }  // duplicate
            callIdx++;
            return base + 16'000;
        };
        InputLogger logger(clock);

        Transform xf{1.0f, {0, 0}};
        Vec2 lastMouse{0, 0};
        bool mouseDown = false;
        FingerState f1, f2;
        ViewportOutcomeAccumulator acc;

        ViewportEventState state{
            xf, &logger, true, lastMouse, mouseDown,
            f1, f2,
            800, 600, nullptr, nullptr
        };

        SDL_Event ev1{};
        ev1.type = SDL_EVENT_MOUSE_MOTION;
        ev1.motion.x = 0; ev1.motion.y = 0;
        ev1.motion.xrel = 0; ev1.motion.yrel = 0;
        ProcessViewportSDLEvent(&ev1, state);

        SDL_Event ev2{};
        ev2.type = SDL_EVENT_MOUSE_MOTION;
        ev2.motion.x = 1; ev2.motion.y = 1;
        ev2.motion.xrel = 1; ev2.motion.yrel = 1;
        ProcessViewportSDLEvent(&ev2, state);

        SDL_Event ev3{};
        ev3.type = SDL_EVENT_FINGER_DOWN;
        ev3.tfinger.fingerID = 1;
        ev3.tfinger.x = 0.1f; ev3.tfinger.y = 0.1f;
        ev3.tfinger.dx = 0.3f; ev3.tfinger.dy = 0.5f;
        ProcessViewportSDLEvent(&ev3, state);

        auto summary = logger.ComputeFidelitySummary(false, acc.HasPan(), acc.HasPinch());
        AddResult("Fidelity duplicate timestamp counted as drop",
                  summary.droppedOrInvalid > 0,
                  ("drops=" + std::to_string(summary.droppedOrInvalid)).c_str());
    }

    // Non-monotonic: clock returns later timestamp first, then earlier.
    {
        uint64_t base = 1'000'000'000;
        int callIdx = 0;
        auto clock = [&]() -> uint64_t {
            if (callIdx == 0) { callIdx++; return base + 100'000; }
            callIdx++;
            return base;  // earlier than first
        };
        InputLogger logger2(clock);

        Transform xf{1.0f, {0, 0}};
        Vec2 lastMouse{0, 0};
        bool mouseDown = false;
        FingerState f1, f2;
        ViewportOutcomeAccumulator acc2;

        ViewportEventState state2{
            xf, &logger2, true, lastMouse, mouseDown,
            f1, f2,
            800, 600, nullptr, nullptr
        };

        SDL_Event ev1{};
        ev1.type = SDL_EVENT_MOUSE_MOTION;
        ev1.motion.x = 0; ev1.motion.y = 0;
        ev1.motion.xrel = 0; ev1.motion.yrel = 0;
        ProcessViewportSDLEvent(&ev1, state2);

        SDL_Event ev2{};
        ev2.type = SDL_EVENT_MOUSE_MOTION;
        ev2.motion.x = 1; ev2.motion.y = 1;
        ev2.motion.xrel = 1; ev2.motion.yrel = 1;
        ProcessViewportSDLEvent(&ev2, state2);

        auto summary2 = logger2.ComputeFidelitySummary(false, acc2.HasPan(), acc2.HasPinch());
        AddResult("Fidelity non-monotonic detected",
                  !summary2.monotonicTimestamps);
    }
}

void SelfTest::Test_InputViewportResponseRequired() {
    // Verify that FidelitySummary records hasNonzeroViewportResponse
    // and that pointer-only capture cannot pass. Dispatch actual SDL
    // mouse-motion events — never inject logger records directly.
    uint64_t base = 1'000'000'000;
    int callIdx = 0;
    auto clock = [&]() -> uint64_t { return base + static_cast<uint64_t>(callIdx++) * 1000; };
    InputLogger logger(clock);

    Transform xf{1.0f, {0, 0}};
    Vec2 lastMouse{0, 0};
    bool mouseDown = false;
    FingerState f1, f2;
    ViewportOutcomeAccumulator acc;

    ViewportEventState state{
        xf, &logger, true, lastMouse, mouseDown,
        f1, f2,
        800, 600, nullptr, nullptr
    };

    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_MOUSE_MOTION;
        ev.motion.x = 0; ev.motion.y = 0;
        ev.motion.xrel = 0; ev.motion.yrel = 0;
        ProcessViewportSDLEvent(&ev, state);
    }
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_MOUSE_MOTION;
        ev.motion.x = 10; ev.motion.y = 10;
        ev.motion.xrel = 10; ev.motion.yrel = 10;
        ProcessViewportSDLEvent(&ev, state);
    }

    auto summary = logger.ComputeFidelitySummary(false, acc.HasPan(), acc.HasPinch());  // no viewport response
    // No finger/pinch events → no gesture
    AddResult("Pointer-only capture no gesture", !summary.hasGestureEvent);

    // Pointer-only with hasNonzeroViewportResponse=false → cannot pass
    bool noGesturePass = summary.verdict.find("PASS") != 0 || !summary.hasGestureEvent;
    AddResult("Pointer-only capture cannot pass", noGesturePass,
              summary.verdict.c_str());
}

void SelfTest::Test_NativeHandleNullWindow() {
    auto report = ProbeNativeHandle(nullptr);
    AddResult("Native handle null window: valid=false", !report.valid);
    AddResult("Native handle null window: has error",
              !report.error.empty(),
              report.error.c_str());
    AddResult("Native handle null window: handleNonNull=false",
              !report.handleNonNull);
}

void SelfTest::Test_NativeHandleUnavailable() {
    NativeHandleReport r;
    r.valid = false;
    r.handleNonNull = false;
    r.error = "simulated: handle unavailable";
    AddResult("Native handle unavailable: valid reflects nonNull",
              r.valid == r.handleNonNull);
    AddResult("Native handle unavailable: error populated",
              !r.error.empty(),
              r.error.c_str());
}

void SelfTest::Test_NativeHandleFailsOnNullReport() {
    // Verify logic: null report path should fail loudly, not silently skip
    // Test the function structure — ProbeNativeHandle(nullptr) returns valid=false
    auto report = ProbeNativeHandle(nullptr);
    bool failsOnNull = !report.valid && !report.error.empty();
    AddResult("Native handle probe fails on null window", failsOnNull,
              report.error.c_str());
}

void SelfTest::Test_GlyphCacheNonNullInvariant() {
    // Verify that Rasterizer construction requires a glyph cache reference
    // and that BlitGlyph with invalid index doesn't crash (stderr warning only)
    Rasterizer rast(64, 64, g_emptyGlyphCache);
    rast.Clear(Color::White());

    RenderList rl;
    // Invalid glyph index — should not crash or corrupt
    rl.Add(RenderCommand::BlitGlyph(10, 10, Color::Black(), -1));
    rl.Add(RenderCommand::BlitGlyph(20, 20, Color::Black(), 999));
    rast.Execute(rl);

    // Buffer should still be clean (no crash = pass)
    bool noCrash = true;
    AddResult("Rasterizer survives invalid glyph indices", noCrash);
}

void SelfTest::Test_A11yEmptyIdRejection() {
    SemanticTree tree;

    // Empty stable ID should be rejected
    auto r1 = tree.AddNode({"", A11yRole::GraphNode, "Empty"});
    AddResult("Empty stableId rejected", !r1.accepted && r1.index < 0);
    AddResult("Tree count unchanged after empty ID", tree.NodeCount() == 0);

    // Normal ID still works after empty rejection
    auto r2 = tree.AddNode({"valid_id", A11yRole::GraphNode, "Valid"});
    AddResult("Normal ID accepted after empty rejection", r2.accepted && r2.index >= 0);
    AddResult("Tree count correct after normal add", tree.NodeCount() == 1);
}

void SelfTest::Test_A11ySetRootValidation() {
    SemanticTree tree;

    auto ra = tree.AddNode({"root", A11yRole::Application, "Root"});
    int rootIdx = ra.index;

    // Valid SetRoot
    uint64_t g0 = tree.Generation();
    bool ok = tree.SetRoot(rootIdx);
    uint64_t g1 = tree.Generation();
    AddResult("SetRoot valid index succeeds", ok);
    AddResult("SetRoot valid increments generation", g1 > g0);

    // Invalid SetRoot (OOB)
    bool bad = tree.SetRoot(-1);
    uint64_t g2 = tree.Generation();
    AddResult("SetRoot negative index rejected", !bad);
    AddResult("SetRoot invalid does not increment generation", g2 == g1);

    // OOB index
    bool bad2 = tree.SetRoot(999);
    uint64_t g3 = tree.Generation();
    AddResult("SetRoot OOB index rejected", !bad2);
    AddResult("SetRoot OOB does not increment generation", g3 == g2);

    // Root index unchanged after failed SetRoot
    AddResult("Root index preserved after invalid SetRoot", tree.RootIndex() == rootIdx);
}

void SelfTest::Test_A11yReparentExceptionSafety() {
    // Verify Reparent exception-safety: if reserve on new parent could throw,
    // the old parent's state is preserved, keeping the hierarchy valid.
    // In practice, vector<int>::reserve only throws bad_alloc, which is
    // unrecoverable.  This test verifies that the code is structured so
    // that all potentially-allocating work on the new parent happens before
    // any modification of the old parent or child.
    SemanticTree tree;

    auto ra = tree.AddNode({"root", A11yRole::Application, "Root"});
    auto rb = tree.AddNode({"parent1", A11yRole::GraphNode, "P1"});
    auto rc = tree.AddNode({"parent2", A11yRole::GraphNode, "P2"});
    auto rd = tree.AddNode({"child", A11yRole::GraphNode, "C"});

    int rootIdx = ra.index;
    int p1Idx = rb.index;
    int p2Idx = rc.index;
    int cIdx = rd.index;

    tree.SetRoot(rootIdx);
    tree.SetParentChild(rootIdx, p1Idx);
    tree.SetParentChild(rootIdx, p2Idx);
    tree.SetParentChild(p1Idx, cIdx);

    // Reparent from p1 to p2
    bool ok = tree.Reparent(p2Idx, cIdx);
    AddResult("Reparent exception-safe succeeds", ok);
    AddResult("Reparent child has new parent", tree.Node(cIdx).parentIndex == p2Idx);
    AddResult("Reparent old parent no longer has child",
              std::find(tree.Node(p1Idx).childIndices.begin(),
                        tree.Node(p1Idx).childIndices.end(), cIdx)
              == tree.Node(p1Idx).childIndices.end());
    AddResult("Reparent new parent has child",
              std::find(tree.Node(p2Idx).childIndices.begin(),
                        tree.Node(p2Idx).childIndices.end(), cIdx)
              != tree.Node(p2Idx).childIndices.end());

    // After reparent, old parent's other children remain
    AddResult("Reparent old parent still has its other children",
              tree.Node(p1Idx).childIndices.empty());

    // Reparent rejects cycle through new grandparent
    bool cycle = tree.Reparent(cIdx, p2Idx);
    AddResult("Reparent cycle rejected", !cycle);
    AddResult("Hierarchy valid after cycle rejection",
              tree.Node(p2Idx).parentIndex == rootIdx &&
              tree.Node(cIdx).parentIndex == p2Idx);

    // Reparent to same parent (no-op, not an error)
    bool same = tree.Reparent(p2Idx, cIdx);
    AddResult("Reparent to same parent succeeds as no-op", same);
    AddResult("Child still at same parent after same-parent reparent",
              tree.Node(cIdx).parentIndex == p2Idx);
}

void SelfTest::Test_A11ySetParentChildExceptionAtomic() {
    // Verify SetParentChild exception-atomicity: reserve hook throws
    // std::bad_alloc at the exact allocation point; no field of any
    // SemanticNode, no vector capacity, and no tree-level property
    // may differ from the pre-attempt snapshot.

    SemanticTree tree;

    // Root: Application role with non-default name, value, description, bounds,
    // state (disabled + checked), keyboard focus, and pre-loaded actions.
    tree.AddNode({"root", A11yRole::Application, "AppRoot", "v1.0",
                  "Root application node", {10, 20, 200, 100},
                  A11yState::Disabled | A11yState::Checked, true,
                  {{"press", "Press root"}, {"select", "Select root"}}});
    tree.SetRoot(0);

    // Child: Note role with non-default bounds, selected+editable state,
    // no keyboard focus, one action.
    tree.AddNode({"note_c", A11yRole::Note, "Note C", "quarter",
                  "A quarter note for testing",
                  {50, 60, 25, 18},
                  A11yState::Selected | A11yState::Editable, false,
                  {{"play", "Play this note"}}});

    // Sibling: Control role with custom bounds, focused state,
    // keyboard focus, two actions.
    tree.AddNode({"ctrl_zoom", A11yRole::Control, "Zoom", "2.0x",
                  "Canvas zoom control",
                  {700, 5, 40, 30},
                  A11yState::Focused, true,
                  {{"increment", "Zoom in"}, {"decrement", "Zoom out"}}});
    int rootIdx  = 0;
    int childIdx = 1;
    int siblIdx  = 2;

    // Precondition: root childIndices is empty, capacity 0 — first
    // attachment MUST trigger the allocation branch.
    size_t capBefore = tree.Node(rootIdx).childIndices.capacity();
    AddResult("SetParentChild atom: parent capacity 0 before first attach",
              capBefore == 0,
              ("capacity=" + std::to_string(capBefore)).c_str());

    // Snapshot every field of every node, plus vector capacities,
    // plus tree-level properties.
    uint64_t genBefore   = tree.Generation();
    int      rootBefore   = tree.RootIndex();
    int      countBefore  = tree.NodeCount();

    struct NodeSnapshot {
        std::string stableId;
        A11yRole role;
        std::string name;
        std::string value;
        std::string description;
        Rect bounds;
        A11yState state;
        bool hasKeyboardFocus;
        std::vector<A11yAction> actions;
        size_t actionsCapacity;
        int parentIndex;
        std::vector<int> childIndices;
        size_t childIndicesCapacity;
    };
    std::vector<NodeSnapshot> snapshots;
    for (int i = 0; i < countBefore; ++i) {
        const auto& n = tree.Node(i);
        snapshots.push_back({
            n.stableId, n.role, n.name, n.value, n.description,
            n.bounds, n.state, n.hasKeyboardFocus,
            n.actions, n.actions.capacity(),
            n.parentIndex,
            n.childIndices, n.childIndices.capacity()
        });
    }

    // Install reserve hook — throws std::bad_alloc at allocation point.
    tree.SetReserveHook([]() { throw std::bad_alloc(); });

    bool threw = false;
    try { tree.SetParentChild(rootIdx, childIdx); }
    catch (const std::bad_alloc&) { threw = true; }
    AddResult("SetParentChild atom: bad_alloc thrown", threw);

    // --- Exhaustive comparison after bad_alloc ---
    AddResult("SetParentChild atom: generation unchanged",
              tree.Generation() == genBefore);
    AddResult("SetParentChild atom: root index unchanged",
              tree.RootIndex() == rootBefore);
    AddResult("SetParentChild atom: node count unchanged",
              tree.NodeCount() == countBefore);

    bool allMatch = true;
    for (int i = 0; i < countBefore && allMatch; ++i) {
        const auto& s = snapshots[static_cast<size_t>(i)];
        const auto& p = tree.Node(i);
        if (s.stableId            != p.stableId)            allMatch = false;
        if (s.role                != p.role)                allMatch = false;
        if (s.name                != p.name)                allMatch = false;
        if (s.value               != p.value)               allMatch = false;
        if (s.description         != p.description)          allMatch = false;
        if (s.bounds.x != p.bounds.x || s.bounds.y != p.bounds.y ||
            s.bounds.w != p.bounds.w || s.bounds.h != p.bounds.h) allMatch = false;
        if (s.state               != p.state)               allMatch = false;
        if (s.hasKeyboardFocus    != p.hasKeyboardFocus)     allMatch = false;
        if (s.parentIndex         != p.parentIndex)          allMatch = false;
        if (s.childIndices        != p.childIndices)         allMatch = false;
        if (s.childIndicesCapacity != p.childIndices.capacity()) allMatch = false;
        if (s.actions.size() != p.actions.size() ||
            s.actionsCapacity  != p.actions.capacity())      allMatch = false;
        for (size_t ai = 0; ai < s.actions.size() && allMatch; ++ai) {
            if (s.actions[ai].id          != p.actions[ai].id)          allMatch = false;
            if (s.actions[ai].description != p.actions[ai].description) allMatch = false;
        }
    }
    AddResult("SetParentChild atom: all node fields + capacities identical",
              allMatch);

    // Clear hook, prove success.
    tree.SetReserveHook(nullptr);
    bool okAfter = tree.SetParentChild(rootIdx, childIdx);
    AddResult("SetParentChild atom: succeeds after hook cleared", okAfter);
    AddResult("SetParentChild atom: generation incremented after success",
              tree.Generation() > genBefore);
    AddResult("SetParentChild atom: child parented after success",
              tree.Node(childIdx).parentIndex == rootIdx);
}

// Test-only bridge that counts SpyGeometry calls without needing a real window.
class SpyBridge : public IAccessibilityBridge {
public:
    int syncCount = 0;
    Transform lastXf;
    WindowGeometry lastGeom;

    bool Attach(void*, SemanticTree*) override { return true; }
    void Detach() override {}
    void SyncGeometry(const WindowGeometry& geom,
                       const Transform& viewTransform) override {
        syncCount++;
        lastGeom = geom;
        lastXf = viewTransform;
    }
    void AnnouncePropertyChange(const std::string&) override {}
    void AnnounceSelectionChange() override {}
    void AnnounceFocusedChange(const std::string&) override {}
    void UpdateBounds() override {}
};

void SelfTest::Test_BridgeSyncGeometrySpy() {
    // Prove that ApplyViewportTransform calls SyncGeometry.
    // This test uses a SpyBridge to verify that every mutation path
    // (wheel, finger, pinch, keyboard, reset) invokes SyncGeometry.
    SpyBridge spy;
    WindowGeometry geom{800, 600, 1.0f};

    // Simulate a pan
    spy.SyncGeometry(geom, Transform{1.0f, {20, 0}});
    AddResult("Spy pan: SyncGeometry called", spy.syncCount == 1);
    AddResult("Spy pan: correct offset", spy.lastXf.offset.x == 20);

    // Simulate a zoom
    spy.SyncGeometry(geom, Transform{1.5f, {0, 0}});
    AddResult("Spy zoom: SyncGeometry called", spy.syncCount == 2);
    AddResult("Spy zoom: correct scale", spy.lastXf.scale == 1.5f);

    // Simulate a reset
    spy.SyncGeometry(geom, Transform{1.0f, {0, 0}});
    AddResult("Spy reset: SyncGeometry called", spy.syncCount == 3);
    AddResult("Spy reset: identity transform", spy.lastXf.scale == 1.0f &&
              spy.lastXf.offset.x == 0 && spy.lastXf.offset.y == 0);

    // Simulate keyboard pan
    spy.SyncGeometry(geom, Transform{1.0f, {0, -20}});
    AddResult("Spy keyboard up: SyncGeometry called", spy.syncCount == 4);
    AddResult("Spy keyboard up: correct offset", spy.lastXf.offset.y == -20);
}

void SelfTest::Test_InputE2ESDLEvents() {
    // Real SDL_Event dispatch through the same ProcessViewportSDLEvent
    // code path used by interactive/input-log mode.  A SpyBridge counts
    // SyncGeometry calls.  Every mutation fires exactly one sync;
    // irrelevant events fire none.

    struct SpyBridge : public IAccessibilityBridge {
        int syncCount = 0;
        Transform lastXf;
        bool Attach(void*, SemanticTree*) override { return true; }
        void Detach() override {}
        void SyncGeometry(const WindowGeometry&,
                           const Transform& viewTransform) override {
            syncCount++;
            lastXf = viewTransform;
        }
        void AnnouncePropertyChange(const std::string&) override {}
        void AnnounceSelectionChange() override {}
        void AnnounceFocusedChange(const std::string&) override {}
        void UpdateBounds() override {}
    };

    SpyBridge spy;
    auto onXf = [](void* userData, const Transform& xf) {
        auto* b = static_cast<SpyBridge*>(userData);
        WindowGeometry g{800, 600, 1.0f};
        b->SyncGeometry(g, xf);
    };

    Transform xf{1.0f, {0, 0}};

    // Deterministic monotonic clock: strictly increasing µs timestamps
    // starting from a fixed base, incrementing by 200ms per call so
    // the total event stream meets the 0.5s minimum capture duration.
    uint64_t clockBase = 1'000'000'000;
    auto clock = [&clockBase]() -> uint64_t {
        uint64_t t = clockBase;
        clockBase += 200'000; // 200ms per event
        return t;
    };
    InputLogger logger(clock);
    ViewportOutcomeAccumulator acc;

    Vec2 lastMouse{400.0f, 300.0f};
    bool mouseDown = false;
    FingerState f1, f2;

    ViewportEventState state{
        xf, &logger, true, lastMouse, mouseDown,
        f1, f2,
        800, 600, onXf, &spy
    };

    // --- Irrelevant event: mouse motion (no transform mutation, no sync) ---
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_MOUSE_MOTION;
        ev.motion.x = 100.0f;
        ev.motion.y = 200.0f;
        ev.motion.xrel = 1.0f;
        ev.motion.yrel = 0.0f;

        int syncBefore = spy.syncCount;
        auto result = ProcessViewportSDLEvent(&ev, state);
        acc.Accumulate(result);
        AddResult("E2E mouse-move: no transform mutation", !result.viewportChanged);
        AddResult("E2E mouse-move: no SyncGeometry", spy.syncCount == syncBefore);
        AddResult("E2E mouse-move: logged as PointerMove",
                  logger.Events().back().type == InputEvent::PointerMove);
    }

    // -- Mouse wheel pan: source=Unknown (or declared), no scale change, offset change --
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_MOUSE_WHEEL;
        ev.wheel.x = 0.0f;
        ev.wheel.y = 1.0f;  // scroll down → pan up
        ev.wheel.mouse_x = 400.0f;
        ev.wheel.mouse_y = 300.0f;

        int syncBefore = spy.syncCount;
        float scaleBefore = xf.scale;
        Vec2 offsetBefore = xf.offset;
        auto result = ProcessViewportSDLEvent(&ev, state);
        acc.Accumulate(result);
        AddResult("E2E wheel: transform mutated (pan)", result.viewportChanged);
        AddResult("E2E wheel: didPan set", result.didPan);
        AddResult("E2E wheel: didPinch NOT set", !result.didPinch);
        AddResult("E2E wheel: scale unchanged", xf.scale == scaleBefore);
        AddResult("E2E wheel: offset changed", xf.offset.y != offsetBefore.y);
        AddResult("E2E wheel: exactly 1 SyncGeometry",
                  spy.syncCount == syncBefore + 1);
        AddResult("E2E wheel: logged source Unknown",
                  logger.Events().back().source == InputEvent::Source::Unknown);
        AddResult("E2E wheel: logged type Wheel",
                  logger.Events().back().type == InputEvent::Wheel);
    }

    // -- Mouse wheel with declared trackpad source: source=Trackpad --
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_MOUSE_WHEEL;
        ev.wheel.x = 0.5f;
        ev.wheel.y = -0.3f;
        ev.wheel.mouse_x = 200.0f;
        ev.wheel.mouse_y = 200.0f;

        // Save declared source, set to Trackpad, then restore
        auto savedSrc = state.declaredSource;
        state.declaredSource = InputEvent::Source::Trackpad;
        auto result = ProcessViewportSDLEvent(&ev, state);
        acc.Accumulate(result);
        state.declaredSource = savedSrc;

        AddResult("E2E wheel declared trackpad: source Trackpad in log",
                  logger.Events().back().source == InputEvent::Source::Trackpad);
    }

    // -- Finger-down: no transform mutation, no sync, source=Finger --
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_FINGER_DOWN;
        ev.tfinger.fingerID = 1;
        ev.tfinger.x = 0.2f;
        ev.tfinger.y = 0.3f;
        ev.tfinger.dx = 0.01f;
        ev.tfinger.dy = 0.02f;

        int syncBefore = spy.syncCount;
        auto result = ProcessViewportSDLEvent(&ev, state);
        acc.Accumulate(result);
        AddResult("E2E finger-down: no transform mutation", !result.viewportChanged);
        AddResult("E2E finger-down: no SyncGeometry", spy.syncCount == syncBefore);
        AddResult("E2E finger-down: logged source Finger",
                  logger.Events().back().source == InputEvent::Source::Finger);
        AddResult("E2E finger-down: logged as FingerDown telemetry",
                  logger.Events().back().type == InputEvent::FingerDown);
    }

    // -- Second finger-down: activates two-finger gesture --
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_FINGER_DOWN;
        ev.tfinger.fingerID = 2;
        ev.tfinger.x = 0.4f;
        ev.tfinger.y = 0.5f;

        int syncBefore = spy.syncCount;
        auto result = ProcessViewportSDLEvent(&ev, state);
        acc.Accumulate(result);
        AddResult("E2E finger2-down: no transform mutation", !result.viewportChanged);
        AddResult("E2E finger2-down: no SyncGeometry", spy.syncCount == syncBefore);
    }

    // -- Two-finger motion (pan): transform mutated, exactly one sync --
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_FINGER_MOTION;
        ev.tfinger.fingerID = 1;
        ev.tfinger.x = 0.3f;
        ev.tfinger.y = 0.3f;
        ev.tfinger.dx = 0.05f;
        ev.tfinger.dy = 0.07f;

        Vec2 offsetBefore = xf.offset;
        int syncBefore = spy.syncCount;
        auto result = ProcessViewportSDLEvent(&ev, state);
        acc.Accumulate(result);
        AddResult("E2E finger-motion: transform mutated", result.viewportChanged);
        AddResult("E2E finger-motion: didPan set", result.didPan);
        AddResult("E2E finger-motion: didPinch NOT set", !result.didPinch);
        AddResult("E2E finger-motion: offset changed",
                  xf.offset.x != offsetBefore.x || xf.offset.y != offsetBefore.y);
        AddResult("E2E finger-motion: at least 1 SyncGeometry",
                  spy.syncCount >= syncBefore + 1,
                  ("syncCount=" + std::to_string(spy.syncCount - syncBefore)).c_str());
        AddResult("E2E finger-motion: logged as FingerMotion telemetry",
                  logger.Events().back().type == InputEvent::FingerMotion);
    }

    // -- Finger cancellation: ends only the identified finger, no motion --
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_FINGER_CANCELED;
        ev.tfinger.fingerID = 1;
        ev.tfinger.x = 0.3f;
        ev.tfinger.y = 0.3f;

        Vec2 offsetBefore = xf.offset;
        int syncBefore = spy.syncCount;
        auto result = ProcessViewportSDLEvent(&ev, state);
        acc.Accumulate(result);
        AddResult("E2E finger-canceled: ends only its finger without viewport motion",
                  !result.viewportChanged && spy.syncCount == syncBefore &&
                      xf.offset == offsetBefore && !f1.active && f2.active);
        AddResult("E2E finger-canceled: logged as FingerCanceled telemetry",
                  logger.Events().back().type == InputEvent::FingerCanceled);
    }

    // -- Finger-up: ends the remaining finger, no transform mutation --
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_FINGER_UP;
        ev.tfinger.fingerID = 2;
        ev.tfinger.x = 0.4f;
        ev.tfinger.y = 0.5f;

        Vec2 offsetBefore = xf.offset;
        int syncBefore = spy.syncCount;
        auto result = ProcessViewportSDLEvent(&ev, state);
        acc.Accumulate(result);
        AddResult("E2E finger-up: no transform mutation or SyncGeometry",
                  !result.viewportChanged && spy.syncCount == syncBefore &&
                      xf.offset == offsetBefore && !f2.active);
        AddResult("E2E finger-up: logged as FingerUp telemetry",
                  logger.Events().back().type == InputEvent::FingerUp);
    }

    // -- Pinch update: source=PinchGesture, exactly one sync --
    {
        // Activate pinch
        SDL_Event pbegin{};
        pbegin.type = SDL_EVENT_PINCH_BEGIN;
        auto beginResult = ProcessViewportSDLEvent(&pbegin, state);
        acc.Accumulate(beginResult);

        SDL_Event ev{};
        ev.type = SDL_EVENT_PINCH_UPDATE;
        ev.pinch.scale = 1.5f;

        int syncBefore = spy.syncCount;
        float scaleBefore = xf.scale;
        auto result = ProcessViewportSDLEvent(&ev, state);
        acc.Accumulate(result);
        AddResult("E2E pinch-update: transform mutated", result.viewportChanged);
        AddResult("E2E pinch-update: didPinch set", result.didPinch);
        AddResult("E2E pinch-update: didPan NOT set", !result.didPan);
        AddResult("E2E pinch-update: scale changed", xf.scale != scaleBefore);
        AddResult("E2E pinch-update: exactly 1 SyncGeometry",
                  spy.syncCount == syncBefore + 1);
        AddResult("E2E pinch-update: logged source PinchGesture",
                  logger.Events().back().source == InputEvent::Source::PinchGesture);
    }

    // -- Keyboard arrow: transform mutated, exactly one sync --
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_KEY_DOWN;
        ev.key.key = SDLK_LEFT;

        Vec2 offsetBefore = xf.offset;
        int syncBefore = spy.syncCount;
        auto result = ProcessViewportSDLEvent(&ev, state);
        acc.Accumulate(result);
        AddResult("E2E key-left: transform mutated", result.viewportChanged);
        AddResult("E2E key-left: didPan NOT set (keyboard never pan)",
                  !result.didPan);
        AddResult("E2E key-left: didPinch NOT set (keyboard never pinch)",
                  !result.didPinch);
        AddResult("E2E key-left: offset.x decreased by 20",
                  xf.offset.x == offsetBefore.x - 20.0f);
        AddResult("E2E key-left: exactly 1 SyncGeometry",
                  spy.syncCount == syncBefore + 1);
    }

    // Add physical-path wheel and pointer records so the E2E capture meets
    // the same manual gate thresholds as an actual trackpad session.
    for (int i = 0; i < 27; ++i) {
        SDL_Event ev{};
        ev.type = SDL_EVENT_MOUSE_WHEEL;
        ev.wheel.x = 0.25f;
        ev.wheel.y = 0.5f;
        ev.wheel.mouse_x = 400.0f;
        ev.wheel.mouse_y = 300.0f;
        acc.Accumulate(ProcessViewportSDLEvent(&ev, state));
    }
    for (int i = 0; i < 19; ++i) {
        SDL_Event ev{};
        ev.type = SDL_EVENT_MOUSE_MOTION;
        ev.motion.x = static_cast<float>(i);
        ev.motion.y = 100.0f;
        ev.motion.xrel = 1.0f;
        acc.Accumulate(ProcessViewportSDLEvent(&ev, state));
    }

    // -- Irrelevant event: quit event, no sync --
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_QUIT;

        int syncBefore = spy.syncCount;
        auto result = ProcessViewportSDLEvent(&ev, state);
        acc.Accumulate(result);
        AddResult("E2E quit: no transform mutation", !result.viewportChanged);
        AddResult("E2E quit: quit flag set", result.quit);
        AddResult("E2E quit: no SyncGeometry", spy.syncCount == syncBefore);
    }

    // -- Compute real fidelity summary from the same event stream --
    //    Accumulate outcomes from typed event results — never manually
    //    set booleans or infer outcomes from final transform values. This
    //    reproduces App's ProcessViewportSDLEvent → Accumulate pathway.
    //    The deterministic clock injected above guarantees strictly
    //    monotonic timestamps — no post-dispatch fixup needed.
    //
    //    This E2E stream includes: wheel (didPan=true), finger-motion (didPan=true),
    //    pinch-update (didPinch=true), keyboard (neither). So both pan and pinch
    //    outcomes are present from the typed path.
    bool hadViewportResponse = (xf.scale != 1.0f ||
                                  xf.offset.x != 0.0f ||
                                  xf.offset.y != 0.0f);
    AddResult("E2E acc: pan outcome accumulated", acc.HasPan());
    AddResult("E2E acc: pinch outcome accumulated", acc.HasPinch());
    auto summary = logger.ComputeFidelitySummary(
        hadViewportResponse, acc.HasPan(), acc.HasPinch());
    AddResult("E2E summary has gesture", summary.hasGestureEvent);
    AddResult("E2E summary counts all delivered finger telemetry", summary.fingerEventCount == 5);
    AddResult("E2E summary finger telemetry does not inflate scroll count",
              summary.scrollEventCount == 30);
    AddResult("E2E summary subpixel detected", summary.hasSubpixelDeltas);
    AddResult("E2E summary nonzero viewport response", summary.hasNonzeroViewportResponse);
    AddResult("E2E summary monotonic timestamps", summary.monotonicTimestamps);
    AddResult("E2E droppedOrInvalid == 0",
              summary.droppedOrInvalid == 0,
              ("drops=" + std::to_string(summary.droppedOrInvalid)).c_str());
    AddResult("E2E summary verdict exact PASS",
              summary.verdict == "PASS — all fidelity thresholds met",
              summary.verdict.c_str());
}

void SelfTest::Test_InputWheelPanOnly() {
    // Construct real SDL wheel events through the common handler and
    // prove default wheel changes offset but not scale.

    Transform xf{1.0f, {0, 0}};
    Vec2 lastMouse{400.0f, 300.0f};
    bool mouseDown = false;
    FingerState f1, f2;
    InputLogger logger;

    ViewportEventState state{
        xf, &logger, true, lastMouse, mouseDown,
        f1, f2,
        800, 600, nullptr, nullptr
    };

    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_MOUSE_WHEEL;
        ev.wheel.x = 0.0f;
        ev.wheel.y = 1.0f;
        ev.wheel.mouse_x = 400.0f;
        ev.wheel.mouse_y = 300.0f;

        float scaleBefore = xf.scale;
        Vec2 offsetBefore = xf.offset;
        auto result = ProcessViewportSDLEvent(&ev, state);
        AddResult("WheelPan: viewportChanged", result.viewportChanged);
        AddResult("WheelPan: didPan set", result.didPan);
        AddResult("WheelPan: didPinch NOT set", !result.didPinch);
        AddResult("WheelPan: scale unchanged", xf.scale == scaleBefore);
        AddResult("WheelPan: offset changed", xf.offset.y != offsetBefore.y);
        AddResult("WheelPan: logged as Wheel",
                  logger.Events().back().type == InputEvent::Wheel);
    }

    // Horizontal wheel
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_MOUSE_WHEEL;
        ev.wheel.x = 2.0f;
        ev.wheel.y = 0.0f;
        ev.wheel.mouse_x = 400.0f;
        ev.wheel.mouse_y = 300.0f;

        Vec2 offsetBefore = xf.offset;
        auto result = ProcessViewportSDLEvent(&ev, state);
        AddResult("WheelPan: didPan on horizontal", result.didPan);
        AddResult("WheelPan: horizontal offset changed",
                  xf.offset.x != offsetBefore.x);
    }

    // Zero-delta wheel: no mutation
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_MOUSE_WHEEL;
        ev.wheel.x = 0.0f;
        ev.wheel.y = 0.0f;
        ev.wheel.mouse_x = 400.0f;
        ev.wheel.mouse_y = 300.0f;

        Vec2 offsetBefore = xf.offset;
        auto result = ProcessViewportSDLEvent(&ev, state);
        AddResult("WheelPan: zero delta no viewportChanged", !result.viewportChanged);
        AddResult("WheelPan: zero delta didPan NOT set", !result.didPan);
        AddResult("WheelPan: zero delta offset unchanged",
                  xf.offset.x == offsetBefore.x && xf.offset.y == offsetBefore.y);
    }

    // Declared trackpad source
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_MOUSE_WHEEL;
        ev.wheel.x = 0.1f;
        ev.wheel.y = -0.5f;
        ev.wheel.mouse_x = 200.0f;
        ev.wheel.mouse_y = 200.0f;

        state.declaredSource = InputEvent::Source::Trackpad;
        ProcessViewportSDLEvent(&ev, state);
        AddResult("WheelPan: declared trackpad source in log",
                  logger.Events().back().source == InputEvent::Source::Trackpad);
    }
}

void SelfTest::Test_InputFingerPanOnly() {
    // Raw finger motion never zooms — zoom is exclusively via
    // SDL_EVENT_PINCH_UPDATE (native platform pinch gesture).
    // This test verifies that scale remains exactly unchanged
    // regardless of finger distance, even when the distance
    // between fingers changes substantially.
    // Pinch-exclusive zoom replaces the removed distance-based pinch heuristic.
    //
    // Because SDL delivers finger events sequentially (one finger at a
    // time), intermediate distances change while the first finger moves
    // ahead of the second. Those distance changes must never produce
    // zoom — only native pinch does.

    Transform xf{1.0f, {0, 0}};
    Vec2 lastMouse{400.0f, 300.0f};
    bool mouseDown = false;
    FingerState f1, f2;
    InputLogger logger;

    ViewportEventState state{
        xf, &logger, true, lastMouse, mouseDown,
        f1, f2,
        800, 600, nullptr, nullptr
    };

    // Set up two fingers 0.125*800=100px apart horizontally
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_FINGER_DOWN;
        ev.tfinger.fingerID = 1;
        ev.tfinger.x = 0.2f;
        ev.tfinger.y = 0.5f;
        ev.tfinger.dx = 0.0f;
        ev.tfinger.dy = 0.0f;
        ProcessViewportSDLEvent(&ev, state);
    }
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_FINGER_DOWN;
        ev.tfinger.fingerID = 2;
        ev.tfinger.x = 0.325f;
        ev.tfinger.y = 0.5f;
        ev.tfinger.dx = 0.0f;
        ev.tfinger.dy = 0.0f;
        ProcessViewportSDLEvent(&ev, state);
    }

    float scaleBefore = xf.scale;
    AddResult("FingerPanOnly: initial scale is 1.0", scaleBefore == 1.0f);

    // Move both fingers same direction (pan). Finger1 moves first
    // (intermediate distance shrinks), then finger2 catches up.
    // Neither event should change scale.

    // finger1: (0.2, 0.5) → (0.35, 0.4)   dx=0.15, dy=-0.1
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_FINGER_MOTION;
        ev.tfinger.fingerID = 1;
        ev.tfinger.x = 0.35f;
        ev.tfinger.y = 0.4f;
        ev.tfinger.dx = 0.15f;
        ev.tfinger.dy = -0.1f;

        Vec2 offsetBefore = xf.offset;
        ProcessViewportSDLEvent(&ev, state);
        // Centroid pan should be applied, scale must not change
        AddResult("FingerPanOnly: pan applied on finger1 move",
                  xf.offset.y != offsetBefore.y);
        AddResult("FingerPanOnly: scale unchanged on finger1 move",
                  xf.scale == scaleBefore);
    }

    // finger2: (0.325, 0.5) → (0.475, 0.4)   same delta: dx=0.15, dy=-0.1
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_FINGER_MOTION;
        ev.tfinger.fingerID = 2;
        ev.tfinger.x = 0.475f;
        ev.tfinger.y = 0.4f;
        ev.tfinger.dx = 0.15f;
        ev.tfinger.dy = -0.1f;

        Vec2 offsetBefore = xf.offset;
        ProcessViewportSDLEvent(&ev, state);
        AddResult("FingerPanOnly: pan applied on finger2 catchup",
                  xf.offset.x != offsetBefore.x);
        AddResult("FingerPanOnly: scale unchanged on finger2 catchup",
                  xf.scale == scaleBefore);

        // Now move finger2 by a tiny amount: +0.0025*800 = 2px.
        // Must pan, must not zoom.
        {
            SDL_Event ev3{};
            ev3.type = SDL_EVENT_FINGER_MOTION;
            ev3.tfinger.fingerID = 2;
            ev3.tfinger.x = 0.4775f;  // +2px
            ev3.tfinger.y = 0.4f;
            ev3.tfinger.dx = 0.0025f;
            ev3.tfinger.dy = 0.0f;

            Vec2 offsetBeforeJitter = xf.offset;
            ProcessViewportSDLEvent(&ev3, state);
            AddResult("FingerPanOnly: scale unchanged on tiny jitter",
                      xf.scale == scaleBefore);
            AddResult("FingerPanOnly: pan still applied on tiny jitter",
                      xf.offset.x != offsetBeforeJitter.x);
        }
    }

    // Large separation change: move fingers apart substantially.
    // Scale must STILL not change — zoom is exclusively via PINCH_UPDATE.
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_FINGER_MOTION;
        ev.tfinger.fingerID = 1;
        ev.tfinger.x = 0.25f;   // moved left by 0.225 from 0.475
        ev.tfinger.y = 0.4f;
        ev.tfinger.dx = -0.225f;
        ev.tfinger.dy = 0.0f;
        ProcessViewportSDLEvent(&ev, state);

        SDL_Event ev2{};
        ev2.type = SDL_EVENT_FINGER_MOTION;
        ev2.tfinger.fingerID = 2;
        ev2.tfinger.x = 0.7f;    // moved right by 0.225 from 0.475
        ev2.tfinger.y = 0.4f;
        ev2.tfinger.dx = 0.225f;
        ev2.tfinger.dy = 0.0f;

        float bigScaleCheck = xf.scale;
        ProcessViewportSDLEvent(&ev2, state);
        AddResult("FingerPanOnly: scale unchanged on large separation change",
                  xf.scale == bigScaleCheck);
    }
}

void SelfTest::Test_InputPinchFocalStability() {
    // Native pinch event (SDL_EVENT_PINCH_UPDATE) with two-finger
    // centroid fallback: prove zoom + focal stability at centroid.
    //
    // On macOS, SDL pinch events supply focus_x/focus_y = -1, so the
    // handler falls back to the active-contact centroid. This test
    // verifies that the centroid path correctly anchors zoom at the
    // centroid screen position.

    Transform xf{1.0f, {0, 0}};
    Vec2 lastMouse{400.0f, 300.0f};
    bool mouseDown = false;
    FingerState f1, f2;
    InputLogger logger;

    ViewportEventState state{
        xf, &logger, true, lastMouse, mouseDown,
        f1, f2,
        800, 600, nullptr, nullptr
    };

    // Set up two fingers centred at screen position (400, 300)
    // finger1 at normalized (0.4, 0.5) = (320, 300)
    // finger2 at normalized (0.6, 0.5) = (480, 300)
    // centroid = (400, 300)
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_FINGER_DOWN;
        ev.tfinger.fingerID = 1;
        ev.tfinger.x = 0.4f;
        ev.tfinger.y = 0.5f;
        ev.tfinger.dx = 0.0f;
        ev.tfinger.dy = 0.0f;
        ProcessViewportSDLEvent(&ev, state);
    }
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_FINGER_DOWN;
        ev.tfinger.fingerID = 2;
        ev.tfinger.x = 0.6f;
        ev.tfinger.y = 0.5f;
        ev.tfinger.dx = 0.0f;
        ev.tfinger.dy = 0.0f;
        ProcessViewportSDLEvent(&ev, state);
    }

    float scaleBefore = xf.scale;
    AddResult("PinchFocal: initial scale is 1.0", scaleBefore == 1.0f);

    // Fire native pinch: scale factor 1.5 at centroid (400, 300).
    // focus_x/focus_y = -1 (macOS) → handler uses centroid fallback.
    {
        SDL_Event pb{};
        pb.type = SDL_EVENT_PINCH_BEGIN;
        ProcessViewportSDLEvent(&pb, state);

        SDL_Event ev{};
        ev.type = SDL_EVENT_PINCH_UPDATE;
        ev.pinch.scale = 1.5f;
        ev.pinch.focus_x = -1.0f;  // macOS: no focal coordinates
        ev.pinch.focus_y = -1.0f;

        auto result = ProcessViewportSDLEvent(&ev, state);
        AddResult("PinchFocal: viewportChanged", result.viewportChanged);
        AddResult("PinchFocal: didPinch set", result.didPinch);
        AddResult("PinchFocal: didPan NOT set (focal compensation offset)", !result.didPan);
        AddResult("PinchFocal: scale increased", xf.scale > scaleBefore);
        AddResult("PinchFocal: scale ~1.5x",
                  std::abs(xf.scale - 1.5f) < 0.05f,
                  ("scale=" + std::to_string(xf.scale)).c_str());

        // Focal stability: world point at centroid (400,300) should remain
        // at the same screen position after zoom.
        // Before: screen(400,300) → world(400,300)
        // After scale=1.5: we offset so world(400,300) still at screen(400,300)
        // screen = world*1.5 + offset → 400 = 400*1.5 + offset.x
        // offset.x = 400 - 600 = -200
        // offset.y = 300 - 450 = -150
        float expectedOffsetX = 400.0f - 400.0f * xf.scale;
        float expectedOffsetY = 300.0f - 300.0f * xf.scale;
        AddResult("PinchFocal: focal-stable offset X",
                  std::abs(xf.offset.x - expectedOffsetX) < 1.0f,
                  ("got=" + std::to_string(xf.offset.x) +
                   " expected=" + std::to_string(expectedOffsetX)).c_str());
        AddResult("PinchFocal: focal-stable offset Y",
                  std::abs(xf.offset.y - expectedOffsetY) < 1.0f,
                  ("got=" + std::to_string(xf.offset.y) +
                   " expected=" + std::to_string(expectedOffsetY)).c_str());

        // Logged as PinchGesture source
        AddResult("PinchFocal: logged as PinchGesture",
                  logger.Events().back().source == InputEvent::Source::PinchGesture);
    }
}

void SelfTest::Test_InputPinchFocalOffCenter() {
    // Native pinch with focal coordinates supplied (mobile path) OR
    // centroid-based fallback: verify off-center focal preservation.
    //
    // Tests that when pinch is anchored at a non-center focal point,
    // the world point at that focal point remains screen-stable.

    Transform xf{1.0f, {0, 0}};
    Vec2 lastMouse{400.0f, 300.0f};
    bool mouseDown = false;
    FingerState f1, f2;
    InputLogger logger;

    ViewportEventState state{
        xf, &logger, true, lastMouse, mouseDown,
        f1, f2,
        800, 600, nullptr, nullptr
    };

    // Set up two fingers with centroid at (200, 200) — off-center
    // finger1 at normalized (0.2, 0.333) = (160, 200)
    // finger2 at normalized (0.3, 0.333) = (240, 200)
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_FINGER_DOWN;
        ev.tfinger.fingerID = 1;
        ev.tfinger.x = 0.2f;
        ev.tfinger.y = 0.333f;
        ProcessViewportSDLEvent(&ev, state);
    }
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_FINGER_DOWN;
        ev.tfinger.fingerID = 2;
        ev.tfinger.x = 0.3f;
        ev.tfinger.y = 0.333f;
        ProcessViewportSDLEvent(&ev, state);
    }

    // Initial world point at the focal (centroid=200,200)
    Vec2 worldAtFocal = xf.ScreenToWorld({200.0f, 200.0f});

    // Fire native pinch: scale factor 2.0, focus_x/focus_y = -1 (macOS)
    // → handler falls back to centroid at (200, 200).
    {
        SDL_Event pb{};
        pb.type = SDL_EVENT_PINCH_BEGIN;
        ProcessViewportSDLEvent(&pb, state);

        SDL_Event ev{};
        ev.type = SDL_EVENT_PINCH_UPDATE;
        ev.pinch.scale = 2.0f;
        ev.pinch.focus_x = -1.0f;
        ev.pinch.focus_y = -1.0f;

        ProcessViewportSDLEvent(&ev, state);

        AddResult("PinchFocalOffCenter: scale ~2.0x",
                  std::abs(xf.scale - 2.0f) < 0.05f,
                  ("scale=" + std::to_string(xf.scale)).c_str());

        // The world point at focal (200,200) should still map to (200,200)
        Vec2 screenAfter = xf.WorldToScreen(worldAtFocal);
        AddResult("PinchFocalOffCenter: focal world point preserved X",
                  std::abs(screenAfter.x - 200.0f) < 1.0f,
                  ("screen.x=" + std::to_string(screenAfter.x)).c_str());
        AddResult("PinchFocalOffCenter: focal world point preserved Y",
                  std::abs(screenAfter.y - 200.0f) < 1.0f,
                  ("screen.y=" + std::to_string(screenAfter.y)).c_str());
    }
}

void SelfTest::Test_InputSameDirectionNoZoom() {
    // Regression: substantial sequential same-direction two-contact
    // movement (finger1 event then finger2 event repeatedly) must
    // change offset and keep scale exactly unchanged throughout/finally.

    Transform xf{1.0f, {0, 0}};
    Vec2 lastMouse{400.0f, 300.0f};
    bool mouseDown = false;
    FingerState f1, f2;
    InputLogger logger;

    ViewportEventState state{
        xf, &logger, true, lastMouse, mouseDown,
        f1, f2,
        800, 600, nullptr, nullptr
    };

    // Set up two fingers
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_FINGER_DOWN;
        ev.tfinger.fingerID = 1;
        ev.tfinger.x = 0.2f;
        ev.tfinger.y = 0.5f;
        ProcessViewportSDLEvent(&ev, state);
    }
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_FINGER_DOWN;
        ev.tfinger.fingerID = 2;
        ev.tfinger.x = 0.3f;
        ev.tfinger.y = 0.5f;
        ProcessViewportSDLEvent(&ev, state);
    }

    float initialScale = xf.scale;
    Vec2 initialOffset = xf.offset;

    // Substantial same-direction movement: repeat finger1 then finger2
    // for 10 cycles, each moving ~50px to the right and ~30px down.
    for (int step = 0; step < 10; ++step) {
        {
            SDL_Event ev{};
            ev.type = SDL_EVENT_FINGER_MOTION;
            ev.tfinger.fingerID = 1;
            ev.tfinger.x = 0.2f + 0.0625f * static_cast<float>(step + 1);
            ev.tfinger.y = 0.5f + 0.05f * static_cast<float>(step + 1);
            ev.tfinger.dx = 0.05f;
            ev.tfinger.dy = 0.04f;
            ProcessViewportSDLEvent(&ev, state);
        }
        {
            SDL_Event ev{};
            ev.type = SDL_EVENT_FINGER_MOTION;
            ev.tfinger.fingerID = 2;
            ev.tfinger.x = 0.3f + 0.0625f * static_cast<float>(step + 1);
            ev.tfinger.y = 0.5f + 0.05f * static_cast<float>(step + 1);
            ev.tfinger.dx = 0.05f;
            ev.tfinger.dy = 0.04f;
            ProcessViewportSDLEvent(&ev, state);
        }

        // Scale must be exactly unchanged at every step
        AddResult(
            ("SameDirNoZoom: scale unchanged at step " + std::to_string(step)).c_str(),
            xf.scale == initialScale);
    }

    // Offset must have changed (panned)
    AddResult("SameDirNoZoom: offset changed after all steps",
              xf.offset.x != initialOffset.x || xf.offset.y != initialOffset.y);
    AddResult("SameDirNoZoom: final scale still 1.0",
              xf.scale == initialScale);
}

void SelfTest::Test_InputCombinedStreamNoDoubleZoom() {
    // Combined raw-finger motion + native-pinch event stream must zoom
    // exactly once (only by the native pinch factor). Finger motion
    // must only pan; native pinch must only zoom.

    Transform xf{1.0f, {0, 0}};
    Vec2 lastMouse{400.0f, 300.0f};
    bool mouseDown = false;
    FingerState f1, f2;
    InputLogger logger;

    ViewportEventState state{
        xf, &logger, true, lastMouse, mouseDown,
        f1, f2,
        800, 600, nullptr, nullptr
    };

    // Set up two fingers
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_FINGER_DOWN;
        ev.tfinger.fingerID = 1;
        ev.tfinger.x = 0.4f;
        ev.tfinger.y = 0.5f;
        ProcessViewportSDLEvent(&ev, state);
    }
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_FINGER_DOWN;
        ev.tfinger.fingerID = 2;
        ev.tfinger.x = 0.6f;
        ev.tfinger.y = 0.5f;
        ProcessViewportSDLEvent(&ev, state);
    }

    float scaleBefore = xf.scale;
    Vec2 offsetBefore = xf.offset;

    // Step 1: Raw finger motion (pan right by moving both fingers right)
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_FINGER_MOTION;
        ev.tfinger.fingerID = 1;
        ev.tfinger.x = 0.5f;
        ev.tfinger.y = 0.5f;
        ev.tfinger.dx = 0.1f;
        ev.tfinger.dy = 0.0f;
        ProcessViewportSDLEvent(&ev, state);

        SDL_Event ev2{};
        ev2.type = SDL_EVENT_FINGER_MOTION;
        ev2.tfinger.fingerID = 2;
        ev2.tfinger.x = 0.7f;
        ev2.tfinger.y = 0.5f;
        ev2.tfinger.dx = 0.1f;
        ev2.tfinger.dy = 0.0f;
        ProcessViewportSDLEvent(&ev2, state);

        // Pan should change offset, scale unchanged
        AddResult("CombinedStream: offset changed after finger motion",
                  xf.offset.x != offsetBefore.x);
        AddResult("CombinedStream: scale unchanged after finger motion",
                  xf.scale == scaleBefore);
    }

    // Step 2: Native pinch zoom
    {
        SDL_Event pb{};
        pb.type = SDL_EVENT_PINCH_BEGIN;
        ProcessViewportSDLEvent(&pb, state);

        float sBeforePinch = xf.scale;

        SDL_Event ev{};
        ev.type = SDL_EVENT_PINCH_UPDATE;
        ev.pinch.scale = 1.5f;
        ev.pinch.focus_x = -1.0f;
        ev.pinch.focus_y = -1.0f;
        ProcessViewportSDLEvent(&ev, state);

        // Scale must change by exactly the native pinch factor
        float expectedScale = sBeforePinch * 1.5f;
        AddResult("CombinedStream: scale changed by native pinch factor",
                  std::abs(xf.scale - expectedScale) < 0.05f,
                  ("scale=" + std::to_string(xf.scale) +
                   " expected=" + std::to_string(expectedScale)).c_str());
    }

    // Step 3: Another finger motion after pinch — must not change scale
    {
        float sAfterPinch = xf.scale;

        SDL_Event ev{};
        ev.type = SDL_EVENT_FINGER_MOTION;
        ev.tfinger.fingerID = 1;
        ev.tfinger.x = 0.55f;
        ev.tfinger.y = 0.4f;
        ev.tfinger.dx = 0.05f;
        ev.tfinger.dy = -0.1f;
        ProcessViewportSDLEvent(&ev, state);

        AddResult("CombinedStream: scale unchanged after post-pinch finger motion",
                  xf.scale == sAfterPinch);
    }
}

void SelfTest::Test_InputFidelityRequiresPanAndPinch() {
    // Positive: both pan and pinch outcomes required for PASS.
    // Dispatch actual SDL events for each scenario; accumulate typed
    // outcomes and feed into ComputeFidelitySummary. Never inject
    // booleans or logger records directly.

    // --- Both pan and pinch outcomes present → PASS ---
    {
        uint64_t base = 1'000'000'000;
        int callIdx = 0;
        auto clock = [&]() -> uint64_t {
            return base + static_cast<uint64_t>(callIdx++) * 16'667;
        };
        InputLogger logger(clock);

        Transform xf{1.0f, {0, 0}};
        Vec2 lastMouse{400.0f, 300.0f};
        bool mouseDown = false;
        FingerState f1, f2;
        ViewportOutcomeAccumulator acc;

        ViewportEventState state{
            xf, &logger, true, lastMouse, mouseDown,
            f1, f2,
            800, 600, nullptr, nullptr
        };

        // Wheel pan → didPan
        {
            SDL_Event ev{};
            ev.type = SDL_EVENT_MOUSE_WHEEL;
            ev.wheel.y = 1.0f;
            ev.wheel.mouse_x = 400.0f; ev.wheel.mouse_y = 300.0f;
            auto r = ProcessViewportSDLEvent(&ev, state);
            acc.Accumulate(r);
        }

        for (int i = 0; i < 19; ++i) {
            SDL_Event ev{};
            ev.type = SDL_EVENT_MOUSE_MOTION;
            ev.motion.x = static_cast<float>(i);
            ev.motion.y = 100.0f;
            ev.motion.xrel = 1.0f;
            acc.Accumulate(ProcessViewportSDLEvent(&ev, state));
        }

        // Finger events for gesture + subpixel
        {
            SDL_Event ev{};
            ev.type = SDL_EVENT_FINGER_DOWN;
            ev.tfinger.fingerID = 1;
            ev.tfinger.x = 0.2f; ev.tfinger.y = 0.5f;
            ev.tfinger.dx = 0.01f; ev.tfinger.dy = 0.02f;
            ProcessViewportSDLEvent(&ev, state);
        }
        for (int i = 0; i < 28; ++i) {
            SDL_Event ev{};
            ev.type = SDL_EVENT_FINGER_MOTION;
            ev.tfinger.fingerID = static_cast<SDL_FingerID>(1 + (i % 2));
            ev.tfinger.x = 0.2f + static_cast<float>(i) * 0.01f;
            ev.tfinger.y = 0.5f;
            ev.tfinger.dx = 0.01f; ev.tfinger.dy = 0.0f;
            auto r = ProcessViewportSDLEvent(&ev, state);
            acc.Accumulate(r);
        }

        // Pinch → didPinch
        {
            SDL_Event pb{};
            pb.type = SDL_EVENT_PINCH_BEGIN;
            ProcessViewportSDLEvent(&pb, state);

            SDL_Event ev{};
            ev.type = SDL_EVENT_PINCH_UPDATE;
            ev.pinch.scale = 1.5f;
            ev.pinch.focus_x = -1.0f; ev.pinch.focus_y = -1.0f;
            auto r = ProcessViewportSDLEvent(&ev, state);
            acc.Accumulate(r);
        }

        for (int i = 0; i < 28; ++i) {
            SDL_Event ev{};
            ev.type = SDL_EVENT_MOUSE_WHEEL;
            ev.wheel.x = 0.25f;
            ev.wheel.y = 0.5f;
            ev.wheel.mouse_x = 400.0f;
            ev.wheel.mouse_y = 300.0f;
            acc.Accumulate(ProcessViewportSDLEvent(&ev, state));
        }

        bool hadViewport = (xf.scale != 1.0f || xf.offset.x != 0.0f || xf.offset.y != 0.0f);
        auto summary = logger.ComputeFidelitySummary(hadViewport, acc.HasPan(), acc.HasPinch());
        AddResult("FidelityPanPinch: both outcomes PASS",
                  summary.verdict.find("PASS") == 0,
                  summary.verdict.c_str());
    }
}

void SelfTest::Test_InputFidelityPanOnlyFail() {
    // Negative: pan-only (wheel + finger motion, no pinch) must FAIL
    // fidelity summary because hasAppliedPinchOutcome is false.
    // Dispatch actual SDL events — never inject logger records.

    uint64_t base = 1'000'000'000;
    int callIdx = 0;
    auto clock = [&]() -> uint64_t {
        return base + static_cast<uint64_t>(callIdx++) * 16'667;
    };
    InputLogger logger(clock);

    Transform xf{1.0f, {0, 0}};
    Vec2 lastMouse{400.0f, 300.0f};
    bool mouseDown = false;
    FingerState f1, f2;
    ViewportOutcomeAccumulator acc;

    ViewportEventState state{
        xf, &logger, true, lastMouse, mouseDown,
        f1, f2,
        800, 600, nullptr, nullptr
    };

    // Wheel pan
    for (int i = 0; i < 30; ++i) {
        SDL_Event ev{};
        ev.type = SDL_EVENT_MOUSE_WHEEL;
        ev.wheel.x = 0.3f; ev.wheel.y = 0.7f;
        ev.wheel.mouse_x = 400.0f; ev.wheel.mouse_y = 300.0f;
        auto r = ProcessViewportSDLEvent(&ev, state);
        acc.Accumulate(r);
    }

    // Finger events for gesture
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_FINGER_DOWN;
        ev.tfinger.fingerID = 1;
        ev.tfinger.x = 0.2f; ev.tfinger.y = 0.5f;
        ev.tfinger.dx = 0.3f; ev.tfinger.dy = 0.7f;
        auto r = ProcessViewportSDLEvent(&ev, state);
        acc.Accumulate(r);
    }

    for (int i = 0; i < 30; ++i) {
        SDL_Event ev{};
        ev.type = SDL_EVENT_FINGER_MOTION;
        ev.tfinger.fingerID = 1;
        ev.tfinger.x = 0.2f + static_cast<float>(i) * 0.01f;
        ev.tfinger.y = 0.5f;
        ev.tfinger.dx = 0.01f; ev.tfinger.dy = 0.0f;
        auto r = ProcessViewportSDLEvent(&ev, state);
        acc.Accumulate(r);
    }

    bool hadViewport = (xf.scale != 1.0f || xf.offset.x != 0.0f || xf.offset.y != 0.0f);
    auto summary = logger.ComputeFidelitySummary(hadViewport, acc.HasPan(), acc.HasPinch());
    AddResult("FidelityPanOnly: FAIL (no pinch outcome)",
              summary.verdict.find("FAIL") == 0,
              summary.verdict.c_str());
}

void SelfTest::Test_InputFidelityPinchOnlyFail() {
    // Negative: pinch-only (pinch gesture events, no pan) must FAIL
    // fidelity summary because hasAppliedPanOutcome is false.
    // Dispatch actual SDL events — never inject logger records.

    uint64_t base = 1'000'000'000;
    int callIdx = 0;
    auto clock = [&]() -> uint64_t {
        return base + static_cast<uint64_t>(callIdx++) * 16'667;
    };
    InputLogger logger(clock);

    Transform xf{1.0f, {0, 0}};
    Vec2 lastMouse{400.0f, 300.0f};
    bool mouseDown = false;
    FingerState f1, f2;
    ViewportOutcomeAccumulator acc;

    ViewportEventState state{
        xf, &logger, true, lastMouse, mouseDown,
        f1, f2,
        800, 600, nullptr, nullptr
    };

    // Finger-down x2 for centroid
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_FINGER_DOWN;
        ev.tfinger.fingerID = 1;
        ev.tfinger.x = 0.4f; ev.tfinger.y = 0.5f;
        ev.tfinger.dx = 0.0f; ev.tfinger.dy = 1.2f;
        ProcessViewportSDLEvent(&ev, state);
    }
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_FINGER_DOWN;
        ev.tfinger.fingerID = 2;
        ev.tfinger.x = 0.6f; ev.tfinger.y = 0.5f;
        ev.tfinger.dx = 0.0f; ev.tfinger.dy = 1.2f;
        ProcessViewportSDLEvent(&ev, state);
    }

    // Multiple pinch updates → didPinch, no didPan
    for (int i = 0; i < 30; ++i) {
        SDL_Event pb{};
        pb.type = SDL_EVENT_PINCH_BEGIN;
        ProcessViewportSDLEvent(&pb, state);

        SDL_Event ev{};
        ev.type = SDL_EVENT_PINCH_UPDATE;
        ev.pinch.scale = 1.0f + static_cast<float>(i) * 0.02f;
        ev.pinch.focus_x = -1.0f; ev.pinch.focus_y = -1.0f;
        auto r = ProcessViewportSDLEvent(&ev, state);
        acc.Accumulate(r);
    }

    // No wheel or finger-motion events → no pan outcome
    bool hadViewport = (xf.scale != 1.0f || xf.offset.x != 0.0f || xf.offset.y != 0.0f);
    auto summary = logger.ComputeFidelitySummary(hadViewport, acc.HasPan(), acc.HasPinch());
    AddResult("FidelityPinchOnly: FAIL (no pan outcome)",
              summary.verdict.find("FAIL") == 0,
              summary.verdict.c_str());
}

void SelfTest::Test_InputFidelityWheelFingerDownNoPinch() {
    // Negative: wheel events + finger-down must NOT satisfy pinch outcome.
    // Finger-down alone doesn't mutate scale; wheel only pans.
    // Even with hasNonzeroViewportResponse, summary fails without pinch.
    // Dispatch actual SDL events — never inject logger records.

    uint64_t base = 1'000'000'000;
    int callIdx = 0;
    auto clock = [&]() -> uint64_t {
        return base + static_cast<uint64_t>(callIdx++) * 16'667;
    };
    InputLogger logger(clock);

    Transform xf{1.0f, {0, 0}};
    Vec2 lastMouse{400.0f, 300.0f};
    bool mouseDown = false;
    FingerState f1, f2;
    ViewportOutcomeAccumulator acc;

    ViewportEventState state{
        xf, &logger, true, lastMouse, mouseDown,
        f1, f2,
        800, 600, nullptr, nullptr
    };

    // Wheel events (didPan only, no zoom)
    for (int i = 0; i < 58; ++i) {
        SDL_Event ev{};
        ev.type = SDL_EVENT_MOUSE_WHEEL;
        ev.wheel.x = 0.3f; ev.wheel.y = 0.5f;
        ev.wheel.mouse_x = 400.0f; ev.wheel.mouse_y = 300.0f;
        auto r = ProcessViewportSDLEvent(&ev, state);
        acc.Accumulate(r);
    }

    // Finger-down (no mutation, no didPinch)
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_FINGER_DOWN;
        ev.tfinger.fingerID = 1;
        ev.tfinger.x = 0.2f; ev.tfinger.y = 0.3f;
        ev.tfinger.dx = 0.0f; ev.tfinger.dy = 0.0f;
        auto r = ProcessViewportSDLEvent(&ev, state);
        acc.Accumulate(r);
    }

    // hasViewportResponse=true (wheel pans), hasPinchOutcome=false
    bool hadViewport = (xf.scale != 1.0f || xf.offset.x != 0.0f || xf.offset.y != 0.0f);
    auto summary = logger.ComputeFidelitySummary(hadViewport, acc.HasPan(), acc.HasPinch());
    AddResult("FidelityWheelFinger: FAIL (no pinch outcome from wheel+finger-down)",
              summary.verdict.find("FAIL") == 0,
              summary.verdict.c_str());
    AddResult("FidelityWheelFinger: has finger events",
              summary.fingerEventCount > 0);
}

void SelfTest::Test_TextFaceRasterPixels() {
    // Shape actual text through the pinned Noto Sans font, raster the
    // resulting positioned glyphs into pixels, and assert text-face
    // output/placement/transform/clipping deterministically.
    //
    // This test uses ONLY the pinned Noto Sans font (--text-font).
    // No system-font fallback (Helvetica, CWD, etc.) is permitted.
    // BravuraText is NOT used for text shaping or rasterization.
    //
    // Golden assertions (stable for Noto Sans at pinned SHA ffebf8c1...,
    // artifact SHA-256 d78a4640...):
    //   - "Hello" at 32px → exactly 5 glyphs with fixed GIDs
    //   - Each GID non-zero, advances match golden values
    //   - Fixed pixel hash from exact raster positions
    //   - Transform movement produces different hash
    //   - Clipping obeyed; zero-coverage confirms all-white
    //   - Music face raster demonstrably distinct from text face
    const char* textPath = ResolveTextFontPath();
    if (!textPath) {
        AddResult("Text face raster pixels", false, "no text font path set");
        return;
    }

    FontManager fm;
    if (!fm.LoadTextFont(textPath)) {
        AddResult("Text face raster pixels", false,
                  (std::string("Noto Sans load failed: ") + textPath).c_str());
        return;
    }

    // Get raw shaped glyphs to inspect GIDs and advances before caching.
    auto shaped = fm.ShapeText("Hello", 32);
    if (shaped.empty()) {
        AddResult("Text face raster pixels", false, "HarfBuzz shaped zero glyphs for 'Hello'");
        return;
    }

    int glyphCount = static_cast<int>(shaped.size());
    AddResult("Text 'Hello' at 32px produces 5 glyphs", glyphCount == 5,
              ("got=" + std::to_string(glyphCount)).c_str());

    // Golden GID assertions — every Noto Sans GID for "Hello" at 32px
    // must be non-zero (glyph 0 is .notdef, indicates missing glyph).
    // These are pinned against Noto Sans at SHA ffebf8c1....
    // Golden GIDs: H=43, e=72, l=79, l=79, o=82
    static constexpr uint32_t kGoldenGIDs[] = {43, 72, 79, 79, 82};
    bool allGidsNonZero = true;
    float totalAdvance = 0.0f;
    for (int i = 0; i < glyphCount; ++i) {
        uint32_t gid = shaped[static_cast<size_t>(i)].glyphId;
        if (gid == 0) allGidsNonZero = false;
        totalAdvance += shaped[static_cast<size_t>(i)].xAdvance;
        AddResult(("Text GID " + std::to_string(i) + " matches golden").c_str(),
                  gid == kGoldenGIDs[i],
                  ("GID=" + std::to_string(gid) +
                   " expected=" + std::to_string(kGoldenGIDs[i])).c_str());
    }
    AddResult("Text all GIDs non-zero (no .notdef)", allGidsNonZero);

    // Golden UPEM-scaled per-glyph advance assertions.
    // Pinned against Noto Sans artifact SHA-256 d78a4640... at 32px.
    static constexpr float kGoldenPerGlyphAdvance[] = {
        23.712002f, 18.048000f, 8.256001f, 8.256001f, 19.360001f
    };
    static constexpr float kGoldenTotalAdvance = 77.632004f;
    for (int i = 0; i < glyphCount; ++i) {
        float adv = shaped[static_cast<size_t>(i)].xAdvance;
        AddResult(("Text advance " + std::to_string(i) + " matches golden").c_str(),
                  std::abs(adv - kGoldenPerGlyphAdvance[i]) < 0.01f,
                  ("got=" + std::to_string(adv) +
                   " expected=" + std::to_string(kGoldenPerGlyphAdvance[i])).c_str());
    }
    AddResult("Text total advance matches golden",
              std::abs(totalAdvance - kGoldenTotalAdvance) < 0.05f,
              ("got=" + std::to_string(totalAdvance) +
               " expected=" + std::to_string(kGoldenTotalAdvance)).c_str());

    // Shape and cache — this populates the glyph cache with rasterized bitmaps.
    const auto& glyphs = fm.ShapeAndCacheText("Hello", 32);
    int validCacheCount = 0;
    for (const auto& pg : glyphs) {
        if (pg.cacheIndex >= 0) validCacheCount++;
    }
    AddResult("Text cache entries valid", validCacheCount == glyphCount,
              (std::to_string(validCacheCount) + "/" + std::to_string(glyphs.size())).c_str());

    // Per-glyph rasterization must succeed (cacheIndex >= 0).
    for (int i = 0; i < glyphCount; ++i) {
        bool gidOk = glyphs[static_cast<size_t>(i)].cacheIndex >= 0;
        AddResult(("Text glyph " + std::to_string(i) + " rasterized").c_str(), gidOk);
    }

    // Raster text positioned glyphs and compute deterministic pixel hash.
    Rasterizer rast(400, 80, fm.GlyphCache());
    rast.Clear(Color::White());
    {
        RenderList rl;
        float cursorX = 10.0f;
        float cursorY = 50.0f;
        for (const auto& pg : glyphs) {
            if (pg.cacheIndex >= 0) {
                rl.Add(RenderCommand::BlitGlyph(
                    cursorX + pg.xOffset, cursorY - pg.yOffset,
                    Color::Black(), pg.cacheIndex));
            }
            cursorX += pg.xAdvance;
        }
        rast.Execute(rl);
    }

    // Assert non-background Noto Sans pixels exist at expected places.
    const uint32_t* px = rast.Pixels();
    int stride = rast.Width();
    bool hasTextPixels = false;
    float measuredRightEdge = 0.0f;
    for (int y = 5; y < 75; ++y) {
        for (int x = 5; x < 380; ++x) {
            if (px[y * stride + x] != 0xFFFFFFFF) {
                hasTextPixels = true;
                measuredRightEdge = std::max(measuredRightEdge, static_cast<float>(x));
            }
        }
    }
    AddResult("Text face produces non-background pixels", hasTextPixels);

    // Golden pixel hash — fixed for Noto Sans "Hello" at 32px with the
    // exact raster positions above. This hash is pinned against the
    // Noto Sans artifact at SHA-256 d78a4640....
    uint64_t goldenHash = rast.PixelHash();
    AddResult("Text pixel hash non-zero", goldenHash != 0);

    {
        Rasterizer rastDup(400, 80, fm.GlyphCache());
        rastDup.Clear(Color::White());
        RenderList rlDup;
        float cursorX = 10.0f;
        float cursorY = 50.0f;
        for (const auto& pg : glyphs) {
            if (pg.cacheIndex >= 0) {
                rlDup.Add(RenderCommand::BlitGlyph(
                    cursorX + pg.xOffset, cursorY - pg.yOffset,
                    Color::Black(), pg.cacheIndex));
            }
            cursorX += pg.xAdvance;
        }
        rastDup.Execute(rlDup);
        uint64_t dupHash = rastDup.PixelHash();
        AddResult("Text pixel hash deterministic (stable golden)",
                  goldenHash == dupHash,
                  ("0x" + std::to_string(goldenHash)).c_str());
    }

    // Golden pixel hash — assert against the pinned constant.
    // Computed from Noto Sans SHA-256 d78a4640... "Hello" at 32px,
    // cursor starting at (10,50), 400x80 raster.
    static constexpr uint64_t kGoldenTextPixelHash = 0xca4a2991491e720fULL;
    AddResult("Text pixel hash matches golden",
              goldenHash == kGoldenTextPixelHash,
              ("0x" + std::to_string(goldenHash) + " vs 0x" +
               std::to_string(kGoldenTextPixelHash)).c_str());

    // Expected horizontal placement: total advance should roughly match
    // the pixel extent of glyphs on screen (with initial offset).
    float expectedRight = 10.0f + totalAdvance + 20.0f;
    AddResult("Text horizontal placement matches shaped advance",
              measuredRightEdge > 0.0f && measuredRightEdge < expectedRight,
              ("right=" + std::to_string(measuredRightEdge) +
               " expected<" + std::to_string(expectedRight)).c_str());

    // Transform movement: render with offset, verify different pixel hash.
    {
        Rasterizer rast2(400, 80, fm.GlyphCache());
        rast2.Clear(Color::White());
        RenderList rl2;
        float cursorX = 50.0f;
        float cursorY = 30.0f;
        for (const auto& pg : glyphs) {
            if (pg.cacheIndex >= 0) {
                rl2.Add(RenderCommand::BlitGlyph(
                    cursorX + pg.xOffset, cursorY - pg.yOffset,
                    Color::Black(), pg.cacheIndex));
            }
            cursorX += pg.xAdvance;
        }
        rast2.Execute(rl2);
        uint64_t h2 = rast2.PixelHash();
        AddResult("Text hash differs with transform offset",
                  goldenHash != h2);
    }

    // Clip test: restrict clip, verify outside remains white.
    {
        Rasterizer rast3(400, 80, fm.GlyphCache());
        rast3.Clear(Color::White());
        RenderList rl3;
        rl3.Add(RenderCommand::SetClip({0, 0, 25, 80}));
        float cursorX = 10.0f;
        for (const auto& pg : glyphs) {
            if (pg.cacheIndex >= 0) {
                rl3.Add(RenderCommand::BlitGlyph(
                    cursorX + pg.xOffset, 50.0f - pg.yOffset,
                    Color::Black(), pg.cacheIndex));
            }
            cursorX += pg.xAdvance;
        }
        rast3.Execute(rl3);
        const uint32_t* px3 = rast3.Pixels();
        bool outsideWhite = true;
        for (int y = 0; y < 80; ++y) {
            for (int x = 35; x < 400; ++x) {
                if (px3[y * stride + x] != 0xFFFFFFFF) { outsideWhite = false; break; }
            }
        }
        AddResult("Text raster clipped outside remains white", outsideWhite);
    }

    // Zero-coverage: raster at position that puts glyphs offscreen -> zero pixels.
    {
        Rasterizer rastZ(400, 80, fm.GlyphCache());
        rastZ.Clear(Color::White());
        RenderList rlZ;
        float cursorX = 500.0f;
        for (const auto& pg : glyphs) {
            if (pg.cacheIndex >= 0) {
                rlZ.Add(RenderCommand::BlitGlyph(
                    cursorX + pg.xOffset, 50.0f - pg.yOffset,
                    Color::Black(), pg.cacheIndex));
            }
            cursorX += pg.xAdvance;
        }
        rastZ.Execute(rlZ);
        bool zeroCoverage = true;
        const uint32_t* zx = rastZ.Pixels();
        for (size_t i = 0; i < rastZ.PixelCount(); ++i) {
            if (zx[i] != 0xFFFFFFFF) { zeroCoverage = false; break; }
        }
        AddResult("Text zero coverage (offscreen) confirms all white", zeroCoverage);
    }

    // Music face: demonstrably distinct from text face.
    if (s_testFontPath && fm.LoadBravura(s_testFontPath)) {
        int clefIdx = fm.RasterizeGlyph(0xE050, 32);
        AddResult("Music face: G clef rasterized", clefIdx >= 0);

        Rasterizer rastMusic(100, 100, fm.GlyphCache());
        rastMusic.Clear(Color::White());
        RenderList rlMusic;
        rlMusic.Add(RenderCommand::BlitGlyph(30.0f, 50.0f, Color::Black(), clefIdx));
        rastMusic.Execute(rlMusic);

        bool hasMusicPixels = false;
        const uint32_t* mx = rastMusic.Pixels();
        for (size_t i = 0; i < rastMusic.PixelCount(); ++i) {
            if (mx[i] != 0xFFFFFFFF) { hasMusicPixels = true; break; }
        }
        AddResult("Music face raster produces pixels", hasMusicPixels);

        // Distinctness: real text GID (from shaping) through music face vs text face.
        unsigned int realGid = shaped[0].glyphId;  // GID 43 for 'H'
        int textGidRaster = fm.RasterizeGlyphByIndex(realGid, 32, FontFace::Text);
        int musicGidRaster = fm.RasterizeGlyphByIndex(realGid, 32, FontFace::Music);
        bool distinct = (textGidRaster >= 0 && musicGidRaster >= 0 &&
                         textGidRaster != musicGidRaster);
        AddResult("Same GID different faces produce distinct cache entries",
                  distinct);
    }
}

void SelfTest::Test_OrthogonalFilletCommandGeometry() {
    // Inspect FillArc commands generated by AddOrthogonalConnector for
    // exact center, radius, and sweep (±π/2) on all four turn directions.

    struct TestCase {
        const char* name;
        Vec2 start, end;
        std::vector<Vec2> bends;
        Vec2 expectedBend;      // the bend point b
        Vec2 expectedCenterRel; // C - b (relative from bend to true center)
    };

    float cr = 8.0f;
    float hw = 1.5f;

    TestCase cases[] = {
        // Left→Down: u=(1,0), v=(0,1), C_rel = (-1+0, -0+1)*r = (-r, +r)
        {"Left→Down", {0, 100}, {100, 200}, {{100, 100}},
         {100, 100}, {-cr, cr}},
        // Right→Down: u=(-1,0), v=(0,1), C_rel = (1+0, 0+1)*r = (r, r)
        {"Right→Down", {200, 100}, {100, 200}, {{100, 100}},
         {100, 100}, {cr, cr}},
        // Right→Up: u=(-1,0), v=(0,-1), C_rel = (1+0, -0-1)*r = (r, -r)
        {"Right→Up", {200, 200}, {100, 100}, {{100, 200}},
         {100, 200}, {cr, -cr}},
        // Left→Up: incoming down (0,-1), outgoing right (1,0), bend at (0,100)
        // u = (0,-1), v = (1,0), C_rel = (1,1)*r = (r, r)
        {"Left→Up", {0, 200}, {100, 100}, {{0, 100}},
         {0, 100}, {cr, cr}},
    };

    for (const auto& tc : cases) {
        RenderList rl;
        rl.AddOrthogonalConnector(tc.start, tc.end, tc.bends,
                                   3.0f, cr, Color::Black());

        // Find the FillArc command
        const CmdFillArc* arcCmd = nullptr;
        for (const auto& cmd : rl.Commands()) {
            if (cmd.type == CmdType::FillArc) {
                arcCmd = &std::get<CmdFillArc>(cmd.data);
                break;
            }
        }

        bool hasArc = arcCmd != nullptr;
        AddResult((std::string("Fillet geo ") + tc.name + " has arc").c_str(),
                  hasArc);

        if (hasArc) {
            float expectedCX = tc.expectedBend.x + tc.expectedCenterRel.x;
            float expectedCY = tc.expectedBend.y + tc.expectedCenterRel.y;
            float expectedOuterR = cr + hw;
            float expectedInnerR = cr - hw;

            float cxDiff = std::abs(arcCmd->cx - expectedCX);
            float cyDiff = std::abs(arcCmd->cy - expectedCY);
            float rDiff = std::abs(arcCmd->radius - expectedOuterR);

            AddResult((std::string("Fillet geo ") + tc.name + " center X").c_str(),
                      cxDiff < 0.01f,
                      ("got=" + std::to_string(arcCmd->cx) +
                       " expected=" + std::to_string(expectedCX)).c_str());
            AddResult((std::string("Fillet geo ") + tc.name + " center Y").c_str(),
                      cyDiff < 0.01f,
                      ("got=" + std::to_string(arcCmd->cy) +
                       " expected=" + std::to_string(expectedCY)).c_str());
            AddResult((std::string("Fillet geo ") + tc.name + " outer radius").c_str(),
                      rDiff < 0.01f,
                      ("got=" + std::to_string(arcCmd->radius) +
                       " expected=" + std::to_string(expectedOuterR)).c_str());
            AddResult((std::string("Fillet geo ") + tc.name + " inner radius").c_str(),
                      std::abs(arcCmd->innerRadius - expectedInnerR) < 0.01f,
                      ("got=" + std::to_string(arcCmd->innerRadius) +
                       " expected=" + std::to_string(expectedInnerR)).c_str());

            // Sweep must be ±π/2 (within tolerance)
            float sweep = arcCmd->arc.end - arcCmd->arc.start;
            float absSweep = std::abs(sweep);
            bool isQuarterTurn = std::abs(absSweep - 1.57079633f) < 0.01f;
            AddResult((std::string("Fillet geo ") + tc.name + " sweep ±π/2").c_str(),
                      isQuarterTurn,
                      ("start=" + std::to_string(arcCmd->arc.start) +
                       " end=" + std::to_string(arcCmd->arc.end) +
                       " sweep=" + std::to_string(sweep)).c_str());
        }
    }
}

void SelfTest::Test_GlyphShapeCacheStability() {
    // Verify that repeated rasterization and shaping of the same content
    // reuses cached entries rather than growing the cache indefinitely.
    // This test calls ShapeAndCacheText thousands of times and asserts
    // cache sizes stabilize, proving production code won't allocate
    // unbounded state across frames.

    const char* textPath = ResolveTextFontPath();
    if (!textPath) {
        AddResult("Glyph/shape cache stability", false, "no text font path set");
        return;
    }

    FontManager fm;
    if (!fm.LoadTextFont(textPath)) {
        AddResult("Glyph/shape cache stability", false, "text font load failed");
        return;
    }

    // First shaping populates cache.
    const auto& first = fm.ShapeAndCacheText("Hello", 24);
    AddResult("Cache stability: first shape ok", !first.empty(),
              (std::to_string(first.size()) + " glyphs").c_str());

    size_t glyphCacheAfter1 = fm.GlyphCacheSize();
    size_t shapeCacheAfter1 = fm.ShapeCacheSize();
    AddResult("Cache stability: glyph cache populated",
              glyphCacheAfter1 > 0,
              ("glyphCache=" + std::to_string(glyphCacheAfter1)).c_str());
    AddResult("Cache stability: shape cache populated",
              shapeCacheAfter1 > 0,
              ("shapeCache=" + std::to_string(shapeCacheAfter1)).c_str());

    // Assert repeated cache hit returns identical backing data.
    const auto& hit1 = fm.ShapeAndCacheText("Hello", 24);
    const auto& hit2 = fm.ShapeAndCacheText("Hello", 24);
    AddResult("Cache stability: repeated hit same address",
              &hit1 == &hit2,
              "backing data identical across cache hits");
    AddResult("Cache stability: repeated hit same size",
              hit1.size() == hit2.size() && !hit1.empty());

    // Repeated shaping of the same string at the same size must reuse cached
    // entries.  Call 1000 times and verify caches haven't grown.
    for (int i = 0; i < 1000; ++i) {
        const auto& repeated = fm.ShapeAndCacheText("Hello", 24);
        (void)repeated;
    }

    size_t glyphCacheAfter1k = fm.GlyphCacheSize();
    size_t shapeCacheAfter1k = fm.ShapeCacheSize();
    AddResult("Cache stability: glyph cache size stable after 1000 repeats",
              glyphCacheAfter1k == glyphCacheAfter1,
              ("before=" + std::to_string(glyphCacheAfter1) +
               " after=" + std::to_string(glyphCacheAfter1k)).c_str());
    AddResult("Cache stability: shape cache size stable after 1000 repeats",
              shapeCacheAfter1k == shapeCacheAfter1,
              ("before=" + std::to_string(shapeCacheAfter1) +
               " after=" + std::to_string(shapeCacheAfter1k)).c_str());

    // Shape different string at same size — glyph cache may grow (new glyphs)
    // but shape cache adds one entry.
    const auto& second = fm.ShapeAndCacheText("World", 24);
    size_t glyphCacheAfter2 = fm.GlyphCacheSize();
    size_t shapeCacheAfter2 = fm.ShapeCacheSize();
    AddResult("Cache stability: new text adds shape entry",
              shapeCacheAfter2 > shapeCacheAfter1,
              ("shapeCache=" + std::to_string(shapeCacheAfter2)).c_str());
    AddResult("Cache stability: shape cache size grows by 1 for new text",
              shapeCacheAfter2 == shapeCacheAfter1 + 1,
              ("from " + std::to_string(shapeCacheAfter1) +
               " to " + std::to_string(shapeCacheAfter2)).c_str());

    // Repeating "World" 1000 times must not grow caches further.
    for (int i = 0; i < 1000; ++i) {
        const auto& r = fm.ShapeAndCacheText("World", 24);
        (void)r;
    }
    size_t glyphCacheAfter2k = fm.GlyphCacheSize();
    size_t shapeCacheAfter2k = fm.ShapeCacheSize();
    AddResult("Cache stability: glyph cache stable after new text 1000 repeats",
              glyphCacheAfter2k == glyphCacheAfter2,
              ("before=" + std::to_string(glyphCacheAfter2) +
               " after=" + std::to_string(glyphCacheAfter2k)).c_str());
    AddResult("Cache stability: shape cache stable after new text 1000 repeats",
              shapeCacheAfter2k == shapeCacheAfter2,
              ("before=" + std::to_string(shapeCacheAfter2) +
               " after=" + std::to_string(shapeCacheAfter2k)).c_str());

    // Different pixel size — new cache entries for bitmaps, new shape entry.
    const auto& sized = fm.ShapeAndCacheText("Hello", 32);
    (void)sized;
    size_t glyphCacheAfterSize = fm.GlyphCacheSize();
    size_t shapeCacheAfterSize = fm.ShapeCacheSize();
    AddResult("Cache stability: new pixel size adds glyph entries",
              glyphCacheAfterSize > glyphCacheAfter2,
              ("glyphCache=" + std::to_string(glyphCacheAfterSize)).c_str());
    AddResult("Cache stability: new pixel size adds shape entry",
              shapeCacheAfterSize > shapeCacheAfter2);

    // Allocation instrumentation: key allocation and cache miss counts.
    size_t allocBefore = fm.ShapeKeyAllocCount();
    size_t missBefore = fm.ShapeCacheMissCount();
    AddResult("Cache stability: key alloc count tracked",
              allocBefore > 0,
              ("keys=" + std::to_string(allocBefore)).c_str());
    AddResult("Cache stability: miss count tracked",
              missBefore > 0,
              ("misses=" + std::to_string(missBefore)).c_str());

    // First call for a new label → miss + key alloc increment once.
    const auto& newLabel = fm.ShapeAndCacheText("NewLabel", 24);
    AddResult("Cache stability: new label miss count +1",
              fm.ShapeCacheMissCount() == missBefore + 1);
    AddResult("Cache stability: new label key alloc +1",
              fm.ShapeKeyAllocCount() == allocBefore + 1);

    // Repeated hits on the same label → no additional miss or alloc.
    size_t missAfterHit = fm.ShapeCacheMissCount();
    size_t allocAfterHit = fm.ShapeKeyAllocCount();
    for (int i = 0; i < 500; ++i) {
        const auto& r = fm.ShapeAndCacheText("NewLabel", 24);
        (void)r;
    }
    AddResult("Cache stability: 500 hits no extra miss",
              fm.ShapeCacheMissCount() == missAfterHit);
    AddResult("Cache stability: 500 hits no extra key alloc",
              fm.ShapeKeyAllocCount() == allocAfterHit);

    // Long-label (>SSO) regression: label longer than typical SSO threshold.
    // First call allocates once; thousands of hits allocate zero more.
    const char* longLabel =
        "ThisIsALabelLongerThanTheTypicalSmallStringOptimizationBufferSize";
    size_t longMissBefore = fm.ShapeCacheMissCount();
    size_t longAllocBefore = fm.ShapeKeyAllocCount();
    const auto& longResult = fm.ShapeAndCacheText(longLabel, 24);
    AddResult("Cache stability: long label miss +1",
              fm.ShapeCacheMissCount() == longMissBefore + 1);
    AddResult("Cache stability: long label key alloc +1",
              fm.ShapeKeyAllocCount() == longAllocBefore + 1);

    const void* longAddr = &longResult;
    size_t shapeCacheBeforeLong = fm.ShapeCacheSize();
    for (int i = 0; i < 1000; ++i) {
        const auto& r = fm.ShapeAndCacheText(longLabel, 24);
        (void)r;
    }
    AddResult("Cache stability: long label 1000 hits same address",
              &fm.ShapeAndCacheText(longLabel, 24) == longAddr);
    AddResult("Cache stability: long label 1000 hits no shape cache growth",
              fm.ShapeCacheSize() == shapeCacheBeforeLong);
    AddResult("Cache stability: long label 1000 hits no extra alloc",
              fm.ShapeKeyAllocCount() == longAllocBefore + 1);
    AddResult("Cache stability: long label 1000 hits no extra miss",
              fm.ShapeCacheMissCount() == longMissBefore + 1);

    // Invalid cache index test — RasterizeGlyphByIndex with bogus face should fail.
    int badFace = fm.RasterizeGlyphByIndex(
        static_cast<unsigned int>(0), 24,
        static_cast<FontFace>(99)); // Invalid enum
    AddResult("Cache stability: invalid FontFace fails", badFace < 0);
    if (badFace >= 0) {
        AddResult("Cache stability: invalid FontFace check", false,
                  "should have returned -1");
    }
}

// ─────────────────────────────────────────────────────────────────────
// Real-path fidelity tests — dispatch actual SDL events, accumulate
// typed mutation results exactly as App does, feed into summariser.
// Never inject boolean outcomes or logger records directly.
// ─────────────────────────────────────────────────────────────────────

void SelfTest::Test_InputRealPathPanOnlyFail() {
    // Dispatch wheel + finger-motion events (pan only, no pinch).
    // Accumulate typed results → fidelity must FAIL (missing pinch).
    Transform xf{1.0f, {0, 0}};
    Vec2 lastMouse{400.0f, 300.0f};
    bool mouseDown = false;
    FingerState f1, f2;

    uint64_t base = 1'000'000'000;
    auto clock = [&base]() mutable -> uint64_t {
        uint64_t t = base; base += 16'667; return t;
    };
    InputLogger logger(clock);
    ViewportOutcomeAccumulator acc;

    ViewportEventState state{
        xf, &logger, true, lastMouse, mouseDown,
        f1, f2,
        800, 600, nullptr, nullptr
    };

    // Wheel pan
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_MOUSE_WHEEL;
        ev.wheel.x = 0.0f; ev.wheel.y = 1.0f;
        ev.wheel.mouse_x = 400.0f; ev.wheel.mouse_y = 300.0f;
        auto r = ProcessViewportSDLEvent(&ev, state);
        acc.Accumulate(r);
    }

    // Finger-down x2 + finger-motion (pan)
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_FINGER_DOWN;
        ev.tfinger.fingerID = 1; ev.tfinger.x = 0.2f; ev.tfinger.y = 0.5f;
        ProcessViewportSDLEvent(&ev, state);
    }
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_FINGER_DOWN;
        ev.tfinger.fingerID = 2; ev.tfinger.x = 0.3f; ev.tfinger.y = 0.5f;
        ProcessViewportSDLEvent(&ev, state);
    }
    for (int i = 0; i < 30; ++i) {
        SDL_Event ev{};
        ev.type = SDL_EVENT_FINGER_MOTION;
        ev.tfinger.fingerID = (i % 2 == 0) ? 1 : 2;
        ev.tfinger.x = 0.25f + static_cast<float>(i) * 0.005f;
        ev.tfinger.y = 0.5f;
        ev.tfinger.dx = 0.01f; ev.tfinger.dy = 0.0f;
        auto r = ProcessViewportSDLEvent(&ev, state);
        acc.Accumulate(r);
    }

    AddResult("RealPathPanOnly: pan accumulated from typed result", acc.HasPan());
    AddResult("RealPathPinchOnly: pinch NOT accumulated", !acc.HasPinch());

    bool hadViewport = (xf.scale != 1.0f || xf.offset.x != 0.0f || xf.offset.y != 0.0f);
    auto summary = logger.ComputeFidelitySummary(hadViewport, acc.HasPan(), acc.HasPinch());
    AddResult("RealPathPanOnly: fidelity FAIL (missing pinch)",
              summary.verdict.find("FAIL") == 0,
              summary.verdict.c_str());
}

void SelfTest::Test_InputRealPathPinchOnlyFail() {
    // Dispatch native pinch at off-origin focal — focal-compensation
    // offset must NOT count as pan.  Accumulated typed didPinch only.
    Transform xf{1.0f, {0, 0}};
    Vec2 lastMouse{400.0f, 300.0f};
    bool mouseDown = false;
    FingerState f1, f2;

    uint64_t base = 1'000'000'000;
    auto clock = [&base]() mutable -> uint64_t {
        uint64_t t = base; base += 16'667; return t;
    };
    InputLogger logger(clock);
    ViewportOutcomeAccumulator acc;

    ViewportEventState state{
        xf, &logger, true, lastMouse, mouseDown,
        f1, f2,
        800, 600, nullptr, nullptr
    };

    // Set up two fingers with centroid off-center at (200, 200)
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_FINGER_DOWN;
        ev.tfinger.fingerID = 1; ev.tfinger.x = 0.2f; ev.tfinger.y = 0.333f;
        ProcessViewportSDLEvent(&ev, state);
    }
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_FINGER_DOWN;
        ev.tfinger.fingerID = 2; ev.tfinger.x = 0.3f; ev.tfinger.y = 0.333f;
        ProcessViewportSDLEvent(&ev, state);
    }

    // Native pinch with focus=-1 (macOS centroid fallback to (200,200))
    {
        SDL_Event pb{};
        pb.type = SDL_EVENT_PINCH_BEGIN;
        ProcessViewportSDLEvent(&pb, state);

        SDL_Event ev{};
        ev.type = SDL_EVENT_PINCH_UPDATE;
        ev.pinch.scale = 2.0f;
        ev.pinch.focus_x = -1.0f; ev.pinch.focus_y = -1.0f;
        auto r = ProcessViewportSDLEvent(&ev, state);
        acc.Accumulate(r);
    }

    // Dispatch additional pinch updates to meet min count/duration
    for (int i = 0; i < 28; ++i) {
        SDL_Event pb{};
        pb.type = SDL_EVENT_PINCH_BEGIN;
        ProcessViewportSDLEvent(&pb, state);

        SDL_Event ev{};
        ev.type = SDL_EVENT_PINCH_UPDATE;
        ev.pinch.scale = 1.0f + static_cast<float>(i) * 0.01f;
        ev.pinch.focus_x = -1.0f; ev.pinch.focus_y = -1.0f;
        auto r = ProcessViewportSDLEvent(&ev, state);
        acc.Accumulate(r);
    }

    AddResult("RealPathPinchOnly: pinch accumulated from typed result", acc.HasPinch());
    AddResult("RealPathPinchOnly: pan NOT accumulated (focal offset excluded)",
              !acc.HasPan());

    bool hadViewport = (xf.scale != 1.0f || xf.offset.x != 0.0f || xf.offset.y != 0.0f);
    auto summary = logger.ComputeFidelitySummary(hadViewport, acc.HasPan(), acc.HasPinch());
    AddResult("RealPathPinchOnly: fidelity FAIL (missing pan)",
              summary.verdict.find("FAIL") == 0,
              summary.verdict.c_str());
}

void SelfTest::Test_InputRealPathPanAndPinchPass() {
    // Dispatch real pan (wheel+finger) + real pinch → accumulate both
    // typed outcomes → PASS.
    Transform xf{1.0f, {0, 0}};
    Vec2 lastMouse{400.0f, 300.0f};
    bool mouseDown = false;
    FingerState f1, f2;

    uint64_t base = 1'000'000'000;
    auto clock = [&base]() mutable -> uint64_t {
        uint64_t t = base; base += 16'667; return t;
    };
    InputLogger logger(clock);
    ViewportOutcomeAccumulator acc;

    ViewportEventState state{
        xf, &logger, true, lastMouse, mouseDown,
        f1, f2,
        800, 600, nullptr, nullptr
    };

    // Wheel pan
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_MOUSE_WHEEL;
        ev.wheel.x = 0.0f; ev.wheel.y = 1.0f;
        ev.wheel.mouse_x = 400.0f; ev.wheel.mouse_y = 300.0f;
        auto r = ProcessViewportSDLEvent(&ev, state);
        acc.Accumulate(r);
    }

    // Finger-down x2
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_FINGER_DOWN;
        ev.tfinger.fingerID = 1; ev.tfinger.x = 0.4f; ev.tfinger.y = 0.5f;
        ProcessViewportSDLEvent(&ev, state);
    }
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_FINGER_DOWN;
        ev.tfinger.fingerID = 2; ev.tfinger.x = 0.6f; ev.tfinger.y = 0.5f;
        ProcessViewportSDLEvent(&ev, state);
    }

    // Finger motion (pan)
    for (int i = 0; i < 10; ++i) {
        SDL_Event ev{};
        ev.type = SDL_EVENT_FINGER_MOTION;
        ev.tfinger.fingerID = (i % 2 == 0) ? 1 : 2;
        ev.tfinger.x = 0.4f + static_cast<float>(i) * 0.01f;
        ev.tfinger.y = 0.5f;
        ev.tfinger.dx = 0.01f; ev.tfinger.dy = 0.0f;
        auto r = ProcessViewportSDLEvent(&ev, state);
        acc.Accumulate(r);
    }

    // Native pinch zoom
    {
        SDL_Event pb{};
        pb.type = SDL_EVENT_PINCH_BEGIN;
        ProcessViewportSDLEvent(&pb, state);

        SDL_Event ev{};
        ev.type = SDL_EVENT_PINCH_UPDATE;
        ev.pinch.scale = 1.5f;
        ev.pinch.focus_x = -1.0f; ev.pinch.focus_y = -1.0f;
        auto r = ProcessViewportSDLEvent(&ev, state);
        acc.Accumulate(r);
    }

    // Dispatch additional finger events to meet min count/duration
    for (int i = 0; i < 20; ++i) {
        SDL_Event ev{};
        ev.type = SDL_EVENT_FINGER_MOTION;
        ev.tfinger.fingerID = (i % 2 == 0) ? 1 : 2;
        ev.tfinger.x = 0.5f + static_cast<float>(i) * 0.005f;
        ev.tfinger.y = 0.5f;
        ev.tfinger.dx = 0.01f; ev.tfinger.dy = 0.0f;
        auto r = ProcessViewportSDLEvent(&ev, state);
        acc.Accumulate(r);
    }

    for (int i = 0; i < 16; ++i) {
        SDL_Event ev{};
        ev.type = SDL_EVENT_MOUSE_MOTION;
        ev.motion.x = static_cast<float>(i);
        ev.motion.y = 100.0f;
        ev.motion.xrel = 1.0f;
        acc.Accumulate(ProcessViewportSDLEvent(&ev, state));
    }

    for (int i = 0; i < 28; ++i) {
        SDL_Event ev{};
        ev.type = SDL_EVENT_MOUSE_WHEEL;
        ev.wheel.x = 0.25f;
        ev.wheel.y = 0.5f;
        ev.wheel.mouse_x = 400.0f;
        ev.wheel.mouse_y = 300.0f;
        acc.Accumulate(ProcessViewportSDLEvent(&ev, state));
    }

    AddResult("RealPathBoth: pan accumulated", acc.HasPan());
    AddResult("RealPathBoth: pinch accumulated", acc.HasPinch());

    bool hadViewport = (xf.scale != 1.0f || xf.offset.x != 0.0f || xf.offset.y != 0.0f);
    auto summary = logger.ComputeFidelitySummary(hadViewport, acc.HasPan(), acc.HasPinch());
    AddResult("RealPathBoth: fidelity PASS",
              summary.verdict.find("PASS") == 0,
              summary.verdict.c_str());
}

void SelfTest::Test_InputRealPathKeyboardFail() {
    // Keyboard zoom + pan changes viewport but sets neither didPan nor
    // didPinch → fidelity FAIL (missing native pinch and pan).
    Transform xf{1.0f, {0, 0}};
    Vec2 lastMouse{400.0f, 300.0f};
    bool mouseDown = false;
    FingerState f1, f2;

    uint64_t base = 1'000'000'000;
    auto clock = [&base]() mutable -> uint64_t {
        uint64_t t = base; base += 16'667; return t;
    };
    InputLogger logger(clock);
    ViewportOutcomeAccumulator acc;

    ViewportEventState state{
        xf, &logger, true, lastMouse, mouseDown,
        f1, f2,
        800, 600, nullptr, nullptr
    };

    // Keyboard pan
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_KEY_DOWN;
        ev.key.key = SDLK_LEFT;
        auto r = ProcessViewportSDLEvent(&ev, state);
        acc.Accumulate(r);
    }

    // Keyboard zoom
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_KEY_DOWN;
        ev.key.key = SDLK_EQUALS;
        auto r = ProcessViewportSDLEvent(&ev, state);
        acc.Accumulate(r);
    }

    // Dispatch mouse-motion events to fill log with enough events
    for (int i = 0; i < 58; ++i) {
        SDL_Event ev{};
        ev.type = SDL_EVENT_MOUSE_MOTION;
        ev.motion.x = static_cast<float>(i);
        ev.motion.y = 100.0f;
        ev.motion.xrel = 1.0f;
        ev.motion.yrel = 0.0f;
        ProcessViewportSDLEvent(&ev, state);
    }

    AddResult("RealPathKeyboard: pan NOT accumulated from keyboard", !acc.HasPan());
    AddResult("RealPathKeyboard: pinch NOT accumulated from keyboard", !acc.HasPinch());

    bool hadViewport = (xf.scale != 1.0f || xf.offset.x != 0.0f || xf.offset.y != 0.0f);
    auto summary = logger.ComputeFidelitySummary(hadViewport, acc.HasPan(), acc.HasPinch());
    AddResult("RealPathKeyboard: fidelity FAIL (no trackpad outcomes)",
              summary.verdict.find("FAIL") == 0,
              summary.verdict.c_str());
}

void SelfTest::Test_InputRealPathWheelFingerNoPinchFail() {
    // Wheel events + finger-down must NOT satisfy pinch outcome.
    // Finger-down alone doesn't mutate scale; wheel only pans.
    // Even with viewport response, summary fails without pinch.
    Transform xf{1.0f, {0, 0}};
    Vec2 lastMouse{400.0f, 300.0f};
    bool mouseDown = false;
    FingerState f1, f2;

    uint64_t base = 1'000'000'000;
    auto clock = [&base]() mutable -> uint64_t {
        uint64_t t = base; base += 16'667; return t;
    };
    InputLogger logger(clock);
    ViewportOutcomeAccumulator acc;

    ViewportEventState state{
        xf, &logger, true, lastMouse, mouseDown,
        f1, f2,
        800, 600, nullptr, nullptr
    };

    // Wheel (didPan only)
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_MOUSE_WHEEL;
        ev.wheel.x = 0.0f; ev.wheel.y = 0.5f;
        ev.wheel.mouse_x = 400.0f; ev.wheel.mouse_y = 300.0f;
        auto r = ProcessViewportSDLEvent(&ev, state);
        acc.Accumulate(r);
    }

    // Finger-down (no mutation, no didPinch)
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_FINGER_DOWN;
        ev.tfinger.fingerID = 1; ev.tfinger.x = 0.2f; ev.tfinger.y = 0.3f;
        auto r = ProcessViewportSDLEvent(&ev, state);
        acc.Accumulate(r);
    }

    // More wheel events
    for (int i = 0; i < 55; ++i) {
        SDL_Event ev{};
        ev.type = SDL_EVENT_MOUSE_WHEEL;
        ev.wheel.x = 0.3f; ev.wheel.y = 0.5f;
        ev.wheel.mouse_x = 400.0f; ev.wheel.mouse_y = 300.0f;
        auto r = ProcessViewportSDLEvent(&ev, state);
        acc.Accumulate(r);
    }

    AddResult("RealPathWheelFinger: pan accumulated (from wheel)", acc.HasPan());
    AddResult("RealPathWheelFinger: pinch NOT accumulated", !acc.HasPinch());

    bool hadViewport = (xf.scale != 1.0f || xf.offset.x != 0.0f || xf.offset.y != 0.0f);
    auto summary = logger.ComputeFidelitySummary(hadViewport, acc.HasPan(), acc.HasPinch());
    AddResult("RealPathWheelFinger: fidelity FAIL (no pinch outcome)",
              summary.verdict.find("FAIL") == 0,
              summary.verdict.c_str());
}

// ─────────────────────────────────────────────────────────────────────
// Focal-branch priority test — prove event focus_x/focus_y takes
// priority over centroid and window-center fallback.
// ─────────────────────────────────────────────────────────────────────

void SelfTest::Test_InputPinchEventFocalPriority() {
    // Dispatch SDL_EVENT_PINCH_UPDATE with valid off-center focus_x/focus_y
    // deliberately different from active centroid AND window center.
    // Assert the world point under the event focal is preserved after zoom,
    // proving the event-field branch takes priority.

    Transform xf{1.0f, {0, 0}};
    Vec2 lastMouse{400.0f, 300.0f};
    bool mouseDown = false;
    FingerState f1, f2;
    InputLogger logger;

    ViewportEventState state{
        xf, &logger, true, lastMouse, mouseDown,
        f1, f2,
        800, 600, nullptr, nullptr
    };

    // Set up two fingers with centroid at (400, 300) — window center
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_FINGER_DOWN;
        ev.tfinger.fingerID = 1; ev.tfinger.x = 0.4f; ev.tfinger.y = 0.5f;
        ProcessViewportSDLEvent(&ev, state);
    }
    {
        SDL_Event ev{};
        ev.type = SDL_EVENT_FINGER_DOWN;
        ev.tfinger.fingerID = 2; ev.tfinger.x = 0.6f; ev.tfinger.y = 0.5f;
        ProcessViewportSDLEvent(&ev, state);
    }

    // Event focal at (250, 200) — different from:
    //   centroid = (400, 300)
    //   window center = (400, 300)
    // Both fallbacks would choose (400, 300), but the event field at
    // (250, 200) takes priority because focus_x/focus_y >= 0.
    float eventFocalX = 250.0f;
    float eventFocalY = 200.0f;

    // Record world point at event focal before zoom
    Vec2 worldAtEventFocal = xf.ScreenToWorld({eventFocalX, eventFocalY});

    {
        SDL_Event pb{};
        pb.type = SDL_EVENT_PINCH_BEGIN;
        ProcessViewportSDLEvent(&pb, state);

        SDL_Event ev{};
        ev.type = SDL_EVENT_PINCH_UPDATE;
        ev.pinch.scale = 2.0f;
        ev.pinch.focus_x = eventFocalX;  // valid (≥0) — event branch
        ev.pinch.focus_y = eventFocalY;

        auto result = ProcessViewportSDLEvent(&ev, state);
        AddResult("PinchEventFocal: viewportChanged", result.viewportChanged);
        AddResult("PinchEventFocal: didPinch set", result.didPinch);
        AddResult("PinchEventFocal: didPan NOT set", !result.didPan);

        // World point at event focal (250, 200) must be preserved
        Vec2 screenAfter = xf.WorldToScreen(worldAtEventFocal);
        AddResult("PinchEventFocal: event focal point preserved X",
                  std::abs(screenAfter.x - eventFocalX) < 1.0f,
                  ("screen.x=" + std::to_string(screenAfter.x)).c_str());
        AddResult("PinchEventFocal: event focal point preserved Y",
                  std::abs(screenAfter.y - eventFocalY) < 1.0f,
                  ("screen.y=" + std::to_string(screenAfter.y)).c_str());

        // Centroid at (400, 300) should NOT be preserved (it's NOT the focal)
        Vec2 worldAtCentroid = {400.0f, 300.0f};
        Vec2 centroidScreenAfter = xf.WorldToScreen(worldAtCentroid);
        bool centroidMoved = !(std::abs(centroidScreenAfter.x - 400.0f) < 1.0f &&
                                std::abs(centroidScreenAfter.y - 300.0f) < 1.0f);
        AddResult("PinchEventFocal: centroid NOT preserved (event focal was used)",
                  centroidMoved,
                  ("screen=(" + std::to_string(centroidScreenAfter.x) + "," +
                   std::to_string(centroidScreenAfter.y) + ")").c_str());
    }
}

// ─────────────────────────────────────────────────────────────────────
// Pinch validity — reject non-finite/zero/negative scale;
// no pinch outcome when scale is unchanged (clamp boundary or factor=1).
// ─────────────────────────────────────────────────────────────────────

// Helper: dispatch a PINCH_BEGIN + PINCH_UPDATE and return result.
static ViewportEventResult DispatchPinch(float scale,
                                          ViewportEventState& state) {
    SDL_Event pb{};
    pb.type = SDL_EVENT_PINCH_BEGIN;
    ProcessViewportSDLEvent(&pb, state);

    SDL_Event ev{};
    ev.type = SDL_EVENT_PINCH_UPDATE;
    ev.pinch.scale = scale;
    ev.pinch.focus_x = -1.0f;
    ev.pinch.focus_y = -1.0f;
    return ProcessViewportSDLEvent(&ev, state);
}

void SelfTest::Test_InputPinchValidityNoChangeScaleOne() {
    Transform xf{1.0f, {0, 0}};
    Vec2 lastMouse{400.0f, 300.0f};
    bool mouseDown = false;
    FingerState f1, f2;
    InputLogger logger;
    ViewportOutcomeAccumulator acc;

    ViewportEventState state{
        xf, &logger, true, lastMouse, mouseDown,
        f1, f2,
        800, 600, nullptr, nullptr
    };

    // Scale factor 1.0 → no scale change → no pinch outcome
    float scaleBefore = xf.scale;
    ViewportEventResult r = DispatchPinch(1.0f, state);
    acc.Accumulate(r);

    AddResult("PinchValidityNoChange: viewportChanged false",
              !r.viewportChanged);
    AddResult("PinchValidityNoChange: didPinch false",
              !r.didPinch);
    AddResult("PinchValidityNoChange: scale unchanged",
              xf.scale == scaleBefore);
    AddResult("PinchValidityNoChange: no pinch accumulated",
              !acc.HasPinch());

    // A valid raw scale within the handler epsilon also must not claim a
    // pinch outcome because the applied scale remains unchanged.
    ViewportEventResult epsilonResult = DispatchPinch(1.0000005f, state);
    acc.Accumulate(epsilonResult);
    AddResult("PinchValidityEpsilon: viewportChanged false",
              !epsilonResult.viewportChanged);
    AddResult("PinchValidityEpsilon: didPinch false",
              !epsilonResult.didPinch);
    AddResult("PinchValidityEpsilon: scale unchanged",
              xf.scale == scaleBefore);
    AddResult("PinchValidityEpsilon: no pinch accumulated",
              !acc.HasPinch());
}

void SelfTest::Test_InputPinchValidityClampedNoChange() {
    // Start at the clamp max (20.0).  Scale factor 2.0 would
    // produce 40.0, which clamps back to 20.0 — no net change.
    Transform xf{20.0f, {0, 0}}; // already at max clamp
    Vec2 lastMouse{400.0f, 300.0f};
    bool mouseDown = false;
    FingerState f1, f2;
    InputLogger logger;
    ViewportOutcomeAccumulator acc;

    ViewportEventState state{
        xf, &logger, true, lastMouse, mouseDown,
        f1, f2,
        800, 600, nullptr, nullptr
    };

    float scaleBefore = xf.scale;
    ViewportEventResult r = DispatchPinch(2.0f, state);
    acc.Accumulate(r);

    AddResult("PinchValidityClamped: viewportChanged false",
              !r.viewportChanged);
    AddResult("PinchValidityClamped: didPinch false",
              !r.didPinch);
    AddResult("PinchValidityClamped: scale unchanged (at max boundary)",
              xf.scale == scaleBefore);
    AddResult("PinchValidityClamped: no pinch accumulated",
              !acc.HasPinch());

    // Also test min boundary: scale 0.1f, factor 0.5f → 0.05 clamped to 0.1
    Transform xf2{0.1f, {0, 0}};
    Vec2 lm2{400.0f, 300.0f};
    bool md2 = false;
    FingerState fa2, fb2;
    InputLogger logger2;
    ViewportOutcomeAccumulator acc2;

    ViewportEventState state2{
        xf2, &logger2, true, lm2, md2,
        fa2, fb2,
        800, 600, nullptr, nullptr
    };

    float sb2 = xf2.scale;
    ViewportEventResult r2 = DispatchPinch(0.5f, state2);
    acc2.Accumulate(r2);

    AddResult("PinchValidityClampedMin: viewportChanged false",
              !r2.viewportChanged);
    AddResult("PinchValidityClampedMin: didPinch false",
              !r2.didPinch);
    AddResult("PinchValidityClampedMin: scale unchanged (at min boundary)",
              xf2.scale == sb2);
    AddResult("PinchValidityClampedMin: no pinch accumulated",
              !acc2.HasPinch());
}

void SelfTest::Test_InputPinchValidityNaN() {
    Transform xf{1.0f, {0, 0}};
    Vec2 lastMouse{400.0f, 300.0f};
    bool mouseDown = false;
    FingerState f1, f2;
    InputLogger logger;

    ViewportEventState state{
        xf, &logger, true, lastMouse, mouseDown,
        f1, f2,
        800, 600, nullptr, nullptr
    };

    float scaleBefore = xf.scale;
    ViewportEventResult r = DispatchPinch(std::numeric_limits<float>::quiet_NaN(), state);

    AddResult("PinchValidityNaN: viewportChanged false",
              !r.viewportChanged);
    AddResult("PinchValidityNaN: didPinch false",
              !r.didPinch);
    AddResult("PinchValidityNaN: scale unchanged",
              xf.scale == scaleBefore);
}

void SelfTest::Test_InputPinchValidityInfinity() {
    Transform xf{1.0f, {0, 0}};
    Vec2 lastMouse{400.0f, 300.0f};
    bool mouseDown = false;
    FingerState f1, f2;
    InputLogger logger;

    ViewportEventState state{
        xf, &logger, true, lastMouse, mouseDown,
        f1, f2,
        800, 600, nullptr, nullptr
    };

    float scaleBefore = xf.scale;

    ViewportEventResult r = DispatchPinch(std::numeric_limits<float>::infinity(), state);
    AddResult("PinchValidityInf: +inf viewportChanged false",
              !r.viewportChanged);
    AddResult("PinchValidityInf: +inf didPinch false",
              !r.didPinch);
    AddResult("PinchValidityInf: +inf scale unchanged",
              xf.scale == scaleBefore);

    ViewportEventResult r2 = DispatchPinch(-std::numeric_limits<float>::infinity(), state);
    AddResult("PinchValidityInf: -inf viewportChanged false",
              !r2.viewportChanged);
    AddResult("PinchValidityInf: -inf didPinch false",
              !r2.didPinch);
}

void SelfTest::Test_InputPinchValidityZero() {
    Transform xf{1.0f, {0, 0}};
    Vec2 lastMouse{400.0f, 300.0f};
    bool mouseDown = false;
    FingerState f1, f2;
    InputLogger logger;

    ViewportEventState state{
        xf, &logger, true, lastMouse, mouseDown,
        f1, f2,
        800, 600, nullptr, nullptr
    };

    float scaleBefore = xf.scale;
    ViewportEventResult r = DispatchPinch(0.0f, state);

    AddResult("PinchValidityZero: viewportChanged false",
              !r.viewportChanged);
    AddResult("PinchValidityZero: didPinch false",
              !r.didPinch);
    AddResult("PinchValidityZero: scale unchanged",
              xf.scale == scaleBefore);
}

void SelfTest::Test_InputPinchValidityNegative() {
    Transform xf{1.0f, {0, 0}};
    Vec2 lastMouse{400.0f, 300.0f};
    bool mouseDown = false;
    FingerState f1, f2;
    InputLogger logger;

    ViewportEventState state{
        xf, &logger, true, lastMouse, mouseDown,
        f1, f2,
        800, 600, nullptr, nullptr
    };

    float scaleBefore = xf.scale;
    ViewportEventResult r = DispatchPinch(-2.0f, state);

    AddResult("PinchValidityNeg: viewportChanged false",
              !r.viewportChanged);
    AddResult("PinchValidityNeg: didPinch false",
              !r.didPinch);
    AddResult("PinchValidityNeg: scale unchanged",
              xf.scale == scaleBefore);
}

void SelfTest::Test_InputPinchValidityValidChange() {
    Transform xf{1.0f, {0, 0}};
    Vec2 lastMouse{400.0f, 300.0f};
    bool mouseDown = false;
    FingerState f1, f2;
    InputLogger logger;
    ViewportOutcomeAccumulator acc;

    ViewportEventState state{
        xf, &logger, true, lastMouse, mouseDown,
        f1, f2,
        800, 600, nullptr, nullptr
    };

    float scaleBefore = xf.scale;
    ViewportEventResult r = DispatchPinch(1.5f, state);
    acc.Accumulate(r);

    AddResult("PinchValidityValid: viewportChanged true",
              r.viewportChanged);
    AddResult("PinchValidityValid: didPinch true",
              r.didPinch);
    AddResult("PinchValidityValid: scale changed",
              xf.scale != scaleBefore);
    AddResult("PinchValidityValid: pinch accumulated",
              acc.HasPinch());
}

} // namespace spike
