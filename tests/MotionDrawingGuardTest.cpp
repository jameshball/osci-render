#include <JuceHeader.h>
#include "../Source/audio/synth/PreparedDrawing.h"

class MotionDrawingGuardTest final : public juce::UnitTest {
public:
    MotionDrawingGuardTest() : juce::UnitTest("Motion prepared vector discontinuity guards", "Motion") {}

    void runTest() override {
        beginTest("Disconnected positive-length shapes blank both sides without moving XYZ");
        std::vector<std::unique_ptr<osci::Shape>> separated;
        line(separated, 0, 0, 1, 0);
        line(separated, 5, 2, 6, 2);
        osci::PreparedDrawing drawing(std::move(separated));
        for (const auto phase : {0.495, 0.5, 0.505}) {
            const auto original = drawing.sample(phase);
            const auto guarded = drawing.sample(phase, 0.01);
            expect(!dark(original));
            expect(dark(guarded));
            samePosition(original, guarded);
        }
        expect(!dark(drawing.sample(0.25, 0.01)));
        expect(!dark(drawing.sample(0.75, 0.01)));
        expect(dark(drawing.sample(0.001, 0.01)) && dark(drawing.sample(0.999, 0.01)), "Cyclic last-to-first jump is guarded on both sides");

        beginTest("A connected closed polygon retains brightness at every join and seam");
        std::vector<std::unique_ptr<osci::Shape>> polygon;
        line(polygon, 0, 0, 1, 0);
        line(polygon, 1, 0, 1, 1);
        line(polygon, 1, 1, 0, 1);
        line(polygon, 0, 1, 0, 0);
        osci::PreparedDrawing closed(std::move(polygon));
        for (const auto phase : {0.0, 0.25, 0.5, 0.75, 0.999}) {
            expect(!dark(closed.sample(phase, 0.01)));
            expect(!dark(closed.sample(phase, 1)));
        }

        beginTest("An open path blanks only its cyclic seam");
        std::vector<std::unique_ptr<osci::Shape>> openShapes;
        line(openShapes, 0, 0, 1, 0);
        line(openShapes, 1, 0, 2, 0);
        osci::PreparedDrawing open(std::move(openShapes));
        expect(!dark(open.sample(0.5, 0.02)), "Connected join is not a discontinuity");
        expect(dark(open.sample(0.005, 0.02)) && dark(open.sample(0.995, 0.02)));
        expect(!dark(open.sample(0.005)), "Zero span retains existing brightness");

        beginTest("Zero-length entries do not create or hide positive-shape adjacency");
        std::vector<std::unique_ptr<osci::Shape>> zeros;
        line(zeros, 100, 100, 100, 100);
        line(zeros, 0, 0, 1, 0);
        line(zeros, -100, -100, -100, -100);
        line(zeros, 1, 0, 0, 0);
        line(zeros, 100, 100, 100, 100);
        osci::PreparedDrawing zeroFiltered(std::move(zeros));
        for (const auto phase : {0.0, 0.5, 0.999}) { expect(!dark(zeroFiltered.sample(phase, 0.02))); }
        expectWithinAbsoluteError(zeroFiltered.sample(1).x, 0.0f, 0.00001f);
        std::vector<std::unique_ptr<osci::Shape>> gapWithZeros;
        line(gapWithZeros, 0, 0, 1, 0);
        line(gapWithZeros, 1, 0, 1, 0);
        line(gapWithZeros, 5, 0, 6, 0);
        osci::PreparedDrawing gaps(std::move(gapWithZeros));
        expect(dark(gaps.sample(.5, .01)));
        std::vector<std::unique_ptr<osci::Shape>> allZero;
        line(allZero, 1, 1, 1, 1);
        osci::PreparedDrawing empty(std::move(allZero));
        expect(empty.empty() && dark(empty.sample(0.5, 0.1)));

        beginTest("Tolerance is absolute and includes Z discontinuities");
        std::vector<std::unique_ptr<osci::Shape>> closeEnough;
        line(closeEnough, 0, 0, 1, 0);
        line(closeEnough, 1, 0.0000001f, 0, 0);
        osci::PreparedDrawing tolerance(std::move(closeEnough));
        expect(!dark(tolerance.sample(.5, .01)));
        std::vector<std::unique_ptr<osci::Shape>> depth;
        depth.push_back(std::make_unique<osci::Line>(osci::Point(0, 0, 0), osci::Point(1, 0, 0)));
        depth.push_back(std::make_unique<osci::Line>(osci::Point(1, 0, 2), osci::Point(2, 0, 2)));
        osci::PreparedDrawing depthGap(std::move(depth));
        expect(dark(depthGap.sample(.5, .01)), "Depth jumps also blank inherited colour");

        beginTest("Invalid and large spans remain bounded and preserve geometry");
        for (const auto span : {-0.1, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
            const auto guarded = closed.sample(.3, span);
            expect(dark(guarded));
            samePosition(closed.sample(.3), guarded);
        }
        for (const auto span : {.5, 1.0, std::numeric_limits<double>::max()}) {
            expect(dark(drawing.sample(.25, span)));
            expect(!dark(closed.sample(.25, span)));
        }
        expect(dark(drawing.sample(std::numeric_limits<double>::quiet_NaN(), .1)));
        expect(dark(drawing.sample(std::numeric_limits<double>::infinity(), .1)));

        beginTest("Under-sampling a nonaligned shape boundary cannot emit a bright connector");
        std::vector<std::unique_ptr<osci::Shape>> unequal;
        line(unequal, 0, 0, 1, 0);
        line(unequal, 10, 0, 12, 0);
        osci::PreparedDrawing uneven(std::move(unequal));
        for (const auto phase : {0.25, 0.375}) {
            expect(!dark(uneven.sample(phase)));
            expect(dark(uneven.sample(phase, .125)));
        }
        expect(!dark(uneven.sample(.75, .01)));
        expect(dark(uneven.sample(.25, .125)), "Seeking and sampling order do not change guard results");
    }

private:
    static void line(std::vector<std::unique_ptr<osci::Shape>>& shapes, float x1, float y1, float x2, float y2) {
        shapes.push_back(std::make_unique<osci::Line>(osci::Point(x1, y1, 0, 0.2f, 0.4f, 0.8f), osci::Point(x2, y2, 0, 0.2f, 0.4f, 0.8f)));
    }
    static bool dark(const osci::Point& point) { return point.r == 0 && point.g == 0 && point.b == 0; }
    void samePosition(const osci::Point& a, const osci::Point& b) {
        expectEquals(a.x, b.x);
        expectEquals(a.y, b.y);
        expectEquals(a.z, b.z);
    }
};
static MotionDrawingGuardTest motionDrawingGuardTest;
