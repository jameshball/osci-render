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
    const auto style = ::GetWindowLongPtrW(window, GWL_EXSTYLE);
    const auto updatedStyle = ignoresMouseEvents ? style | WS_EX_TRANSPARENT : style & ~WS_EX_TRANSPARENT;
    if (updatedStyle != style) {
        updateExtendedStyle(window, updatedStyle);
    }
}

bool isRecoveryModifierDown() {
    return juce::ModifierKeys::getCurrentModifiersRealtime().isCtrlDown();
}

juce::String getRecoveryModifierName() {
    return "Control";
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
