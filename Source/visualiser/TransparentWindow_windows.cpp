#include "TransparentWindow.h"

#if JUCE_WINDOWS
#include <atomic>
#include <windows.h>
#include <dwmapi.h>

namespace {

constexpr DWORD minimumRedirectionBitmapAlphaBuild = 26100;

bool supportsRedirectionBitmapAlpha() {
    static const bool supported = [] {
        using RtlGetVersionFunction = LONG (WINAPI*)(OSVERSIONINFOW*);
        const auto ntdll = ::GetModuleHandleW(L"ntdll.dll");
        if (ntdll == nullptr) {
            return false;
        }
        const auto getVersion = reinterpret_cast<RtlGetVersionFunction>(::GetProcAddress(ntdll, "RtlGetVersion"));
        if (getVersion == nullptr) {
            return false;
        }

        OSVERSIONINFOW version{};
        version.dwOSVersionInfoSize = sizeof(version);
        return getVersion(&version) == 0 && version.dwBuildNumber >= minimumRedirectionBitmapAlphaBuild;
    }();
    return supported;
}

HWND getNativeWindowHandle(juce::Component* topLevelWindow) {
    if (topLevelWindow == nullptr) {
        return nullptr;
    }
    auto* peer = topLevelWindow->getPeer();
    return peer != nullptr ? static_cast<HWND>(peer->getNativeHandle()) : nullptr;
}

void updateExtendedStyle(HWND window, LONG_PTR style) {
    ::SetLastError(ERROR_SUCCESS);
    const auto previousStyle = ::SetWindowLongPtrW(window, GWL_EXSTYLE, style);
    if (previousStyle == 0 && ::GetLastError() != ERROR_SUCCESS) {
        return;
    }
    ::SetWindowPos(window, nullptr, 0, 0, 0, 0,
                   SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER);
}

bool enableRedirectionBitmapAlpha(HWND window) {
    if (window == nullptr) {
        return false;
    }

    constexpr auto redirectionBitmapAlpha = static_cast<DWMWINDOWATTRIBUTE>(39);
    const BOOL enabled = TRUE;
    const auto result = ::DwmSetWindowAttribute(window, redirectionBitmapAlpha, &enabled, sizeof(enabled));
    if (FAILED(result)) {
        static std::atomic<bool> failureLogged { false };
        if (!failureLogged.exchange(true)) {
            juce::Logger::writeToLog("Transparent popout: DWM redirection alpha is unavailable (HRESULT "
                                     + juce::String(static_cast<juce::int64>(result)) + ").");
        }
        return false;
    }
    return true;
}

void configureGpuTransparency(HWND window) {
    if (window == nullptr || !supportsRedirectionBitmapAlpha()) {
        return;
    }

    const auto style = ::GetWindowLongPtrW(window, GWL_EXSTYLE);
    if ((style & WS_EX_LAYERED) != 0) {
        updateExtendedStyle(window, style & ~WS_EX_LAYERED);
    }
    enableRedirectionBitmapAlpha(window);
}

void setWindowIgnoresMouseEvents(HWND window, bool ignoresMouseEvents, bool useLayeredHitTesting) {
    if (window == nullptr) {
        return;
    }
    const auto style = ::GetWindowLongPtrW(window, GWL_EXSTYLE);
    auto updatedStyle = ignoresMouseEvents ? style | WS_EX_TRANSPARENT : style & ~WS_EX_TRANSPARENT;
    if (useLayeredHitTesting) {
        // The layered top-level style makes WS_EX_TRANSPARENT apply across processes even though
        // the OpenGL child continues to render normally. No software layered-window rendering is used.
        updatedStyle = ignoresMouseEvents ? updatedStyle | WS_EX_LAYERED : updatedStyle & ~WS_EX_LAYERED;
    }
    if (updatedStyle != style) {
        updateExtendedStyle(window, updatedStyle);
    }
    if (useLayeredHitTesting && ignoresMouseEvents) {
        ::SetLayeredWindowAttributes(window, 0, 255, LWA_ALPHA);
    }
}

} // namespace

bool TransparentWindow::isTransparencySupported() {
    return supportsRedirectionBitmapAlpha();
}

bool TransparentWindow::supportsClickThroughInTransparentFullScreen() {
    return false;
}

juce::Rectangle<int> TransparentWindow::getTransparentFullScreenBounds(juce::Rectangle<int> displayBounds) {
    // An exact monitor-sized window can be promoted out of desktop composition on Windows.
    return displayBounds.reduced(1);
}

void TransparentWindow::configureNativeTransparency() {
    auto* window = getNativeWindowHandle(this);
    configureGpuTransparency(window);
}

void TransparentWindow::setNativeIgnoresMouseEvents(bool ignoresMouseEvents) {
    auto* window = getNativeWindowHandle(this);
    if (window == nullptr) {
        return;
    }
    setWindowIgnoresMouseEvents(window, ignoresMouseEvents, true);
    ::EnumChildWindows(window, [](HWND child, LPARAM parameter) -> BOOL {
        setWindowIgnoresMouseEvents(child, parameter != 0, false);
        return TRUE;
    }, ignoresMouseEvents ? 1 : 0);
    if (!ignoresMouseEvents) {
        configureGpuTransparency(window);
    }
}

bool TransparentWindow::isNativeMouseInteractionStateApplied(bool ignoresMouseEvents) const {
    auto* window = getNativeWindowHandle(const_cast<TransparentWindow*>(this));
    if (window == nullptr) {
        return false;
    }
    const auto style = ::GetWindowLongPtrW(window, GWL_EXSTYLE);
    const bool ignoresMouse = (style & WS_EX_TRANSPARENT) != 0;
    const bool layered = (style & WS_EX_LAYERED) != 0;
    return ignoresMouseEvents ? ignoresMouse && layered : !ignoresMouse && !layered;
}

void TransparentWindow::setNativeAlwaysOnTop(bool) {}

void TransparentWindow::setMovesToActiveSpace(bool) {}

void TransparentWindow::setNativeRoundedWindowRegion(float cornerRadius) {
    auto* window = getNativeWindowHandle(this);
    if (window == nullptr) {
        return;
    }

    // A window region is integer-scaled and clips against window rather than client bounds,
    // which produces cropped edges while a resizable peer is changing size. Let DWM own the
    // non-client corner treatment instead.
    ::SetWindowRgn(window, nullptr, TRUE);
    constexpr auto windowCornerPreference = static_cast<DWMWINDOWATTRIBUTE>(33);
    constexpr auto windowBorderColour = static_cast<DWMWINDOWATTRIBUTE>(34);
    constexpr DWORD doNotRound = 1;
    constexpr DWORD round = 2;
    constexpr COLORREF noBorder = 0xfffffffe;
    const DWORD preference = cornerRadius > 0.0f ? round : doNotRound;
    ::DwmSetWindowAttribute(window, windowCornerPreference, &preference, sizeof(preference));
    ::DwmSetWindowAttribute(window, windowBorderColour, &noBorder, sizeof(noBorder));
}

void TransparentWindow::configureOpenGLSurface(void*) {
    auto* deviceContext = ::wglGetCurrentDC();
    if (deviceContext == nullptr) {
        return;
    }

    auto* surfaceWindow = ::WindowFromDC(deviceContext);
    configureGpuTransparency(surfaceWindow);
    configureGpuTransparency(::GetAncestor(surfaceWindow, GA_ROOT));
}
#endif
