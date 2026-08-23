#pragma once

#include <cstdint>

enum class PopoutInteractionMode {
    interactive,
    alphaAware,
    passAll,
};

struct PopoutInteractionContext {
    bool transparencyEnabled = false;
    bool frameRequestedVisible = true;
    bool paused = false;
    bool allMouseEventsPassThroughRequested = false;
    bool waitingForSurface = false;
    bool alphaClickThroughAllowed = true;
};

struct PopoutInteractionPolicy {
    bool frameVisible = true;
    bool alphaCaptureRequired = false;
    PopoutInteractionMode mode = PopoutInteractionMode::interactive;

    bool operator==(const PopoutInteractionPolicy&) const = default;
};

inline PopoutInteractionPolicy derivePopoutInteractionPolicy(const PopoutInteractionContext& context) {
    const bool passAll = context.transparencyEnabled
                      && context.allMouseEventsPassThroughRequested
                      && !context.paused;

    PopoutInteractionPolicy policy;
    policy.frameVisible = (context.frameRequestedVisible || context.paused) && !passAll;

    if (!context.transparencyEnabled || context.paused || policy.frameVisible || !context.alphaClickThroughAllowed) {
        policy.mode = PopoutInteractionMode::interactive;
    } else if (context.waitingForSurface || passAll) {
        policy.mode = PopoutInteractionMode::passAll;
    } else {
        policy.mode = PopoutInteractionMode::alphaAware;
    }

    policy.alphaCaptureRequired = context.transparencyEnabled
                               && (context.waitingForSurface || policy.mode == PopoutInteractionMode::alphaAware);
    return policy;
}

class AlphaInteractionHold {
public:
    static constexpr std::uint32_t durationMs = 250;

    void update(bool alphaHit, bool mouseEventsIgnored, bool movedBeyondPadding, std::uint32_t now) {
        if (alphaHit && (!mouseEventsIgnored || movedBeyondPadding)) {
            lastHitTime = now;
            hasHit = true;
        }
    }

    void reset() {
        hasHit = false;
    }

    bool isActive(std::uint32_t now) const {
        return hasHit && now - lastHitTime <= durationMs;
    }

private:
    std::uint32_t lastHitTime = 0;
    bool hasHit = false;
};
