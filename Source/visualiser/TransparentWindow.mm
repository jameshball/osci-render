#include "TransparentWindow.h"

#if JUCE_MAC
#import <Cocoa/Cocoa.h>
#import <OpenGL/OpenGL.h>

namespace {

static void setViewTreeTransparent(NSView* view) {
    if (view.wantsLayer && view.layer) {
        view.layer.opaque = NO;
        view.layer.backgroundColor = [NSColor clearColor].CGColor;
    }
    for (NSView* subview in view.subviews) {
        setViewTreeTransparent(subview);
    }
}

} // namespace

void configureNativeWindowTransparency(juce::Component* topLevelWindow) {
    if (topLevelWindow == nullptr) {
        return;
    }

    auto* peer = topLevelWindow->getPeer();
    if (peer == nullptr) {
        return;
    }

    NSView* view = static_cast<NSView*>(peer->getNativeHandle());
    if (view == nil) {
        return;
    }

    NSWindow* window = [view window];
    if (window != nil) {
        [window setOpaque:NO];
        [window setBackgroundColor:[NSColor clearColor]];
        [window setHasShadow:NO];
    }

    setViewTreeTransparent(view);

    NSView* contentView = window != nil ? [window contentView] : view;
    if (contentView != nil) {
        contentView.wantsLayer = YES;
        contentView.layer.cornerRadius = 10.0;
        contentView.layer.masksToBounds = YES;
    }
}

void setNativeWindowIgnoresMouseEvents(juce::Component* topLevelWindow, bool ignoresMouseEvents) {
    if (topLevelWindow == nullptr) {
        return;
    }

    auto* peer = topLevelWindow->getPeer();
    if (peer == nullptr) {
        return;
    }

    NSView* view = static_cast<NSView*>(peer->getNativeHandle());
    NSWindow* window = view != nil ? [view window] : nil;
    if (window != nil) {
        [window setIgnoresMouseEvents:ignoresMouseEvents ? YES : NO];
    }
}

void configureOpenGLSurfaceTransparency(void* rawGLContext) {
    if (rawGLContext == nullptr) {
        return;
    }

    NSOpenGLContext* context = static_cast<NSOpenGLContext*>(rawGLContext);
    if ([context isKindOfClass:[NSOpenGLContext class]]) {
        GLint opacity = 0;
        [context setValues:&opacity forParameter:NSOpenGLCPSurfaceOpacity];
    }
}

#else
void configureNativeWindowTransparency(juce::Component*) {}
void setNativeWindowIgnoresMouseEvents(juce::Component*, bool) {}
void configureOpenGLSurfaceTransparency(void*) {}
#endif
