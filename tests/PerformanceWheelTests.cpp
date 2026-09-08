#if OSCI_PREMIUM
#include <osci_gui/osci_gui.h>
#include "../Source/audio/modulation/WheelParameters.h"
#include "../Source/audio/modulation/ModulationEngine.h"
#include "TestCleanup.h"

class PerformanceWheelTests final : public juce::UnitTest {
public:
    PerformanceWheelTests() : juce::UnitTest("Performance wheels", "Wheels") {}
    void runTest() override {
        juce::MessageManager::getInstance();
        beginTest("CC1 is sample accurate and ignores unrelated controllers");
        {
            WheelParameters parameters;
            parameters.prepareToPlay(48000.0, 8);
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::controllerEvent(2, 1, 127), 3);
            midi.addEvent(juce::MidiMessage::controllerEvent(2, 7, 0), 4);
            midi.addEvent(juce::MidiMessage::controllerEvent(5, 1, 0), 6);
            parameters.fillBlock(8, midi);
            for (int i = 0; i < 8; ++i) {
                expectEquals(parameters.buffer[i], i >= 3 && i < 6 ? 1.0f : 0.0f);
            }
            parameters.modulation->setValueUnnormalised(0.25f);
            midi.clear();
            parameters.fillBlock(8, midi);
            expectEquals(parameters.buffer[0], 0.25f);
            expectEquals(parameters.buffer[7], 0.25f);
            parameters.addAssignment({0, "pitchModulation", 0.3f, true});
            juce::XmlElement state("state");
            parameters.saveToXml(&state);
            parameters.loadFromXml(&state);
            const auto assignments = parameters.getAssignments();
            expectEquals(int(assignments.size()), 1);
            if (!assignments.empty()) {
                expectEquals(assignments[0].depth, 0.3f);
                expect(assignments[0].bipolar);
            }
            for (auto* parameter : parameters.getFloatParameters()) { delete parameter; }
        }
        beginTest("CC1 modulates pitch sample accurately and engine state restores its routing");
        {
            WheelParameters parameters;
            osci::SimpleEffect target(new osci::EffectParameter("Pitch", "Pitch", "pitchModulation", VERSION_HINT, 0.0f, -1.0f, 1.0f));
            std::unordered_map<juce::String, ParamLocation> locations{{"pitchModulation", {&target, 0}}};
            ModulationEngine engine(locations);
            engine.addSource(&parameters);
            engine.prepareToPlay(48000.0, 8);
            target.prepareToPlay(48000.0, 8);
            parameters.addAssignment({0, "pitchModulation", 0.5f, false});
            juce::XmlElement state("state");
            engine.saveToXml(&state);
            parameters.removeAssignment(0, "pitchModulation");
            engine.loadFromXml(&state);
            expectEquals(int(parameters.getAssignments().size()), 1);
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::controllerEvent(1, 1, 127), 3);
            midi.addEvent(juce::MidiMessage::controllerEvent(1, 1, 0), 6);
            parameters.fillBlock(8, midi);
            target.animateValues(8, nullptr);
            engine.applyAllModulation(8);
            for (int i = 0; i < 8; ++i) {
                expectEquals(target.getAnimatedValue(0, i), i >= 3 && i < 6 ? 1.0f : 0.0f);
            }
            juce::XmlElement emptyState("empty");
            engine.loadFromXml(&emptyState);
            expect(parameters.getAssignments().empty());
            testutil::cleanupEffectParams(target);
            for (auto* parameter : parameters.getFloatParameters()) {
                delete parameter;
            }
        }
        beginTest("Wheel input respects block boundaries and preserves each channel's pitch");
        {
            WheelParameters parameters;
            parameters.prepareToPlay(48000.0, 8);
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::pitchWheel(2, 16383), 0);
            midi.addEvent(juce::MidiMessage::pitchWheel(16, 0), 7);
            midi.addEvent(juce::MidiMessage::controllerEvent(1, 1, 64), 7);
            midi.addEvent(juce::MidiMessage::controllerEvent(1, 1, 0), 8);
            midi.addEvent(juce::MidiMessage::pitchWheel(2, 8192), 8);
            parameters.fillBlock(8, midi);
            expectEquals(parameters.pitch[0]->getValueUnnormalised(), 0.0f);
            expectEquals(parameters.pitch[1]->getValueUnnormalised(), 8191.0f / 8192.0f);
            expectEquals(parameters.pitch[15]->getValueUnnormalised(), -1.0f);
            expectEquals(parameters.lastPitch[1], 16383);
            expectEquals(parameters.buffer[6], 0.0f);
            expectEquals(parameters.buffer[7], 64.0f / 127.0f);
            midi.clear();
            parameters.fillBlock(8, midi);
            expectEquals(parameters.buffer[0], 64.0f / 127.0f);
            expectEquals(parameters.buffer[7], 64.0f / 127.0f);
            for (auto* parameter : parameters.getFloatParameters()) {
                delete parameter;
            }
        }
        beginTest("External wheel updates repaint without feeding changes back to the host");
        {
            osci::PerformanceWheel wheel(osci::PerformanceWheel::Mode::modulation);
            wheel.setBounds(0, 0, 20, 54);
            const auto before = wheel.createComponentSnapshot(wheel.getLocalBounds());
            int notifications = 0;
            wheel.onValueChange = [&] { ++notifications; };
            wheel.setExternalValue(1.0);
            const auto after = wheel.createComponentSnapshot(wheel.getLocalBounds());
            expectEquals(wheel.getValue(), 1.0);
            expectEquals(notifications, 0);
            bool changed = false;
            for (int y = 0; y < before.getHeight(); ++y) {
                for (int x = 0; x < before.getWidth(); ++x) {
                    changed |= before.getPixelAt(x, y) != after.getPixelAt(x, y);
                }
            }
            expect(changed, "The wheel must display the incoming value");
        }
        beginTest("Pitch returns to neutral within the drag gesture; modulation stays put");
        for (auto mode : {osci::PerformanceWheel::Mode::pitch, osci::PerformanceWheel::Mode::modulation}) {
            osci::PerformanceWheel wheel(mode);
            wheel.setBounds(0, 0, 45, 140);
            const auto now = juce::Time::getCurrentTime();
            const juce::MouseEvent event(juce::Desktop::getInstance().getMainMouseSource(), {20.0f, 60.0f},
                juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                &wheel, &wheel, now, {20.0f, 60.0f}, now, 1, false);
            double valueAtGestureEnd = -10.0;
            wheel.onDragEnd = [&] { valueAtGestureEnd = wheel.getValue(); };
            wheel.mouseDown(event);
            wheel.setValue(0.75, juce::sendNotificationSync);
            wheel.mouseUp(event);
            const double expected = mode == osci::PerformanceWheel::Mode::pitch ? 0.0 : 0.75;
            expectEquals(wheel.getValue(), expected);
            expectEquals(valueAtGestureEnd, expected);
        }
        beginTest("Neutral pitch indicator is symmetric at keyboard and MIDI-row sizes");
        for (const auto size : {juce::Point<int>{16, 34}, juce::Point<int>{20, 54}}) {
            osci::PerformanceWheel wheel(osci::PerformanceWheel::Mode::pitch);
            const int width = size.x, height = size.y;
            wheel.setBounds(0, 0, width, height);
            wheel.setAccentColour(juce::Colours::lime);
            juce::Image image(juce::Image::ARGB, width * 2, height * 2, true);
            {
                juce::Graphics g(image);
                g.addTransform(juce::AffineTransform::scale(2.0f));
                wheel.paint(g);
            }
            int maxDifference = 0;
            for (int y = height - height / 5; y < height + height / 5; ++y) {
                for (int x = 0; x < image.getWidth(); ++x) {
                    const auto colour = image.getPixelAt(x, y);
                    for (auto mirror : {image.getPixelAt(x, image.getHeight() - 1 - y), image.getPixelAt(image.getWidth() - 1 - x, y)}) {
                        // Isolate the green indicator from the neutral background and ribs.
                        const int green = int(colour.getGreen()) - int(colour.getRed());
                        const int otherGreen = int(mirror.getGreen()) - int(mirror.getRed());
                        maxDifference = juce::jmax(maxDifference, std::abs(green - otherGreen));
                    }
                }
            }
            expect(maxDifference <= 2, "Indicator symmetry differs by " + juce::String(maxDifference) + " colour levels");
        }
        beginTest("Alt-drag assigns modulation without changing the value or opening a parameter gesture");
        {
            osci::PerformanceWheel wheel(osci::PerformanceWheel::Mode::modulation);
            wheel.setBounds(0, 0, 24, 54);
            wheel.setValue(0.4, juce::dontSendNotification);
            int assignments = 0, gestures = 0;
            wheel.onAssignmentDrag = [&](const juce::MouseEvent&) { ++assignments; };
            wheel.onDragStart = [&] { ++gestures; };
            const auto now = juce::Time::getCurrentTime();
            const juce::MouseEvent down(juce::Desktop::getInstance().getMainMouseSource(), {12.0f, 30.0f},
                juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier),
                1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &wheel, &wheel, now, {12.0f, 30.0f}, now, 1, false);
            wheel.mouseDown(down);
            wheel.mouseDrag(down.withNewPosition(juce::Point<float>{40.0f, 10.0f}));
            wheel.mouseUp(down);
            expectEquals(assignments, 1);
            expectEquals(gestures, 0);
            expectEquals(wheel.getValue(), 0.4);
        }
    }
};
static PerformanceWheelTests performanceWheelTests;
#endif
