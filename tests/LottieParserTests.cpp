#include <JuceHeader.h>
#include "../Source/parser/lottie/DotLottieArchive.h"
#include "../Source/parser/lottie/LottieParser.h"
#include "../modules/osci_render_core/shape/osci_Line.h"
#include "../modules/osci_render_core/shape/osci_CubicBezierCurve.h"

namespace {
using ShapeList = std::vector<std::unique_ptr<osci::Shape>>;

constexpr float kGeometryTolerance = 0.01f;

juce::String jsonNumber(float value) {
    return juce::String(value, 3);
}

juce::String identityGroupTransform() {
    return R"json({"ty":"tr","p":{"a":0,"k":[0,0]},"a":{"a":0,"k":[0,0]},"s":{"a":0,"k":[100,100]},"r":{"a":0,"k":0},"o":{"a":0,"k":100},"sk":{"a":0,"k":0},"sa":{"a":0,"k":0},"nm":"Transform"})json";
}

juce::String identityLayerTransformProperties() {
    return R"json("o":{"a":0,"k":100},"r":{"a":0,"k":0},"p":{"a":0,"k":[0,0,0]},"a":{"a":0,"k":[0,0,0]},"s":{"a":0,"k":[100,100,100]})json";
}

juce::String animatedLayerTransformProperties() {
    return R"json("o":{"a":0,"k":100},"r":{"a":0,"k":0},"p":{"a":1,"k":[{"t":0,"s":[0,0,0],"e":[20,0,0],"i":{"x":[0.833],"y":[0.833]},"o":{"x":[0.167],"y":[0.167]}},{"t":2,"s":[20,0,0]}]},"a":{"a":0,"k":[0,0,0]},"s":{"a":0,"k":[100,100,100]})json";
}

juce::String makeAnimation(const juce::String& layers, int outPoint = 1, double frameRate = 30.0) {
    return R"json({"v":"5.7.4","fr":)json" + juce::String(frameRate, 3)
        + R"json(,"ip":0,"op":)json" + juce::String(outPoint)
        + R"json(,"w":100,"h":100,"nm":"unit-test","ddd":0,"assets":[],"layers":[)json"
        + layers + R"json(]})json";
}

juce::String makeShapeLayer(const juce::String& shapeItems,
                            int layerIndex = 1,
                            int outPoint = 1,
                            const juce::String& transformProperties = identityLayerTransformProperties()) {
    return R"json({"ddd":0,"ind":)json" + juce::String(layerIndex)
        + R"json(,"ty":4,"nm":"Shape Layer","sr":1,"ks":{)json" + transformProperties
        + R"json(},"ao":0,"shapes":[)json" + shapeItems
        + R"json(],"ip":0,"op":)json" + juce::String(outPoint)
        + R"json(,"st":0,"bm":0})json";
}

juce::String makeGroup(const juce::String& items, const juce::String& name = "Group") {
    return R"json({"ty":"gr","nm":")json" + name + R"json(","it":[)json" + items
        + "," + identityGroupTransform() + R"json(],"bm":0})json";
}

juce::String makeRectanglePath(float left, float top, float right, float bottom, const juce::String& name = "Rectangle") {
    return R"json({"ty":"sh","nm":")json" + name
        + R"json(","ks":{"a":0,"k":{"i":[[0,0],[0,0],[0,0],[0,0]],"o":[[0,0],[0,0],[0,0],[0,0]],"v":[[)json"
        + jsonNumber(left) + "," + jsonNumber(top) + "],[" + jsonNumber(right) + "," + jsonNumber(top) + "],["
        + jsonNumber(right) + "," + jsonNumber(bottom) + "],[" + jsonNumber(left) + "," + jsonNumber(bottom)
        + R"json(]],"c":true}}})json";
}

juce::String makeLinePath(float startX, float startY, float endX, float endY, const juce::String& name = "Line") {
    return R"json({"ty":"sh","nm":")json" + name
        + R"json(","ks":{"a":0,"k":{"i":[[0,0],[0,0]],"o":[[0,0],[0,0]],"v":[[)json"
        + jsonNumber(startX) + "," + jsonNumber(startY) + "],[" + jsonNumber(endX) + "," + jsonNumber(endY)
        + R"json(]],"c":false}}})json";
}

juce::String makeCubicPath() {
    return R"json({"ty":"sh","nm":"Cubic","ks":{"a":0,"k":{"i":[[0,0],[-30,40]],"o":[[30,-40],[0,0]],"v":[[10,50],[90,50]],"c":false}}})json";
}

juce::String makeFill(float opacity = 100.0f) {
    return R"json({"ty":"fl","nm":"Fill","c":{"a":0,"k":[1,0,0,1]},"o":{"a":0,"k":)json"
        + jsonNumber(opacity) + R"json(},"r":1,"bm":0})json";
}

juce::String makeStroke(float opacity = 100.0f, float width = 2.0f) {
    return R"json({"ty":"st","nm":"Stroke","c":{"a":0,"k":[0,0,1,1]},"o":{"a":0,"k":)json"
        + jsonNumber(opacity) + R"json(},"w":{"a":0,"k":)json" + jsonNumber(width)
        + R"json(},"lc":1,"lj":1,"ml":4,"bm":0})json";
}

juce::String makeTrim(float startPercent, float endPercent, float offsetDegrees = 0.0f) {
    return R"json({"ty":"tm","nm":"Trim","s":{"a":0,"k":)json" + jsonNumber(startPercent)
        + R"json(},"e":{"a":0,"k":)json" + jsonNumber(endPercent)
        + R"json(},"o":{"a":0,"k":)json" + jsonNumber(offsetDegrees) + R"json(},"m":1})json";
}

juce::String singleLayerAnimation(const juce::String& shapeItems,
                                  int outPoint = 1,
                                  double frameRate = 30.0,
                                  const juce::String& transformProperties = identityLayerTransformProperties()) {
    return makeAnimation(makeShapeLayer(shapeItems, 1, outPoint, transformProperties), outPoint, frameRate);
}

struct ArchiveEntry {
    juce::String path;
    juce::String content;
};

juce::MemoryBlock makeDotLottieArchive(std::initializer_list<ArchiveEntry> entries) {
    juce::ZipFile::Builder builder;
    for (const auto& entry : entries) {
        builder.addEntry(new juce::MemoryInputStream(entry.content.toRawUTF8(),
                                                     entry.content.getNumBytesAsUTF8(),
                                                     true),
                         9,
                         entry.path,
                                                     juce::Time());
    }

    juce::MemoryBlock archiveData;
    juce::MemoryOutputStream output(archiveData, false);
    builder.writeToStream(output, nullptr);
    return archiveData;
}

osci::Line* asLine(ShapeList& shapes, int shapeIndex) {
    return dynamic_cast<osci::Line*>(shapes[(size_t) shapeIndex].get());
}

osci::CubicBezierCurve* asCubic(ShapeList& shapes, int shapeIndex) {
    return dynamic_cast<osci::CubicBezierCurve*>(shapes[(size_t) shapeIndex].get());
}

int countCubics(ShapeList& shapes) {
    int cubicCount = 0;
    for (auto& shape : shapes) {
        if (dynamic_cast<osci::CubicBezierCurve*>(shape.get()) != nullptr) {
            ++cubicCount;
        }
    }
    return cubicCount;
}

bool lineNear(const osci::Line& line, float x1, float y1, float x2, float y2, float tolerance = kGeometryTolerance) {
    return std::abs(line.x1 - x1) <= tolerance
        && std::abs(line.y1 - y1) <= tolerance
        && std::abs(line.x2 - x2) <= tolerance
        && std::abs(line.y2 - y2) <= tolerance;
}

bool segmentNear(osci::Shape& shape, float x1, float y1, float x2, float y2, float tolerance = kGeometryTolerance) {
    const auto start = shape.nextVector(0.0f);
    const auto end = shape.nextVector(1.0f);
    return std::abs(start.x - x1) <= tolerance
        && std::abs(start.y - y1) <= tolerance
        && std::abs(end.x - x2) <= tolerance
        && std::abs(end.y - y2) <= tolerance;
}

bool pointInsideOscilloscopeBounds(const osci::Point& point) {
    return point.x >= -1.0f - kGeometryTolerance && point.x <= 1.0f + kGeometryTolerance
        && point.y >= -1.0f - kGeometryTolerance && point.y <= 1.0f + kGeometryTolerance;
}
}

class LottieParserTest : public juce::UnitTest {
public:
    LottieParserTest() : juce::UnitTest("Lottie Parser", "Lottie") {}

    void runTest() override {
        testInvalidJsonFallbackAndFrameClamping();
        testStaticRectangleGeometry();
        testDrawReturnsIndependentClones();
        testFrameMetadataAndClamping();
        testAnimatedLayerTransformUsesCachedFrames();
        testCubicPathIsPreservedWithoutClipping();
        testTrimPathCutsStrokeGeometry();
        testTrimPathOffsetWrapsGeometry();
        testInvisibleShapesAreSkipped();
        testOpaqueFillOccludesLowerOpenPaths();
        testDotLottieManifestSelection();
        testPrecompViewportClipsChildGeometry();
        testPrecompViewportClipsFilledGeometryBoundary();
        testPrecompViewportClipsOpaqueOccluder();
        testRepeatedConstructionKeepsThorVGInitialised();
    }

private:
    bool expectShapeCount(ShapeList& shapes, int expectedCount, const juce::String& message) {
        expectEquals((int) shapes.size(), expectedCount, message);
        return (int) shapes.size() >= expectedCount;
    }

    void expectLineNear(const osci::Line* line, float x1, float y1, float x2, float y2, const juce::String& message) {
        expect(line != nullptr, message + " was not a line");
        if (line == nullptr) return;
        expect(lineNear(*line, x1, y1, x2, y2), message + " had unexpected endpoints");
    }

    void expectSegmentNear(osci::Shape* shape, float x1, float y1, float x2, float y2, const juce::String& message) {
        expect(shape != nullptr, message + " was missing");
        if (shape == nullptr) return;
        expect(segmentNear(*shape, x1, y1, x2, y2), message + " had unexpected endpoints");
    }

    void expectAllSamplesInsideBounds(ShapeList& shapes) {
        for (auto& shape : shapes) {
            expect(pointInsideOscilloscopeBounds(shape->nextVector(0.0f)), "shape start point should be normalised");
            expect(pointInsideOscilloscopeBounds(shape->nextVector(0.5f)), "shape midpoint should be normalised");
            expect(pointInsideOscilloscopeBounds(shape->nextVector(1.0f)), "shape end point should be normalised");
        }
    }

    void testInvalidJsonFallbackAndFrameClamping() {
        beginTest("Invalid JSON falls back to a visible cross");

        OsciLottieParser parser("{ this is not lottie json");
        expectEquals(parser.getNumFrames(), 1);
        expectEquals(parser.getCurrentFrame(), 0);

        parser.setFrame(-20);
        expectEquals(parser.getCurrentFrame(), 0);
        parser.setFrame(99);
        expectEquals(parser.getCurrentFrame(), 0);

        auto shapes = parser.draw();
        if (!expectShapeCount(shapes, 2, "fallback should contain two diagonal lines")) return;
        expectLineNear(asLine(shapes, 0), -0.5f, -0.5f, 0.5f, 0.5f, "fallback diagonal A");
        expectLineNear(asLine(shapes, 1), -0.5f, 0.5f, 0.5f, -0.5f, "fallback diagonal B");
    }

    void testStaticRectangleGeometry() {
        beginTest("Static filled rectangle becomes four normalised line segments");

        OsciLottieParser parser(singleLayerAnimation(
            makeGroup(makeRectanglePath(25.0f, 25.0f, 75.0f, 75.0f) + "," + makeFill())));

        expectEquals(parser.getNumFrames(), 1);
        expectWithinAbsoluteError(parser.getFrameRate(), 30.0, 0.001);

        auto shapes = parser.draw();
        if (!expectShapeCount(shapes, 4, "rectangle should contain four segments")) return;
        expectSegmentNear(shapes[0].get(), -0.5f, 0.5f, 0.5f, 0.5f, "rectangle top edge");
        expectSegmentNear(shapes[1].get(), 0.5f, 0.5f, 0.5f, -0.5f, "rectangle right edge");
        expectSegmentNear(shapes[2].get(), 0.5f, -0.5f, -0.5f, -0.5f, "rectangle bottom edge");
        expectSegmentNear(shapes[3].get(), -0.5f, -0.5f, -0.5f, 0.5f, "rectangle left edge");
        expectAllSamplesInsideBounds(shapes);
    }

    void testDrawReturnsIndependentClones() {
        beginTest("draw() returns clones rather than cached shape instances");

        OsciLottieParser parser(singleLayerAnimation(
            makeGroup(makeRectanglePath(25.0f, 25.0f, 75.0f, 75.0f) + "," + makeFill())));

        auto firstDraw = parser.draw();
        expect(!firstDraw.empty());
        if (!firstDraw.empty()) {
            firstDraw[0]->translate(10.0f, 0.0f, 0.0f);
        }

        auto secondDraw = parser.draw();
        if (!expectShapeCount(secondDraw, 4, "fresh draw should contain four segments")) return;
        expectSegmentNear(secondDraw[0].get(), -0.5f, 0.5f, 0.5f, 0.5f, "fresh rectangle top edge");
    }

    void testFrameMetadataAndClamping() {
        beginTest("Frame count, frame rate, and setFrame clamping follow the Lottie timeline");

        OsciLottieParser parser(singleLayerAnimation(
            makeGroup(makeRectanglePath(25.0f, 25.0f, 75.0f, 75.0f) + "," + makeFill()),
            4,
            12.0));

        expectEquals(parser.getNumFrames(), 4);
        expectWithinAbsoluteError(parser.getFrameRate(), 12.0, 0.001);

        parser.setFrame(2);
        expectEquals(parser.getCurrentFrame(), 2);
        parser.setFrame(100);
        expectEquals(parser.getCurrentFrame(), 3);
        parser.setFrame(-1);
        expectEquals(parser.getCurrentFrame(), 0);
    }

    void testAnimatedLayerTransformUsesCachedFrames() {
        beginTest("Animated layer transform produces distinct cached frames");

        OsciLottieParser parser(singleLayerAnimation(
            makeGroup(makeRectanglePath(25.0f, 25.0f, 75.0f, 75.0f) + "," + makeStroke()),
            3,
            30.0,
            animatedLayerTransformProperties()));

        parser.setFrame(0);
        auto frameZero = parser.draw();
        parser.setFrame(2);
        auto frameTwo = parser.draw();

        expect(!frameZero.empty() && !frameTwo.empty(), "animated frames should contain geometry");
        if (frameZero.empty() || frameTwo.empty()) return;

        const auto frameZeroStart = frameZero[0]->nextVector(0.0f);
        const auto frameTwoStart = frameTwo[0]->nextVector(0.0f);
        expectWithinAbsoluteError(frameZeroStart.x, -0.5f, 0.02f);
        expectWithinAbsoluteError(frameTwoStart.x, -0.1f, 0.06f);
        expect(std::abs(frameTwoStart.x - frameZeroStart.x) > 0.25f, "animated frame should move horizontally");
    }

    void testCubicPathIsPreservedWithoutClipping() {
        beginTest("Unclipped cubic paths stay as cubic Bezier shapes");

        OsciLottieParser parser(singleLayerAnimation(
            makeGroup(makeCubicPath() + "," + makeStroke())));

        auto shapes = parser.draw();
        if (!expectShapeCount(shapes, 1, "cubic path should contain one segment")) return;
        expectEquals(countCubics(shapes), 1);

        auto* cubic = asCubic(shapes, 0);
        expect(cubic != nullptr);
        if (cubic == nullptr) return;

        expectWithinAbsoluteError(cubic->x1, -0.8f, 0.02f);
        expectWithinAbsoluteError(cubic->y1, 0.0f, 0.02f);
        expectWithinAbsoluteError(cubic->x4, 0.8f, 0.02f);
        expectWithinAbsoluteError(cubic->y4, 0.0f, 0.02f);
    }

    void testTrimPathCutsStrokeGeometry() {
        beginTest("Trim path emits only the requested stroke range");

        OsciLottieParser parser(singleLayerAnimation(
            makeGroup(makeRectanglePath(25.0f, 25.0f, 75.0f, 75.0f) + "," + makeTrim(0.0f, 50.0f) + "," + makeStroke())));

        auto shapes = parser.draw();
        if (!expectShapeCount(shapes, 2, "trimmed stroke should contain two segments")) return;
        expectSegmentNear(shapes[0].get(), -0.5f, 0.5f, 0.5f, 0.5f, "trimmed first edge");
        expectSegmentNear(shapes[1].get(), 0.5f, 0.5f, 0.5f, -0.5f, "trimmed second edge");
    }

    void testTrimPathOffsetWrapsGeometry() {
        beginTest("Trim path offset wraps around the path instead of dropping geometry");

        OsciLottieParser parser(singleLayerAnimation(
            makeGroup(makeRectanglePath(25.0f, 25.0f, 75.0f, 75.0f) + "," + makeTrim(0.0f, 50.0f, 270.0f) + "," + makeStroke())));

        auto shapes = parser.draw();
        if (!expectShapeCount(shapes, 2, "offset trimmed stroke should contain two wrapped segments")) return;
        expectSegmentNear(shapes[0].get(), -0.5f, -0.5f, -0.5f, 0.5f, "wrapped trim first edge");
        expectSegmentNear(shapes[1].get(), -0.5f, 0.5f, 0.5f, 0.5f, "wrapped trim second edge");
    }

    void testInvisibleShapesAreSkipped() {
        beginTest("Path without fill or stroke is ignored");
        OsciLottieParser noPaintParser(singleLayerAnimation(
            makeGroup(makeRectanglePath(25.0f, 25.0f, 75.0f, 75.0f))));
        expectEquals((int) noPaintParser.draw().size(), 0);

        beginTest("Transparent fill is ignored");
        OsciLottieParser transparentFillParser(singleLayerAnimation(
            makeGroup(makeRectanglePath(25.0f, 25.0f, 75.0f, 75.0f) + "," + makeFill(0.0f))));
        expectEquals((int) transparentFillParser.draw().size(), 0);

        beginTest("Fully trimmed stroke is ignored");
        OsciLottieParser fullyTrimmedParser(singleLayerAnimation(
            makeGroup(makeRectanglePath(25.0f, 25.0f, 75.0f, 75.0f) + "," + makeTrim(25.0f, 25.0f) + "," + makeStroke())));
        expectEquals((int) fullyTrimmedParser.draw().size(), 0);
    }

    void testOpaqueFillOccludesLowerOpenPaths() {
        beginTest("Opaque fills split lower open paths that pass behind them");

        const auto lowerLine = makeGroup(makeLinePath(0.0f, 50.0f, 100.0f, 50.0f, "LowerLine") + "," + makeStroke(), "LowerGroup");
        const auto topOccluder = makeGroup(makeRectanglePath(40.0f, 40.0f, 60.0f, 60.0f, "TopRect") + "," + makeFill(), "TopGroup");
        OsciLottieParser parser(singleLayerAnimation(topOccluder + "," + lowerLine));

        auto shapes = parser.draw();
        if (!expectShapeCount(shapes, 6, "occluded scene should contain six segments")) return;

        int centerLineSegmentCount = 0;
        for (auto& shape : shapes) {
            const auto start = shape->nextVector(0.0f);
            const auto end = shape->nextVector(1.0f);
            if (std::abs(start.y) <= kGeometryTolerance && std::abs(end.y) <= kGeometryTolerance) {
                ++centerLineSegmentCount;
                const bool leftSegment = start.x <= -0.19f && end.x <= -0.19f;
                const bool rightSegment = start.x >= 0.19f && end.x >= 0.19f;
                expect(leftSegment || rightSegment, "lower line should be split around the occluder");
            }
        }
        expectEquals(centerLineSegmentCount, 2);
    }

    void testDotLottieManifestSelection() {
        beginTest("dotLottie manifest activeAnimationId selects the intended JSON");

        const juce::String firstJson = R"json({"nm":"first"})json";
        const juce::String secondJson = R"json({"nm":"second"})json";
        auto archive = makeDotLottieArchive({
            { "animations/first.json", firstJson },
            { "animations/second.json", secondJson },
            { "manifest.json", R"json({"activeAnimationId":"second","animations":[{"id":"first","path":"animations/first.json"},{"id":"second","path":"animations/second.json"}]})json" }
        });

        expectEquals(osci::lottie::extractAnimationJsonFromDotLottie(archive), secondJson);

        beginTest("dotLottie fallback is deterministic without a manifest");
        auto fallbackArchive = makeDotLottieArchive({
            { "animations/z-last.json", R"json({"nm":"z"})json" },
            { "animations/a-first.json", R"json({"nm":"a"})json" }
        });

        expectEquals(osci::lottie::extractAnimationJsonFromDotLottie(fallbackArchive), juce::String(R"json({"nm":"a"})json"));
    }

    void testPrecompViewportClipsChildGeometry() {
        beginTest("Precomp viewport clips child geometry before flattening");

        const auto childLayer = makeShapeLayer(
            makeGroup(makeLinePath(0.0f, 50.0f, 200.0f, 50.0f) + "," + makeStroke()),
            1,
            1);

        const juce::String precompLayer =
            R"json({"ddd":0,"ind":1,"ty":0,"nm":"Cell","refId":"cell","sr":1,"ks":{"o":{"a":0,"k":100},"r":{"a":0,"k":0},"p":{"a":0,"k":[50,50,0]},"a":{"a":0,"k":[50,50,0]},"s":{"a":0,"k":[100,100,100]}})json"
            R"json(,"ao":0,"w":100,"h":100,"ip":0,"op":1,"st":0,"bm":0})json";

        const juce::String json =
            R"json({"v":"5.7.4","fr":30,"ip":0,"op":1,"w":200,"h":100,"nm":"precomp-clip-test","ddd":0,"assets":[{"id":"cell","w":100,"h":100,"layers":[)json"
            + childLayer
            + R"json(]}],"layers":[)json"
            + precompLayer
            + R"json(]})json";

        OsciLottieParser parser(json);
        auto shapes = parser.draw();
        if (!expectShapeCount(shapes, 1, "clipped precomp line should contain one segment")) return;

        const auto start = shapes[0]->nextVector(0.0f);
        const auto end = shapes[0]->nextVector(1.0f);
        expect(std::max(start.x, end.x) <= 0.02f, "precomp content should not leak past the cell boundary");
    }

    void testPrecompViewportClipsFilledGeometryBoundary() {
        beginTest("Precomp viewport clipping preserves filled-shape boundary edges");

        const auto childLayer = makeShapeLayer(
            makeGroup(makeRectanglePath(0.0f, 25.0f, 200.0f, 75.0f) + "," + makeFill()),
            1,
            1);

        const juce::String precompLayer =
            R"json({"ddd":0,"ind":1,"ty":0,"nm":"Cell","refId":"cell","sr":1,"ks":{"o":{"a":0,"k":100},"r":{"a":0,"k":0},"p":{"a":0,"k":[50,50,0]},"a":{"a":0,"k":[50,50,0]},"s":{"a":0,"k":[100,100,100]}})json"
            R"json(,"ao":0,"w":100,"h":100,"ip":0,"op":1,"st":0,"bm":0})json";

        const juce::String json =
            R"json({"v":"5.7.4","fr":30,"ip":0,"op":1,"w":200,"h":100,"nm":"precomp-filled-clip-test","ddd":0,"assets":[{"id":"cell","w":100,"h":100,"layers":[)json"
            + childLayer
            + R"json(]}],"layers":[)json"
            + precompLayer
            + R"json(]})json";

        OsciLottieParser parser(json);
        auto shapes = parser.draw();

        bool hasRightBoundary = false;
        for (auto& shape : shapes) {
            const auto start = shape->nextVector(0.0f);
            const auto end = shape->nextVector(1.0f);
            if (std::abs(start.x) <= 0.02f && std::abs(end.x) <= 0.02f
                && std::abs(start.y - end.y) > 0.2f) {
                hasRightBoundary = true;
                break;
            }
        }

        expect(hasRightBoundary, "clipped fill should emit the viewport boundary edge");
    }

    void testPrecompViewportClipsOpaqueOccluder() {
        beginTest("Precomp viewport clipping limits opaque occluders to visible cell area");

        const auto occluderLayer = makeShapeLayer(
            makeGroup(makeRectanglePath(0.0f, 25.0f, 200.0f, 75.0f) + "," + makeFill()),
            1,
            1);

        const juce::String precompLayer =
            R"json({"ddd":0,"ind":1,"ty":0,"nm":"LeftCell","refId":"left-cell","sr":1,"ks":{"o":{"a":0,"k":100},"r":{"a":0,"k":0},"p":{"a":0,"k":[50,50,0]},"a":{"a":0,"k":[50,50,0]},"s":{"a":0,"k":[100,100,100]}})json"
            R"json(,"ao":0,"w":100,"h":100,"ip":0,"op":1,"st":0,"bm":0})json";
        const auto lowerLineLayer = makeShapeLayer(
            makeGroup(makeLinePath(125.0f, 50.0f, 175.0f, 50.0f) + "," + makeStroke()),
            2,
            1);

        const juce::String json =
            R"json({"v":"5.7.4","fr":30,"ip":0,"op":1,"w":200,"h":100,"nm":"precomp-occluder-clip-test","ddd":0,"assets":[{"id":"left-cell","w":100,"h":100,"layers":[)json"
            + occluderLayer
            + R"json(]}],"layers":[)json"
            + precompLayer + "," + lowerLineLayer
            + R"json(]})json";

        OsciLottieParser parser(json);
        auto shapes = parser.draw();

        bool hasRightCellLine = false;
        for (auto& shape : shapes) {
            const auto start = shape->nextVector(0.0f);
            const auto end = shape->nextVector(1.0f);
            if (std::abs(start.y) <= 0.02f && std::abs(end.y) <= 0.02f
                && std::min(start.x, end.x) >= 0.2f
                && std::max(start.x, end.x) <= 0.8f) {
                hasRightCellLine = true;
                break;
            }
        }

        expect(hasRightCellLine, "hidden off-cell fill should not occlude neighboring-cell geometry");
    }

    void testRepeatedConstructionKeepsThorVGInitialised() {
        beginTest("Repeated parser construction and destruction keeps ThorVG balanced");

        const auto json = singleLayerAnimation(
            makeGroup(makeRectanglePath(25.0f, 25.0f, 75.0f, 75.0f) + "," + makeFill()));

        for (int constructionIndex = 0; constructionIndex < 8; ++constructionIndex) {
            OsciLottieParser parser(json);
            auto shapes = parser.draw();
            expectEquals((int) shapes.size(), 4);
        }
    }
};

static LottieParserTest lottieParserTest;
