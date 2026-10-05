#include <JuceHeader.h>
#include "../Source/motion/model/Document.h"
#include "../Source/motion/import/SourceDecoding.h"
#include "../Source/motion/model/PropertyTarget.h"
#include "../Source/motion/render/CompositionRenderer.h"

class MotionModulatorsTest : public juce::UnitTest {
public:
    MotionModulatorsTest() : juce::UnitTest("Motion shared modulators and links", "Motion") {}

    struct Fixture {
        juce::UndoManager undo;
        motion::Document document{undo};
        motion::Id first = 0, second = 0, drums = 0;
        void initialise() {
            auto asset = std::make_shared<motion::Asset>();
            asset->id = document.newId(); asset->name = "triangle.obj"; asset->extension = ".obj";
            const juce::String obj("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
            asset->data.append(obj.toRawUTF8(), obj.getNumBytesAsUTF8());
            motion::decodeAsset(*asset);
            auto a = motion::Document::makeClip(document.newId(), *asset, 0);
            a.duration = 8;
            a.properties["position.x"].setKey({0, 0, motion::Interpolation::linear});
            a.properties["position.x"].setKey({4, 2, motion::Interpolation::linear});
            auto b = motion::Document::makeClip(document.newId(), *asset, 2);
            b.duration = 6;
            b.offset = 1;
            auto c = motion::Document::makeClip(document.newId(), *asset, 0);
            c.duration = 8;
            std::vector<motion::MidiNote> notes {{1, 0, 0.5, 36, 127, 1}, {2, 2, 0.5, 38, 64, 1}, {3, 4, 0.5, 36, 127, 1}};
            c.midi = motion::MidiNotes::create(notes).source;
            first = a.id; second = b.id; drums = c.id;
            motion::Track one, two, three;
            one.id = document.newId(); one.name = "One"; one.insert(a, motion::Tempo(120));
            two.id = document.newId(); two.name = "Two"; two.insert(b, motion::Tempo(120));
            three.id = document.newId(); three.name = "Drums"; three.insert(c, motion::Tempo(120));
            motion::Project project;
            project.duration = 10; project.bpm = 120; project.assets = {asset}; project.tracks = {one, two, three};
            document.reset(std::move(project));
        }
        const motion::PreparedClip* prepared(const motion::PreparedComposition& composition, motion::Id id) const {
            for (const auto& clip : composition.clips) { if (clip.id == id) { return &clip; } }
            return nullptr;
        }
    };

    void runTest() override {
        beginTest("One oscillator drives several properties on one project clock");
        {
            Fixture f; f.initialise();
            motion::Modulator lfo;
            lfo.name = "Wobble";
            lfo.shape.waveform = motion::ModulationWaveform::saw;
            lfo.shape.rateHz = 0.5;
            motion::Id lfoId = 0, route = 0;
            expect(f.document.addModulator(lfo, lfoId).wasOk());
            expect(f.document.addRoute({0, lfoId, f.first, "position.y", 2, motion::ModulationMode::add}, route).wasOk());
            expect(f.document.addRoute({0, lfoId, f.second, "position.y", -1, motion::ModulationMode::add}, route).wasOk());
            motion::PreparedComposition composition(f.document.project(), 48000, nullptr, motion::CompositionPurpose::editorGeometry);
            expect(composition.preparationError.isEmpty());
            const auto* a = f.prepared(composition, f.first);
            const auto* b = f.prepared(composition, f.second);
            expect(a != nullptr && b != nullptr);
            // At project time 3 s the saw is 2 * (1.5 mod 1) - 1 = 0.
            // Clip b starts at 2 s with offset 1, so its local time is 2.
            expectWithinAbsoluteError(a->curves[1].evaluate(a->localTime(3.0)), 0.0, 1.0e-9);
            expectWithinAbsoluteError(a->curves[1].evaluate(a->localTime(3.5)), 2 * (2 * 0.75 - 1), 1.0e-9);
            expectWithinAbsoluteError(b->curves[1].evaluate(b->localTime(3.5)), -(2 * 0.75 - 1), 1.0e-9);
            expect(f.document.removeModulator(lfoId).wasOk());
            expect(f.document.project().routes.empty());
        }
        beginTest("Envelopes follow a MIDI clip's notes, pitch range and velocity");
        {
            Fixture f; f.initialise();
            motion::Modulator kick;
            kick.name = "Kick";
            kick.kind = motion::ModulatorKind::envelope;
            kick.source = f.drums;
            kick.attack = 0.01; kick.decay = 0.1; kick.sustain = 0.5; kick.release = 0.2;
            kick.lowestPitch = 36; kick.highestPitch = 36;
            motion::Id id = 0, route = 0;
            expect(f.document.addModulator(kick, id).wasOk());
            expect(f.document.addRoute({0, id, f.first, "scale.x", 1, motion::ModulationMode::add}, route).wasOk());
            motion::PreparedComposition composition(f.document.project(), 48000, nullptr, motion::CompositionPurpose::editorGeometry);
            const auto* a = f.prepared(composition, f.first);
            const auto scaleAt = [&](double time) { return a->curves[6].evaluate(a->localTime(time)) - 1; };
            expectWithinAbsoluteError(scaleAt(0.005), 0.5, 1.0e-9); // halfway up the attack
            expectWithinAbsoluteError(scaleAt(0.06), 0.75, 1.0e-9); // halfway down the decay
            expectWithinAbsoluteError(scaleAt(0.2), 0.5, 1.0e-9);   // sustain while held (0.25 s note)
            expectWithinAbsoluteError(scaleAt(0.35), 0.25, 1.0e-9); // halfway through the release
            expectWithinAbsoluteError(scaleAt(1.1), 0.0, 1.0e-9);   // the snare (38) is outside the range
            expectWithinAbsoluteError(scaleAt(2.2), 0.5, 1.0e-9);   // the second kick at beat 4 = 2 s
            kick.id = id; kick.velocity = 1; kick.lowestPitch = 0; kick.highestPitch = 127;
            expect(f.document.setModulator(kick).wasOk());
            motion::PreparedComposition all(f.document.project(), 48000, nullptr, motion::CompositionPurpose::editorGeometry);
            const auto* b = f.prepared(all, f.first);
            expectWithinAbsoluteError(b->curves[6].evaluate(b->localTime(1.2)) - 1, 0.5 * 64 / 127.0, 1.0e-9);
        }
        beginTest("Links replace keys with a scaled, delayed copy of another property");
        {
            Fixture f; f.initialise();
            expect(f.document.setLink(f.second, "position.y", motion::PropertyLink {f.first, "position.x", 2, 1, 0.5}).wasOk());
            motion::PreparedComposition composition(f.document.project(), 48000, nullptr, motion::CompositionPurpose::editorGeometry);
            const auto* b = f.prepared(composition, f.second);
            // At project 3 s the source is read at 2.5 s: x = 1.25, so y = 3.5.
            expectWithinAbsoluteError(b->curves[1].evaluate(b->localTime(3.0)), 3.5, 1.0e-9);
            expect(f.document.setLink(f.first, "position.x", motion::PropertyLink {f.second, "position.y", 1, 0, 0}).failed());
            expect(f.document.setLink(f.second, "position.y", std::nullopt).wasOk());
            expect(!f.document.project().tracks[1].clips[0].properties.at("position.y").link.has_value());
        }
        beginTest("Modulators, routes and links survive save and reload");
        {
            Fixture f; f.initialise();
            motion::Modulator random;
            random.name = "Drift";
            random.shape.waveform = motion::ModulationWaveform::noiseSmooth;
            random.shape.tempoSync = true;
            random.shape.beatsPerCycle = 3;
            random.shape.seed = 77;
            motion::Id id = 0, route = 0;
            expect(f.document.addModulator(random, id).wasOk());
            expect(f.document.addRoute({0, id, f.first, "rotation.z", 45.5, motion::ModulationMode::multiply}, route).wasOk());
            expect(f.document.setLink(f.second, "red", motion::PropertyLink {f.first, "position.x", 0.25, 0.1, -0.2}).wasOk());
            const auto xml = f.document.save();
            motion::Project loaded;
            const auto result = motion::Document::prepareLoad(xml, loaded);
            expect(result.wasOk(), result.getErrorMessage());
            expect(loaded.modulators == f.document.project().modulators);
            expect(loaded.routes == f.document.project().routes);
            const auto* link = motion::findPropertyCurve(loaded, f.second, "red");
            expect(link != nullptr && link->link == f.document.project().tracks[1].clips[0].properties.at("red").link);
        }
        beginTest("Deleting a clip prunes the routes and links that referred to it");
        {
            Fixture f; f.initialise();
            motion::Modulator lfo;
            motion::Id id = 0, route = 0;
            expect(f.document.addModulator(lfo, id).wasOk());
            expect(f.document.addRoute({0, id, f.first, "position.y", 1, motion::ModulationMode::add}, route).wasOk());
            expect(f.document.setLink(f.second, "position.y", motion::PropertyLink {f.first, "position.x", 1, 0, 0}).wasOk());
            expect(f.document.removeClips({f.first}).wasOk());
            expect(f.document.project().routes.empty());
            expect(!f.document.project().tracks[1].clips[0].properties.at("position.y").link.has_value());
            motion::Project loaded;
            expect(motion::Document::prepareLoad(f.document.save(), loaded).wasOk());
            expect(f.undo.undo());
            expectEquals(static_cast<int>(f.document.project().routes.size()), 1);
        }
        beginTest("Duplicates keep routes; compositions take their clips' routes and modulators");
        {
            Fixture f; f.initialise();
            motion::Modulator lfo;
            motion::Id id = 0, route = 0;
            expect(f.document.addModulator(lfo, id).wasOk());
            expect(f.document.addRoute({0, id, f.first, "position.y", 1, motion::ModulationMode::add}, route).wasOk());
            std::vector<motion::Id> duplicates;
            expect(f.document.duplicateClips({f.first}, duplicates).wasOk());
            expectEquals(static_cast<int>(f.document.project().routes.size()), 2);
            expect(f.document.project().routes[1].target == duplicates[0] && f.document.project().routes[1].modulator == id);
            motion::Id instance = 0;
            expect(f.document.createComposition({f.first}, "Moved", instance).wasOk());
            const auto& project = f.document.project();
            expectEquals(static_cast<int>(project.routes.size()), 1);
            expect(project.routes[0].target == duplicates[0]);
            const auto& definition = *project.definitions.back();
            expectEquals(static_cast<int>(definition.modulators.size()), 1);
            expectEquals(static_cast<int>(definition.routes.size()), 1);
            expect(definition.modulators[0].id != id && definition.routes[0].modulator == definition.modulators[0].id && definition.routes[0].target == f.first);
            motion::Project loaded;
            const auto result = motion::Document::prepareLoad(f.document.save(), loaded);
            expect(result.wasOk(), result.getErrorMessage());
            motion::Id unique = 0;
            expect(f.document.makeCompositionUnique(instance, unique).wasOk());
            const auto& fork = *f.document.project().definitions.back();
            expectEquals(static_cast<int>(fork.routes.size()), 1);
            expect(fork.routes[0].target != f.first && fork.routes[0].modulator == fork.modulators[0].id);
            expect(motion::findPropertyCurve(fork, fork.routes[0].target, "position.y") != nullptr);
        }
        beginTest("Envelopes stay exact and cheap under a long held note");
        {
            motion::PreparedModulator envelope;
            envelope.kind = motion::ModulatorKind::envelope;
            envelope.envelope.sustainLevel = 0.25;
            envelope.envelope.releaseSeconds = 0.1;
            envelope.notes.push_back({0, 60, 1});
            for (int index = 0; index < 2000; ++index) { envelope.notes.push_back({0.5 + index * 0.02, 0.5 + index * 0.02 + 0.01, 0.8}); }
            envelope.buildIndex();
            expectWithinAbsoluteError(envelope.value(10.505, 0), 0.25, 1.0e-9); // drone sustain vs a short note's sustain
            expectWithinAbsoluteError(envelope.value(60.05, 0), 0.125, 1.0e-9);  // drone halfway through its release
            expectWithinAbsoluteError(envelope.value(61, 0), 0.0, 1.0e-9);
        }
        beginTest("Audio properties refuse routes and links; duplicates keep internal links");
        {
            Fixture f; f.initialise();
            motion::Id id = 0, route = 0;
            motion::Modulator lfo;
            expect(f.document.addModulator(lfo, id).wasOk());
            f.document.edit("Self link", [&](motion::Project& project) {
                project.tracks[0].clips[0].properties["position.y"].link = motion::PropertyLink {f.first, "position.x", 1, 0, 0};
            });
            std::vector<motion::Id> duplicates;
            expect(f.document.duplicateClips({f.first}, duplicates).wasOk());
            const auto* copied = motion::findPropertyCurve(f.document.project(), duplicates[0], "position.y");
            expect(copied != nullptr && copied->link.has_value() && copied->link->source == duplicates[0], "the copy follows its own X");
            motion::Id created = 0;
            expect(f.document.addRoutedModulator(lfo, {0, 0, f.second, "position.y", 1, motion::ModulationMode::add}, created).wasOk());
            expectEquals(static_cast<int>(f.document.project().modulators.size()), 2);
            expect(f.undo.getUndoDescription() == "Route new modulator");
            expect(f.document.addRoutedModulator(lfo, {0, 0, 424242, "position.y", 1, motion::ModulationMode::add}, created).failed());
            expectEquals(static_cast<int>(f.document.project().modulators.size()), 2);
            juce::ignoreUnused(route);
        }
        beginTest("Loading rejects cyclic links and dangling routes");
        {
            Fixture f; f.initialise();
            auto xml = f.document.save();
            auto* clip = xml.getChildByName("track")->getChildByName("clip");
            for (auto* property : clip->getChildWithTagNameIterator("property")) {
                if (property->getStringAttribute("name") == "position.x") {
                    auto* link = property->createNewChildElement("link");
                    link->setAttribute("source", juce::String(f.first));
                    link->setAttribute("property", "position.x");
                }
            }
            motion::Project loaded;
            expect(motion::Document::prepareLoad(xml, loaded).failed());
            auto dangling = f.document.save();
            auto* route = dangling.createNewChildElement("route");
            route->setAttribute("id", "9999"); route->setAttribute("modulator", "9998");
            route->setAttribute("target", juce::String(f.first)); route->setAttribute("property", "position.x");
            route->setAttribute("amount", "1"); route->setAttribute("mode", 0);
            expect(motion::Document::prepareLoad(dangling, loaded).failed());
        }
    }
};

static MotionModulatorsTest motionModulatorsTest;
