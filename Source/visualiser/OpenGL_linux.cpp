#include <JuceHeader.h>
#include "OpenGL_linux.h"

#if JUCE_LINUX
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrender.h>

#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <vector>

extern "C" EGLBoolean __real_eglChooseConfig(EGLDisplay display, const EGLint* attributes, EGLConfig* configs, EGLint configSize, EGLint* configCount);
extern "C" EGLBoolean __real_eglInitialize(EGLDisplay display, EGLint* major, EGLint* minor);
extern "C" EGLBoolean __real_eglTerminate(EGLDisplay display);

namespace {

struct EGLDisplayReferences {
    // Initialisation and termination may block inside the driver.
    std::mutex mutex;
    std::map<EGLDisplay, unsigned int> counts;
};

EGLDisplayReferences& getEGLDisplayReferences() {
    static EGLDisplayReferences references;
    return references;
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

} // namespace

bool osci::hasCompatibleOpenGLVisual(Display* display) {
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

// JUCE initialises/terminates the same EGL display for each OpenGL context.
// EGL does not count these calls: hiding one visualiser would otherwise destroy
// the editor's surfaces too. Terminate only when the last context releases it.
extern "C" EGLBoolean __wrap_eglInitialize(EGLDisplay display, EGLint* major, EGLint* minor) {
    auto& references = getEGLDisplayReferences();
    const std::lock_guard<std::mutex> lock(references.mutex);
    const auto result = __real_eglInitialize(display, major, minor);
    if (result == EGL_TRUE) {
        ++references.counts[display];
    }
    return result;
}

extern "C" EGLBoolean __wrap_eglTerminate(EGLDisplay display) {
    auto& references = getEGLDisplayReferences();
    const std::lock_guard<std::mutex> lock(references.mutex);
    const auto entry = references.counts.find(display);
    if (entry != references.counts.end()) {
        if (--entry->second != 0) {
            return EGL_TRUE;
        }
        references.counts.erase(entry);
    }
    return __real_eglTerminate(display);
}

// Mesa exposes both opaque and ARGB native visuals for otherwise identical RGBA
// configs. JUCE accepts the first match, so prefer an actual depth-32 X11 visual.
extern "C" EGLBoolean __wrap_eglChooseConfig(EGLDisplay eglDisplay, const EGLint* attributes, EGLConfig* configs, EGLint configSize, EGLint* configCount) {
    if (configs == nullptr || configSize <= 0 || configCount == nullptr || !requestsAlpha(attributes)
        || getAlphaVisualId().load(std::memory_order_acquire) == 0) {
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
    *configCount = copyCount;
    return EGL_TRUE;
}

#endif
