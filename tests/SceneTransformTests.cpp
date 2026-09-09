#include <JuceHeader.h>
#include "../Source/scene/Scene.h"
#include "../Source/scene/NavigationMotion.h"

class SceneTransformTests final : public juce::UnitTest {
public:
    SceneTransformTests() : juce::UnitTest("Scene transforms and automation", "Scene") {}
    void runTest() override {
        beginTest("Held navigation is independent of frame rate and slows smoothly on release");
        auto travel = [](int rate) {
            scene::NavigationMotion motion;
            float distance = 0;
            for (int i = 0; i < rate; ++i) { distance += motion.advance({ 0, 0, 1 }, 1.0f / rate, 1).z; }
            for (int i = 0; i < rate; ++i) { distance += motion.advance({}, 1.0f / rate, 1).z; }
            return distance;
        };
        expectWithinAbsoluteError(travel(30), travel(60), 0.00001f);
        expectWithinAbsoluteError(travel(60), travel(120), 0.00001f);
        expectWithinAbsoluteError(travel(60), 1.0f, 0.00001f);
        scene::NavigationMotion diagonal;
        auto firstStep = diagonal.advance({ 1, 1, 1 }, 1.0f / 60, 1);
        expect(firstStep.magnitude() > 0 && firstStep.magnitude() < 1.0f / 60);

        scene::Automation bank;
        std::vector<osci::FloatParameter*> parameters;
        bank.initialise(parameters);
        {
            beginTest("Object transform and camera inverse preserve depth and colour");
            scene::Transform object(bank), camera(bank);
            object.set(0, 0.8f);
            object.set(1, -0.3f);
            object.set(2, 1.2f);
            object.set(3, 37.0f);
            object.set(4, -28.0f);
            object.set(5, 63.0f);
            for (int i = 0; i < 6; ++i) { camera.set(i, object.get(i)); }
            const osci::Point original(0.2f, 0.4f, -0.6f, 0.3f, 0.7f, 0.9f);
            const auto restored = camera.view(object.apply(original));
            for (int i = 0; i < 6; ++i) { expectWithinAbsoluteError(restored[i], original[i], 0.00001f); }

            beginTest("Slots bind to identities across scenes, independent of selection");
            scene::Transform secondObject(bank), secondCamera(bank);
            expect(object.expose());
            expect(camera.expose());
            expect(secondObject.expose());
            expect(secondCamera.expose());
            bank.parameters[0][0]->setValueUnnormalised(4.0f);
            expectEquals(object.get(0), 4.0f);
            expectEquals(secondObject.get(0), 0.0f);
            expectEquals(camera.slot.load(), 1);
            expectEquals(secondCamera.slot.load(), 3);

            beginTest("Unexposing preserves the current automated transform");
            object.unexpose();
            bank.parameters[0][0]->setValueUnnormalised(-2.0f);
            expectEquals(object.get(0), 4.0f);
            expectEquals(object.slot.load(), -1);

            beginTest("Copied entities retain values but never inherit a host binding");
            juce::XmlElement xml("object");
            secondObject.set(2, 2.5f);
            secondObject.save(xml);
            scene::Transform copy(bank);
            copy.load(xml, true);
            expectEquals(copy.get(2), 2.5f);
            expectEquals(copy.slot.load(), -1);
            expect(copy.id != secondObject.id);

            beginTest("Unexposed object count does not consume automation capacity");
            std::vector<std::unique_ptr<scene::Transform>> objects;
            for (int i = 0; i < 100; ++i) { objects.push_back(std::make_unique<scene::Transform>(bank)); }
            int exposed = 0;
            for (auto& entity : objects) { if (entity->expose()) { ++exposed; } }
            expectEquals(exposed, scene::slotCount - 3);
            expectEquals((int)objects.size(), 100);

            beginTest("Malformed persisted values cannot escape parameter ranges");
            juce::XmlElement invalid("object");
            invalid.setAttribute("slot", 999);
            invalid.setAttribute("v6", -200.0);
            scene::Transform safe(bank);
            safe.load(invalid);
            expectEquals(safe.slot.load(), -1);
            expectEquals(safe.get(6), 0.01f);
        }
        for (auto* parameter : parameters) { delete parameter; }
    }
};
static SceneTransformTests sceneTransformTests;
