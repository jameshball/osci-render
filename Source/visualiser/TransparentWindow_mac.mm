#include "TransparentWindow.h"

#if JUCE_MAC
#import <Cocoa/Cocoa.h>
#import <OpenGL/OpenGL.h>

namespace {

void setViewTreeTransparent(NSView* view) {
    if (view.wantsLayer && view.layer) {
        view.layer.opaque = NO;
        view.layer.backgroundColor = [NSColor clearColor].CGColor;
    }
    for (NSView* subview in view.subviews) {
        setViewTreeTransparent(subview);
    }
}

NSWindow* getWindow(juce::Component* topLevelWindow) {
    if (topLevelWindow == nullptr) {
        return nil;
    }
    auto* peer = topLevelWindow->getPeer();
    if (peer == nullptr) {
        return nil;
    }
    NSView* view = static_cast<NSView*>(peer->getNativeHandle());
    return view != nil ? [view window] : nil;
}

void removeMouseTrackingAreas(NSView* view) {
    for (NSTrackingArea* trackingArea in [view trackingAreas]) {
        [view removeTrackingArea:trackingArea];
    }
}

} // namespace

namespace osci::windowing {

bool isTransparencySupported() {
    return true;
}

bool supportsClickThroughInTransparentFullScreen() {
    return true;
}

juce::Rectangle<int> getTransparentFullScreenBounds(juce::Rectangle<int> displayBounds) {
    return displayBounds;
}

void configureTransparency(juce::Component* topLevelWindow) {
    NSWindow* window = getWindow(topLevelWindow);
    if (window == nil) {
        return;
    }

    [window setOpaque:NO];
    [window setBackgroundColor:[NSColor clearColor]];
    [window setHasShadow:NO];

    NSView* contentView = [window contentView];
    setViewTreeTransparent(contentView);
    if (contentView != nil) {
        contentView.wantsLayer = YES;
    }
}

void setIgnoresMouseEvents(juce::Component* topLevelWindow, bool ignoresMouseEvents) {
    auto* peer = topLevelWindow != nullptr ? topLevelWindow->getPeer() : nullptr;
    NSView* view = peer != nullptr ? static_cast<NSView*>(peer->getNativeHandle()) : nil;
    NSWindow* window = view != nil ? [view window] : nil;
    if (window == nil) {
        return;
    }

    if (ignoresMouseEvents) {
        // JUCE's peer tracking area is active even when its window is not. Leaving it installed
        // allows the ignored popout to overwrite JUCE's single component-under-mouse state after
        // AppKit has correctly routed the pointer to the window beneath it.
        // Flush any tracking-area update queued while the new native peer was being laid out before
        // removing the area, otherwise JUCE can recreate it immediately after the popout opens.
        [view updateTrackingAreas];
        removeMouseTrackingAreas(view);
        [window setAcceptsMouseMovedEvents:NO];
        [window setIgnoresMouseEvents:YES];
    } else {
        [window setIgnoresMouseEvents:NO];
        [window setAcceptsMouseMovedEvents:YES];
        [view updateTrackingAreas];
    }
}

bool isMouseInteractionStateApplied(juce::Component* topLevelWindow, bool ignoresMouseEvents) {
    auto* peer = topLevelWindow != nullptr ? topLevelWindow->getPeer() : nullptr;
    NSView* view = peer != nullptr ? static_cast<NSView*>(peer->getNativeHandle()) : nil;
    NSWindow* window = view != nil ? [view window] : nil;
    if (window == nil) {
        return false;
    }

    const bool windowIgnoresMouse = [window ignoresMouseEvents] == YES;
    const bool acceptsMouseMovedEvents = [window acceptsMouseMovedEvents] == YES;
    const bool hasTrackingAreas = [[view trackingAreas] count] != 0;
    if (ignoresMouseEvents) {
        return windowIgnoresMouse && !acceptsMouseMovedEvents && !hasTrackingAreas;
    }
    return !windowIgnoresMouse && acceptsMouseMovedEvents && hasTrackingAreas;
}

void setMovesToActiveSpace(juce::Component* topLevelWindow, bool shouldMove) {
    NSWindow* window = getWindow(topLevelWindow);
    if (window == nil) {
        return;
    }

    auto behaviour = [window collectionBehavior];
    if (shouldMove) {
        behaviour |= NSWindowCollectionBehaviorMoveToActiveSpace;
    } else {
        behaviour &= ~NSWindowCollectionBehaviorMoveToActiveSpace;
    }
    [window setCollectionBehavior:behaviour];
    if (shouldMove) {
        [window makeKeyAndOrderFront:nil];
    }
}

void setRoundedWindowRegion(juce::Component* topLevelWindow, float cornerRadius) {
    NSWindow* window = getWindow(topLevelWindow);
    NSView* contentView = window != nil ? [window contentView] : nil;
    if (contentView == nil) {
        return;
    }
    contentView.wantsLayer = YES;
    contentView.layer.cornerRadius = cornerRadius;
    contentView.layer.masksToBounds = cornerRadius > 0.0f ? YES : NO;
}

void configureOpenGLSurface(void* rawGLContext) {
    if (rawGLContext == nullptr) {
        return;
    }
    NSOpenGLContext* context = static_cast<NSOpenGLContext*>(rawGLContext);
    if ([context isKindOfClass:[NSOpenGLContext class]]) {
        GLint opacity = 0;
        [context setValues:&opacity forParameter:NSOpenGLCPSurfaceOpacity];
    }
}

} // namespace osci::windowing
#endif
