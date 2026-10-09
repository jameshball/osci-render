#include <JuceHeader.h>
#include "../Source/motion/model/Document.h"
#include "../Source/motion/model/SourceParts.h"
#include "../Source/motion/import/SourceDecoding.h"

// Picking parts of a still vector source and splitting them off keeps every
// shape exactly where it was.
class MotionPartsTest : public juce::UnitTest {
public:
    MotionPartsTest() : juce::UnitTest("Motion source parts", "Motion") {}

    void runTest() override {
        testFlatSplit();
        testLineSetSplit();
        testAvailability();
        testDocumentExtraction();
    }

private:
    static std::shared_ptr<motion::Asset> source(const juce::String& name, const juce::String& extension, const juce::String& content) {
        auto asset = std::make_shared<motion::Asset>();
        asset->id = 1;
        asset->name = name + extension;
        asset->extension = extension;
        asset->data.append(content.toRawUTF8(), content.getNumBytesAsUTF8());
        return asset;
    }

    // Each positive-length shape as its start, middle and end.
    using Trace = std::array<osci::Point, 3>;
    static std::vector<Trace> traces(const motion::PreparedDrawing& drawing, const std::set<std::size_t>& only = {}) {
        std::vector<Trace> result;
        for (std::size_t index = 0; index < drawing.shapeCount(); ++index) {
            auto* shape = drawing.shape(index);
            if (!(shape->length() > 0) || (!only.empty() && !only.contains(index))) { continue; }
            result.push_back({shape->nextVector(0), shape->nextVector(.5f), shape->nextVector(1)});
        }
        return result;
    }

    // The same shapes in any order, within a tolerance (arcs become cubics).
    static bool sameShapes(std::vector<Trace> expected, std::vector<Trace> actual) {
        if (expected.size() != actual.size()) { return false; }
        const auto close = [](const Trace& a, const Trace& b) {
            for (std::size_t point = 0; point < 3; ++point) {
                if (std::abs(a[point].x - b[point].x) > 2.0e-3f || std::abs(a[point].y - b[point].y) > 2.0e-3f || std::abs(a[point].z - b[point].z) > 2.0e-3f) { return false; }
            }
            return true;
        };
        for (const auto& trace : expected) {
            const auto found = std::find_if(actual.begin(), actual.end(), [&](const auto& candidate) { return close(trace, candidate); });
            if (found == actual.end()) { return false; }
            actual.erase(found);
        }
        return true;
    }

    static std::shared_ptr<motion::Asset> prepared(const juce::String& extension, const juce::String& content) {
        auto asset = source("Part", extension, content);
        return motion::decodeAsset(*asset).wasOk() ? asset : nullptr;
    }

    void expectSplitKeepsShapes(const motion::PreparedDrawing& drawing, const std::set<std::size_t>& chosen, const juce::String& extension) {
        const auto split = motion::parts::split(drawing, chosen);
        expect(split.has_value());
        if (!split.has_value()) { return; }
        expectEquals(split->extension, extension);
        const auto part = prepared(split->extension, split->part), rest = prepared(split->extension, split->rest);
        expect(part != nullptr && rest != nullptr, "both halves prepare");
        if (part == nullptr || rest == nullptr) { return; }
        expect(sameShapes(traces(drawing, chosen), traces(*motion::parts::drawingOf(*part))), "the part holds exactly the chosen shapes, in place");
        std::set<std::size_t> others;
        for (std::size_t index = 0; index < drawing.shapeCount(); ++index) { if (!chosen.contains(index)) { others.insert(index); } }
        expect(sameShapes(traces(drawing, others), traces(*motion::parts::drawingOf(*rest))), "the rest holds the others, in place");
    }

    void testFlatSplit() {
        beginTest("Text splits into two editable drawings with every curve in place");
        auto text = source("Hello", ".txt", "Hello");
        expect(motion::decodeAsset(*text).wasOk());
        const auto* drawing = motion::parts::drawingOf(*text);
        expect(drawing != nullptr);
        if (drawing == nullptr) { return; }
        const auto paths = motion::parts::pathIndices(*drawing);
        expectGreaterOrEqual(static_cast<int>(*std::max_element(paths.begin(), paths.end()) + 1), 7, "each outline of Hello is its own path");
        // The H: the first path.
        std::set<std::size_t> letter;
        for (std::size_t index = 0; index < paths.size(); ++index) { if (paths[index] == paths.front()) { letter.insert(index); } }
        expectSplitKeepsShapes(*drawing, letter, ".svg");
        const auto split = motion::parts::split(*drawing, letter);
        const auto restored = motion::drawing::fromSvg(split->part);
        expect(restored.has_value() && restored->strokes.size() == 1 && restored->strokes.front().closed, "a closed outline becomes one closed, editable stroke");
        expect(!motion::parts::split(*drawing, {}).has_value(), "picking nothing splits nothing");
        std::set<std::size_t> all;
        for (std::size_t index = 0; index < drawing->shapeCount(); ++index) { all.insert(index); }
        expect(!motion::parts::split(*drawing, all).has_value(), "picking everything leaves nothing to split");

        beginTest("SVG arcs and curves keep their place as drawing curves");
        auto svg = source("Shapes", ".svg", R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100"><circle cx="30" cy="30" r="20"/><path d="M60 60 C70 90 90 70 95 95"/></svg>)");
        expect(motion::decodeAsset(*svg).wasOk());
        const auto* shapes = motion::parts::drawingOf(*svg);
        expect(shapes != nullptr);
        if (shapes != nullptr) { expectSplitKeepsShapes(*shapes, {0}, ".svg"); }
    }

    void testLineSetSplit() {
        beginTest("3D wireframes split into line sets that load where they were cut");
        auto cube = source("Cube", ".obj", "v -1 -1 -1\nv 1 -1 -1\nv 1 1 -1\nv -1 1 -1\nv -1 -1 1\nv 1 -1 1\nv 1 1 1\nv -1 1 1\nf 1 2 3 4\nf 5 6 7 8\nf 1 2 6 5\n");
        expect(motion::decodeAsset(*cube).wasOk());
        const auto* drawing = motion::parts::drawingOf(*cube);
        expect(drawing != nullptr && drawing->shapeCount() > 4);
        if (drawing == nullptr) { return; }
        expect(!motion::parts::flat(*drawing, {0, 1, 2, 3}), "a cube has depth");
        expectSplitKeepsShapes(*drawing, {0, 1, 2}, ".obj");
        expect(motion::parts::isLineSet(motion::parts::split(*drawing, {0})->part));
    }

    void testAvailability() {
        beginTest("Only still vector sources offer their parts");
        auto animated = source("Hi", ".txt", "Hi");
        animated->textSettings.animation = motion::TextSettings::Animation::typeOn;
        expect(motion::decodeAsset(*animated).wasOk());
        expect(motion::parts::drawingOf(*animated) == nullptr);
        expect(motion::parts::unavailableReason(*animated).contains("Animated"));
        auto still = source("Hi", ".txt", "Hi");
        expect(motion::decodeAsset(*still).wasOk());
        expect(motion::parts::unavailableReason(*still).isEmpty());
    }

    void testDocumentExtraction() {
        beginTest("Extracting parts is one undo step: the clip keeps the rest, a copy above plays the part");
        auto text = source("Hello", ".txt", "Hello");
        expect(motion::decodeAsset(*text).wasOk());
        juce::UndoManager undo;
        motion::Document document(undo);
        motion::Project project;
        project.assets.push_back(text);
        motion::Track below;
        below.id = 5;
        below.name = "Below";
        motion::Track track;
        track.id = 10;
        track.name = "Title";
        track.group = 0;
        track.solo = true;
        track.effects.push_back(motion::makeEffect(40, *motion::effectDefinition("bulge")));
        auto clip = motion::Document::makeClip(20, *text, 1);
        clip.properties["position.x"].setKey({0, .5, motion::Interpolation::linear});
        clip.effects.push_back(motion::makeEffect(30, *motion::effectDefinition("bulge")));
        expect(track.insert(clip, project.tempo()));
        project.tracks.push_back(below);
        project.tracks.push_back(track);
        document.reset(project);

        const auto* drawing = motion::parts::drawingOf(*text);
        const auto split = motion::parts::split(*drawing, {0, 1, 2});
        auto part = prepared(split->extension, split->part), rest = prepared(split->extension, split->rest);
        std::vector<motion::Id> made;
        expect(document.extractParts({{20, part, rest, "Hello part"}}, made).wasOk());
        const auto created = made.empty() ? motion::Id(0) : made.front();
        const auto& after = document.project();
        expectEquals(static_cast<int>(after.tracks.size()), 3);
        expectEquals(after.tracks[1].clips.front().id, created, "the part sits on a new track just above the clip");
        const auto& copy = after.tracks[1].clips.front();
        const auto& kept = after.tracks[2].clips.front();
        expect(copy.asset == part->id && kept.asset == rest->id);
        expect(copy.sameTiming(kept) && copy.properties.at("position.x").keyframes().size() == 1, "the part keeps the clip's timing and keys");
        expect(copy.effects.size() == 1 && copy.effects.front().id != 30, "effects are copied with their own identities");
        expect(after.tracks[1].solo && after.tracks[1].effects.size() == 1 && after.tracks[1].effects.front().id != 40, "the part's track keeps the track's solo and effects");
        expect(motion::findAsset(after.assets, 1) == nullptr, "the unused original leaves the library");
        expectEquals(part->name, juce::String("Hello part.svg"));
        expectEquals(rest->name, juce::String("Hello.svg"), "the rest keeps its source's name");
        expectEquals(juce::String(kept.name), juce::String("Hello.txt"), "the clip keeps its own name");
        expect(undo.undo());
        expectEquals(static_cast<int>(document.project().tracks.size()), 2);
        expect(motion::findAsset(document.project().assets, 1) != nullptr && document.project().tracks[1].clips.front().asset == 1, "undo restores the original");

        beginTest("Extraction refuses locked tracks");
        auto locked = project;
        locked.tracks.change(1).locked = true;
        document.reset(locked);
        auto again = prepared(split->extension, split->part), remaining = prepared(split->extension, split->rest);
        expect(document.extractParts({{20, again, remaining, "Hello part"}}, made).failed());
    }
};

static MotionPartsTest motionPartsTest;
