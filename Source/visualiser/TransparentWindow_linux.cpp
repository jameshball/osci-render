#include "TransparentWindow.h"

#if JUCE_LINUX
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/shape.h>

#include <algorithm>
#include <vector>

extern "C" EGLBoolean __real_eglChooseConfig(EGLDisplay display, const EGLint* attributes, EGLConfig* configs, EGLint configSize, EGLint* configCount);

namespace {

class XDisplayConnection {
public:
    XDisplayConnection() : display(XOpenDisplay(nullptr)) {}

    ~XDisplayConnection() {
        if (display != nullptr) {
            XCloseDisplay(display);
        }
    }

    Display* get() const { return display; }

private:
    Display* display = nullptr;
};

Display* getDisplay() {
    static XDisplayConnection connection;
    return connection.get();
}

Window getWindow(const juce::Component& component) {
    auto* peer = component.getPeer();
    return peer != nullptr ? reinterpret_cast<Window>(peer->getNativeHandle()) : None;
}

bool hasExtension(Display* display, const char* name) {
    int opcode = 0;
    int firstEvent = 0;
    int firstError = 0;
    return display != nullptr && XQueryExtension(display, name, &opcode, &firstEvent, &firstError);
}

bool requestsAlpha(const EGLint* attributes) {
    if (attributes == nullptr) {
        return false;
    }
    for (auto* attribute = attributes; attribute[0] != EGL_NONE; attribute += 2) {
        if (attribute[0] == EGL_ALPHA_SIZE) {
            return attribute[1] > 0;
        }
    }
    return false;
}

bool usesAlphaVisual(Display* display, EGLDisplay eglDisplay, EGLConfig config) {
    EGLint visualId = 0;
    if (display == nullptr
        || eglGetConfigAttrib(eglDisplay, config, EGL_NATIVE_VISUAL_ID, &visualId) != EGL_TRUE) {
        return false;
    }

    XVisualInfo visualTemplate{};
    visualTemplate.visualid = static_cast<VisualID>(visualId);
    int visualCount = 0;
    auto* visuals = XGetVisualInfo(display, VisualIDMask, &visualTemplate, &visualCount);
    const bool result = visuals != nullptr && visualCount > 0 && visuals[0].depth == 32;
    if (visuals != nullptr) {
        XFree(visuals);
    }
    return result;
}

bool supportsInputPassthrough(Display* display) {
    static const bool supported = hasExtension(display, "XFIXES") && hasExtension(display, "SHAPE");
    return supported;
}

bool hasCompatibleOpenGLVisual(Display* display) {
    if (display == nullptr) {
        return false;
    }

    auto eglDisplay = eglGetPlatformDisplay(EGL_PLATFORM_X11_KHR, display, nullptr);
    if (eglDisplay == EGL_NO_DISPLAY || eglInitialize(eglDisplay, nullptr, nullptr) != EGL_TRUE) {
        return false;
    }

    // Match JUCE's default OpenGLPixelFormat. A compositor-capable ARGB peer is
    // not sufficient if JUCE's child EGL window would use an opaque X11 visual.
    const EGLint attributes[] {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 16,
        EGL_STENCIL_SIZE, 0,
        EGL_SAMPLE_BUFFERS, 0,
        EGL_SAMPLES, 0,
        EGL_NONE,
    };
    EGLConfig config = nullptr;
    EGLint configCount = 0;
    const bool configFound = eglChooseConfig(eglDisplay, attributes, &config, 1, &configCount) == EGL_TRUE
                          && configCount > 0;
    EGLint visualId = 0;
    const bool visualFound = configFound
                          && eglGetConfigAttrib(eglDisplay, config, EGL_NATIVE_VISUAL_ID, &visualId) == EGL_TRUE;
    eglTerminate(eglDisplay);
    if (!visualFound) {
        return false;
    }

    XVisualInfo visualTemplate{};
    visualTemplate.visualid = static_cast<VisualID>(visualId);
    int visualCount = 0;
    auto* visuals = XGetVisualInfo(display, VisualIDMask, &visualTemplate, &visualCount);
    const bool hasAlphaVisual = visuals != nullptr && visualCount > 0 && visuals[0].depth == 32;
    if (visuals != nullptr) {
        XFree(visuals);
    }
    return hasAlphaVisual;
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
    if (windowHasState(display, window, state) == enabled) {
        return;
    }
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
    XFlush(display);
}

void setInputPassthrough(Display* display, Window window, bool passthrough) {
    if (display == nullptr || window == None || !supportsInputPassthrough(display)) {
        return;
    }

    XserverRegion region = None;
    if (passthrough) {
        region = XFixesCreateRegion(display, nullptr, 0);
    }
    XFixesSetWindowShapeRegion(display, window, ShapeInput, 0, 0, region);
    if (region != None) {
        XFixesDestroyRegion(display, region);
    }
    XFlush(display);
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

bool isInputPassthrough(Display* display, Window window) {
    if (display == nullptr || window == None || !supportsInputPassthrough(display)) {
        return false;
    }
    int rectangleCount = 0;
    int ordering = 0;
    auto* rectangles = XShapeGetRectangles(display, window, ShapeInput, &rectangleCount, &ordering);
    if (rectangles != nullptr) {
        XFree(rectangles);
    }
    return rectangleCount == 0;
}

bool areChildWindowsInputTransparent(Display* display, Window window) {
    Window root = None;
    Window parent = None;
    Window* children = nullptr;
    unsigned int childCount = 0;
    if (display == nullptr || window == None
        || XQueryTree(display, window, &root, &parent, &children, &childCount) == False) {
        return true;
    }
    bool result = true;
    for (unsigned int child = 0; child < childCount; ++child) {
        result = result && isInputPassthrough(display, children[child]);
    }
    if (children != nullptr) {
        XFree(children);
    }
    return result;
}

juce::Rectangle<int> getDesktopWorkArea(Display* display) {
    if (display == nullptr) {
        return {};
    }
    Atom type = None;
    int format = 0;
    unsigned long count = 0;
    unsigned long remaining = 0;
    unsigned char* data = nullptr;
    const auto status = XGetWindowProperty(display, DefaultRootWindow(display),
                                           XInternAtom(display, "_NET_WORKAREA", False), 0, 4, False,
                                           XA_CARDINAL, &type, &format, &count, &remaining, &data);
    if (status != Success || type != XA_CARDINAL || format != 32 || count < 4 || data == nullptr) {
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

// Mesa exposes both opaque and ARGB native visuals for otherwise identical RGBA
// configs. JUCE accepts the first match, so prefer an actual depth-32 X11 visual.
extern "C" EGLBoolean __wrap_eglChooseConfig(EGLDisplay eglDisplay, const EGLint* attributes, EGLConfig* configs, EGLint configSize, EGLint* configCount) {
    if (configs == nullptr || configSize <= 0 || !requestsAlpha(attributes)) {
        return __real_eglChooseConfig(eglDisplay, attributes, configs, configSize, configCount);
    }

    EGLint matchCount = 0;
    if (__real_eglChooseConfig(eglDisplay, attributes, nullptr, 0, &matchCount) != EGL_TRUE || matchCount <= 0) {
        return __real_eglChooseConfig(eglDisplay, attributes, configs, configSize, configCount);
    }

    std::vector<EGLConfig> matches(static_cast<std::size_t>(matchCount));
    if (__real_eglChooseConfig(eglDisplay, attributes, matches.data(), matchCount, &matchCount) != EGL_TRUE) {
        return EGL_FALSE;
    }

    auto alphaConfig = std::find_if(matches.begin(), matches.end(), [eglDisplay](EGLConfig config) {
        return usesAlphaVisual(getDisplay(), eglDisplay, config);
    });
    if (alphaConfig != matches.end()) {
        std::rotate(matches.begin(), alphaConfig, std::next(alphaConfig));
    }

    const auto copyCount = juce::jmin(configSize, matchCount);
    std::copy_n(matches.begin(), copyCount, configs);
    if (configCount != nullptr) {
        *configCount = matchCount;
    }
    return EGL_TRUE;
}

bool TransparentWindow::isTransparencySupported() {
    static const bool supported = juce::Desktop::canUseSemiTransparentWindows()
                               && hasCompatibleOpenGLVisual(getDisplay());
    return supported;
}

bool TransparentWindow::supportsClickThroughInTransparentFullScreen() {
    return supportsInputPassthrough(getDisplay());
}

juce::Rectangle<int> TransparentWindow::getTransparentFullScreenBounds(juce::Rectangle<int> displayBounds) {
    const auto workArea = getDesktopWorkArea(getDisplay());
    const auto usableDisplay = displayBounds.getIntersection(workArea);
    return usableDisplay.isEmpty() ? displayBounds : usableDisplay;
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
    if (transparencyEnabled) {
        // Mutter promotes an undecorated display-sized window to EWMH fullscreen.
        // Keep transparent fullscreen as an ordinary composited window instead.
        setWindowState(display, window, "_NET_WM_STATE_FULLSCREEN", false);
    }
    // JUCE's EGL surface is a native child that otherwise intercepts events
    // before the parent peer can route them to the composited toolbar.
    makeChildWindowsInputTransparent(display, window);
    setWindowState(display, window, "_NET_WM_STATE_ABOVE", pinned);
    setInputPassthrough(display, window, nativeIgnoresMouseEvents);
    XFlush(display);
}

void TransparentWindow::setNativeIgnoresMouseEvents(bool ignoresMouseEvents) {
    auto* display = getDisplay();
    const auto window = getWindow(*this);
    makeChildWindowsInputTransparent(display, window);
    setInputPassthrough(display, window, ignoresMouseEvents);
}

bool TransparentWindow::isNativeMouseInteractionStateApplied(bool ignoresMouseEvents) const {
    auto* display = getDisplay();
    const auto window = getWindow(*this);
    return isInputPassthrough(display, window) == ignoresMouseEvents
        && areChildWindowsInputTransparent(display, window);
}

void TransparentWindow::setNativeAlwaysOnTop(bool alwaysOnTop) {
    setWindowState(getDisplay(), getWindow(*this), "_NET_WM_STATE_ABOVE", alwaysOnTop);
}

bool TransparentWindow::isNativeFullScreenStateActive() const {
    auto* display = getDisplay();
    const auto window = getWindow(*this);
    return display != nullptr && window != None
        && windowHasState(display, window, XInternAtom(display, "_NET_WM_STATE_FULLSCREEN", False));
}

void TransparentWindow::setMovesToActiveSpace(bool) {}
void TransparentWindow::setNativeRoundedWindowRegion(float) {}

void TransparentWindow::setNativeBounds(juce::Rectangle<int> bounds) {
    auto* display = getDisplay();
    const auto window = getWindow(*this);
    if (display == nullptr || window == None) {
        return;
    }
    XMoveResizeWindow(display, window, bounds.getX(), bounds.getY(),
                      static_cast<unsigned int>(bounds.getWidth()),
                      static_cast<unsigned int>(bounds.getHeight()));
    XFlush(display);
}

void TransparentWindow::configureOpenGLSurface(void*) {}

#endif
