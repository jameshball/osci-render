#include <JuceHeader.h>
#include "../Source/motion/model/Document.h"
#include "../Source/motion/model/PropertyTarget.h"

class MotionClipboardTest : public juce::UnitTest {
public:
    MotionClipboardTest() : juce::UnitTest("Motion clipboard commands", "Motion") {}

    struct Fixture {
        juce::UndoManager undo;
        motion::Document document{undo};
        motion::Id first = 0, second = 0, track = 0;
        void initialise() {
            auto asset = std::make_shared<motion::Asset>();
            asset->id = document.newId(); asset->name = "triangle.obj"; asset->extension = ".obj";
            const juce::String obj("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
            asset->data.append(obj.toRawUTF8(), obj.getNumBytesAsUTF8());
            motion::Document::decodeAsset(*asset);
            auto a = motion::Document::makeClip(document.newId(), *asset, 0);
            a.duration = 2;
            a.properties["position.x"].setKey({0, 0, motion::Interpolation::linear});
            a.properties["position.x"].setKey({1, 1, motion::Interpolation::cubic, 0, 2, .3, .6});
            auto b = motion::Document::makeClip(document.newId(), *asset, 4);
            b.duration = 2;
            first = a.id; second = b.id;
            motion::Track row;
            row.id = track = document.newId(); row.name = "Row";
            row.insert(a); row.insert(b);
            motion::Project project;
            project.duration = 10; project.assets = {asset}; project.tracks = {row};
            document.reset(std::move(project));
        }
        std::vector<motion::Document::CopiedClip> copy(std::initializer_list<motion::Id> ids) const {
            std::vector<motion::Document::CopiedClip> result;
            for (const auto& row : document.project().tracks) {
                for (const auto& clip : row.clips) {
                    if (std::find(ids.begin(), ids.end(), clip.id) != ids.end()) { result.push_back({row.id, row.kind, row.name, clip}); }
                }
            }
            return result;
        }
        const motion::Clip* clip(motion::Id id) const {
            for (const auto& row : document.project().tracks) {
                for (const auto& item : row.clips) { if (item.id == id) { return &item; } }
            }
            return nullptr;
        }
    };

    void runTest() override {
        beginTest("Pasting into free space reuses the original track with fresh identities");
        {
            Fixture f; f.initialise();
            std::vector<motion::Id> pasted;
            expect(f.document.pasteClips(f.copy({f.first}), 7, pasted).wasOk());
            expectEquals(static_cast<int>(pasted.size()), 1);
            const auto* copy = f.clip(pasted[0]);
            expect(copy != nullptr && copy->start == 7 && copy->id != f.first);
            expectEquals(static_cast<int>(f.document.project().tracks.size()), 1);
            if (copy != nullptr) { expectEquals(copy->properties.at("position.x").evaluate(.5), f.clip(f.first)->properties.at("position.x").evaluate(.5)); }
            expect(f.undo.getUndoDescription() == "Paste clip");
            expect(f.undo.undo());
            expectEquals(static_cast<int>(f.document.project().tracks[0].clips.size()), 2);
        }
        beginTest("Pasting onto occupied time creates one overflow track below the original");
        {
            Fixture f; f.initialise();
            std::vector<motion::Id> pasted;
            expect(f.document.pasteClips(f.copy({f.first, f.second}), 1, pasted).wasOk());
            const auto& tracks = f.document.project().tracks;
            expectEquals(static_cast<int>(tracks.size()), 2);
            expect(tracks[1].clips.size() == 2 && tracks[1].name == "Row", "Both copies share one overflow track");
            expectEquals(f.clip(pasted[1])->start - f.clip(pasted[0])->start, 4.0, "Relative timing is kept");
            expect(f.undo.undo());
            expectEquals(static_cast<int>(f.document.project().tracks.size()), 1);
        }
        beginTest("Keys paste with relative timing, interpolation and handles onto another clip");
        {
            Fixture f; f.initialise();
            const auto& keys = f.clip(f.first)->properties.at("position.x").keyframes();
            std::vector<motion::Document::CopiedKey> copied {{"position.x", 0, keys[0]}, {"position.x", 1, keys[1]}};
            expect(f.document.pasteKeys(f.second, copied, 4.5).wasOk());
            const auto& pasted = f.clip(f.second)->properties.at("position.x").keyframes();
            expectEquals(static_cast<int>(pasted.size()), 2);
            if (pasted.size() == 2) {
                expectWithinAbsoluteError(pasted[0].time, .5, 1e-12);
                expectWithinAbsoluteError(pasted[1].time, 1.5, 1e-12);
                expect(pasted[1].interpolation == motion::Interpolation::cubic && pasted[1].outgoingSlope == 2 && pasted[1].outgoingInfluence == .6);
            }
            expect(f.undo.getUndoDescription() == "Paste keyframes");
            expect(f.undo.undo());
            expect(!f.clip(f.second)->properties.at("position.x").animated());
        }
        beginTest("Locked tracks reject pasted keys without an undo step");
        {
            Fixture f; f.initialise();
            f.document.edit("Lock", [](motion::Project& project) { project.tracks[0].locked = true; });
            const auto description = f.undo.getUndoDescription();
            const auto& keys = f.clip(f.first)->properties.at("position.x").keyframes();
            expect(f.document.pasteKeys(f.second, {{"position.x", 0, keys[0]}}, 4).failed());
            expect(f.undo.getUndoDescription() == description);
        }
    }
};
static MotionClipboardTest motionClipboardTest;
