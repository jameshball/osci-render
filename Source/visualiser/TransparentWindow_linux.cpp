#include "TransparentWindow.h"

#if JUCE_LINUX
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrender.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/shape.h>

#include <algorithm>
#include <atomic>
#include <cmath>
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

std::atomic<VisualID>& getAlphaVisualId() {
    static std::atomic<VisualID> id{ 0 };
    return id;
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

VisualID getVisualId(EGLDisplay eglDisplay, EGLConfig config) {
    EGLint visualId = 0;
    if (eglGetConfigAttrib(eglDisplay, config, EGL_NATIVE_VISUAL_ID, &visualId) != EGL_TRUE) {
        return 0;
    }
    return static_cast<VisualID>(visualId);
}

bool usesAlphaVisual(EGLDisplay eglDisplay, EGLConfig config) {
    return getVisualId(eglDisplay, config) == getAlphaVisualId().load(std::memory_order_acquire);
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
    EGLint configCount = 0;
    if (__real_eglChooseConfig(eglDisplay, attributes, nullptr, 0, &configCount) != EGL_TRUE || configCount <= 0) {
        eglTerminate(eglDisplay);
        return false;
    }

    std::vector<EGLConfig> configs(static_cast<std::size_t>(configCount));
    if (__real_eglChooseConfig(eglDisplay, attributes, configs.data(), configCount, &configCount) != EGL_TRUE) {
        eglTerminate(eglDisplay);
        return false;
    }
    configs.resize(static_cast<std::size_t>(configCount));

    for (const auto config : configs) {
        const auto visualId = getVisualId(eglDisplay, config);
        XVisualInfo visualTemplate{};
        visualTemplate.visualid = visualId;
        int visualCount = 0;
        auto* visuals = XGetVisualInfo(display, VisualIDMask, &visualTemplate, &visualCount);
        bool foundAlphaVisual = false;
        if (visuals != nullptr && visualCount > 0) {
            const auto* format = XRenderFindVisualFormat(display, visuals[0].visual);
            if (format != nullptr && format->type == PictTypeDirect && format->direct.alphaMask != 0) {
                getAlphaVisualId().store(visualId, std::memory_order_release);
                foundAlphaVisual = true;
            }
        }
        if (visuals != nullptr) {
            XFree(visuals);
        }
        if (foundAlphaVisual) {
            break;
        }
    }
    eglTerminate(eglDisplay);
    return getAlphaVisualId().load(std::memory_order_acquire) != 0;
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

    XserverRegion region = None;
    if (passthrough) {
        region = XFixesCreateRegion(display, nullptr, 0);
    }
    XFixesSetWindowShapeRegion(display, window, ShapeInput, 0, 0, region);
    if (region != None) {
        XFixesDestroyRegion(display, region);
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
    matches.resize(static_cast<std::size_t>(matchCount));

    auto alphaConfig = std::find_if(matches.begin(), matches.end(), [eglDisplay](EGLConfig config) {
        return usesAlphaVisual(eglDisplay, config);
    });
    if (alphaConfig != matches.end()) {
        std::rotate(matches.begin(), alphaConfig, std::next(alphaConfig));
    }

    const auto copyCount = juce::jmin(configSize, matchCount);
    std::copy_n(matches.begin(), copyCount, configs);
    if (configCount != nullptr) {
        *configCount = copyCount;
    }
    return EGL_TRUE;
}

bool TransparentWindow::isTransparencySupported() {
    static const bool supported = juce::Desktop::canUseSemiTransparentWindows()
                               && supportsInputPassthrough(getDisplay())
                               && hasCompatibleOpenGLVisual(getDisplay());
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
    if (transparencyEnabled) {
        // Mutter promotes an undecorated display-sized window to EWMH fullscreen.
        // Keep transparent fullscreen as an ordinary composited window instead.
        setWindowState(display, window, "_NET_WM_STATE_FULLSCREEN", false);
    }
    setWindowState(display, window, "_NET_WM_STATE_ABOVE", pinned);
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

void TransparentWindow::setNativeAlwaysOnTop(bool alwaysOnTop) {
    auto* display = getDisplay();
    setWindowState(display, getWindow(*this), "_NET_WM_STATE_ABOVE", alwaysOnTop);
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
        XFixesSetWindowShapeRegion(display, window, ShapeBounding, 0, 0, None);
        XFlush(display);
        return;
    }

    XWindowAttributes attributes{};
    if (XGetWindowAttributes(display, window, &attributes) == False) {
        return;
    }
    const auto scale = getPeer() != nullptr ? static_cast<float>(getPeer()->getPlatformScaleFactor()) : 1.0f;
    const int scaledRadius = juce::jlimit(1, juce::jmin(attributes.width, attributes.height) / 2,
                                          juce::roundToInt(radius * scale));
    std::vector<XRectangle> rectangles;
    rectangles.reserve(static_cast<std::size_t>(scaledRadius * 2 + 1));
    rectangles.push_back({ 0, static_cast<short>(scaledRadius), static_cast<unsigned short>(attributes.width),
                           static_cast<unsigned short>(juce::jmax(0, attributes.height - scaledRadius * 2)) });
    for (int y = 0; y < scaledRadius; ++y) {
        const auto dy = static_cast<double>(scaledRadius - y) - 0.5;
        const int inset = juce::roundToInt(static_cast<double>(scaledRadius)
                                          - std::sqrt(static_cast<double>(scaledRadius * scaledRadius) - dy * dy));
        const auto width = static_cast<unsigned short>(juce::jmax(0, attributes.width - inset * 2));
        rectangles.push_back({ static_cast<short>(inset), static_cast<short>(y), width, 1 });
        rectangles.push_back({ static_cast<short>(inset), static_cast<short>(attributes.height - y - 1), width, 1 });
    }
    const auto region = XFixesCreateRegion(display, rectangles.data(), static_cast<int>(rectangles.size()));
    XFixesSetWindowShapeRegion(display, window, ShapeBounding, 0, 0, region);
    XFixesDestroyRegion(display, region);
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
