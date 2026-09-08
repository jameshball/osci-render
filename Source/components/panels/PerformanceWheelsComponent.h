#pragma once
#include "../../PluginProcessor.h"
#include "../effects/EffectComponent.h"
#include "../../visualiser/FramePresenter.h"

// The reusable wheels know nothing about MIDI or modulation routing.
class PerformanceWheelsComponent final : public juce::Component, public juce::DragAndDropTarget, private juce::Timer {
public:
    static constexpr int wheelWidth = 20;
    static constexpr int compactWheelWidth = 16;
    static constexpr int wheelGap = 3;
    static constexpr int preferredWidth = 2 * wheelWidth + wheelGap;
    static constexpr int compactPreferredWidth = 2 * compactWheelWidth + wheelGap;

    PerformanceWheelsComponent(OscirenderAudioProcessor& p, juce::CustomMidiKeyboardComponent& k)
        : processor(p), keyboard(k), pitchRouting(*p.pitchModulation) {
        addAndMakeVisible(pitch);
        addAndMakeVisible(mod);
        setName("Performance wheels");
        setSize(preferredWidth, 34);
        pitchRouting.wireModulation(processor);
        pitch.setTooltip("Pitch bend: drag to bend, release to centre. Drop modulation here; right-click to edit its depth.");
        mod.setTooltip("Mod wheel (MIDI CC1). Hold Alt/Option and drag onto a parameter to assign modulation.");
        mod.onAssignmentDrag = [this](const juce::MouseEvent&) {
            auto* container = juce::DragAndDropContainer::findParentDragContainerFor(&mod);
            if (container != nullptr && !container->isDragAndDropActive()) {
                container->startDragging(ModDrag::make("MW", 0), &mod, juce::ScaledImage(mod.createComponentSnapshot(mod.getLocalBounds())), !FramePresenter::usesNativeSurface());
                ModulationState::anyDragActive.store(true);
            }
        };
        connect(pitch, true);
        connect(mod, false);
        startTimerHz(30);
        timerCallback();
    }
    ~PerformanceWheelsComponent() override {
        stopTimer();
        // The editor can be closed while a wheel is held.
        if (pitchGesture != nullptr) {
            pitchGesture->setUnnormalisedValueNotifyingHost(0.0f);
            pitchGesture->endChangeGesture();
        }
        if (modGesture) { processor.wheelParameters.modulation->endChangeGesture(); }
    }
    void resized() override {
        auto area = getLocalBounds();
        const int width = (area.getWidth() - wheelGap) / 2;
        pitch.setBounds(area.removeFromLeft(width));
        area.removeFromLeft(wheelGap);
        mod.setBounds(area.removeFromLeft(width));
    }
    bool isInterestedInDragSource(const SourceDetails& details) override { return details.localPosition.x < pitch.getRight() && pitchRouting.isInterestedInDragSource(details); }
    void itemDragEnter(const SourceDetails&) override { dropHighlight = true; repaint(); }
    void itemDragExit(const SourceDetails&) override { dropHighlight = false; repaint(); }
    void itemDropped(const SourceDetails& details) override {
        pitchRouting.itemDropped(details);
        dropHighlight = false;
        repaint();
    }
    void paintOverChildren(juce::Graphics& g) override {
        if (dropHighlight) {
            g.setColour(osci::Colours::accentColor());
            g.drawRoundedRectangle(pitch.getBounds().toFloat(), 5.0f, 1.5f);
        }
    }
private:
    void connect(osci::PerformanceWheel& wheel, bool isPitch) {
        wheel.onDragStart = [this, isPitch] {
            if (isPitch) {
                pitchGesture = processor.wheelParameters.pitch[keyboard.getMidiChannel() - 1];
                pitchGesture->beginChangeGesture();
            } else {
                modGesture = true;
                processor.wheelParameters.modulation->beginChangeGesture();
            }
        };
        wheel.onValueChange = [this, &wheel, isPitch] {
            if (refreshing) { return; }
            auto* parameter = isPitch ? (pitchGesture != nullptr ? pitchGesture : processor.wheelParameters.pitch[keyboard.getMidiChannel() - 1]) : processor.wheelParameters.modulation;
            const bool inGesture = isPitch ? pitchGesture != nullptr : modGesture;
            if (!inGesture) { parameter->beginChangeGesture(); }
            parameter->setUnnormalisedValueNotifyingHost(float(wheel.getValue()));
            if (!inGesture) { parameter->endChangeGesture(); }
        };
        wheel.onDragEnd = [this, isPitch] {
            if (isPitch && pitchGesture != nullptr) {
                pitchGesture->endChangeGesture();
                pitchGesture = nullptr;
            } else if (!isPitch && modGesture) {
                processor.wheelParameters.modulation->endChangeGesture();
                modGesture = false;
            }
        };
        wheel.setPopupMenuEnabled(false);
        wheel.addMouseListener(this, false);
    }
    void mouseDown(const juce::MouseEvent& e) override {
        if (e.eventComponent == &pitch && e.mods.isPopupMenu()) { pitchRouting.showContextMenu(e.getScreenPosition()); }
    }
    void timerCallback() override {
        const juce::ScopedValueSetter<bool> guard(refreshing, true);
        if (pitchGesture == nullptr) {
            pitch.setValue(processor.wheelParameters.pitch[keyboard.getMidiChannel() - 1]->getValueUnnormalised(), juce::sendNotificationSync);
        }
        if (!modGesture) { mod.setValue(processor.wheelParameters.modulation->getValueUnnormalised(), juce::sendNotificationSync); }
        const auto offset = processor.wheelParameters.pitchDisplay.load(std::memory_order_relaxed);
        pitch.setModulatedValue(pitch.getValue() + offset, std::abs(offset) > 0.0001f);
    }
    OscirenderAudioProcessor& processor;
    juce::CustomMidiKeyboardComponent& keyboard;
    osci::PerformanceWheel pitch{osci::PerformanceWheel::Mode::pitch}, mod{osci::PerformanceWheel::Mode::modulation};
    EffectComponent pitchRouting;
    osci::FloatParameter* pitchGesture = nullptr;
    bool modGesture = false, refreshing = false, dropHighlight = false;
};
