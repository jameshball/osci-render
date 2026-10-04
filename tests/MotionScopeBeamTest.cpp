#include <JuceHeader.h>
#include "../Source/motion/model/Document.h"
#include "../Source/motion/render/CompositionRenderer.h"
#include "../Source/motion/ScopeBeam.h"

class MotionScopeBeamTest : public juce::UnitTest {
public:
    MotionScopeBeamTest() : juce::UnitTest("Motion Scope beam properties", "Motion") {}

    struct Fixture {
        juce::UndoManager undo;
        motion::Document document{undo};
        motion::Id camera = 0;
        void initialise() {
            motion::Project project;
            project.duration = 10;
            motion::Camera view;
            view.id = camera = document.newId();
            project.cameras = {view};
            document.reset(std::move(project));
        }
        motion::Id beam() const { return document.project().beam.id; }
        motion::PreparedComposition prepare() const { return motion::PreparedComposition(document.project(), 48000, nullptr, motion::CompositionPurpose::editorGeometry); }
    };

    void runTest() override {
        beginTest("The Scope is a property target with the visualiser's ranges and defaults");
        {
            Fixture f; f.initialise();
            expect(f.beam() == motion::beamIdentity);
            const auto target = motion::findPropertyTarget(f.document.project(), f.beam());
            expect(target.has_value() && target->beam && !target->camera);
            expect(motion::propertySpecs(*target).size() == motion::beamPropertyNames.size());
            VisualiserParameters parameters;
            const auto specs = motion::beamPropertySpecs();
            for (std::size_t index = 0; index < motion::beamPropertyNames.size(); ++index) {
                const juce::String id(motion::beamPropertyNames[index]);
                expect(specs[index].id == motion::beamPropertyNames[index]);
                const osci::EffectParameter* found = nullptr;
                for (const auto* list : {&parameters.effects, &parameters.audioEffects}) {
                    for (const auto& effect : *list) {
                        for (auto* parameter : effect->parameters) {
                            if (parameter->paramID == id) { found = parameter; }
                        }
                    }
                }
                expect(found != nullptr, id);
                if (found == nullptr) { continue; }
                expectWithinAbsoluteError(specs[index].minimum, static_cast<double>(found->min.load()), 1.0e-6, id);
                expectWithinAbsoluteError(specs[index].maximum, static_cast<double>(found->max.load()), 1.0e-6, id);
                expectWithinAbsoluteError(specs[index].fallback, static_cast<double>(found->defaultValue.load()), 1.0e-6, id);
                expectWithinAbsoluteError(motion::Beam::defaults[index], static_cast<double>(found->defaultValue.load()), 1.0e-6, id);
            }
        }

        beginTest("Keys on Scope properties drive the prepared beam, clamped to range");
        {
            Fixture f; f.initialise();
            const auto id = f.beam();
            f.document.edit("Key intensity", [](motion::Project& project) {
                auto& curve = project.beam.properties["intensity"];
                curve.setKey({0.0, 2.0, motion::Interpolation::linear});
                curve.setKey({4.0, 8.0, motion::Interpolation::linear});
                project.beam.properties["glow"].base = 5.0;
            });
            const auto prepared = f.prepare();
            expectWithinAbsoluteError(prepared.beam.at(0)[0], 2.0f, 1.0e-4f);
            expectWithinAbsoluteError(prepared.beam.at(2)[0], 5.0f, 1.0e-4f);
            expectWithinAbsoluteError(prepared.beam.at(6)[0], 8.0f, 1.0e-4f);
            // Glow's range is 0-1.
            expectWithinAbsoluteError(prepared.beam.at(1)[4], 1.0f, 1.0e-6f);
            f.undo.undo();
            expect(!f.document.project().beam.properties.at("intensity").animated());
            expect(f.document.project().beam.id == id);
        }

        beginTest("Modulators route to Scope properties from the main composition");
        {
            Fixture f; f.initialise();
            motion::Modulator lfo;
            lfo.shape.rateHz = 1;
            motion::Id created = 0;
            expect(f.document.addRoutedModulator(lfo, {0, 0, f.beam(), "hue", 90, motion::ModulationMode::add}, created).wasOk());
            const auto prepared = f.prepare();
            float lowest = 1.0e9f, highest = -1.0e9f;
            for (int step = 0; step < 40; ++step) {
                const auto value = prepared.beam.at(step * 0.025)[5];
                lowest = std::min(lowest, value);
                highest = std::max(highest, value);
            }
            expect(highest - lowest > 60.0f, "The oscillator should swing the hue");
            expect(lowest >= 0.0f && highest <= 359.0f);
            // A default route depth is a quarter of the property's range.
            expectWithinAbsoluteError(motion::Document::routeAmount(f.document.project(), f.beam(), "hue"), 0.25 * 359, 1.0e-9);
            // Nothing links to the Scope; the Scope may follow other properties.
            expect(f.document.setLink(f.camera, "fov", motion::PropertyLink {f.beam(), "focus"}).failed());
            expect(f.document.setLink(f.beam(), "focus", motion::PropertyLink {f.camera, "fov"}).wasOk());
        }

        beginTest("Scope properties save and load; older projects get the defaults; the Scope's identity is reserved");
        {
            Fixture f; f.initialise();
            f.document.edit("Key focus", [](motion::Project& project) {
                project.beam.properties["focus"].setKey({1.5, 3.0, motion::Interpolation::smooth});
                project.beam.properties["ambient"].base = 0.75;
            });
            const auto saved = f.document.save();
            juce::UndoManager undo;
            motion::Document loaded(undo);
            expect(loaded.load(saved).wasOk());
            expect(loaded.project().beam.id == motion::beamIdentity);
            expect(loaded.project().beam.properties.at("focus").keyframes().size() == 1);
            expectWithinAbsoluteError(loaded.project().beam.properties.at("ambient").base, 0.75, 1.0e-9);

            auto older = saved;
            older.removeChildElement(older.getChildByName("scopeBeam"), true);
            motion::Document legacy(undo);
            expect(legacy.load(older).wasOk());
            expect(legacy.project().beam.valid());
            expectWithinAbsoluteError(legacy.project().beam.properties.at("intensity").base, 5.0, 1.0e-9);

            auto broken = saved;
            broken.getChildByName("scopeBeam")->createNewChildElement("property")->setAttribute("name", "position.x");
            motion::Document rejected(undo);
            expect(rejected.load(broken).failed());
            auto clash = saved;
            clash.getChildByName("camera")->setAttribute("id", juce::String(static_cast<juce::int64>(motion::beamIdentity)));
            expect(rejected.load(clash).failed());
        }

        beginTest("The visualiser's beam effects take the document's values without touching their parameters");
        {
            VisualiserParameters parameters;
            const motion::ScopeBeamSlots slots(parameters);
            const auto before = parameters.intensityEffect->parameters[0]->getValueUnnormalised();
            parameters.intensityEffect->animateValues(16, nullptr);
            slots.write(0, 16, [](int) { return 7.5f; });
            parameters.intensityEffect->publishAnimatedToActual(16);
            expectWithinAbsoluteError(parameters.intensityEffect->getActualValue(), 7.5f, 1.0e-6f);
            expectWithinAbsoluteError(parameters.intensityEffect->parameters[0]->getValueUnnormalised(), before, 1.0e-6f);
        }
    }
};

static MotionScopeBeamTest motionScopeBeamTest;
