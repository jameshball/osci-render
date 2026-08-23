#include <JuceHeader.h>

#include <array>

#include "../Source/visualiser/PopoutInteractionGeometry.h"
#include "../Source/visualiser/PopoutInteractionPolicy.h"
#include "../modules/osci_gui/visualiser/osci_PopoutAlphaHitTest.h"

class PopoutInteractionTest : public juce::UnitTest {
public:
    PopoutInteractionTest() : juce::UnitTest("Popout interaction", "Visualiser") {}

    void runTest() override {
        beginTest("Alpha threshold and OpenGL Y inversion");
        std::array<unsigned char, 4 * 4 * 4> pixels{};
        pixels[(0 * 4 + 1) * 4 + 3] = popoutInteractionAlphaThreshold;
        expect(osci::popoutAlphaHitTest(pixels.data(), 4, 4, 0.375f, 0.875f, 0.0f, 0.0f,
                                        popoutInteractionAlphaThreshold));
        expect(!osci::popoutAlphaHitTest(pixels.data(), 4, 4, 0.375f, 0.125f, 0.0f, 0.0f,
                                         popoutInteractionAlphaThreshold));
        pixels[(0 * 4 + 1) * 4 + 3] = popoutInteractionAlphaThreshold - 1;
        expect(!osci::popoutAlphaHitTest(pixels.data(), 4, 4, 0.375f, 0.875f, 0.0f, 0.0f,
                                         popoutInteractionAlphaThreshold));

        beginTest("Padding includes nearby alpha");
        pixels.fill(0);
        pixels[(2 * 4 + 2) * 4 + 3] = 255;
        expect(!osci::popoutAlphaHitTest(pixels.data(), 4, 4, 0.125f, 0.375f, 0.0f, 0.0f,
                                         popoutInteractionAlphaThreshold));
        expect(osci::popoutAlphaHitTest(pixels.data(), 4, 4, 0.125f, 0.375f, 0.5f, 0.0f,
                                        popoutInteractionAlphaThreshold));

        beginTest("Empty frames do not hit");
        expect(!osci::popoutAlphaHitTest(nullptr, 0, 0, 0.5f, 0.5f, 0.0f, 0.0f,
                                         popoutInteractionAlphaThreshold));

        beginTest("Aspect-fit mapping rejects letterboxing");
        const auto outside = makePopoutAlphaQuery({0, 0, 200, 100}, {100, 100}, {25, 50}, 8.0f);
        expect(!outside.valid);
        const auto centre = makePopoutAlphaQuery({0, 0, 200, 100}, {100, 100}, {100, 50}, 8.0f);
        expect(centre.valid);
        expectWithinAbsoluteError(centre.normalisedPoint.x, 0.5f, 0.001f);
        expectWithinAbsoluteError(centre.normalisedPoint.y, 0.5f, 0.001f);
        expectWithinAbsoluteError(centre.normalisedRadius.x, 0.08f, 0.001f);

        beginTest("Alpha interaction remains active for 250 ms");
        AlphaInteractionHold hold;
        hold.update(true, false, false, 1000);
        expect(hold.isActive(1250));
        expect(!hold.isActive(1251));

        beginTest("Stationary rendered content does not reclaim ignored mouse events");
        AlphaInteractionHold hoverHold;
        hoverHold.update(true, true, false, 1000);
        expect(!hoverHold.isActive(1000));
        hoverHold.update(true, true, true, 1001);
        expect(hoverHold.isActive(1001));
        hoverHold.update(true, false, false, 1100);
        expect(hoverHold.isActive(1100));
        hoverHold.reset();
        expect(!hoverHold.isActive(1100));

        beginTest("Frame visibility overrides and restoration");
        PopoutInteractionContext context;
        expect(derivePopoutInteractionPolicy(context).frameVisible);
        context.frameRequestedVisible = false;
        expect(!derivePopoutInteractionPolicy(context).frameVisible);
        context.paused = true;
        expect(derivePopoutInteractionPolicy(context).frameVisible);
        context.paused = false;
        expect(!derivePopoutInteractionPolicy(context).frameVisible);

        beginTest("Pass-through suppresses controls without changing their preference");
        context.transparencyEnabled = true;
        context.frameRequestedVisible = true;
        context.allMouseEventsPassThroughRequested = true;
        auto policy = derivePopoutInteractionPolicy(context);
        expect(!policy.frameVisible);
        expect(context.frameRequestedVisible);
        context.allMouseEventsPassThroughRequested = false;
        policy = derivePopoutInteractionPolicy(context);
        expect(policy.frameVisible);

        beginTest("Surface refresh does not hide framed or paused controls");
        context.waitingForSurface = true;
        policy = derivePopoutInteractionPolicy(context);
        expect(policy.frameVisible);
        expect(policy.mode == PopoutInteractionMode::interactive);
        context.paused = true;
        policy = derivePopoutInteractionPolicy(context);
        expect(policy.frameVisible);
        expect(policy.mode == PopoutInteractionMode::interactive);

        beginTest("Paused presentation restores interaction but preserves pass-through intent");
        context.allMouseEventsPassThroughRequested = true;
        context.waitingForSurface = false;
        policy = derivePopoutInteractionPolicy(context);
        expect(policy.frameVisible);
        expect(context.allMouseEventsPassThroughRequested);
        expect(policy.mode == PopoutInteractionMode::interactive);

        beginTest("Opaque presentation never captures alpha or passes events through");
        context.transparencyEnabled = false;
        context.frameRequestedVisible = false;
        context.paused = false;
        policy = derivePopoutInteractionPolicy(context);
        expect(policy.mode == PopoutInteractionMode::interactive);
        expect(!policy.alphaCaptureRequired);

        beginTest("Transparent fullscreen honours platform interaction capability");
        context.transparencyEnabled = true;
        context.allMouseEventsPassThroughRequested = false;
        context.alphaClickThroughAllowed = true;
        const auto capable = derivePopoutInteractionPolicy(context);
        context.alphaClickThroughAllowed = false;
        const auto limited = derivePopoutInteractionPolicy(context);
        expect(capable.mode == PopoutInteractionMode::alphaAware);
        expect(limited.mode == PopoutInteractionMode::interactive);
    }
};

static PopoutInteractionTest popoutInteractionTest;
