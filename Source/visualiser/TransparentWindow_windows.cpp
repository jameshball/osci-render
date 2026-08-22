#include "TransparentWindow.h"

#if JUCE_WINDOWS
#include <windows.h>
#include <dwmapi.h>

namespace {

constexpr DWORD minimumRedirectionBitmapAlphaBuild = 26100;

bool supportsRedirectionBitmapAlpha() {
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
}

HWND getWindowHandle(juce::Component* topLevelWindow) {
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

void enableRedirectionBitmapAlpha(HWND window) {
    if (window == nullptr) {
        return;
    }

    constexpr auto redirectionBitmapAlpha = static_cast<DWMWINDOWATTRIBUTE>(39);
    const BOOL enabled = TRUE;
    ::DwmSetWindowAttribute(window, redirectionBitmapAlpha, &enabled, sizeof(enabled));
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

namespace osci::windowing {

bool isTransparencySupported() {
    return supportsRedirectionBitmapAlpha();
}

void configureTransparency(juce::Component* topLevelWindow) {
    auto* window = getWindowHandle(topLevelWindow);
    configureGpuTransparency(window);
}

void setIgnoresMouseEvents(juce::Component* topLevelWindow, bool ignoresMouseEvents) {
    auto* window = getWindowHandle(topLevelWindow);
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

void toggleWindowMaximised(juce::Component* topLevelWindow) {
    auto* window = getWindowHandle(topLevelWindow);
    if (window == nullptr) {
        return;
    }
    ::ShowWindow(window, ::IsZoomed(window) ? SW_RESTORE : SW_MAXIMIZE);
    configureGpuTransparency(window);
}

void setMovesToActiveSpace(juce::Component*, bool) {}

void setRoundedWindowRegion(juce::Component* topLevelWindow, float cornerRadius) {
    auto* window = getWindowHandle(topLevelWindow);
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

float getInteractiveAlphaFloor() {
    return 1.0f / 255.0f;
}

void configureOpenGLSurface(void*) {
    auto* deviceContext = ::wglGetCurrentDC();
    if (deviceContext == nullptr) {
        return;
    }

    auto* surfaceWindow = ::WindowFromDC(deviceContext);
    configureGpuTransparency(surfaceWindow);
    configureGpuTransparency(::GetAncestor(surfaceWindow, GA_ROOT));
}

} // namespace osci::windowing
#endif
