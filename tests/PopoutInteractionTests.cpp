#include <JuceHeader.h>

#include <array>

#include "../Source/visualiser/TransparentWindowInteraction.h"
#include "../modules/osci_gui/visualiser/osci_AlphaHitTest.h"

class PopoutInteractionTest : public juce::UnitTest {
public:
    PopoutInteractionTest() : juce::UnitTest("Popout interaction", "Visualiser") {}

    void runTest() override {
        beginTest("Alpha threshold and OpenGL Y inversion");
        std::array<unsigned char, 4 * 4 * 4> pixels{};
        pixels[(0 * 4 + 1) * 4 + 3] = transparentWindowAlphaThreshold;
        expect(osci::alphaHitTest(pixels.data(), 4, 4, 0.375f, 0.875f, 0.0f, 0.0f,
                                        transparentWindowAlphaThreshold));
        expect(!osci::alphaHitTest(pixels.data(), 4, 4, 0.375f, 0.125f, 0.0f, 0.0f,
                                         transparentWindowAlphaThreshold));
        pixels[(0 * 4 + 1) * 4 + 3] = transparentWindowAlphaThreshold - 1;
        expect(!osci::alphaHitTest(pixels.data(), 4, 4, 0.375f, 0.875f, 0.0f, 0.0f,
                                         transparentWindowAlphaThreshold));

        beginTest("Padding includes nearby alpha");
        pixels.fill(0);
        pixels[(2 * 4 + 2) * 4 + 3] = 255;
        expect(!osci::alphaHitTest(pixels.data(), 4, 4, 0.125f, 0.375f, 0.0f, 0.0f,
                                         transparentWindowAlphaThreshold));
        expect(osci::alphaHitTest(pixels.data(), 4, 4, 0.125f, 0.375f, 0.5f, 0.0f,
                                        transparentWindowAlphaThreshold));

        beginTest("Empty frames do not hit");
        expect(!osci::alphaHitTest(nullptr, 0, 0, 0.5f, 0.5f, 0.0f, 0.0f,
                                         transparentWindowAlphaThreshold));

        beginTest("Aspect-fit mapping rejects letterboxing");
        const auto outside = makeTransparentWindowAlphaQuery({0, 0, 200, 100}, {100, 100}, {25, 50}, 8.0f);
        expect(!outside.valid);
        const auto centre = makeTransparentWindowAlphaQuery({0, 0, 200, 100}, {100, 100}, {100, 50}, 8.0f);
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
        TransparentWindowInteractionContext context;
        expect(deriveTransparentWindowInteractionPolicy(context).frameVisible);
        context.frameRequestedVisible = false;
        expect(!deriveTransparentWindowInteractionPolicy(context).frameVisible);
        context.paused = true;
        expect(deriveTransparentWindowInteractionPolicy(context).frameVisible);
        context.paused = false;
        expect(!deriveTransparentWindowInteractionPolicy(context).frameVisible);

        beginTest("Pass-through suppresses controls without changing their preference");
        context.transparencyEnabled = true;
        context.frameRequestedVisible = true;
        context.passThroughRequested = true;
        auto policy = deriveTransparentWindowInteractionPolicy(context);
        expect(!policy.frameVisible);
        expect(context.frameRequestedVisible);
        context.passThroughRequested = false;
        policy = deriveTransparentWindowInteractionPolicy(context);
        expect(policy.frameVisible);

        beginTest("Surface refresh does not hide framed or paused controls");
        context.waitingForSurface = true;
        policy = deriveTransparentWindowInteractionPolicy(context);
        expect(policy.frameVisible);
        expect(policy.mode == TransparentWindowInteractionMode::interactive);
        context.paused = true;
        policy = deriveTransparentWindowInteractionPolicy(context);
        expect(policy.frameVisible);
        expect(policy.mode == TransparentWindowInteractionMode::interactive);

        beginTest("Paused presentation restores interaction but preserves pass-through intent");
        context.passThroughRequested = true;
        context.waitingForSurface = false;
        policy = deriveTransparentWindowInteractionPolicy(context);
        expect(policy.frameVisible);
        expect(context.passThroughRequested);
        expect(policy.mode == TransparentWindowInteractionMode::interactive);

        beginTest("Opaque presentation never captures alpha or passes events through");
        context.transparencyEnabled = false;
        context.frameRequestedVisible = false;
        context.paused = false;
        policy = deriveTransparentWindowInteractionPolicy(context);
        expect(policy.mode == TransparentWindowInteractionMode::interactive);
        expect(!policy.alphaCaptureRequired);

        beginTest("Transparent fullscreen honours platform interaction capability");
        context.transparencyEnabled = true;
        context.passThroughRequested = false;
        context.alphaClickThroughAllowed = true;
        const auto capable = deriveTransparentWindowInteractionPolicy(context);
        context.alphaClickThroughAllowed = false;
        const auto limited = deriveTransparentWindowInteractionPolicy(context);
        expect(capable.mode == TransparentWindowInteractionMode::alphaAware);
        expect(limited.mode == TransparentWindowInteractionMode::interactive);

        beginTest("Unavailable fullscreen pass-through preserves requested controls");
        context.frameRequestedVisible = true;
        context.passThroughRequested = true;
        const auto unavailable = deriveTransparentWindowInteractionPolicy(context);
        expect(unavailable.frameVisible);
        expect(unavailable.mode == TransparentWindowInteractionMode::interactive);
        expect(context.passThroughRequested);
        context.alphaClickThroughAllowed = true;
        const auto restored = deriveTransparentWindowInteractionPolicy(context);
        expect(!restored.frameVisible);
        expect(restored.mode == TransparentWindowInteractionMode::passAll);
    }
};

static PopoutInteractionTest popoutInteractionTest;
