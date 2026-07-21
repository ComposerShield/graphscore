// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <vector>

namespace spike {

struct TestResult {
    std::string name;
    bool passed = false;
    std::string detail;
};

class SelfTest {
public:
    SelfTest();

    // Run all self-tests
    bool RunAll();

    // Get individual results
    const std::vector<TestResult>& Results() const { return m_results; }

    // Save results as JSON to path
    bool SaveResults(const char* path) const;

    // Access to test helper data
    static const char* GetTestFontPath() { return s_testFontPath; }
    static void SetTestFontPath(const char* p) { s_testFontPath = p; }
    static const char* GetTestTextFontPath() { return s_testTextFontPath; }
    static void SetTestTextFontPath(const char* p) { s_testTextFontPath = p; }

private:
    std::vector<TestResult> m_results;

    void AddResult(const char* name, bool passed, const char* detail = "");

    // Individual tests
    void Test_Vec2Math();
    void Test_RectOps();
    void Test_TransformInverse();
    void Test_HitTestDeterministic();
    void Test_RoundedRectCommands();
    void Test_OrthogonalConnectorCommands();
    void Test_OrthogonalAxisAlignment();
    void Test_OrthogonalTrimPoints();
    void Test_OrthogonalRadiusBounds();
    void Test_ClipRect();
    void Test_StaffLines();
    void Test_OffscreenRenderHash();
    void Test_FontLoading();
    void Test_HarfBuzzShaping();
    void Test_SMuFLCodepoints();
    void Test_A11yTreeStructure();
    void Test_A11yTreeIDs();
    void Test_A11yTreeHierarchy();
    void Test_A11yTreeLabels();
    void Test_A11yTreeSelectionState();
    void Test_A11yTreeActions();
    void Test_A11yTreeTransformedBounds();
    void Test_A11yTreeHitTest();
    void Test_A11yTreeActionTransition();
    void Test_A11yTreeMutationValidation();
    void Test_A11yTreeGenerationCounter();
    void Test_A11yTreeInvalidAction();
    void Test_A11yTreeLifetimeDetach();
    void Test_ClipPixelAssertions();
    void Test_ShapedGlyphRendering();
    void Test_GlyphPixelAssertions();
    void Test_GlyphTransformMoves();
    void Test_GlyphClipObeys();
    void Test_OrthogonalAllTurns();
    void Test_OrthogonalReversedTraversal();
    void Test_OrthogonalShortSegmentClamp();
    void Test_OrthogonalZeroLengthReject();
    void Test_OrthogonalDuplicateReject();
    void Test_OrthogonalNonAxisReject();
    void Test_OrthogonalArcContinuity();
    void Test_OrthogonalFilletPixelRaster();
    void Test_TextVsMusicGlyphDistinction();
    void Test_A11yDuplicateIdRejection();
    void Test_A11yReparentDetachIntegrity();
    void Test_InputPanMath();
    void Test_InputPinchMath();
    void Test_InputScrollMath();
    void Test_InputFidelitySummaryPass();
    void Test_InputFidelitySummaryFail();
    void Test_InputFidelityThresholds();
    void Test_InputFidelityPhysicalGateBoundaries();
    void Test_InputFidelityTimestampDrops();
    void Test_InputViewportResponseRequired();
    void Test_NativeHandleNullWindow();
    void Test_NativeHandleUnavailable();
    void Test_NativeHandleFailsOnNullReport();
    void Test_GlyphCacheNonNullInvariant();
    void Test_A11yEmptyIdRejection();
    void Test_A11ySetRootValidation();
    void Test_A11yReparentExceptionSafety();
    void Test_A11ySetParentChildExceptionAtomic();
    void Test_BridgeSyncGeometrySpy();
    void Test_InputE2ESDLEvents();
    void Test_InputWheelPanOnly();
    void Test_InputFingerPanOnly();
    void Test_InputPinchFocalStability();
    void Test_InputPinchFocalOffCenter();
    void Test_InputSameDirectionNoZoom();
    void Test_InputCombinedStreamNoDoubleZoom();
    void Test_InputFidelityRequiresPanAndPinch();
    void Test_InputFidelityPanOnlyFail();
    void Test_InputFidelityPinchOnlyFail();
    void Test_InputFidelityWheelFingerDownNoPinch();
    void Test_TextFaceRasterPixels();
    void Test_OrthogonalFilletCommandGeometry();
    void Test_GlyphShapeCacheStability();
    void Test_InputRealPathPanOnlyFail();
    void Test_InputRealPathPinchOnlyFail();
    void Test_InputRealPathPanAndPinchPass();
    void Test_InputRealPathKeyboardFail();
    void Test_InputRealPathWheelFingerNoPinchFail();
    void Test_InputPinchEventFocalPriority();

    // Pinch validity — reject non-finite/zero/negative scale;
    // no outcome at clamp boundary or scale=1.
    void Test_InputPinchValidityNoChangeScaleOne();
    void Test_InputPinchValidityClampedNoChange();
    void Test_InputPinchValidityNaN();
    void Test_InputPinchValidityInfinity();
    void Test_InputPinchValidityZero();
    void Test_InputPinchValidityNegative();
    void Test_InputPinchValidityValidChange();

    static inline const char* s_testFontPath = nullptr;
    static inline const char* s_testTextFontPath = nullptr;
};

} // namespace spike
