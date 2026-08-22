#pragma once

#include <cstdint>

enum class PopoutInteractionMode {
    interactive,
    alphaAware,
    passAll,
};

struct PopoutPresentation {
    bool frameVisible = true;
    bool requestedFrameVisible = true;
    bool alwaysOnTop = true;
    bool fullScreen = false;
    bool allMouseEventsPassThrough = false;
    bool paused = false;
    bool clickThroughHintVisible = false;
    bool transparencyEnabled = true;
    bool clickThroughAvailable = true;
    bool alphaCaptureRequired = false;
    PopoutInteractionMode interactionMode = PopoutInteractionMode::interactive;

    bool operator==(const PopoutPresentation& other) const {
        return frameVisible == other.frameVisible
            && requestedFrameVisible == other.requestedFrameVisible
            && alwaysOnTop == other.alwaysOnTop
            && fullScreen == other.fullScreen
            && allMouseEventsPassThrough == other.allMouseEventsPassThrough
            && paused == other.paused
            && clickThroughHintVisible == other.clickThroughHintVisible
            && transparencyEnabled == other.transparencyEnabled
            && clickThroughAvailable == other.clickThroughAvailable
            && alphaCaptureRequired == other.alphaCaptureRequired
            && interactionMode == other.interactionMode;
    }

    bool operator!=(const PopoutPresentation& other) const { return !(*this == other); }
};

struct PopoutPresentationState {
    static constexpr std::uint32_t interactionHoldMs = 250;

    bool requestedFrameVisible = true;
    bool paused = false;

    bool isFrameVisible() const {
        return requestedFrameVisible || paused;
    }

    PopoutPresentation derive(bool transparencyEnabled, bool alwaysOnTop, bool fullScreen,
                              bool passAllRequested, bool clickThroughHintVisible,
                              bool waitingForSurface, bool fullScreenClickThroughSupported) const {
        const bool passAll = transparencyEnabled && passAllRequested && !paused;
        PopoutPresentation result;
        result.frameVisible = isFrameVisible() && !waitingForSurface && !passAll;
        result.requestedFrameVisible = requestedFrameVisible;
        result.alwaysOnTop = alwaysOnTop;
        result.fullScreen = fullScreen;
        result.allMouseEventsPassThrough = transparencyEnabled && passAllRequested;
        result.paused = paused;
        result.clickThroughHintVisible = clickThroughHintVisible;
        result.transparencyEnabled = transparencyEnabled;
        result.clickThroughAvailable = transparencyEnabled && (!fullScreen || fullScreenClickThroughSupported);

        if (!transparencyEnabled || paused || result.frameVisible
            || (fullScreen && !fullScreenClickThroughSupported)) {
            result.interactionMode = PopoutInteractionMode::interactive;
        } else if (waitingForSurface || passAll) {
            result.interactionMode = PopoutInteractionMode::passAll;
        } else {
            result.interactionMode = PopoutInteractionMode::alphaAware;
        }
        result.alphaCaptureRequired = transparencyEnabled
                                   && (waitingForSurface || result.interactionMode == PopoutInteractionMode::alphaAware);
        return result;
    }

    void registerAlphaHit(std::uint32_t now) {
        lastAlphaHitTime = now;
        hasAlphaHit = true;
    }

    void updateAlphaHit(bool alphaHit, bool mouseEventsIgnored, bool movedBeyondPadding, std::uint32_t now) {
        if (alphaHit && (!mouseEventsIgnored || movedBeyondPadding)) {
            registerAlphaHit(now);
        }
    }

    void resetAlphaInteraction() {
        hasAlphaHit = false;
    }

    bool isAlphaInteractionHeld(std::uint32_t now) const {
        return hasAlphaHit && now - lastAlphaHitTime <= interactionHoldMs;
    }

private:
    std::uint32_t lastAlphaHitTime = 0;
    bool hasAlphaHit = false;
};
