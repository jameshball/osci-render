#include "TransparentWindow.h"
#include "OpenGL_linux.h"

#if JUCE_LINUX
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/extensions/shape.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace {

Display* getDisplay() {
    static const std::unique_ptr<Display, decltype(&XCloseDisplay)> display(XOpenDisplay(nullptr), XCloseDisplay);
    return display.get();
}

Window getWindow(const juce::Component& component) {
    auto* peer = component.getPeer();
    return peer != nullptr ? reinterpret_cast<Window>(peer->getNativeHandle()) : None;
}

bool supportsInputPassthrough(Display* display) {
    static const bool supported = [display] {
        int major = 0;
        int minor = 0;
        return display != nullptr && XShapeQueryVersion(display, &major, &minor)
            && (major > 1 || (major == 1 && minor >= 1));
    }();
    return supported;
}

bool windowHasState(Display* display, Window window, Atom requestedState) {
    Atom type = None;
    int format = 0;
    unsigned long count = 0;
    unsigned long remaining = 0;
    unsigned char* data = nullptr;
    const auto status = XGetWindowProperty(display, window, XInternAtom(display, "_NET_WM_STATE", False),
                                           0, 64, False, XA_ATOM, &type, &format, &count, &remaining, &data);
    if (status != Success || type != XA_ATOM || format != 32 || data == nullptr) {
        if (data != nullptr) {
            XFree(data);
        }
        return false;
    }
    const auto* states = reinterpret_cast<const Atom*>(data);
    const bool result = std::find(states, states + count, requestedState) != states + count;
    XFree(data);
    return result;
}

void setWindowState(Display* display, Window window, const char* stateName, bool enabled) {
    if (display == nullptr || window == None) {
        return;
    }

    const auto state = XInternAtom(display, stateName, False);
    XEvent event{};
    event.xclient.type = ClientMessage;
    event.xclient.window = window;
    event.xclient.message_type = XInternAtom(display, "_NET_WM_STATE", False);
    event.xclient.format = 32;
    event.xclient.data.l[0] = enabled ? 1 : 0;
    event.xclient.data.l[1] = static_cast<long>(state);
    event.xclient.data.l[3] = 1;
    XSendEvent(display, DefaultRootWindow(display), False,
               SubstructureRedirectMask | SubstructureNotifyMask, &event);
}

void setInputPassthrough(Display* display, Window window, bool passthrough) {
    if (display == nullptr || window == None || !supportsInputPassthrough(display)) {
        return;
    }

    if (passthrough) {
        XShapeCombineRectangles(display, window, ShapeInput, 0, 0, nullptr, 0, ShapeSet, Unsorted);
    } else {
        XShapeCombineMask(display, window, ShapeInput, 0, 0, None, ShapeSet);
    }
}

void makeChildWindowsInputTransparent(Display* display, Window window) {
    Window root = None;
    Window parent = None;
    Window* children = nullptr;
    unsigned int childCount = 0;
    if (display == nullptr || window == None
        || XQueryTree(display, window, &root, &parent, &children, &childCount) == False) {
        return;
    }
    for (unsigned int child = 0; child < childCount; ++child) {
        setInputPassthrough(display, children[child], true);
    }
    if (children != nullptr) {
        XFree(children);
    }
}

juce::Rectangle<int> getDesktopWorkArea(Display* display) {
    if (display == nullptr) {
        return {};
    }

    const auto root = DefaultRootWindow(display);
    unsigned long desktop = 0;
    unsigned char* data = nullptr;
    Atom type = None;
    int format = 0;
    unsigned long count = 0;
    unsigned long remaining = 0;
    if (XGetWindowProperty(display, root, XInternAtom(display, "_NET_CURRENT_DESKTOP", False), 0, 1, False,
                           XA_CARDINAL, &type, &format, &count, &remaining, &data) == Success
        && type == XA_CARDINAL && format == 32 && count == 1 && data != nullptr) {
        desktop = *reinterpret_cast<unsigned long*>(data);
    }
    if (data != nullptr) {
        XFree(data);
        data = nullptr;
    }

    const auto offset = static_cast<long>(desktop * 4);
    if (XGetWindowProperty(display, root, XInternAtom(display, "_NET_WORKAREA", False), offset, 4, False,
                           XA_CARDINAL, &type, &format, &count, &remaining, &data) != Success
        || type != XA_CARDINAL || format != 32 || count != 4 || data == nullptr) {
        if (data != nullptr) {
            XFree(data);
        }
        return {};
    }
    const auto* values = reinterpret_cast<const unsigned long*>(data);
    const juce::Rectangle<int> result(static_cast<int>(values[0]), static_cast<int>(values[1]),
                                      static_cast<int>(values[2]), static_cast<int>(values[3]));
    XFree(data);
    return result;
}

} // namespace

bool TransparentWindow::isTransparencySupported() {
    static const bool supported = juce::Desktop::canUseSemiTransparentWindows()
                               && supportsInputPassthrough(getDisplay())
                               && osci::hasCompatibleOpenGLVisual(getDisplay());
    return supported;
}

bool TransparentWindow::supportsClickThroughInTransparentFullScreen() {
    return supportsInputPassthrough(getDisplay());
}

juce::Rectangle<int> TransparentWindow::getTransparentFullScreenBounds(juce::Rectangle<int> displayBounds) {
    auto& displays = juce::Desktop::getInstance().getDisplays();
    auto* display = displays.getDisplayForRect(displayBounds);
    const auto workArea = getDesktopWorkArea(getDisplay());
    if (display == nullptr || workArea.isEmpty()) {
        return displayBounds;
    }
    const auto physicalDisplay = displays.logicalToPhysical(displayBounds, display);
    const auto usablePhysical = physicalDisplay.getIntersection(workArea);
    return usablePhysical.isEmpty() ? displayBounds : displays.physicalToLogical(usablePhysical, display);
}

void TransparentWindow::configureNativeTransparency() {
    auto* display = getDisplay();
    const auto window = getWindow(*this);
    if (display == nullptr || window == None) {
        return;
    }

    const auto bypassCompositor = XInternAtom(display, "_NET_WM_BYPASS_COMPOSITOR", False);
    const unsigned long forceCompositing = 2;
    XChangeProperty(display, window, bypassCompositor, XA_CARDINAL, 32, PropModeReplace,
                    reinterpret_cast<const unsigned char*>(&forceCompositing), 1);
    if (transparencyEnabled || !fullScreenRequested) {
        // Mutter can promote a display-sized window to EWMH fullscreen. Clear
        // that state for transparent mode and when leaving opaque fullscreen.
        setWindowState(display, window, "_NET_WM_STATE_FULLSCREEN", false);
    }
    setNativeIgnoresMouseEvents(nativeIgnoresMouseEvents);
}

void TransparentWindow::setNativeIgnoresMouseEvents(bool ignoresMouseEvents) {
    auto* display = getDisplay();
    const auto window = getWindow(*this);
    if (display == nullptr || window == None) {
        return;
    }

    // Prevent the EGL child from disappearing between discovery and shaping.
    XGrabServer(display);
    makeChildWindowsInputTransparent(display, window);
    setInputPassthrough(display, window, ignoresMouseEvents);
    XUngrabServer(display);
    XFlush(display);
}

bool TransparentWindow::isNativeMouseInteractionStateApplied(bool ignoresMouseEvents) const {
    return nativeIgnoresMouseEvents == ignoresMouseEvents;
}

void TransparentWindow::applyAlwaysOnTop() {
    // JUCE's X11 peer cannot change this flag and recreates the peer instead.
    auto* display = getDisplay();
    setWindowState(display, getWindow(*this), "_NET_WM_STATE_ABOVE", pinned);
    if (display != nullptr) {
        XFlush(display);
    }
}

bool TransparentWindow::isNativeFullScreenStateActive() const {
    auto* display = getDisplay();
    const auto window = getWindow(*this);
    return display != nullptr && window != None
        && windowHasState(display, window, XInternAtom(display, "_NET_WM_STATE_FULLSCREEN", False));
}

void TransparentWindow::setMovesToActiveSpace(bool) {}

void TransparentWindow::setNativeRoundedWindowRegion(float radius) {
    auto* display = getDisplay();
    const auto window = getWindow(*this);
    if (display == nullptr || window == None || !supportsInputPassthrough(display)) {
        return;
    }
    if (radius <= 0.0f) {
        XShapeCombineMask(display, window, ShapeBounding, 0, 0, None, ShapeSet);
        XFlush(display);
        return;
    }

    // JUCE's resize request may still be queued on its X11 connection. Use the
    // requested component size rather than querying the previous native size.
    const auto scale = getDesktopScaleFactor() * (getPeer() != nullptr ? static_cast<float>(getPeer()->getPlatformScaleFactor()) : 1.0f);
    const int width = juce::jmax(1, juce::roundToInt(getWidth() * scale));
    const int height = juce::jmax(1, juce::roundToInt(getHeight() * scale));
    const int scaledRadius = juce::jlimit(0, juce::jmin(width, height) / 2, juce::roundToInt(radius * scale));
    std::vector<XRectangle> rectangles;
    rectangles.reserve(static_cast<std::size_t>(scaledRadius * 2 + 1));
    rectangles.push_back({ 0, static_cast<short>(scaledRadius), static_cast<unsigned short>(width),
                           static_cast<unsigned short>(juce::jmax(0, height - scaledRadius * 2)) });
    for (int y = 0; y < scaledRadius; ++y) {
        const auto dy = static_cast<double>(scaledRadius - y) - 0.5;
        const int inset = juce::roundToInt(static_cast<double>(scaledRadius)
                                          - std::sqrt(static_cast<double>(scaledRadius * scaledRadius) - dy * dy));
        const auto rowWidth = static_cast<unsigned short>(juce::jmax(0, width - inset * 2));
        rectangles.push_back({ static_cast<short>(inset), static_cast<short>(y), rowWidth, 1 });
        rectangles.push_back({ static_cast<short>(inset), static_cast<short>(height - y - 1), rowWidth, 1 });
    }
    XShapeCombineRectangles(display, window, ShapeBounding, 0, 0, rectangles.data(),
                            static_cast<int>(rectangles.size()), ShapeSet, Unsorted);
    XFlush(display);
}

void TransparentWindow::setNativeBounds(juce::Rectangle<int> bounds) {
    auto* display = getDisplay();
    const auto window = getWindow(*this);
    if (display == nullptr || window == None) {
        return;
    }
    auto& displays = juce::Desktop::getInstance().getDisplays();
    auto* targetDisplay = displays.getDisplayForRect(bounds);
    const auto physicalBounds = displays.logicalToPhysical(bounds, targetDisplay);
    XMoveResizeWindow(display, window, physicalBounds.getX(), physicalBounds.getY(),
                      static_cast<unsigned int>(physicalBounds.getWidth()),
                      static_cast<unsigned int>(physicalBounds.getHeight()));
    XFlush(display);
}

void TransparentWindow::configureOpenGLSurface(void*) {}

#endif
