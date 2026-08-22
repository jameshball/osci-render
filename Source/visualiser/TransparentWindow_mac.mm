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

void enableMouseMovedEventsForApplicationWindows() {
    for (NSWindow* window in [NSApp windows]) {
        [window setAcceptsMouseMovedEvents:YES];
    }
}

} // namespace

namespace osci::windowing {

bool isTransparencySupported() {
    return true;
}

void configureTransparency(juce::Component* topLevelWindow) {
    NSWindow* window = getWindow(topLevelWindow);
    if (window == nil) {
        return;
    }

    // AppKit disables mouse-moved delivery by default. Ensure windows behind an ignored transparent
    // popout still receive the movement needed for JUCE hover tracking.
    enableMouseMovedEventsForApplicationWindows();
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
    NSWindow* window = getWindow(topLevelWindow);
    if (window != nil) {
        enableMouseMovedEventsForApplicationWindows();
        [window setIgnoresMouseEvents:ignoresMouseEvents ? YES : NO];
    }
}

void toggleWindowMaximised(juce::Component*) {}

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

float getInteractiveAlphaFloor() {
    return 0.0f;
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
