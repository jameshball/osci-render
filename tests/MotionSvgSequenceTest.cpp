#include <JuceHeader.h>
#include "../Source/motion/import/SvgSequence.h"
#include "../Source/motion/import/SourceDecoding.h"

// SVGs as an animation's frames: kept in order with their rate, each placed
// on the first's page so what stays still in them stays still on screen.
class MotionSvgSequenceTest : public juce::UnitTest {
public:
    MotionSvgSequenceTest() : juce::UnitTest("Motion SVG sequence", "Motion") {}

    static juce::String frame(int ballX) {
        return "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 200 200\"><rect x=\"90\" y=\"90\" width=\"20\" height=\"20\" fill=\"none\" stroke=\"black\"/>"
            "<circle cx=\"" + juce::String(ballX) + "\" cy=\"180\" r=\"10\" fill=\"none\" stroke=\"black\"/></svg>";
    }

    void runTest() override {
        beginTest("A sequence keeps its frames in order and its frame rate");
        {
            motion::svgsequence::Frames frames;
            frames.frameRate = 12;
            frames.svgs = {frame(20), frame(180)};
            const auto decoded = motion::svgsequence::decode(motion::svgsequence::encode(frames));
            expect(decoded.has_value());
            expectEquals(decoded->frameRate, 12.0);
            expect(decoded->svgs == frames.svgs);
            expect(!motion::svgsequence::decode(juce::MemoryBlock("<svg/>", 6)).has_value(), "an SVG alone isn't a sequence");
        }

        beginTest("Files sort as people number them, and the sequence is named for what they share");
        {
            const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory);
            juce::Array<juce::File> files {folder.getChildFile("walk_10.svg"), folder.getChildFile("walk_2.svg"), folder.getChildFile("walk_1.svg")};
            motion::svgsequence::sortByName(files);
            expectEquals(files[0].getFileName(), juce::String("walk_1.svg"));
            expectEquals(files[2].getFileName(), juce::String("walk_10.svg"));
            expectEquals(motion::svgsequence::sharedName(files), juce::String("walk"));
            expect(motion::svgsequence::numbered(files));
            expect(!motion::svgsequence::numbered({folder.getChildFile("cat.svg"), folder.getChildFile("dog.svg")}), "different drawings aren't frames");
        }

        beginTest("Frames share the first's page: what stays still doesn't move as the rest does");
        {
            motion::svgsequence::Frames frames;
            frames.svgs = {frame(20), frame(180)};
            motion::Asset asset;
            asset.extension = motion::svgsequence::extension;
            asset.data = motion::svgsequence::encode(frames);
            expect(motion::decodeAsset(asset).wasOk());
            expect(asset.source != nullptr && asset.source->frameCount() == 2);
            // The square's corners, wherever the ball is: the drawing's right
            // and left edges come from the ball, its top from the square.
            const auto top = [&](std::size_t index) {
                float highest = -1e9f;
                for (int step = 0; step <= 400; ++step) { highest = std::max(highest, asset.source->sampleFrame(index, step / 400.0, 0).y); }
                return highest;
            };
            expectWithinAbsoluteError(top(0), top(1), 1.0e-3f);
            expectWithinAbsoluteError(top(0), .09f, .01f);
        }

        beginTest("An SVG nested thousands deep is refused, not read until a thread's stack runs out");
        {
            // Parsed as an import does: on a worker thread, with its usual stack.
            const auto parse = [](int depth) {
                const auto svg = "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 100\">" + juce::String::repeatedString("<g>", depth)
                    + "<path d=\"M10 10L90 30\"/>" + juce::String::repeatedString("</g>", depth) + "</svg>";
                struct Worker final : juce::Thread {
                    explicit Worker(juce::String text) : juce::Thread("SVG import"), svg(std::move(text)) {}
                    void run() override { count = SvgParser(svg).draw().size(); }
                    juce::String svg;
                    std::size_t count = 0;
                } worker(svg);
                worker.startThread();
                worker.waitForThreadToExit(60000);
                return worker.count;
            };
            // The one line, or the cross drawn when the SVG can't be loaded.
            expectEquals(static_cast<int>(parse(200)), 1);
            expectEquals(static_cast<int>(parse(5000)), 2);
            // osci-motion says so itself: refused, it is not loaded and draws nothing.
            const auto deep = "<svg>" + juce::String::repeatedString("<g>", 5000) + juce::String::repeatedString("</g>", 5000) + "</svg>";
            expect(!SvgParser::readable(deep));
            expect(SvgParser::readable("<svg><!-- <g><g><g> --><g a=\"<g>\"/></svg>"));
            SvgParser refused(deep, false);
            expect(!refused.loaded());
            expect(refused.draw().empty());
            expect(!motion::svgsequence::decode(juce::MemoryBlock(deep.toRawUTF8(), deep.getNumBytesAsUTF8())).has_value());
        }
    }
};

static MotionSvgSequenceTest motionSvgSequenceTest;
