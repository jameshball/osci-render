#include <JuceHeader.h>
#include "../Source/motion/model/Document.h"
#include "../Source/motion/render/CompositionRenderer.h"

class MotionSpatialTest : public juce::UnitTest {
public:
    MotionSpatialTest() : juce::UnitTest("Motion spatial paths and quaternion rotation", "Motion") {}

    static motion::Curve keyed(std::initializer_list<std::pair<double, double>> keys, motion::Interpolation interpolation = motion::Interpolation::linear) {
        motion::Curve curve;
        for (const auto& [time, value] : keys) { curve.setKey({time, value, interpolation}); }
        return curve;
    }

    void runTest() override {
        beginTest("A spatial path passes through every key and travels at constant speed");
        {
            const auto x = keyed({{0, 0}, {1, 1}, {2, 1}});
            const auto y = keyed({{0, 0}, {1, 0}, {2, 1}});
            const auto z = keyed({{0, 0}, {1, 0}, {2, 0}});
            const auto path = motion::PreparedPath::prepare(x, y, z);
            expect(path != nullptr);
            for (const auto& [time, px, py] : std::vector<std::tuple<double, double, double>> {{0, 0, 0}, {1, 1, 0}, {2, 1, 1}}) {
                const auto point = path->at(time);
                expectWithinAbsoluteError(point[0], px, 1.0e-9);
                expectWithinAbsoluteError(point[1], py, 1.0e-9);
            }
            // Equal time steps cover (nearly) equal distances along a segment.
            std::vector<double> steps;
            auto previous = path->at(0);
            for (int index = 1; index <= 10; ++index) {
                const auto point = path->at(index * 0.1);
                steps.push_back(std::hypot(point[0] - previous[0], point[1] - previous[1], point[2] - previous[2]));
                previous = point;
            }
            const auto [low, high] = std::minmax_element(steps.begin(), steps.end());
            expect(*high / *low < 1.05, "speed varies by " + juce::String(*high / *low));
            // The corner at (1, 0) is rounded rather than a sharp per-axis turn.
            const auto before = path->at(0.95), after = path->at(1.05);
            expect(before[1] < 0 || after[0] > 1, "the path curves through the middle key");
        }
        beginTest("Misaligned keys fall back to independent axes");
        {
            const auto x = keyed({{0, 0}, {1, 1}});
            const auto y = keyed({{0, 0}, {2, 1}});
            const auto z = keyed({{0, 0}, {1, 0}});
            expect(motion::PreparedPath::prepare(x, y, z) == nullptr);
            expect(motion::PreparedOrientation::prepare(x, y, z) == nullptr);
        }
        beginTest("Quaternions match Euler rotation at keys and take the shortest arc between them");
        {
            for (const auto& angles : std::vector<std::array<double, 3>> {{30, 0, 0}, {0, 45, 0}, {0, 0, 60}, {20, -35, 70}}) {
                osci::Point expected(0.3f, -0.7f, 0.5f);
                constexpr auto radians = std::numbers::pi / 180.0;
                expected.rotate(angles[0] * radians, angles[1] * radians, angles[2] * radians);
                const auto rotated = motion::Quaternion::fromEulerDegrees(angles[0], angles[1], angles[2]).rotate(0.3, -0.7, 0.5);
                expectWithinAbsoluteError(rotated[0], static_cast<double>(expected.x), 1.0e-5);
                expectWithinAbsoluteError(rotated[1], static_cast<double>(expected.y), 1.0e-5);
                expectWithinAbsoluteError(rotated[2], static_cast<double>(expected.z), 1.0e-5);
            }
            // 0 -> 350 degrees about Z is a 10 degree turn the short way.
            const auto x = keyed({{0, 0}, {1, 0}});
            const auto y = keyed({{0, 0}, {1, 0}});
            const auto z = keyed({{0, 0}, {1, 350}});
            const auto orientation = motion::PreparedOrientation::prepare(x, y, z);
            expect(orientation != nullptr);
            const auto half = orientation->at(0.5).rotate(1, 0, 0);
            expectWithinAbsoluteError(std::atan2(half[1], half[0]) * 180 / std::numbers::pi, -5.0, 1.0e-6);
        }
        beginTest("Clips render their path and orientation, and the modes survive save and reload");
        {
            juce::UndoManager undo;
            motion::Document document(undo);
            auto asset = std::make_shared<motion::Asset>();
            asset->id = document.newId(); asset->name = "point.obj"; asset->extension = ".obj";
            const juce::String obj("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
            asset->data.append(obj.toRawUTF8(), obj.getNumBytesAsUTF8());
            motion::Document::decodeAsset(*asset);
            auto clip = motion::Document::makeClip(document.newId(), *asset, 0);
            clip.duration = 4;
            clip.spatialPath = true;
            clip.quaternionRotation = true;
            clip.properties["position.x"] = keyed({{0, 0}, {1, 1}, {2, 1}});
            clip.properties["position.y"] = keyed({{0, 0}, {1, 0}, {2, 1}});
            clip.properties["position.z"] = keyed({{0, 0}, {1, 0}, {2, 0}});
            clip.properties["rotation.z"] = keyed({{0, 0}, {2, 350}});
            clip.properties["rotation.x"] = keyed({{0, 0}, {2, 0}});
            clip.properties["rotation.y"] = keyed({{0, 0}, {2, 0}});
            motion::Track track;
            track.id = document.newId(); track.name = "Path"; track.insert(clip, motion::Tempo(120));
            motion::Project project;
            project.duration = 4; project.assets = {asset}; project.tracks = {track};
            motion::Modulator square;
            square.id = document.newId();
            square.shape.waveform = motion::ModulationWaveform::square;
            project.modulators.push_back(square);
            motion::ModulationRoute route;
            route.id = document.newId(); route.modulator = square.id; route.target = clip.id; route.property = "position.y"; route.amount = .5;
            project.routes.push_back(route);
            document.reset(project);
            motion::PreparedComposition prepared(document.project(), 48000, nullptr, motion::CompositionPurpose::editorGeometry);
            expect(prepared.clips.size() == 1 && prepared.clips[0].spatial != nullptr);
            const auto expectedPath = motion::PreparedPath::prepare(clip.properties["position.x"], clip.properties["position.y"], clip.properties["position.z"])->at(0.25);
            const auto point = prepared.clips[0].processPoint(osci::Point(0, 0, 0), 0.25);
            expectWithinAbsoluteError(static_cast<double>(point.x), expectedPath[0], 1.0e-5);
            // Modulation still adds on top of the path: square wave is +1 at 0.25 s phase.
            expectWithinAbsoluteError(static_cast<double>(point.y), expectedPath[1] + 0.5, 1.0e-5);
            motion::Project loaded;
            expect(motion::Document::prepareLoad(document.save(), loaded).wasOk());
            expect(loaded.tracks[0].clips[0].spatialPath && loaded.tracks[0].clips[0].quaternionRotation);
        }
    }
};

static MotionSpatialTest motionSpatialTest;
