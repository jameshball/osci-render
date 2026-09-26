#include <JuceHeader.h>
#include "../Source/audio/synth/PreparedDrawing.h"
#include "../Source/motion/render/TraversalEligibility.h"

class MotionDrawingGuardTest final : public juce::UnitTest {
public:
    MotionDrawingGuardTest() : juce::UnitTest("Motion prepared vector discontinuity guards", "Motion") {}

    void runTest() override {
        beginTest("Allocation eligibility proves closed-interval curve constancy structurally");
        using motion::curveConstantOnInterval;
        motion::Curve constant(2);
        expect(curveConstantOnInterval(constant, -100, 100));
        expect(!curveConstantOnInterval(constant, 2, 1));
        expect(!curveConstantOnInterval(constant, 0, std::numeric_limits<double>::infinity()));
        expect(!curveConstantOnInterval(constant, std::numeric_limits<double>::quiet_NaN(), 0));
        constant.modulation.enabled = true;
        constant.modulation.rateHz = 1;
        expect(!curveConstantOnInterval(constant, 0, 1), "Matching sine endpoints do not prove constancy");
        constant.modulation.amount = 0;
        expect(curveConstantOnInterval(constant, 0, 1));
        constant.modulation.enabled = false;
        constant.modulation.amount = .5;
        expect(curveConstantOnInterval(constant, 0, 1));

        motion::Curve holds;
        holds.setKey({0, 2, motion::Interpolation::hold});
        holds.setKey({1, 3, motion::Interpolation::hold});
        holds.setKey({2, 2, motion::Interpolation::hold});
        expect(curveConstantOnInterval(holds, -1, std::nextafter(1.0, 0.0)));
        expect(!curveConstantOnInterval(holds, 0, 1), "Closed ending boundary includes a jump");
        expect(curveConstantOnInterval(holds, 1, std::nextafter(2.0, 1.0)));
        expect(curveConstantOnInterval(holds, 2, 10));
        expect(curveConstantOnInterval(holds, 1, 1));
        expect(!curveConstantOnInterval(holds, 0, 2), "Equal outer values cannot hide an interior excursion");

        for (const auto interpolation : {motion::Interpolation::linear, motion::Interpolation::smooth, motion::Interpolation::cubic}) {
            motion::Curve flat;
            flat.setKey({0, 2, interpolation});
            flat.setKey({1, 2, interpolation});
            flat.setKey({2, 2, interpolation});
            expect(curveConstantOnInterval(flat, -.5, 2.5));
            flat.setKey({1, 3, interpolation});
            expect(!curveConstantOnInterval(flat, .1, .9));
            expect(!curveConstantOnInterval(flat, 0, 2));
            expect(curveConstantOnInterval(flat, -2, 0));
            expect(curveConstantOnInterval(flat, 2, 3));
        }
        motion::Curve cubic;
        cubic.setKey({0, 2, motion::Interpolation::cubic, 0, 1});
        cubic.setKey({1, 2, motion::Interpolation::linear, 0, 0});
        expect(!curveConstantOnInterval(cubic, 0, 1));
        cubic.setKey({0, 2, motion::Interpolation::cubic, 99, 0});
        cubic.setKey({1, 2, motion::Interpolation::linear, 1, 99});
        expect(!curveConstantOnInterval(cubic, .2, .8));
        cubic.setKey({1, 2, motion::Interpolation::linear, 0, 99});
        expect(curveConstantOnInterval(cubic, 0, 1), "Unused outer tangents do not affect this segment");
        motion::Curve extreme;
        extreme.setKey({-std::numeric_limits<double>::max(), 2, motion::Interpolation::linear});
        extreme.setKey({std::numeric_limits<double>::max(), 2, motion::Interpolation::linear});
        expect(!curveConstantOnInterval(extreme, -1, 1));
        constant.base = std::numeric_limits<double>::infinity();
        expect(!curveConstantOnInterval(constant, 0, 1));

        beginTest("Flat cubic allocation weights evaluate exactly at every sampled time");
        for (const auto value : {0.0, .1, .22, 1.0, 2.0, -3.7, 1000000.0, std::numeric_limits<double>::max()}) {
            motion::Curve flatCubic;
            flatCubic.setKey({-.37, value, motion::Interpolation::cubic, 19, 0});
            flatCubic.setKey({1.73, value, motion::Interpolation::smooth, 0, -23});
            expect(curveConstantOnInterval(flatCubic, -.37, 1.73));
            for (int step = 0; step <= 257; ++step) {
                const auto time = -.37 + 2.1 * (step / 257.0);
                expectEquals(flatCubic.evaluateBase(time), value);
                expectEquals(flatCubic.evaluate(time), value);
            }
            for (const auto time : {-.5, -.37, std::nextafter(-.37, 1.73), std::nextafter(1.73, -.37), 1.73, 2.0}) {
                expectEquals(flatCubic.evaluateBase(time), value);
            }
        }
        motion::Curve nonFlatCubic;
        nonFlatCubic.setKey({0, 2, motion::Interpolation::cubic, 0, 1});
        nonFlatCubic.setKey({1, 2, motion::Interpolation::linear, 0, 0});
        expectEquals(nonFlatCubic.evaluateBase(.5), 2.125);
        nonFlatCubic.setKey({0, 2, motion::Interpolation::cubic, 0, 0});
        nonFlatCubic.setKey({1, 3, motion::Interpolation::linear, 0, 0});
        expectEquals(nonFlatCubic.evaluateBase(.5), 2.5);

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

        beginTest("Integer traversal preserves every closed polygon corner and closure");
        expectEquals(closed.minimumTraversalSamples(), std::int64_t(7));
        const std::array<osci::Point, 7> squarePoints { osci::Point(0, 0), osci::Point(0, 0),
            osci::Point(1, 0), osci::Point(1, 1), osci::Point(0, 1), osci::Point(0, 0), osci::Point(0, 0) };
        for (std::int64_t index = 0; index < 7; ++index) {
            const auto point = closed.sampleTraversal(index, 7);
            samePosition(point, squarePoints[static_cast<std::size_t>(index)]);
            expect(dark(point) == (index == 0 || index == 6));
        }

        beginTest("Disconnected integer traversals reserve dark endpoints without losing lit endpoints");
        expectEquals(uneven.minimumTraversalSamples(), std::int64_t(8));
        // Six extras split 2:4 between lengths 1:2; both full strokes survive.
        for (const auto index : {0, 5, 6, 13}) { expect(dark(uneven.sampleTraversal(index, 14))); }
        for (const auto index : {1, 2, 3, 4, 7, 8, 9, 10, 11, 12}) { expect(!dark(uneven.sampleTraversal(index, 14))); }
        expectEquals(uneven.sampleTraversal(1, 14).x, 0.0f);
        expectEquals(uneven.sampleTraversal(4, 14).x, 1.0f);
        expectEquals(uneven.sampleTraversal(7, 14).x, 10.0f);
        expectEquals(uneven.sampleTraversal(12, 14).x, 12.0f);
        expectWithinAbsoluteError(uneven.sampleTraversal(2, 14).x, 1.0f / 3.0f, 1e-6f);
        for (std::int64_t index = 0; index < 14; ++index) {
            const auto a = uneven.sampleTraversal(index, 14);
            const auto b = uneven.sampleTraversal((index + 1) % 14, 14);
            if (std::abs(a.x - b.x) > 2) { expect(dark(a) && dark(b), "Every spatial travel has two dark endpoints"); }
        }

        std::array<osci::Point, 29> disconnectedOrder;
        for (std::int64_t index = 0; index < 29; ++index) { disconnectedOrder[static_cast<std::size_t>(index)] = uneven.sampleTraversal(index, 29); }
        for (std::int64_t step = 28; step >= 0; --step) {
            const auto index = step * 11 % 29;
            const auto actual = uneven.sampleTraversal(index, 29);
            const auto expected = disconnectedOrder[static_cast<std::size_t>(index)];
            samePosition(actual, expected);
            expect(dark(actual) == dark(expected));
        }

        beginTest("Traversal ignores zero lengths and rejects missing or insufficient budgets");
        expectEquals(zeroFiltered.minimumTraversalSamples(), std::int64_t(5));
        expectEquals(empty.minimumTraversalSamples(), std::int64_t(0));
        expect(dark(empty.sampleTraversal(0, 100)));
        for (const auto count : {-1, 0, 1, 6}) {
            expect(dark(closed.sampleTraversal(0, count)));
        }
        expect(dark(closed.sampleTraversal(-1, 7)));
        expect(dark(closed.sampleTraversal(7, 7)));
        expect(dark(closed.sampleTraversal(std::numeric_limits<std::int64_t>::max(), 7)));
        const auto huge = std::numeric_limits<std::int64_t>::max();
        expect(!dark(closed.sampleTraversal(1, huge)));
        expect(dark(closed.sampleTraversal(huge - 1, huge)));
        samePosition(closed.sampleTraversal(huge - 2, huge), squarePoints[5]);

        beginTest("Curve traversal retains native shape evaluation and inherited colour");
        std::vector<std::unique_ptr<osci::Shape>> curvedShapes;
        curvedShapes.push_back(std::make_unique<osci::CubicBezierCurve>(0, 0, 0, 1, 1, 1, 1, 0));
        osci::CubicBezierCurve reference(0, 0, 0, 1, 1, 1, 1, 0);
        osci::PreparedDrawing curved(std::move(curvedShapes));
        expectEquals(curved.minimumTraversalSamples(), std::int64_t(4));
        for (std::int64_t index = 1; index <= 4; ++index) {
            const auto expected = reference.nextVector(static_cast<float>(index - 1) / 3.0f);
            const auto actual = curved.sampleTraversal(index, 6);
            samePosition(actual, expected);
            expectEquals(actual.r, expected.r);
            expectEquals(actual.g, expected.g);
            expectEquals(actual.b, expected.b);
        }

        beginTest("Traversal preserves explicit colour and is independent of sample order");
        std::vector<std::unique_ptr<osci::Shape>> colouredShapes;
        colouredShapes.push_back(std::make_unique<ColourLine>());
        osci::PreparedDrawing coloured(std::move(colouredShapes));
        std::array<osci::Point, 19> ordered;
        for (std::int64_t index = 0; index < 19; ++index) { ordered[static_cast<std::size_t>(index)] = coloured.sampleTraversal(index, 19); }
        // Multiplication by seven permutes all indices modulo this prime count.
        for (std::int64_t step = 0; step < 19; ++step) {
            const auto index = step * 7 % 19;
            const auto actual = coloured.sampleTraversal(index, 19);
            const auto expected = ordered[static_cast<std::size_t>(index)];
            samePosition(actual, expected);
            expectEquals(actual.r, expected.r);
            expectEquals(actual.g, expected.g);
            expectEquals(actual.b, expected.b);
            if (index > 0 && index < 18) {
                expectEquals(actual.r, .2f);
                expectEquals(actual.g, .4f);
                expectEquals(actual.b, .8f);
            } else { expect(dark(actual)); }
        }

    }

private:
    class ColourLine final : public osci::Line {
    public:
        ColourLine() : osci::Line(0, 0, 1, 0) {}
        osci::Point nextVector(float progress) override {
            auto point = osci::Line::nextVector(progress);
            point.r = .2f; point.g = .4f; point.b = .8f;
            return point;
        }
    };
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
