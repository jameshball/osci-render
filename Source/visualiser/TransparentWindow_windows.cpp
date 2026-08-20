#include "TransparentWindow.h"

#if JUCE_WINDOWS
#include <windows.h>

namespace {

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

} // namespace

namespace osci::windowing {

bool isTransparencySupported() {
    return true;
}

void configureTransparency(juce::Component* topLevelWindow) {
    if (topLevelWindow != nullptr) {
        auto* peer = topLevelWindow->getPeer();
        if (peer != nullptr) {
            const auto softwareRenderer = peer->getAvailableRenderingEngines().indexOf("Software Renderer");
            if (softwareRenderer >= 0) {
                peer->setCurrentRenderingEngine(softwareRenderer);
            }
        }
    }
    auto* window = getWindowHandle(topLevelWindow);
    if (window == nullptr) {
        return;
    }
    const auto style = ::GetWindowLongPtrW(window, GWL_EXSTYLE);
    if ((style & WS_EX_LAYERED) == 0) {
        updateExtendedStyle(window, style | WS_EX_LAYERED);
    }
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

void configureOpenGLSurface(void*) {}

} // namespace osci::windowing
#endif
