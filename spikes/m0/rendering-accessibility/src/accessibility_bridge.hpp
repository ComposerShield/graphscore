// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "semantic_tree.hpp"

#include <memory>

namespace spike {

// macOS-specific implementation (Objective-C++). Non-macOS platforms get no-op.
// All Cocoa/AppKit/NSAccessibility types are confined to accessibility_bridge_mac.mm.
// This header exposes only GraphScore-owned types.

class AccessibilityBridgeMac : public IAccessibilityBridge {
public:
    // Defined in accessibility_bridge_mac.mm; exposed so the
    // Objective-C GSAccessibilityElement class can reference it.
    struct Impl;

    AccessibilityBridgeMac();
    ~AccessibilityBridgeMac() override;

    AccessibilityBridgeMac(const AccessibilityBridgeMac&) = delete;
    AccessibilityBridgeMac& operator=(const AccessibilityBridgeMac&) = delete;

    bool Attach(void* sdlWindow, SemanticTree* tree) override;
    void Detach() override;
    void SyncGeometry(const WindowGeometry& geom,
                       const Transform& viewTransform) override;
    void AnnouncePropertyChange(const std::string& stableId) override;
    void AnnounceSelectionChange() override;
    void AnnounceFocusedChange(const std::string& stableId) override;

    // Deprecated — use SyncGeometry.
    void UpdateBounds() override;

#ifdef __APPLE__
    friend int RunAccessibilityBridgeMacReport(void* sdlWindow, SemanticTree* tree);
#endif

    std::unique_ptr<Impl> m_impl;
};

class AccessibilityBridgeNoOp : public IAccessibilityBridge {
public:
    bool Attach(void*, SemanticTree*) override { return true; }
    void Detach() override {}
    void SyncGeometry(const WindowGeometry&, const Transform&) override {}
    void AnnouncePropertyChange(const std::string&) override {}
    void AnnounceSelectionChange() override {}
    void AnnounceFocusedChange(const std::string&) override {}
    void UpdateBounds() override {}
};

// Platform-conditional factory
inline std::unique_ptr<IAccessibilityBridge> CreateAccessibilityBridge() {
#ifdef __APPLE__
    return std::make_unique<AccessibilityBridgeMac>();
#else
    return std::make_unique<AccessibilityBridgeNoOp>();
#endif
}

// Run a native macOS accessibility report on a live SDL window.
// If reportPath is non-null, writes structured report there.
// Returns 0 on success.
int RunAccessibilityReport(void* sdlWindow, SemanticTree* tree,
                            const char* reportPath);

#ifdef __APPLE__
// macOS-specific: run the native bridge test (called from accessibility_report.cpp)
int RunAccessibilityBridgeMacReport(void* sdlWindow, SemanticTree* tree);
#endif

} // namespace spike
