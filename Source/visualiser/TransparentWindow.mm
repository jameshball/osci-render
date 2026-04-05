#include "TransparentWindow.h"

#if JUCE_MAC
#import <Cocoa/Cocoa.h>
#import <OpenGL/OpenGL.h>

static void setViewTreeTransparent(NSView* view) {
    if (view.wantsLayer && view.layer) {
        view.layer.opaque = NO;
        view.layer.backgroundColor = [NSColor clearColor].CGColor;
    }
    for (NSView* subview in view.subviews) {
        setViewTreeTransparent(subview);
    }
}

void configureNativeWindowTransparency(juce::Component* topLevelWindow) {
    if (topLevelWindow == nullptr)
        return;

    if (auto* peer = topLevelWindow->getPeer()) {
        NSView* view = (NSView*)peer->getNativeHandle();
        if (view != nil) {
            NSWindow* window = [view window];
            if (window != nil) {
                [window setOpaque:NO];
                [window setBackgroundColor:[NSColor clearColor]];
                [window setHasShadow:NO];
            }
            // Also make all views in the tree non-opaque so
            // layer-backed GL views don't block transparency.
            setViewTreeTransparent(view);
            // Round the content view corners
            NSView* contentView = [view window] ? [[view window] contentView] : view;
            if (contentView) {
                contentView.wantsLayer = YES;
                contentView.layer.cornerRadius = 10.0;
                contentView.layer.masksToBounds = YES;
            }
        }
    }
}

void configureOpenGLSurfaceTransparency(void* rawGLContext) {
    if (rawGLContext == nullptr) return;
    NSOpenGLContext* nsglCtx = (NSOpenGLContext*)rawGLContext;
    if ([nsglCtx isKindOfClass:[NSOpenGLContext class]]) {
        GLint opacity = 0;
        [nsglCtx setValues:&opacity forParameter:NSOpenGLCPSurfaceOpacity];
    }
}

#else
void configureNativeWindowTransparency(juce::Component*) {}
void configureOpenGLSurfaceTransparency(void*) {}
#endif
