#pragma once

#include <cstdint>

struct PopoutPresentationState {
    static constexpr std::uint32_t interactionHoldMs = 250;

    bool requestedFrameVisible = true;
    bool paused = false;
    bool recoveryModifierDown = false;
    bool menuOpen = false;
    bool gestureActive = false;
    bool presentationHintShown = false;

    bool isFrameVisible() const {
        return requestedFrameVisible || paused || recoveryModifierDown || menuOpen || gestureActive;
    }

    bool consumePresentationHint(bool enabled) {
        if (!enabled || presentationHintShown) {
            return false;
        }
        presentationHintShown = true;
        return true;
    }

    void registerAlphaHit(std::uint32_t now) {
        lastAlphaHitTime = now;
        hasAlphaHit = true;
    }

    bool isAlphaInteractionHeld(std::uint32_t now) const {
        return hasAlphaHit && now - lastAlphaHitTime <= interactionHoldMs;
    }

private:
    std::uint32_t lastAlphaHitTime = 0;
    bool hasAlphaHit = false;
};
