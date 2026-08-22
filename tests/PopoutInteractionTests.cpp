#include <JuceHeader.h>

#include <array>

#include "../Source/visualiser/PopoutInteractionGeometry.h"
#include "../Source/visualiser/PopoutPresentationState.h"
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
        PopoutPresentationState holdState;
        holdState.registerAlphaHit(1000);
        expect(holdState.isAlphaInteractionHeld(1250));
        expect(!holdState.isAlphaInteractionHeld(1251));

        beginTest("Stationary rendered content does not reclaim ignored mouse events");
        PopoutPresentationState hoverState;
        hoverState.updateAlphaHit(true, true, false, 1000);
        expect(!hoverState.isAlphaInteractionHeld(1000));
        hoverState.updateAlphaHit(true, true, true, 1001);
        expect(hoverState.isAlphaInteractionHeld(1001));
        hoverState.updateAlphaHit(true, false, false, 1100);
        expect(hoverState.isAlphaInteractionHeld(1100));
        hoverState.resetAlphaInteraction();
        expect(!hoverState.isAlphaInteractionHeld(1100));

        beginTest("Frame visibility overrides and restoration");
        PopoutPresentationState state;
        expect(state.isFrameVisible());
        state.requestedFrameVisible = false;
        expect(!state.isFrameVisible());
        state.paused = true;
        expect(state.isFrameVisible());
        state.paused = false;
        expect(!state.isFrameVisible());
        expect(PopoutPresentationState().isFrameVisible());
    }
};

static PopoutInteractionTest popoutInteractionTest;
