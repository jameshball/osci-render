#pragma once

#include <JuceHeader.h>

#include <cstdint>

static constexpr std::uint8_t transparentWindowAlphaThreshold = 50;

enum class TransparentWindowInteractionMode {
    interactive,
    alphaAware,
    passAll,
};

struct TransparentWindowInteractionContext {
    bool transparencyEnabled = false;
    bool frameRequestedVisible = true;
    bool paused = false;
    bool passThroughRequested = false;
    bool waitingForSurface = false;
    bool alphaClickThroughAllowed = true;
};

struct TransparentWindowInteractionPolicy {
    bool frameVisible = true;
    bool alphaCaptureRequired = false;
    TransparentWindowInteractionMode mode = TransparentWindowInteractionMode::interactive;

    bool operator==(const TransparentWindowInteractionPolicy&) const = default;
};

inline TransparentWindowInteractionPolicy deriveTransparentWindowInteractionPolicy(
    const TransparentWindowInteractionContext& context) {
    const bool passAll = context.transparencyEnabled && context.passThroughRequested
                      && !context.paused && context.alphaClickThroughAllowed;

    TransparentWindowInteractionPolicy policy;
    policy.frameVisible = (context.frameRequestedVisible || context.paused) && !passAll;

    if (!context.transparencyEnabled || context.paused || policy.frameVisible || !context.alphaClickThroughAllowed) {
        policy.mode = TransparentWindowInteractionMode::interactive;
    } else if (context.waitingForSurface || passAll) {
        policy.mode = TransparentWindowInteractionMode::passAll;
    } else {
        policy.mode = TransparentWindowInteractionMode::alphaAware;
    }

    policy.alphaCaptureRequired = context.transparencyEnabled
                               && (context.waitingForSurface
                                   || policy.mode == TransparentWindowInteractionMode::alphaAware);
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

struct TransparentWindowAlphaQuery {
    bool valid = false;
    juce::Point<float> normalisedPoint;
    juce::Point<float> normalisedRadius;
};

inline TransparentWindowAlphaQuery makeTransparentWindowAlphaQuery(
    juce::Rectangle<int> windowBounds, juce::Point<int> frameSize, juce::Point<int> cursor, float padding) {
    if (windowBounds.isEmpty() || frameSize.x <= 0 || frameSize.y <= 0) {
        return {};
    }

    const double targetAspect = static_cast<double>(frameSize.x) / static_cast<double>(frameSize.y);
    const double windowAspect = static_cast<double>(windowBounds.getWidth())
                              / static_cast<double>(windowBounds.getHeight());
    int fittedWidth = windowBounds.getWidth();
    int fittedHeight = windowBounds.getHeight();
    if (windowAspect > targetAspect) {
        fittedWidth = juce::roundToInt(static_cast<double>(fittedHeight) * targetAspect);
    } else {
        fittedHeight = juce::roundToInt(static_cast<double>(fittedWidth) / targetAspect);
    }
    fittedWidth = juce::jlimit(1, windowBounds.getWidth(), fittedWidth);
    fittedHeight = juce::jlimit(1, windowBounds.getHeight(), fittedHeight);
    const juce::Rectangle<int> fitted(
        windowBounds.getX() + (windowBounds.getWidth() - fittedWidth) / 2,
        windowBounds.getY() + (windowBounds.getHeight() - fittedHeight) / 2,
        fittedWidth,
        fittedHeight);
    if (fitted.isEmpty() || !fitted.contains(cursor)) {
        return {};
    }
    return {
        true,
        {static_cast<float>(cursor.x - fitted.getX()) / static_cast<float>(fitted.getWidth()),
         static_cast<float>(cursor.y - fitted.getY()) / static_cast<float>(fitted.getHeight())},
        {padding / static_cast<float>(fitted.getWidth()), padding / static_cast<float>(fitted.getHeight())},
    };
}
