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

    [window setOpaque:NO];
    [window setBackgroundColor:[NSColor clearColor]];
    [window setHasShadow:NO];

    NSView* contentView = [window contentView];
    setViewTreeTransparent(contentView);
    if (contentView != nil) {
        contentView.wantsLayer = YES;
        contentView.layer.cornerRadius = 10.0;
        contentView.layer.masksToBounds = YES;
    }
}

void setIgnoresMouseEvents(juce::Component* topLevelWindow, bool ignoresMouseEvents) {
    NSWindow* window = getWindow(topLevelWindow);
    if (window != nil) {
        [window setIgnoresMouseEvents:ignoresMouseEvents ? YES : NO];
    }
}

void setRoundedWindowRegion(juce::Component*, float) {}

bool isRecoveryModifierDown() {
    return juce::ModifierKeys::getCurrentModifiersRealtime().isCommandDown();
}

juce::String getRecoveryModifierName() {
    return "Command";
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
