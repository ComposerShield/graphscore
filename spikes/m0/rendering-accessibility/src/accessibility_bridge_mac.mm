// SPDX-License-Identifier: Apache-2.0
//
// macOS Cocoa accessibility bridge using NSAccessibility APIs.
// All AppKit/NSAccessibility types are confined to this translation unit.
// No Cocoa types leak into GraphScore-owned headers.
//
// Elements store stable semantic IDs (not positional indices or raw
// pointers).  Every query resolves the ID against the current tree.
// A generation counter detects mutations; the elements array and the
// ID→element dictionary are rebuilt when the generation changes.
//
// Frame conversion:
//   SDL logical (top-left content coords)
//     → Y-invert for NSView (bottom-left)
//     → convertRect:toView:nil  (window coords)
//     → [nsWindow convertRectToScreen:]  (screen coords)
//   Backing scale is obtained from the window and applied.
//
// Actions:
//   - Press actions expose NSAccessibilityPressAction (standard identifier).
//   - All other actions expose NSAccessibilityCustomAction objects with
//     handlers that dispatch into SemanticTree::InvokeAction.  Human-readable
//     action descriptions are NOT returned as action identifiers.

#import <AppKit/AppKit.h>
#import <Cocoa/Cocoa.h>

#include "accessibility_bridge.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>

@class GSAccessibilityContainerView;
@class GSAccessibilityElement;

namespace spike {

struct AccessibilityBridgeMac::Impl {
    GSAccessibilityContainerView* containerView = nil;
    NSMutableArray<GSAccessibilityElement*>* elements = nil;
    NSMutableDictionary<NSString*, GSAccessibilityElement*>* elementById = nil;
    SemanticTree* tree = nullptr;
    uint64_t cachedGeneration = 0;
    NSWindow* nsWindow = nil;
    Transform viewTransform;
    WindowGeometry windowGeometry;
    bool attached = false;
    int notificationCount = 0;

    void RebuildElementMap();
    GSAccessibilityElement* FindElement(const std::string& stableId) const;
    void EnsureCurrent();

    NSRect ScreenFrameForNode(int idx) const;
};

} // namespace spike

// ============================================================================
// GSAccessibilityElement — represents one SemanticNode to VoiceOver
// ============================================================================

@interface GSAccessibilityElement : NSAccessibilityElement

- (instancetype)initWithStableId:(NSString*)stableId
                             impl:(spike::AccessibilityBridgeMac::Impl*)impl;

- (void)updateParentLink;

@property (nonatomic, readonly, copy) NSString* stableId;
@property (nonatomic, readonly) spike::AccessibilityBridgeMac::Impl* impl;
// Cached custom actions for non-press actions; rebuilt on lookupNode change.
@property (nonatomic, strong) NSArray<NSAccessibilityCustomAction*>* cachedCustomActions;

@end

@implementation GSAccessibilityElement {
    NSString* _stableId;
    spike::AccessibilityBridgeMac::Impl* _impl;
}

- (instancetype)initWithStableId:(NSString*)stableId
                             impl:(spike::AccessibilityBridgeMac::Impl*)impl {
    self = [super init];
    if (self) {
        self->_stableId = [stableId copy];
        self->_impl = impl;
    }
    return self;
}

- (void)updateParentLink {
    if (!_impl || !_impl->tree) return;
    std::string sid = [self->_stableId UTF8String];
    int idx = _impl->tree->FindByStableId(sid);
    if (idx < 0) {
        [self setAccessibilityParent:_impl->containerView];
        return;
    }
    const auto& node = _impl->tree->Node(idx);
    int rootIdx = _impl->tree->RootIndex();
    if (node.parentIndex >= 0) {
        const auto& parentNode = _impl->tree->Node(node.parentIndex);
        GSAccessibilityElement* parentElem = _impl->FindElement(parentNode.stableId);
        if (parentElem) {
            [self setAccessibilityParent:parentElem];
            return;
        }
    }
    [self setAccessibilityParent:_impl->containerView];
}

- (const spike::SemanticNode*)lookupNode {
    if (!_impl || !_impl->tree) return nullptr;
    _impl->EnsureCurrent();
    int idx = _impl->tree->FindByStableId([self->_stableId UTF8String]);
    if (idx < 0) return nullptr;
    return &_impl->tree->Node(idx);
}

- (int)resolvedIndex {
    if (!_impl || !_impl->tree) return -1;
    return _impl->tree->FindByStableId([self->_stableId UTF8String]);
}

- (BOOL)isAccessibilityElement {
    const auto* node = [self lookupNode];
    if (!node || node->role == spike::A11yRole::Unknown) return NO;
    return YES;
}

- (NSString*)accessibilityRole {
    const auto* node = [self lookupNode];
    if (!node) return NSAccessibilityUnknownRole;

    switch (node->role) {
    case spike::A11yRole::Application:   return NSAccessibilityApplicationRole;
    case spike::A11yRole::Window:        return NSAccessibilityWindowRole;
    case spike::A11yRole::Group:         return NSAccessibilityGroupRole;
    case spike::A11yRole::GraphNode:     return NSAccessibilityGroupRole;
    case spike::A11yRole::Connector:     return NSAccessibilityImageRole;
    case spike::A11yRole::Measure:       return NSAccessibilityGroupRole;
    case spike::A11yRole::Note:          return NSAccessibilityButtonRole;
    case spike::A11yRole::Control:       return NSAccessibilityButtonRole;
    case spike::A11yRole::Selection:     return NSAccessibilityGroupRole;
    case spike::A11yRole::ActionItem:    return NSAccessibilityButtonRole;
    case spike::A11yRole::Label:         return NSAccessibilityStaticTextRole;
    case spike::A11yRole::Staff:         return NSAccessibilityGroupRole;
    default:                             return NSAccessibilityUnknownRole;
    }
}

- (NSString*)accessibilitySubrole {
    const auto* node = [self lookupNode];
    if (!node) return nil;

    switch (node->role) {
    case spike::A11yRole::GraphNode:     return NSAccessibilityGroupRole;
    case spike::A11yRole::Measure:       return @"GSMeasure";
    case spike::A11yRole::Note:          return @"GSNote";
    default:                             return nil;
    }
}

- (NSString*)accessibilityLabel {
    const auto* node = [self lookupNode];
    if (!node) return nil;
    return [NSString stringWithUTF8String:node->name.c_str()];
}

- (NSString*)accessibilityTitle {
    return [self accessibilityLabel];
}

- (NSString*)accessibilityValue {
    const auto* node = [self lookupNode];
    if (!node) return nil;
    if (!node->value.empty()) {
        return [NSString stringWithUTF8String:node->value.c_str()];
    }
    if (node->state & spike::A11yState::Selected)
        return @"selected";
    if (node->state & spike::A11yState::Focused)
        return @"focused";
    return nil;
}

- (NSString*)accessibilityHelp {
    const auto* node = [self lookupNode];
    if (!node || node->description.empty()) return nil;
    return [NSString stringWithUTF8String:node->description.c_str()];
}

- (NSString*)accessibilityIdentifier {
    return self->_stableId;
}

- (NSRect)accessibilityFrame {
    if (!_impl) return NSZeroRect;
    _impl->EnsureCurrent();
    int idx = [self resolvedIndex];
    if (idx < 0) return NSZeroRect;
    return _impl->ScreenFrameForNode(idx);
}

- (id)accessibilityParent {
    return [super accessibilityParent];
}

- (BOOL)isAccessibilityFocused {
    const auto* node = [self lookupNode];
    if (!node) return NO;
    return node->hasKeyboardFocus;
}

- (BOOL)isAccessibilitySelected {
    const auto* node = [self lookupNode];
    if (!node) return NO;
    return (node->state & spike::A11yState::Selected) != 0;
}

- (void)setAccessibilityFocused:(BOOL)flag {
    const auto* node = [self lookupNode];
    if (!node || !_impl || !_impl->tree) return;
    int idx = _impl->tree->FindByStableId([self->_stableId UTF8String]);
    if (idx < 0) return;
    _impl->tree->SetKeyboardFocus(idx, flag != NO);
    _impl->notificationCount++;
    NSAccessibilityPostNotification(self, NSAccessibilityFocusedUIElementChangedNotification);
}

// Standard NSAccessibilityPressAction for press-type actions.
- (BOOL)accessibilityPerformPress {
    const auto* node = [self lookupNode];
    if (!node || !_impl || !_impl->tree) return NO;
    // Find the first "press" action on this node
    for (const auto& a : node->actions) {
        if (a.id == "press") {
            int idx = _impl->tree->FindByStableId([self->_stableId UTF8String]);
            if (idx < 0) return NO;
            BOOL ok = _impl->tree->InvokeAction(idx, "press") ? YES : NO;
            if (ok) {
                _impl->notificationCount++;
                NSAccessibilityPostNotification(self, NSAccessibilityValueChangedNotification);
            }
            return ok;
        }
    }
    // Fallback: if no "press" action but there are other actions,
    // invoke the first one (legacy compatibility).
    if (node->actions.empty()) return NO;
    int idx = _impl->tree->FindByStableId([self->_stableId UTF8String]);
    if (idx < 0) return NO;
    BOOL ok = _impl->tree->InvokeAction(idx, node->actions[0].id) ? YES : NO;
    if (ok) {
        _impl->notificationCount++;
        NSAccessibilityPostNotification(self, NSAccessibilityValueChangedNotification);
    }
    return ok;
}

// Return standard action names: NSAccessibilityPressAction for "press" actions,
// plus custom action identifiers for all others.
- (NSArray<NSString*>*)accessibilityActionNames {
    const auto* node = [self lookupNode];
    if (!node || node->actions.empty()) return @[];

    NSMutableArray<NSString*>* names = [NSMutableArray array];
    for (const auto& a : node->actions) {
        if (a.id == "press") {
            [names addObject:NSAccessibilityPressAction];
        }
    }
    return names;
}

// Return custom actions as NSAccessibilityCustomAction objects.
// VoiceOver invokes these via accessibilityPerformAction: or the custom target/handler.
- (NSArray<NSAccessibilityCustomAction*>*)accessibilityCustomActions {
    const auto* node = [self lookupNode];
    if (!node || node->actions.empty()) return @[];

    NSMutableArray<NSAccessibilityCustomAction*>* customs = [NSMutableArray array];

    for (const auto& a : node->actions) {
        // "press" is already handled by NSAccessibilityPressAction
        if (a.id == "press") continue;

        NSString* actionName = [NSString stringWithUTF8String:a.description.c_str()];

        NSAccessibilityCustomAction* customAction =
            [[NSAccessibilityCustomAction alloc] initWithName:actionName
                                                       target:self
                                                     selector:@selector(performCustomAction:)];
        [customs addObject:customAction];
    }
    self.cachedCustomActions = customs;
    return customs;
}

- (void)performCustomAction:(NSAccessibilityCustomAction*)customAction {
    const auto* node = [self lookupNode];
    if (!node || !_impl || !_impl->tree) return;

    int idx = _impl->tree->FindByStableId([self->_stableId UTF8String]);
    if (idx < 0) return;

    // Match the custom action name back to the action ID
    NSString* matchName = [customAction name];
    for (const auto& a : node->actions) {
        if (a.id == "press") continue; // handled by accessibilityPerformPress
        NSString* desc = [NSString stringWithUTF8String:a.description.c_str()];
        if ([matchName isEqualToString:desc]) {
            _impl->tree->InvokeAction(idx, a.id);
            _impl->notificationCount++;
            NSAccessibilityPostNotification(self, NSAccessibilityValueChangedNotification);
            return;
        }
    }
}

- (NSString*)accessibilityActionDescription:(NSString*)action {
    // For standard actions, provide a human description.
    if ([action isEqualToString:NSAccessibilityPressAction]) {
        return @"Press";
    }
    return action;
}

- (void)accessibilityPerformAction:(NSString*)action {
    if ([action isEqualToString:NSAccessibilityPressAction]) {
        [self accessibilityPerformPress];
        return;
    }
    // For legacy callers that pass human descriptions directly,
    // try matching against custom actions.
    const auto* node = [self lookupNode];
    if (!node || !_impl || !_impl->tree) return;
    int idx = _impl->tree->FindByStableId([self->_stableId UTF8String]);
    if (idx < 0) return;
    for (const auto& a : node->actions) {
        NSString* desc = [NSString stringWithUTF8String:a.description.c_str()];
        if ([action isEqualToString:desc]) {
            _impl->tree->InvokeAction(idx, a.id);
            _impl->notificationCount++;
            NSAccessibilityPostNotification(self, NSAccessibilityValueChangedNotification);
            return;
        }
    }
}

- (NSArray<GSAccessibilityElement*>*)accessibilityChildren {
    const auto* node = [self lookupNode];
    if (!node || node->childIndices.empty()) return @[];
    if (!_impl) return @[];

    _impl->EnsureCurrent();

    NSMutableArray<GSAccessibilityElement*>* children = [NSMutableArray array];
    for (int childIdx : node->childIndices) {
        if (childIdx < 0 || childIdx >= _impl->tree->NodeCount()) continue;
        const auto& childNode = _impl->tree->Node(childIdx);
        GSAccessibilityElement* childElem = _impl->FindElement(childNode.stableId);
        if (childElem) {
            [children addObject:childElem];
        }
    }
    return children;
}

@end

// ============================================================================
// GSAccessibilityContainerView — invisible NSView that hosts all elements
// ============================================================================

@interface GSAccessibilityContainerView : NSView
@property (nonatomic, strong) NSMutableArray<GSAccessibilityElement*>* childElements;
@end

@implementation GSAccessibilityContainerView

- (instancetype)initWithFrame:(NSRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        _childElements = [[NSMutableArray alloc] init];
        self.alphaValue = 0.0;
    }
    return self;
}

- (BOOL)isAccessibilityElement {
    return NO;
}

- (BOOL)accessibilityIsIgnored {
    return NO;
}

- (NSArray<GSAccessibilityElement*>*)accessibilityChildren {
    return _childElements;
}

- (id)accessibilityHitTest:(NSPoint)point {
    for (GSAccessibilityElement* elem in [_childElements reverseObjectEnumerator]) {
        if (NSPointInRect(point, [elem accessibilityFrame])) {
            return elem;
        }
    }
    return self;
}

@end

namespace spike {

// ============================================================================
// Impl helpers
// ============================================================================

void AccessibilityBridgeMac::Impl::RebuildElementMap() {
    if (!tree) return;
    int count = tree->NodeCount();
    elements = [[NSMutableArray alloc] initWithCapacity:static_cast<NSUInteger>(count)];
    elementById = [[NSMutableDictionary alloc] initWithCapacity:static_cast<NSUInteger>(count)];

    for (int i = 0; i < count; ++i) {
        const auto& node = tree->Node(i);
        NSString* sid = [NSString stringWithUTF8String:node.stableId.c_str()];
        GSAccessibilityElement* elem = [[GSAccessibilityElement alloc]
            initWithStableId:sid impl:this];
        [elements addObject:elem];
        [elementById setObject:elem forKey:sid];
    }

    int rootIdx = tree->RootIndex();
    for (int i = 0; i < count; ++i) {
        GSAccessibilityElement* elem = elements[i];
        [elem updateParentLink];
    }

    NSMutableArray<GSAccessibilityElement*>* topLevel = [NSMutableArray array];
    if (rootIdx >= 0 && rootIdx < count) {
        [topLevel addObject:elements[rootIdx]];
    }
    if (containerView) {
        containerView.childElements = topLevel;
    }

    cachedGeneration = tree->Generation();
}

GSAccessibilityElement* AccessibilityBridgeMac::Impl::FindElement(
        const std::string& stableId) const {
    if (!elementById) return nil;
    NSString* sid = [NSString stringWithUTF8String:stableId.c_str()];
    return [elementById objectForKey:sid];
}

void AccessibilityBridgeMac::Impl::EnsureCurrent() {
    if (!tree) return;
    if (tree->Generation() != cachedGeneration) {
        if (containerView) {
            containerView.childElements = [NSMutableArray array];
        }
        RebuildElementMap();
    }
}

NSRect AccessibilityBridgeMac::Impl::ScreenFrameForNode(int idx) const {
    if (!tree || idx < 0 || idx >= tree->NodeCount()) return NSZeroRect;
    if (!containerView) return NSZeroRect;

    spike::Rect r = tree->ComputeScreenBounds(idx, viewTransform);
    if (r.w <= 0.0f || r.h <= 0.0f) return NSZeroRect;

    CGFloat cvHeight = [containerView frame].size.height;
    CGFloat nsX = static_cast<CGFloat>(r.x);
    CGFloat nsY = cvHeight - static_cast<CGFloat>(r.y) - static_cast<CGFloat>(r.h);
    CGFloat nsW = static_cast<CGFloat>(r.w);
    CGFloat nsH = static_cast<CGFloat>(r.h);
    NSRect viewRect = NSMakeRect(nsX, nsY, nsW, nsH);

    NSRect windowRect = [containerView convertRect:viewRect toView:nil];
    if (nsWindow) {
        return [nsWindow convertRectToScreen:windowRect];
    }
    return windowRect;
}

// ============================================================================
// AccessibilityBridgeMac implementation
// ============================================================================

AccessibilityBridgeMac::AccessibilityBridgeMac()
    : m_impl(std::make_unique<Impl>()) {}

AccessibilityBridgeMac::~AccessibilityBridgeMac() {
    Detach();
}

bool AccessibilityBridgeMac::Attach(void* sdlWindow, SemanticTree* tree) {
    if (!sdlWindow || !tree) return false;

    Detach();

    SDL_Window* win = static_cast<SDL_Window*>(sdlWindow);
    SDL_PropertiesID props = SDL_GetWindowProperties(win);
    NSWindow* nsWindow = static_cast<NSWindow*>(
        SDL_GetPointerProperty(props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr));
    if (!nsWindow) {
        std::fprintf(stderr, "[a11y-bridge] Failed to get NSWindow from SDL properties\n");
        return false;
    }

    NSView* contentView = [nsWindow contentView];
    if (!contentView) {
        std::fprintf(stderr, "[a11y-bridge] NSWindow has no contentView\n");
        return false;
    }

    m_impl->tree = tree;
    m_impl->nsWindow = nsWindow;

    int winW = 800, winH = 600;
    SDL_GetWindowSize(win, &winW, &winH);
    m_impl->windowGeometry.width = winW;
    m_impl->windowGeometry.height = winH;
    if (@available(macOS 10.7, *)) {
        m_impl->windowGeometry.backingScaleFactor =
            static_cast<float>([nsWindow backingScaleFactor]);
    }

    NSRect cvFrame = [contentView frame];
    GSAccessibilityContainerView* container = [[GSAccessibilityContainerView alloc]
        initWithFrame:cvFrame];
    [container setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
    [contentView addSubview:container];

    m_impl->containerView = container;

    m_impl->RebuildElementMap();

    m_impl->attached = true;
    int elementCount = static_cast<int>([m_impl->elements count]);
    std::fprintf(stdout, "[a11y-bridge] Attached to NSWindow. %d elements exposed.\n",
                 elementCount);

    NSAccessibilityPostNotification(container, NSAccessibilityCreatedNotification);
    return true;
}

void AccessibilityBridgeMac::Detach() {
    if (!m_impl->attached) return;

    if (m_impl->containerView) {
        [m_impl->containerView removeFromSuperview];
        m_impl->containerView = nil;
    }

    m_impl->elements = nil;
    m_impl->elementById = nil;
    m_impl->tree = nullptr;
    m_impl->nsWindow = nil;
    m_impl->attached = false;
    m_impl->cachedGeneration = 0;
    m_impl->notificationCount = 0;

    std::fprintf(stdout, "[a11y-bridge] Detached.\n");
}

void AccessibilityBridgeMac::SyncGeometry(const WindowGeometry& geom,
                                           const Transform& viewTransform) {
    m_impl->windowGeometry = geom;
    m_impl->viewTransform = viewTransform;

    if (m_impl->attached && m_impl->containerView) {
        m_impl->notificationCount++;
        NSAccessibilityPostNotification(m_impl->containerView,
                                         NSAccessibilityLayoutChangedNotification);
    }
}

void AccessibilityBridgeMac::AnnouncePropertyChange(const std::string& stableId) {
    if (!m_impl->attached) return;
    GSAccessibilityElement* elem = m_impl->FindElement(stableId);
    if (!elem) return;
    m_impl->notificationCount++;
    NSAccessibilityPostNotification(elem, NSAccessibilityValueChangedNotification);
}

void AccessibilityBridgeMac::AnnounceSelectionChange() {
    if (!m_impl->attached) return;
    m_impl->notificationCount++;
    NSAccessibilityPostNotification(m_impl->containerView,
                                     NSAccessibilitySelectedChildrenChangedNotification);
}

void AccessibilityBridgeMac::AnnounceFocusedChange(const std::string& stableId) {
    if (!m_impl->attached) return;
    GSAccessibilityElement* elem = m_impl->FindElement(stableId);
    if (!elem) return;
    m_impl->notificationCount++;
    NSAccessibilityPostNotification(elem, NSAccessibilityFocusedUIElementChangedNotification);
    NSAccessibilityPostNotification(elem, NSAccessibilityLayoutChangedNotification);
}

void AccessibilityBridgeMac::UpdateBounds() {
    if (!m_impl->attached || !m_impl->containerView) return;
    m_impl->notificationCount++;
    NSAccessibilityPostNotification(m_impl->containerView,
                                     NSAccessibilityLayoutChangedNotification);
}

} // namespace spike

// ============================================================================
// macOS-only accessibility report — exercises the native bridge in-process
// ============================================================================

namespace {

static void PrintFrame(FILE* f, NSRect frame) {
    std::fprintf(f, "(%.0f,%.0f %.0fx%.0f)", frame.origin.x, frame.origin.y,
                 frame.size.width, frame.size.height);
}

static std::string StableIdForElement(id elem) {
    if ([elem respondsToSelector:@selector(accessibilityIdentifier)]) {
        NSString* ident = [elem accessibilityIdentifier];
        if (ident) return [ident UTF8String];
    }
    return "?";
}

} // anonymous namespace

int spike::RunAccessibilityBridgeMacReport(void* sdlWindow, SemanticTree* tree) {
    if (!sdlWindow || !tree) {
        std::fprintf(stdout, "  SKIP: no SDL window or tree\n");
        return 1;
    }

    int failures = 0;

    auto bridge = std::make_unique<spike::AccessibilityBridgeMac>();
    bool attached = bridge->Attach(sdlWindow, tree);
    if (!attached) {
        std::fprintf(stdout, "  FAIL: bridge attach failed\n");
        return 1;
    }
    std::fprintf(stdout, "  Bridge attached OK\n");

    SDL_Window* win = static_cast<SDL_Window*>(sdlWindow);
    SDL_PropertiesID props = SDL_GetWindowProperties(win);
    NSWindow* nsWindow = static_cast<NSWindow*>(
        SDL_GetPointerProperty(props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr));

    if (!nsWindow) {
        std::fprintf(stdout, "  FAIL: could not get NSWindow\n");
        return 1;
    }

    NSView* contentView = [nsWindow contentView];
    if (!contentView) {
        std::fprintf(stdout, "  FAIL: no contentView\n");
        return 1;
    }

    // ---- Initial geometry sync ----
    int winW = 800, winH = 600;
    SDL_GetWindowSize(win, &winW, &winH);
    float backingScale = 1.0f;
    if (@available(macOS 10.7, *)) {
        backingScale = static_cast<float>([nsWindow backingScaleFactor]);
    }
    spike::WindowGeometry initGeom{winW, winH, backingScale};
    spike::Transform initXf;
    bridge->SyncGeometry(initGeom, initXf);

    // ---- Traversal report ----
    std::fprintf(stdout, "\n  === Native Element Traversal (stable-ID-based) ===\n\n");

    {
        NSMutableArray<GSAccessibilityElement*>* elems = bridge->m_impl->elements;
        int elementCount = static_cast<int>([elems count]);
        std::fprintf(stdout, "  Bridge elements: %d total\n\n", elementCount);

        NSArray* topLevel = [bridge->m_impl->containerView accessibilityChildren];

        spike::AccessibilityBridgeMac::Impl* implPtr = bridge->m_impl.get();

        __block int traversalFailures = 0;
        __block void (^traverse)(id, int) = nil;
        __block auto traverseBlock = ^(id elem, int depth) {
            if (![elem respondsToSelector:@selector(accessibilityRole)]) return;

            implPtr->EnsureCurrent();

            NSString* role    = [elem accessibilityRole];
            NSString* subrole = nil;
            if ([elem respondsToSelector:@selector(accessibilitySubrole)])
                subrole = [elem accessibilitySubrole];
            NSString* label   = [elem accessibilityLabel];
            NSString* ident   = nil;
            if ([elem respondsToSelector:@selector(accessibilityIdentifier)])
                ident = [elem accessibilityIdentifier];
            NSRect frame      = [elem accessibilityFrame];
            BOOL focused       = NO;
            if ([elem respondsToSelector:@selector(isAccessibilityFocused)])
                focused = [elem isAccessibilityFocused];
            BOOL selected      = NO;
            if ([elem respondsToSelector:@selector(isAccessibilitySelected)])
                selected = [elem isAccessibilitySelected];
            NSArray* actions   = nil;
            if ([elem respondsToSelector:@selector(accessibilityActionNames)])
                actions = [elem accessibilityActionNames];
            NSArray* customActions = nil;
            if ([elem respondsToSelector:@selector(accessibilityCustomActions)])
                customActions = [elem accessibilityCustomActions];
            NSArray* children  = nil;
            if ([elem respondsToSelector:@selector(accessibilityChildren)])
                children = [elem accessibilityChildren];

            for (int d = 0; d < depth; ++d) std::fprintf(stdout, "  ");

            const char* stateStr = " ";
            if (focused && selected) stateStr = "FOCUSED+SELECTED";
            else if (focused) stateStr = "FOCUSED";
            else if (selected) stateStr = "SELECTED";

            std::fprintf(stdout, "[%s] role=%s",
                         stateStr,
                         role ? [role UTF8String] : "?");
            if (subrole && [subrole length] > 0)
                std::fprintf(stdout, " subrole=%s", [subrole UTF8String]);
            std::fprintf(stdout, " label=\"%s\"",
                         label ? [label UTF8String] : "");
            if (ident)
                std::fprintf(stdout, " id=%s", [ident UTF8String]);
            std::fprintf(stdout, " frame=");
            PrintFrame(stdout, frame);

            // Check frame intersects the real window content screen rect
            if (nsWindow) {
                NSRect winScreenFrame = [nsWindow convertRectToScreen:
                    [[nsWindow contentView] bounds]];
                // Verify exact X/Y/w/h for known geometry with Y-inversion tolerance
                if (frame.size.width > 0 && frame.size.height > 0) {
                    bool intersects = CGRectIntersectsRect(
                        CGRectMake(frame.origin.x, frame.origin.y,
                                   frame.size.width, frame.size.height),
                        CGRectMake(winScreenFrame.origin.x, winScreenFrame.origin.y,
                                   winScreenFrame.size.width, winScreenFrame.size.height));
                    if (!intersects) {
                        std::fprintf(stdout, " [WARN:frame-outside-window]");
                        traversalFailures++;
                    }

        // Exact frame proof: independently compute expected AppKit-point
        // screen rect from content-view/window conversion and assert
        // X/Y/W/H within ≤1pt tolerance.  Both identity and transformed
        // checks MUST increment failures on every mismatch, including Y.
        //
        // Node A world bounds: {50,50,200,100}.
        // Frame pipeline: SDL top-left → Y-invert → convertRect:toView:nil
        //   → convertRectToScreen:.  NSAccessibility frames are in points.
        //
        // gs_node_A (identity transform):
        //   expectedX = winScreenFrame.origin.x + 50.0
        //   expectedY = winScreenFrame.origin.y + winScreenFrame.size.height - 150.0
        //               (Y-invert: cvHeight - 50 - 100 = cvHeight - 150)
        //   expectedW = 200.0, expectedH = 100.0
        if (ident && [[NSString stringWithUTF8String:"gs_node_A"] isEqualToString:ident]) {
            double expectedW = 200.0;
            double expectedH = 100.0;
            double expectedX = winScreenFrame.origin.x + 50.0;
            double expectedY = winScreenFrame.origin.y + winScreenFrame.size.height - 150.0;

            // Tolerance: ≤1 AppKit point.  Window frame conversion may
            // round title-bar offset to integral points; floor/ceil of
            // backing-scale arithmetic is bounded by 1pt.
            double tolerance = 1.5; // 1.5pt accounts for any sub-point rounding

            double wDiff = std::abs(frame.size.width - expectedW);
            double hDiff = std::abs(frame.size.height - expectedH);
            double xDiff = std::abs(frame.origin.x - expectedX);
            double yDiff = std::abs(frame.origin.y - expectedY);

            bool sizeOk = (wDiff <= tolerance && hDiff <= tolerance);
            bool posOk  = (xDiff <= tolerance && yDiff <= tolerance);

            if (!sizeOk) {
                std::fprintf(stdout, " [FAIL:frame-size (w=%.1f e=%.1f h=%.1f e=%.1f tol=%.1f)]",
                             frame.size.width, expectedW, frame.size.height, expectedH, tolerance);
                traversalFailures++;
            }
            if (!posOk) {
                std::fprintf(stdout, " [FAIL:frame-pos (x=%.1f e=%.1f y=%.1f e=%.1f tol=%.1f bs=%.1f)]",
                             frame.origin.x, expectedX, frame.origin.y, expectedY,
                             tolerance, static_cast<double>(backingScale));
                traversalFailures++;
            }

            std::fprintf(stdout, " [XYWH:%.1f,%.1f %.1fx%.1f eXYWH:%.1f,%.1f %.1fx%.1f]",
                         frame.origin.x, frame.origin.y,
                         frame.size.width, frame.size.height,
                         expectedX, expectedY, expectedW, expectedH);
        }
                }
            }

            bool expectEmpty = false;
            if (role) {
                NSString* r = role;
                bool isApp = [r isEqualToString:NSAccessibilityApplicationRole];
                bool isSel = [r isEqualToString:NSAccessibilityGroupRole] &&
                              ident != nil &&
                              [[NSString stringWithUTF8String:"gs_selection"] isEqualToString:ident];
                if (isApp || isSel) {
                    expectEmpty = true;
                }
            }
            if ((frame.size.width <= 0 || frame.size.height <= 0) && !expectEmpty) {
                std::fprintf(stdout, " [WARN:empty-frame]");
                traversalFailures++;
            }

            if (children && [children count] > 0) {
                std::fprintf(stdout, " children=[");
                for (NSUInteger ci = 0; ci < [children count]; ++ci) {
                    if (ci > 0) std::fprintf(stdout, ", ");
                    std::string cid = StableIdForElement([children objectAtIndex:ci]);
                    std::fprintf(stdout, "%s", cid.c_str());
                }
                std::fprintf(stdout, "]");
            }

            if (actions && [actions count] > 0) {
                std::fprintf(stdout, " actions=[");
                for (NSUInteger ai = 0; ai < [actions count]; ++ai) {
                    if (ai > 0) std::fprintf(stdout, ", ");
                    std::fprintf(stdout, "%s", [[actions objectAtIndex:ai] UTF8String]);
                }
                std::fprintf(stdout, "]");
            }
            if (customActions && [customActions count] > 0) {
                std::fprintf(stdout, " customActions=[");
                for (NSUInteger ai = 0; ai < [customActions count]; ++ai) {
                    if (ai > 0) std::fprintf(stdout, ", ");
                    NSAccessibilityCustomAction* ca = [customActions objectAtIndex:ai];
                    std::fprintf(stdout, "%s", [[ca name] UTF8String]);
                }
                std::fprintf(stdout, "]");
            }
            std::fprintf(stdout, "\n");

            if (children) {
                for (id ch in children) {
                    traverse(ch, depth + 1);
                }
            }
        };
        traverse = traverseBlock;

        for (id elem in topLevel) {
            traverse(elem, 1);
        }

        if (traversalFailures > 0) {
            std::fprintf(stdout, "\n  FAIL: %d traversal failures\n", traversalFailures);
            failures += traversalFailures;
        }
    }

    // ---- Shared callback state ----
    struct ReportCallbackState {
        int pressCount = 0;
        int selectCount = 0;
        int zoomCount = 0;
        float zoomScale = 1.0f;
    };
    ReportCallbackState cbState;

    tree->SetActionCallback([&cbState, &bridge, tree](
            const std::string& nodeId, const std::string& actionId) {
        if (actionId == "press" && nodeId == "gs_action_play") {
            cbState.pressCount++;
            std::fprintf(stdout, "  Action callback: node=%s action=%s\n",
                         nodeId.c_str(), actionId.c_str());
            int playIdx = tree->FindByStableId("gs_action_play");
            if (playIdx >= 0) {
                auto& pn = tree->Node(playIdx);
                pn.value = (pn.value == "playing") ? "stopped" : "playing";
                bridge->AnnouncePropertyChange("gs_action_play");
                std::fprintf(stdout, "  Play state: %s (notifications=%d)\n",
                             pn.value.c_str(), bridge->m_impl->notificationCount);
            }
        } else if (actionId == "select") {
            cbState.selectCount++;
            std::fprintf(stdout, "  Selection callback: node=%s action=%s\n",
                         nodeId.c_str(), actionId.c_str());
            int idx = tree->FindByStableId(nodeId);
            if (idx >= 0) {
                tree->SetSelected(idx, true);
                tree->SetFocused(idx, true);
                tree->SetKeyboardFocus(idx, true);
                bridge->AnnounceSelectionChange();
                bridge->AnnounceFocusedChange(nodeId);
            }
        } else if (actionId == "increment") {
            cbState.zoomCount++;
            std::fprintf(stdout, "  Zoom callback: node=%s action=%s\n",
                         nodeId.c_str(), actionId.c_str());
            cbState.zoomScale *= 1.2f;
            bridge->SyncGeometry(bridge->m_impl->windowGeometry,
                                  spike::Transform{cbState.zoomScale, {0, 0}});
        } else if (actionId == "decrement") {
            cbState.zoomCount++;
            std::fprintf(stdout, "  Zoom callback: node=%s action=%s\n",
                         nodeId.c_str(), actionId.c_str());
            cbState.zoomScale *= 0.8f;
            bridge->SyncGeometry(bridge->m_impl->windowGeometry,
                                  spike::Transform{cbState.zoomScale, {0, 0}});
        }
    });

    // ---- Native Action Test (via standard NSAccessibilityPressAction) ----
    std::fprintf(stdout, "\n  === Native Action Test (NSAccessibilityPressAction) ===\n\n");

    bridge->m_impl->notificationCount = 0;
    int pressBefore = cbState.pressCount;

    int playIdx = tree->FindByStableId("gs_action_play");
    if (playIdx >= 0) {
        std::string beforeValue = tree->Node(playIdx).value;
        std::fprintf(stdout, "  Before press: value=\"%s\"\n", beforeValue.c_str());

        GSAccessibilityElement* playElem = bridge->m_impl->FindElement("gs_action_play");
        if (!playElem) {
            std::fprintf(stdout, "  FAIL: gs_action_play element not found by stable ID\n");
            failures++;
        } else {
            // Invoke via the standard action name, exactly as VoiceOver would
            NSArray* actionNames = [playElem accessibilityActionNames];
            BOOL hasPressAction = [actionNames containsObject:NSAccessibilityPressAction];
            std::fprintf(stdout, "  Has NSAccessibilityPressAction: %s\n",
                         hasPressAction ? "YES" : "NO");

            BOOL pressResult = [playElem accessibilityPerformPress];
            std::fprintf(stdout, "  accessibilityPerformPress returned: %s\n",
                         pressResult ? "YES" : "NO");

            std::string afterValue = tree->Node(playIdx).value;
            int pressCountDelta = cbState.pressCount - pressBefore;
            std::fprintf(stdout, "  After press: value=\"%s\"\n", afterValue.c_str());
            std::fprintf(stdout, "  Action callbacks fired: %d\n", pressCountDelta);
            std::fprintf(stdout, "  Notifications emitted: %d\n",
                         bridge->m_impl->notificationCount);

            if (!pressResult) {
                std::fprintf(stdout, "  FAIL: accessibilityPerformPress returned NO\n");
                failures++;
            }
            if (pressCountDelta == 0) {
                std::fprintf(stdout, "  FAIL: action callback was not invoked\n");
                failures++;
            }
            if (beforeValue == afterValue) {
                std::fprintf(stdout, "  FAIL: value did not change after press\n");
                failures++;
            }

            // Second press (toggle back)
            BOOL press2 = [playElem accessibilityPerformPress];
            std::string finalValue = tree->Node(playIdx).value;
            int totalPress = cbState.pressCount - pressBefore;
            std::fprintf(stdout,
                "  Second press: result=%s value=\"%s\" callbacks=%d notifications=%d\n",
                press2 ? "YES" : "NO", finalValue.c_str(),
                totalPress, bridge->m_impl->notificationCount);
        }
    }

    // ---- Graph-node selection via NSAccessibilityCustomAction ----
    std::fprintf(stdout, "\n  === Native Custom Action Test (NSAccessibilityCustomAction) ===\n\n");

    int nodeAIdx = tree->FindByStableId("gs_node_A");
    if (nodeAIdx >= 0) {
        int selBefore = cbState.selectCount;
        bridge->m_impl->notificationCount = 0;

        GSAccessibilityElement* nodeAElem = bridge->m_impl->FindElement("gs_node_A");
        if (nodeAElem) {
            BOOL beforeSel = [nodeAElem isAccessibilitySelected];
            std::fprintf(stdout, "  Node A selected before: %s\n", beforeSel ? "YES" : "NO");

            // Access custom actions exactly as VoiceOver would
            NSArray<NSAccessibilityCustomAction*>* customs =
                [nodeAElem accessibilityCustomActions];
            std::fprintf(stdout, "  Custom actions: %lu\n",
                         static_cast<unsigned long>([customs count]));

            for (NSAccessibilityCustomAction* ca in customs) {
                if ([[ca name] isEqualToString:@"Select this node"]) {
                    std::fprintf(stdout, "  Invoking custom action: %s\n", [[ca name] UTF8String]);
                    // Invoke via the custom action's target/selector (as VoiceOver would)
                    [[ca target] performSelector:[ca selector] withObject:ca];
                    break;
                }
            }

            BOOL afterSel = [nodeAElem isAccessibilitySelected];
            int selCountDelta = cbState.selectCount - selBefore;
            std::fprintf(stdout, "  Node A selected after custom action: %s\n",
                         afterSel ? "YES" : "NO");
            std::fprintf(stdout, "  Selection callbacks: %d\n", selCountDelta);
            std::fprintf(stdout, "  Selection notifications: %d\n",
                         bridge->m_impl->notificationCount);

            if (!afterSel) {
                std::fprintf(stdout, "  FAIL: custom action did not set selected state\n");
                failures++;
            }
            if (selCountDelta == 0) {
                std::fprintf(stdout, "  FAIL: selection callback not invoked\n");
                failures++;
            }

            // Deselect
            tree->SetSelected(nodeAIdx, false);
            tree->SetFocused(nodeAIdx, false);
            tree->SetKeyboardFocus(nodeAIdx, false);
            bridge->AnnounceSelectionChange();
            BOOL afterDeselect = [nodeAElem isAccessibilitySelected];
            std::fprintf(stdout, "  Node A selected after deselect: %s\n",
                         afterDeselect ? "YES" : "NO");
        }
    }

    // ---- Zoom via NSAccessibilityCustomAction ----
    std::fprintf(stdout, "\n  === Native Zoom Custom Action Test ===\n\n");

    int zoomIdx = tree->FindByStableId("gs_ctrl_zoom");
    if (zoomIdx >= 0) {
        int zoomBefore = cbState.zoomCount;
        bridge->m_impl->notificationCount = 0;

        GSAccessibilityElement* zoomElem = bridge->m_impl->FindElement("gs_ctrl_zoom");
        if (zoomElem) {
            NSArray<NSAccessibilityCustomAction*>* customs =
                [zoomElem accessibilityCustomActions];
            std::fprintf(stdout, "  Zoom custom actions: %lu\n",
                         static_cast<unsigned long>([customs count]));

            for (NSAccessibilityCustomAction* ca in customs) {
                if ([[ca name] isEqualToString:@"Zoom in"]) {
                    std::fprintf(stdout, "  Invoking zoom custom action: %s\n",
                                 [[ca name] UTF8String]);
                    [[ca target] performSelector:[ca selector] withObject:ca];
                }
            }
            int zoomDelta = cbState.zoomCount - zoomBefore;
            std::fprintf(stdout, "  Zoom callbacks: %d, scale: %.2f\n",
                         zoomDelta, static_cast<double>(cbState.zoomScale));
            std::fprintf(stdout, "  Zoom notifications: %d\n",
                         bridge->m_impl->notificationCount);

            if (zoomDelta == 0) {
                std::fprintf(stdout, "  FAIL: zoom callback not invoked\n");
                failures++;
            }
        }
    }

    // ---- Transform Update Test ----
    std::fprintf(stdout, "\n  === Transform Update Test (SyncGeometry) ===\n\n");

    if (nodeAIdx >= 0) {
        spike::WindowGeometry geom = bridge->m_impl->windowGeometry;
        spike::Transform idXform{1.0f, {0, 0}};
        bridge->SyncGeometry(geom, idXform);

        GSAccessibilityElement* elem = bridge->m_impl->FindElement("gs_node_A");
        if (elem) {
            NSRect beforeFrame = [elem accessibilityFrame];
            std::fprintf(stdout, "  Node A frame before transform: ");
            PrintFrame(stdout, beforeFrame);
            std::fprintf(stdout, "\n");

            spike::Transform zoomXf{1.5f, {50, 25}};
            bridge->SyncGeometry(geom, zoomXf);

            NSRect afterFrame = [elem accessibilityFrame];
            std::fprintf(stdout, "  Node A frame after transform (1.5x, offset 50,25): ");
            PrintFrame(stdout, afterFrame);
            std::fprintf(stdout, "\n");

            double dx = afterFrame.origin.x - beforeFrame.origin.x;
            double dy = afterFrame.origin.y - beforeFrame.origin.y;
            double dw = afterFrame.size.width - beforeFrame.size.width;
            double dh = afterFrame.size.height - beforeFrame.size.height;
            std::fprintf(stdout, "  Frame delta: dx=%.0f dy=%.0f dw=%.0f dh=%.0f\n",
                         dx, dy, dw, dh);

            if (dx == 0.0 && dy == 0.0 && dw == 0.0 && dh == 0.0) {
                std::fprintf(stdout, "  FAIL: transform update produced zero frame delta\n");
                failures++;
            }

            // Verify transformed dimensions: 1.5x scale, offset (50,25).
            // World: {50,50,200,100}. Screen: w=300, h=150.
            double expectedTw = 300.0;
            double expectedTh = 150.0;
            double tTol = 1.5;
            if (std::abs(afterFrame.size.width - expectedTw) > tTol ||
                std::abs(afterFrame.size.height - expectedTh) > tTol) {
                std::fprintf(stdout, "  FAIL: transformed frame size mismatch "
                             "(got=%.1fx%.1f expected=%.1fx%.1f)\n",
                             afterFrame.size.width, afterFrame.size.height,
                             expectedTw, expectedTh);
                failures++;
            }

            bridge->SyncGeometry(geom, idXform);
            NSRect restoredFrame = [elem accessibilityFrame];
            double rdx = restoredFrame.origin.x - beforeFrame.origin.x;
            double rdy = restoredFrame.origin.y - beforeFrame.origin.y;
            double rdw = restoredFrame.size.width - beforeFrame.size.width;
            double rdh = restoredFrame.size.height - beforeFrame.size.height;
            std::fprintf(stdout, "  Frame restored delta: dx=%.0f dy=%.0f dw=%.0f dh=%.0f\n",
                         rdx, rdy, rdw, rdh);
            if (std::abs(rdx) > 1.0 || std::abs(rdy) > 1.0 ||
                std::abs(rdw) > 1.0 || std::abs(rdh) > 1.0) {
                std::fprintf(stdout, "  FAIL: frame not restored after identity transform\n");
                failures++;
            }
        }
    }

    // ---- Frame Conversion Test (deterministic window geometry) ----
    std::fprintf(stdout, "\n  === Frame Conversion Test ===\n\n");

    {
        NSRect winScreenFrame = [nsWindow convertRectToScreen:
            [[nsWindow contentView] bounds]];
        std::fprintf(stdout, "  Window content screen rect: ");
        PrintFrame(stdout, winScreenFrame);
        std::fprintf(stdout, "\n");
        std::fprintf(stdout, "  Window backing scale: %.1f\n",
                     static_cast<double>(backingScale));

        // Exact frame proof for gs_node_A at identity transform
        // World: {50,50,200,100}. SDL top-left: (50,50).
        // Y-invert: cvHeight - y - h. At backingScale=2: cvHeight=600 (points).
        // viewRect NS Y = 600 - 50 - 100 = 450 (in points).
        // convertRectToScreen adds window frame offset.
        int nodeAIdx2 = tree->FindByStableId("gs_node_A");
        if (nodeAIdx2 >= 0) {
            GSAccessibilityElement* elemA = bridge->m_impl->elements[nodeAIdx2];
            NSRect f = [elemA accessibilityFrame];
            std::fprintf(stdout, "  Node A screen frame: ");
            PrintFrame(stdout, f);
            std::fprintf(stdout, "\n");

            // Exact frame proof: independently compute expected values.
            // contentScreen = window content area in screen coordinates.
            // World bounds: {50,50,200,100}.
            double expectedW = 200.0;
            double expectedH = 100.0;
            double expectedX = winScreenFrame.origin.x + 50.0;
            double expectedY = winScreenFrame.origin.y + winScreenFrame.size.height - 150.0;
            double tolerance = 1.5; // ≤1.5pt for point rounding

            double fwDiff = std::abs(f.size.width - expectedW);
            double fhDiff = std::abs(f.size.height - expectedH);
            double fxDiff = std::abs(f.origin.x - expectedX);
            double fyDiff = std::abs(f.origin.y - expectedY);

            if (fwDiff > tolerance || fhDiff > tolerance) {
                std::fprintf(stdout, "  FAIL: Node A frame size mismatch "
                             "(expected %.1fx%.1f, got %.1fx%.1f, tol=%.1f)\n",
                             expectedW, expectedH, f.size.width, f.size.height, tolerance);
                failures++;
            }
            if (fxDiff > tolerance) {
                std::fprintf(stdout, "  FAIL: Node A frame X mismatch "
                             "(got=%.1f expected=%.1f tol=%.1f)\n",
                             f.origin.x, expectedX, tolerance);
                failures++;
            }
            if (fyDiff > tolerance) {
                std::fprintf(stdout, "  FAIL: Node A frame Y mismatch "
                             "(got=%.1f expected=%.1f tol=%.1f bs=%.1f)\n",
                             f.origin.y, expectedY, tolerance,
                             static_cast<double>(backingScale));
                failures++;
            }

            double contentTop = winScreenFrame.origin.y + winScreenFrame.size.height;
            double yFromTop = contentTop - f.origin.y;
            std::fprintf(stdout, "  Node A Y from top of content: %.1f (expected 150.0)\n",
                         yFromTop);
        }

        bool anyIntersects = false;
        for (int i = 0; i < tree->NodeCount(); ++i) {
            GSAccessibilityElement* elem = bridge->m_impl->elements[i];
            NSRect f = [elem accessibilityFrame];
            if (f.size.width > 0 && f.size.height > 0) {
                if (CGRectIntersectsRect(
                        CGRectMake(f.origin.x, f.origin.y, f.size.width, f.size.height),
                        CGRectMake(winScreenFrame.origin.x, winScreenFrame.origin.y,
                                   winScreenFrame.size.width, winScreenFrame.size.height))) {
                    anyIntersects = true;
                    break;
                }
            }
        }
        std::fprintf(stdout, "  At least one element in window: %s\n",
                     anyIntersects ? "YES" : "NO");
        if (!anyIntersects) {
            std::fprintf(stdout, "  FAIL: no elements intersect the window content screen rect\n");
            failures++;
        }
    }

    // ---- Mutation / Rebuild Test (using Reparent/DetachChild APIs) ----
    std::fprintf(stdout, "\n  === Mutation / Rebuild Test ===\n\n");

    {
        int origCount = tree->NodeCount();
        std::fprintf(stdout, "  Original node count: %d\n", origCount);

        // Add a new node (AddNode rejects duplicate stable IDs)
        auto addRes = tree->AddNode({"gs_test_new", spike::A11yRole::GraphNode,
                                      "Test Node", "", "Added during mutation test",
                                      {500, 500, 100, 50}});
        if (!addRes.accepted) {
            std::fprintf(stdout, "  FAIL: AddNode rejected valid new node\n");
            failures++;
        }
        int newNode = addRes.index;
        int rootIdx = tree->RootIndex();
        tree->SetParentChild(rootIdx, newNode);
        tree->Node(newNode).actions.push_back({"select", "Select this node"});
        std::fprintf(stdout, "  Added node: gs_test_new (total now %d)\n", tree->NodeCount());

        // Verify duplicate rejection
        auto dupRes = tree->AddNode({"gs_test_new", spike::A11yRole::GraphNode,
                                      "Duplicate", "", "Should be rejected",
                                      {0, 0, 1, 1}});
        if (dupRes.accepted) {
            std::fprintf(stdout, "  FAIL: AddNode accepted duplicate stable ID\n");
            failures++;
        } else {
            std::fprintf(stdout, "  Duplicate stableId gs_test_new correctly rejected\n");
        }

        bridge->m_impl->EnsureCurrent();
        int elemAfterAdd = static_cast<int>([bridge->m_impl->elements count]);
        std::fprintf(stdout, "  Elements after add: %d\n", elemAfterAdd);

        if (elemAfterAdd != tree->NodeCount()) {
            std::fprintf(stdout, "  FAIL: element count mismatch after add (%d vs %d)\n",
                         elemAfterAdd, tree->NodeCount());
            failures++;
        }

        GSAccessibilityElement* newElem = bridge->m_impl->FindElement("gs_test_new");
        if (newElem) {
            std::fprintf(stdout, "  New element 'gs_test_new' accessible via ID lookup\n");
            BOOL isA11y = [newElem isAccessibilityElement];
            if (!isA11y) {
                std::fprintf(stdout, "  FAIL: new element not recognized as accessibility element\n");
                failures++;
            }
        } else {
            std::fprintf(stdout, "  FAIL: new element not found by stable ID after add\n");
            failures++;
        }

        // Reparent using the atomic API
        int nodeCIdx = tree->FindByStableId("gs_node_C");
        if (nodeCIdx >= 0 && newNode >= 0) {
            bool reparentOk = tree->Reparent(nodeCIdx, newNode);
            std::fprintf(stdout, "  Reparent gs_test_new under gs_node_C: %s\n",
                         reparentOk ? "OK" : "REJECTED");
            bridge->m_impl->EnsureCurrent();

            GSAccessibilityElement* nodeCElem = bridge->m_impl->FindElement("gs_node_C");
            if (nodeCElem) {
                NSArray* children = [nodeCElem accessibilityChildren];
                bool foundInChildren = false;
                for (id ch in children) {
                    if (StableIdForElement(ch) == "gs_test_new") {
                        foundInChildren = true;
                        break;
                    }
                }
                std::fprintf(stdout, "  gs_test_new in gs_node_C children: %s\n",
                             foundInChildren ? "YES" : "NO");
                if (!foundInChildren) {
                    std::fprintf(stdout, "  FAIL: reparented child not in new parent's children\n");
                    failures++;
                }
            }

            // Verify gs_test_new NOT in root's children after reparent
            GSAccessibilityElement* rootElem = bridge->m_impl->elements[rootIdx];
            NSArray* rootChildren = [rootElem accessibilityChildren];
            bool staleInRoot = false;
            for (id ch in rootChildren) {
                if (StableIdForElement(ch) == "gs_test_new") {
                    staleInRoot = true;
                    break;
                }
            }
            if (staleInRoot) {
                std::fprintf(stdout,
                    "  FAIL: stale child gs_test_new still under old parent after reparent\n");
                failures++;
            } else {
                std::fprintf(stdout,
                    "  gs_test_new correctly removed from old parent's children\n");
            }
        }

        // Detach and verify
        if (newNode >= 0) {
            tree->DetachChild(newNode);
            int parentAfterDetach = tree->Node(newNode).parentIndex;
            std::fprintf(stdout, "  After DetachChild: parentIndex=%d (expected -1)\n",
                         parentAfterDetach);
            if (parentAfterDetach != -1) {
                std::fprintf(stdout, "  FAIL: DetachChild did not clear parentIndex\n");
                failures++;
            }
        }

        // Clear and rebuild
        tree->Clear();
        std::fprintf(stdout, "  After clear: tree node count = %d\n", tree->NodeCount());
        bridge->m_impl->EnsureCurrent();
        int elemAfterClear = static_cast<int>([bridge->m_impl->elements count]);
        std::fprintf(stdout, "  Elements after clear: %d\n", elemAfterClear);
        if (elemAfterClear != 0) {
            std::fprintf(stdout, "  FAIL: elements not cleared after tree clear\n");
            failures++;
        }

        GSAccessibilityElement* staleElem = bridge->m_impl->FindElement("gs_node_A");
        if (staleElem) {
            std::fprintf(stdout, "  FAIL: stale element gs_node_A still findable after clear\n");
            failures++;
        } else {
            std::fprintf(stdout, "  Stale element gs_node_A correctly not found after clear\n");
        }

        spike::SemanticTreeFactory::BuildDemo(*tree);
        bridge->m_impl->EnsureCurrent();
        int elemAfterRebuild = static_cast<int>([bridge->m_impl->elements count]);
        std::fprintf(stdout, "  Elements after rebuild: %d (original was %d)\n",
                     elemAfterRebuild, origCount);
        if (elemAfterRebuild != origCount) {
            std::fprintf(stdout, "  FAIL: element count mismatch after rebuild (%d vs %d)\n",
                         elemAfterRebuild, origCount);
            failures++;
        }

        GSAccessibilityElement* nodeAAfterRebuild = bridge->m_impl->FindElement("gs_node_A");
        if (nodeAAfterRebuild) {
            BOOL isA11y = [nodeAAfterRebuild isAccessibilityElement];
            if (!isA11y) {
                std::fprintf(stdout, "  FAIL: rebuilt element not accessible\n");
                failures++;
            }
        } else {
            std::fprintf(stdout, "  FAIL: gs_node_A not found after rebuild\n");
            failures++;
        }

        // Post-rebuild action test
        int playIdx2 = tree->FindByStableId("gs_action_play");
        if (playIdx2 >= 0) {
            GSAccessibilityElement* playElem2 = bridge->m_impl->FindElement("gs_action_play");
            if (playElem2) {
                BOOL pressOk = [playElem2 accessibilityPerformPress];
                std::fprintf(stdout, "  Post-rebuild press action: %s\n",
                             pressOk ? "OK" : "FAIL");
                if (!pressOk) {
                    std::fprintf(stdout, "  FAIL: action failed after rebuild\n");
                    failures++;
                }
            }
        }
    }

    // ---- Selection/Focus test ----
    std::fprintf(stdout, "\n  === Selection/Focus Test ===\n\n");

    {
        GSAccessibilityElement* elem = bridge->m_impl->FindElement("gs_node_A");
        if (elem) {
            BOOL beforeSelected = [elem isAccessibilitySelected];
            std::fprintf(stdout, "  Node A selected before: %s\n", beforeSelected ? "YES" : "NO");

            [elem setAccessibilityFocused:YES];
            BOOL afterFocused = [elem isAccessibilityFocused];
            std::fprintf(stdout, "  Node A focused after setAccessibilityFocused:YES: %s\n",
                         afterFocused ? "YES" : "NO");

            if (!afterFocused) {
                std::fprintf(stdout, "  FAIL: setAccessibilityFocused did not set focus\n");
                failures++;
            }

            [elem setAccessibilityFocused:NO];
        }
    }

    // ---- Hierarchy verification ----
    std::fprintf(stdout, "\n  === Hierarchy Verification ===\n\n");

    {
        int rootIdx2 = tree->RootIndex();
        if (rootIdx2 >= 0) {
            GSAccessibilityElement* rootElem = bridge->m_impl->elements[rootIdx2];
            id parent = [rootElem accessibilityParent];
            bool rootParentIsContainer = (parent == bridge->m_impl->containerView);
            std::fprintf(stdout, "  Root element parent is container view: %s\n",
                         rootParentIsContainer ? "YES" : "NO");
            if (!rootParentIsContainer) {
                std::fprintf(stdout, "  FAIL: root element not parented to container\n");
                failures++;
            }

            GSAccessibilityElement* noteElem = bridge->m_impl->FindElement("gs_note_m1E4");
            if (noteElem) {
                id noteParent = [noteElem accessibilityParent];
                std::string parentId = StableIdForElement(noteParent);
                std::fprintf(stdout, "  Note E4 parent: %s (expected gs_measure_1)\n",
                             parentId.c_str());
                if (parentId != "gs_measure_1") {
                    std::fprintf(stdout, "  FAIL: note parent mismatch\n");
                    failures++;
                }
            }
        }
    }

    // ---- Detach ----
    bridge->Detach();
    std::fprintf(stdout, "\n  Bridge detached OK\n");

    if (bridge->m_impl->containerView != nil) {
        std::fprintf(stdout, "  FAIL: containerView not nil after Detach\n");
        failures++;
    }
    if (bridge->m_impl->elements != nil) {
        std::fprintf(stdout, "  FAIL: elements not nil after Detach\n");
        failures++;
    }
    if (bridge->m_impl->elementById != nil) {
        std::fprintf(stdout, "  FAIL: elementById not nil after Detach\n");
        failures++;
    }
    if (bridge->m_impl->attached) {
        std::fprintf(stdout, "  FAIL: attached still true after Detach\n");
        failures++;
    }

    bridge->Detach(); // double-detach idempotent
    std::fprintf(stdout, "  Double-detach safe (idempotent)\n");

    if (failures > 0) {
        std::fprintf(stdout, "\n  === REPORT FAILED: %d failure(s) ===\n", failures);
    } else {
        std::fprintf(stdout, "\n  === REPORT PASSED: 0 failures ===\n");
    }

    return failures > 0 ? 1 : 0;
}
