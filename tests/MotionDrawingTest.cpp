#include <JuceHeader.h>
#include "../Source/motion/model/Document.h"
#include "../Source/motion/model/Drawing.h"

class MotionDrawingTest : public juce::UnitTest {
public:
    MotionDrawingTest() : juce::UnitTest("Motion drawn shapes", "Motion") {}

    void runTest() override {
        using namespace motion::drawing;
        beginTest("Drawings round-trip through SVG with handles, corners and closure");
        {
            Drawing drawing;
            drawing.strokes.push_back(ellipse({.2f, -.1f}, {.3f, .2f}));
            drawing.strokes.push_back(rectangle({-.8f, -.8f}, {-.4f, -.5f}));
            Stroke open;
            open.anchors.push_back(Anchor::corner({0, 0}));
            open.anchors.push_back({{.5f, .5f}, {.4f, .3f}, {.6f, .7f}, true});
            open.anchors.push_back(Anchor::corner({.9f, 0}));
            drawing.strokes.push_back(open);
            const auto svg = toSvg(drawing);
            expect(isDrawing(svg));
            const auto restored = fromSvg(svg);
            expect(restored.has_value());
            if (restored.has_value()) {
                expectEquals(static_cast<int>(restored->strokes.size()), 3);
                for (std::size_t stroke = 0; stroke < 3 && stroke < restored->strokes.size(); ++stroke) {
                    const auto& a = drawing.strokes[stroke];
                    const auto& b = restored->strokes[stroke];
                    expect(b.closed == a.closed);
                    expectEquals(static_cast<int>(b.anchors.size()), static_cast<int>(a.anchors.size()));
                    for (std::size_t index = 0; index < a.anchors.size() && index < b.anchors.size(); ++index) {
                        expect(a.anchors[index].point.getDistanceFrom(b.anchors[index].point) < 1.0e-4f);
                        expect(a.anchors[index].in.getDistanceFrom(b.anchors[index].in) < 1.0e-4f);
                        expect(a.anchors[index].out.getDistanceFrom(b.anchors[index].out) < 1.0e-4f);
                        expect(b.anchors[index].smooth == a.anchors[index].smooth);
                    }
                }
            }
            // An open stroke that ends where it started stays open.
            Drawing loop;
            Stroke back;
            back.anchors = {Anchor::corner({0, 0}), Anchor::corner({.5f, 0}), Anchor::corner({0, 0})};
            loop.strokes.push_back(back);
            const auto reopened = fromSvg(toSvg(loop));
            expect(reopened.has_value() && !reopened->strokes[0].closed && reopened->strokes[0].anchors.size() == 3);
            expect(!fromSvg("<svg viewBox=\"0 0 1 1\"><path d=\"M0 0 L1 1\"/></svg>").has_value(), "ordinary SVG files are not drawings");
        }
        beginTest("Freehand input becomes a few smooth anchors close to the stroke");
        {
            std::vector<Point> points;
            for (int index = 0; index <= 400; ++index) {
                const auto angle = index / 400.0f * juce::MathConstants<float>::twoPi;
                points.push_back({.5f * std::cos(angle), .5f * std::sin(angle) + .002f * std::sin(angle * 40)});
            }
            const auto stroke = simplify(points, .01f);
            expect(stroke.anchors.size() > 6 && stroke.anchors.size() < 60, juce::String(static_cast<int>(stroke.anchors.size())));
            const auto path = toPath(stroke);
            float worst = 0;
            for (const auto& point : points) {
                juce::Point<float> nearest;
                path.getNearestPoint(point, nearest);
                worst = std::max(worst, nearest.getDistanceFrom(point));
            }
            expect(worst < .03f, "the fitted curve stays within " + juce::String(worst));
            expectEquals(static_cast<int>(simplify({{0, 0}, {1, 0}}, .01f).anchors.size()), 2);
        }
        beginTest("A drawing imports where it was drawn, unlike a normalised SVG");
        {
            Drawing drawing;
            drawing.strokes.push_back(rectangle({.5f, .5f}, {.7f, .9f}));
            motion::Asset asset;
            asset.name = "Drawing.svg";
            asset.extension = ".svg";
            const auto svg = toSvg(drawing);
            asset.data.append(svg.toRawUTF8(), svg.getNumBytesAsUTF8());
            const auto result = motion::Document::decodeAsset(asset);
            expect(result.wasOk(), result.getErrorMessage());
            if (result.wasOk()) {
                float minX = 10, maxX = -10, minY = 10, maxY = -10;
                for (int sample = 0; sample < 400; ++sample) {
                    const auto point = asset.source->sample(0, sample / 400.0);
                    minX = std::min(minX, point.x); maxX = std::max(maxX, point.x);
                    minY = std::min(minY, point.y); maxY = std::max(maxY, point.y);
                }
                expectWithinAbsoluteError(minX, .5f, .02f);
                expectWithinAbsoluteError(maxX, .7f, .02f);
                expectWithinAbsoluteError(minY, .5f, .02f);
                expectWithinAbsoluteError(maxY, .9f, .02f);
            }
        }
    }
};

static MotionDrawingTest motionDrawingTest;
