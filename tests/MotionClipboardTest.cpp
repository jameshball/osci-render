#include <JuceHeader.h>
#include "../Source/motion/model/Document.h"
#include "../Source/motion/import/SourceDecoding.h"
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
            motion::decodeAsset(*asset);
            auto a = motion::Document::makeClip(document.newId(), *asset, 0);
            a.duration = 2;
            a.properties["position.x"].setKey({0, 0, motion::Interpolation::linear});
            a.properties["position.x"].setKey({1, 1, motion::Interpolation::cubic, 0, 2, .3, .6});
            auto b = motion::Document::makeClip(document.newId(), *asset, 4);
            b.duration = 2;
            first = a.id; second = b.id;
            motion::Track row;
            row.id = track = document.newId(); row.name = "Row";
            row.insert(a, motion::Tempo(120)); row.insert(b, motion::Tempo(120));
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
        beginTest("Several clips retime together in one undo step, checked after all move");
        {
            Fixture f; f.initialise();
            const motion::Tempo tempo(120);
            auto a = f.clip(f.first)->timing(tempo);
            auto b = f.clip(f.second)->timing(tempo);
            a.moveTo(3);
            b.moveTo(7);
            // Moving the first clip alone would overlap the second one.
            expect(f.document.setClipTiming(f.first, a).failed());
            expect(f.document.setClipTimings({{f.first, a}, {f.second, b}}).wasOk());
            expectEquals(f.clip(f.first)->start, 3.0);
            expectEquals(f.clip(f.second)->start, 7.0);
            expect(f.undo.getUndoDescription() == "Change clip timing");
            expect(f.undo.undo());
            expectEquals(f.clip(f.first)->start, 0.0);
            expectEquals(f.clip(f.second)->start, 4.0);
            b.moveTo(4);
            const auto description = f.undo.getUndoDescription();
            expect(f.document.setClipTimings({{f.first, a}, {f.second, b}}).failed(), "Overlapping results are rejected");
            expectEquals(f.clip(f.first)->start, 0.0);
            expect(f.undo.getUndoDescription() == description, "A rejected batch adds no undo step");
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
        beginTest("Musical tempo changes keep markers, cuts and project-time keys on their beats");
        for (const bool musical : {true, false}) {
            Fixture f; f.initialise();
            motion::Camera camera; camera.id = f.document.newId();
            camera.properties["position.x"].setKey({2, 0, motion::Interpolation::cubic, 0, 1});
            camera.properties["position.x"].setKey({4, 2, motion::Interpolation::linear});
            f.document.edit("Setup", [&](motion::Project& project) {
                project.timeDisplay = musical ? motion::TimeDisplay::beats : motion::TimeDisplay::seconds;
                project.cameras = {camera};
                project.cameraCuts = {{f.document.newId(), camera.id, 2, 2}};
                project.markers = {{f.document.newId(), 3, "Drop"}};
            });
            expect(f.document.changeTempo(240).wasOk());
            const auto& project = f.document.project();
            const auto scale = musical ? .5 : 1.0;
            expectWithinAbsoluteError(project.markers[0].time, 3 * scale, 1e-12);
            expectWithinAbsoluteError(project.cameraCuts[0].start, 2 * scale, 1e-12);
            expect(project.cameraCuts[0].end() <= 4 * scale + 1e-12);
            const auto& keys = project.cameras[0].properties.at("position.x").keyframes();
            expectWithinAbsoluteError(keys[1].time, 4 * scale, 1e-12);
            expectWithinAbsoluteError(keys[0].outgoingSlope, 1 / scale, 1e-12, "Slopes steepen with the shorter span");
            juce::UndoManager reopenedUndo;
            motion::Document reopened(reopenedUndo);
            expect(reopened.load(f.document.save()).wasOk(), "A tempo change always saves a loadable project");
            expect(f.undo.undo());
            expectWithinAbsoluteError(f.document.project().markers[0].time, 3.0, 1e-12);
        }
        beginTest("Slowing a musical project keeps late markers inside the longer composition");
        {
            Fixture f; f.initialise();
            f.document.edit("Setup", [&](motion::Project& project) {
                project.timeDisplay = motion::TimeDisplay::beats;
                project.markers = {{f.document.newId(), 9.5, "Late"}};
            });
            expect(f.document.changeTempo(60).wasOk());
            expect(f.document.project().markers[0].time <= f.document.project().duration);
            juce::UndoManager reopenedUndo;
            motion::Document reopened(reopenedUndo);
            expect(reopened.load(f.document.save()).wasOk());
        }

        beginTest("Sources rename and only unused sources can be removed, in one undo step");
        {
            Fixture f; f.initialise();
            auto spare = std::make_shared<motion::Asset>(*f.document.project().assets[0]);
            spare->id = f.document.newId(); spare->name = "spare.obj";
            const auto used = f.document.project().assets[0]->id;
            f.document.edit("Add spare", [spare](motion::Project& project) { project.assets.push_back(spare); });
            expect(f.document.assetUses(used) == 2 && f.document.assetUses(spare->id) == 0);
            expect(f.document.renameAsset(spare->id, "  Spare shape  ").wasOk());
            expect(f.document.project().assets[1]->name == "Spare shape");
            expect(f.document.renameAsset(spare->id, "").failed());
            int removed = 0;
            expect(f.document.removeUnusedAssets({used}, removed).failed() && removed == 0, "A used source is kept");
            expect(f.document.removeUnusedAssets({}, removed).wasOk() && removed == 1);
            expectEquals(static_cast<int>(f.document.project().assets.size()), 1);
            expect(f.undo.getUndoDescription() == "Remove source");
            expect(f.undo.undo());
            expectEquals(static_cast<int>(f.document.project().assets.size()), 2);
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
        beginTest("Undo history is bounded by memory while keeping recent steps");
        {
            juce::UndoManager undo(300, 3);
            motion::Document document(undo);
            motion::Project project;
            motion::Camera camera;
            camera.id = document.newId();
            project.cameras = {camera};
            document.reset(project);
            // Each step adds 2000 keys (~100 KiB per snapshot pair).
            for (int step = 0; step < 20; ++step) {
                document.edit("Add keys", [step](motion::Project& updated) {
                    auto& curve = updated.cameras[0].properties["position.x"];
                    for (int key = 0; key < 2000; ++key) { curve.setKey({step * 10000.0 + key, 1.0, motion::Interpolation::linear}); }
                });
            }
            int kept = 0;
            while (undo.canUndo() && kept < 50) { undo.undo(); ++kept; }
            expect(kept >= 3 && kept < 20, "kept " + juce::String(kept) + " of 20 steps");
        }
        beginTest("Replacing a source swaps its media under the same identity");
        {
            Fixture f; f.initialise();
            const auto original = f.document.project().assets[0];
            auto square = std::make_shared<motion::Asset>();
            square->id = original->id; square->name = "square.svg"; square->extension = ".svg";
            const juce::String svg("<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 10 10'><rect x='1' y='1' width='8' height='8' fill='none' stroke='black'/></svg>");
            square->data.append(svg.toRawUTF8(), svg.getNumBytesAsUTF8());
            expect(motion::decodeAsset(*square).wasOk());
            expect(f.document.replaceAsset(original->id, square).wasOk());
            expect(f.document.project().assets[0]->name == "square.svg");
            expect(f.clip(f.first) != nullptr && f.clip(f.first)->asset == original->id, "clips keep their source identity");
            expect(f.undo.getUndoDescription() == "Replace source");
            auto notes = std::make_shared<motion::Asset>(*square);
            notes->midi = motion::MidiNotes::create({}).source;
            notes->source.reset();
            expect(f.document.replaceAsset(original->id, notes).failed(), "a visual source cannot become a MIDI file");
            expect(f.undo.undo());
            expect(f.document.project().assets[0] == original);
        }
    }
};
static MotionClipboardTest motionClipboardTest;
