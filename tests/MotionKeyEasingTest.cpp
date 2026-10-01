#include <JuceHeader.h>
#include "../Source/motion/model/KeyEasing.h"

class MotionKeyEasingTest : public juce::UnitTest {
public:
    MotionKeyEasingTest() : juce::UnitTest("Motion key easing", "Motion") {}

    static motion::Curve threeKeys(motion::Interpolation first, motion::Interpolation middle) {
        motion::Curve curve;
        curve.setKey({0, 0, first});
        curve.setKey({1, 1, middle});
        curve.setKey({2, 0, motion::Interpolation::linear});
        return curve;
    }

    void runTest() override {
        beginTest("Easy ease stops the key with a third of each segment as influence");
        {
            auto curve = threeKeys(motion::Interpolation::linear, motion::Interpolation::linear);
            expect(motion::easeKey(curve, 1, true, true));
            const auto& keys = curve.keyframes();
            expect(keys[0].interpolation == motion::Interpolation::cubic && keys[1].interpolation == motion::Interpolation::cubic);
            expectEquals(keys[1].incomingSlope, 0.0);
            expectEquals(keys[1].outgoingSlope, 0.0);
            expectWithinAbsoluteError(keys[1].incomingInfluence, 1.0 / 3.0, 1e-12);
            // The linear neighbours keep their straight departure and arrival.
            expectWithinAbsoluteError(keys[0].outgoingSlope, 1.0, 1e-12);
            expectWithinAbsoluteError(keys[2].incomingSlope, -1.0, 1e-12);
            // Speed is zero at the key, so values flatten towards it.
            expect(curve.evaluate(0.95, 120) > 0.98, "the curve settles into the key");
            expect(!motion::easeKey(curve, 1, true, true), "easing again changes nothing");
        }
        beginTest("Ease in and ease out touch one side; a hold still holds");
        {
            auto curve = threeKeys(motion::Interpolation::hold, motion::Interpolation::smooth);
            expect(motion::easeKey(curve, 1, true, false) == false, "a hold into the key keeps holding");
            expect(curve.keyframes()[0].interpolation == motion::Interpolation::hold);
            expect(motion::easeKey(curve, 1, false, true));
            expect(curve.keyframes()[0].interpolation == motion::Interpolation::hold);
            expect(curve.keyframes()[1].interpolation == motion::Interpolation::cubic);
            expectEquals(curve.keyframes()[1].outgoingSlope, 0.0);
            expect(!motion::easeKey(curve, 5, true, true), "a missing key is refused");
        }
    }
};

static MotionKeyEasingTest motionKeyEasingTest;
