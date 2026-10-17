#include <JuceHeader.h>
#include "../Source/motion/live/LiveTextureController.h"

// A picture another app shares: who shares it, fitted as video frames are,
// and a capture of it at a steady rate.
class MotionLivePictureTest : public juce::UnitTest {
public:
    MotionLivePictureTest() : juce::UnitTest("Motion live picture", "Motion") {}

    void runTest() override {
        beginTest("A source remembers the app and picture it follows");
        {
            const motion::texture::Sender sender {"Composition", "Resolume Arena"};
            const auto decoded = motion::texture::decode(motion::texture::encode(sender));
            expect(decoded.has_value() && *decoded == sender);
            expectEquals(sender.describe(), juce::String("Composition (Resolume Arena)"));
            expect(!motion::texture::decode(juce::MemoryBlock("<x/>", 4)).has_value());
        }

        beginTest("A wide frame is fitted and centred in the square, its alpha made opaque");
        {
            // 4 by 2, white on the left half, rows bottom first.
            std::vector<std::uint8_t> frame(4 * 2 * 4, 0);
            for (int y = 0; y < 2; ++y) {
                for (int x = 0; x < 2; ++x) {
                    auto* pixel = frame.data() + (y * 4 + x) * 4;
                    pixel[0] = pixel[1] = pixel[2] = 255;
                }
            }
            std::vector<std::uint8_t> fitted;
            motion::texture::fit(frame.data(), 4, 2, true, 4, fitted);
            const auto at = [&](int x, int y, int channel) { return fitted[static_cast<std::size_t>((y * 4 + x) * 4 + channel)]; };
            expectEquals(static_cast<int>(at(0, 0, 3)), 0, "above the picture stays transparent");
            expectEquals(static_cast<int>(at(0, 1, 3)), 255, "the picture is opaque");
            expectEquals(static_cast<int>(at(0, 1, 0)), 255, "its left half is white");
            expectEquals(static_cast<int>(at(3, 1, 0)), 0, "its right half is black");
        }

        beginTest("A capture plays at a steady rate, each frame what had arrived by then");
        {
            motion::TextureCapture capture;
            capture.pointsPerFrame = 16;
            capture.duration = .2;
            capture.times = {0, .1};
            capture.frames = {std::vector<motion::PointSample>(16, {1, 0, 0, 1, 1, 1}), std::vector<motion::PointSample>(16, {2, 0, 0, 1, 1, 1})};
            const auto frames = capture.steady();
            expect(static_cast<bool>(frames), frames.error);
            if (!frames) { return; }
            expectEquals(static_cast<int>(frames.source->frameCount()), 6);
            expectEquals(frames.source->sample(.5 / 30, .5).x, 1.0f);
            expectEquals(frames.source->sample(5.5 / 30, .5).x, 2.0f);
        }
    }
};

static MotionLivePictureTest motionLivePictureTest;
