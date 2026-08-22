#pragma once

#include <cstdint>

struct PopoutPresentationState {
    static constexpr std::uint32_t interactionHoldMs = 250;

    bool requestedFrameVisible = true;
    bool paused = false;

    bool isFrameVisible() const {
        return requestedFrameVisible || paused;
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
