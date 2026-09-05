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
    while ([[view trackingAreas] count] != 0) {
        [view removeTrackingArea:[[view trackingAreas] objectAtIndex:0]];
    }
}

} // namespace

bool TransparentWindow::isTransparencySupported() {
    return true;
}

bool TransparentWindow::supportsClickThroughInTransparentFullScreen() {
    return true;
}

juce::Rectangle<int> TransparentWindow::getTransparentFullScreenBounds(juce::Rectangle<int> displayBounds) {
    // AppKit constrains ordinary windows below the menu bar. Fit the usable
    // desktop so this adjustment cannot push the bottom of the window off-screen.
    auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(displayBounds);
    return display != nullptr ? display->userBounds.toNearestInt() : displayBounds;
}

bool TransparentWindow::deferCloseUntilFullScreenExit() {
    closeAfterFullScreenExit = fullScreenExitObserver != nullptr;
    return closeAfterFullScreenExit;
}

void TransparentWindow::removeFullScreenExitObserver() {
    if (fullScreenExitObserver != nullptr) {
        [[NSNotificationCenter defaultCenter] removeObserver:static_cast<id>(fullScreenExitObserver)];
        fullScreenExitObserver = nullptr;
    }
}

void TransparentWindow::observeNativeFullScreenExit() {
    removeFullScreenExitObserver();
    const juce::Component::SafePointer<TransparentWindow> safeWindow(this);
    fullScreenExitObserver = [[NSNotificationCenter defaultCenter]
        addObserverForName:NSWindowDidExitFullScreenNotification object:getWindow(this) queue:nil
        usingBlock:^(NSNotification*) {
            // Let JUCE finish its own fullscreen-exit handling before changing bounds.
            juce::MessageManager::callAsync([safeWindow] {
                if (safeWindow == nullptr) {
                    return;
                }
                safeWindow->removeFullScreenExitObserver();
                if (safeWindow->closeAfterFullScreenExit) {
                    safeWindow->closeAfterFullScreenExit = false;
                    safeWindow->reenterFullScreenAfterTransition = false;
                    safeWindow->closeRequested();
                } else {
                    safeWindow->updatePresentation();
                }
            });
        }];
}

void TransparentWindow::configureNativeTransparency() {
    NSWindow* window = getWindow(this);
    if (window == nil) {
        return;
    }

    [window setOpaque:NO];
    [window setBackgroundColor:[NSColor clearColor]];
    [window setHasShadow:NO];

    NSView* contentView = [window contentView];
    contentView.wantsLayer = YES;
    setViewTreeTransparent(contentView);
}

void TransparentWindow::setNativeIgnoresMouseEvents(bool ignoresMouseEvents) {
    auto* peer = getPeer();
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

bool TransparentWindow::isNativeMouseInteractionStateApplied(bool ignoresMouseEvents) const {
    auto* peer = getPeer();
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

void TransparentWindow::setMovesToActiveSpace(bool shouldMove) {
    NSWindow* window = getWindow(this);
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

void TransparentWindow::setNativeRoundedWindowRegion(float cornerRadius) {
    NSWindow* window = getWindow(this);
    NSView* contentView = window != nil ? [window contentView] : nil;
    if (contentView == nil) {
        return;
    }
    contentView.wantsLayer = YES;
    contentView.layer.cornerRadius = cornerRadius;
    contentView.layer.masksToBounds = cornerRadius > 0.0f ? YES : NO;
}

void TransparentWindow::configureOpenGLSurface(void* rawGLContext) {
    if (rawGLContext == nullptr) {
        return;
    }
    NSOpenGLContext* context = static_cast<NSOpenGLContext*>(rawGLContext);
    if ([context isKindOfClass:[NSOpenGLContext class]]) {
        GLint opacity = 0;
        [context setValues:&opacity forParameter:NSOpenGLCPSurfaceOpacity];
    }
}
#endif
