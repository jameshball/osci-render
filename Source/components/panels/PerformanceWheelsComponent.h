#pragma once
#include "../../PluginProcessor.h"
#include "../ModulationState.h"
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
        : processor(p), keyboard(k) {
        addAndMakeVisible(pitch);
        addAndMakeVisible(mod);
        setName("Performance wheels");
        setSize(preferredWidth, 34);
        pitch.setTooltip("Pitch bend: drag to bend, release to centre. Drop modulation here; right-click to edit its depth.");
        mod.setTooltip("Mod wheel (MIDI CC1). Hold Alt/Option and drag onto a parameter to assign modulation; right-click to edit assignments.");
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
    bool isInterestedInDragSource(const SourceDetails& details) override { return sourceForDrag(details) != nullptr; }
    void itemDragEnter(const SourceDetails& details) override { itemDragMove(details); }
    void itemDragMove(const SourceDetails& details) override {
        dropHighlight = pitch.getBounds().contains(details.localPosition);
        repaint();
    }
    void itemDragExit(const SourceDetails&) override { dropHighlight = false; repaint(); }
    void itemDropped(const SourceDetails& details) override {
        ModulationState::anyDragActive.store(false);
        auto* source = sourceForDrag(details);
        if (source != nullptr && pitch.getBounds().contains(details.localPosition)) {
            source->addAssignment({ModDrag::parse(details.description.toString()).index, processor.pitchModulation->getId(), 0.5f, false});
        }
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
    ModulationSource* sourceForDrag(const SourceDetails& details) const {
        const auto drag = ModDrag::parse(details.description.toString());
        if (drag.valid) {
            for (auto* source : processor.getModulationSources()) {
                if (source->getTypeLabel() == drag.type && drag.index >= 0 && drag.index < source->getSourceCount()) {
                    return source;
                }
            }
        }
        return nullptr;
    }

    class DepthControl final : public juce::PopupMenu::CustomComponent {
    public:
        DepthControl(PerformanceWheelsComponent& wheelOwner, ModulationSource& modulationSource, ModAssignment routing)
            : juce::PopupMenu::CustomComponent(false), owner(&wheelOwner), source(modulationSource), assignment(std::move(routing)) {
            depth.setName("Modulation depth");
            depth.setSliderStyle(juce::Slider::LinearHorizontal);
            depth.setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 24);
            depth.setRange(-100.0, 100.0, 0.1);
            depth.setTextValueSuffix("%");
            depth.setValue(assignment.depth * 100.0, juce::dontSendNotification);
            depth.onValueChange = [this] {
                if (owner != nullptr) {
                    assignment.depth = float(depth.getValue() / 100.0);
                    source.addAssignment(assignment);
                }
            };
            addAndMakeVisible(depth);
        }
        void getIdealSize(int& width, int& height) override { width = 260; height = 40; }
        void resized() override { depth.setBounds(getLocalBounds().reduced(8)); }
    private:
        juce::Component::SafePointer<PerformanceWheelsComponent> owner;
        ModulationSource& source;
        ModAssignment assignment;
        juce::Slider depth;
    };

    void showAssignments(bool forPitch, juce::Point<int> screenPosition) {
        juce::PopupMenu menu;
        juce::Component::SafePointer<PerformanceWheelsComponent> safeThis(this);
        for (auto* source : processor.getModulationSources()) {
            for (auto assignment : source->getAssignments()) {
                if (forPitch ? assignment.paramId != processor.pitchModulation->getId() : source != &processor.wheelParameters) {
                    continue;
                }
                juce::PopupMenu connection;
                connection.addCustomItem(1, std::make_unique<DepthControl>(*this, *source, assignment), nullptr, "Modulation depth");
                connection.addItem("Bipolar", true, assignment.bipolar, [safeThis, source, assignment] {
                    if (safeThis != nullptr) {
                        for (auto current : source->getAssignments()) {
                            if (current.sourceIndex == assignment.sourceIndex && current.paramId == assignment.paramId) {
                                current.bipolar = !current.bipolar;
                                source->addAssignment(current);
                                break;
                            }
                        }
                    }
                });
                connection.addItem("Remove", [safeThis, source, assignment] {
                    if (safeThis != nullptr) {
                        source->removeAssignment(assignment.sourceIndex, assignment.paramId);
                    }
                });
                auto label = source->getTypeLabel();
                if (source->getSourceCount() > 1) {
                    label += " " + juce::String(assignment.sourceIndex + 1);
                }
                menu.addSubMenu(label + " → " + processor.getParamDisplayName(assignment.paramId), connection);
            }
        }
        if (menu.getNumItems() == 0) {
            menu.addItem(1, "No modulation assignments", false);
        }
        osci::showContextMenuAsync(std::move(menu), screenPosition, this, {});
    }

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
        if (e.mods.isPopupMenu()) {
            showAssignments(e.eventComponent == &pitch, e.getScreenPosition());
        }
    }
    void timerCallback() override {
        if (pitchGesture == nullptr) {
            pitch.setExternalValue(processor.wheelParameters.pitch[keyboard.getMidiChannel() - 1]->getValueUnnormalised());
        }
        if (!modGesture) { mod.setExternalValue(processor.wheelParameters.modulation->getValueUnnormalised()); }
        const auto offset = processor.wheelParameters.pitchDisplay.load(std::memory_order_relaxed);
        pitch.setModulatedValue(pitch.getValue() + offset, std::abs(offset) > 0.0001f);
    }
    OscirenderAudioProcessor& processor;
    juce::CustomMidiKeyboardComponent& keyboard;
    osci::PerformanceWheel pitch{osci::PerformanceWheel::Mode::pitch}, mod{osci::PerformanceWheel::Mode::modulation};
    osci::FloatParameter* pitchGesture = nullptr;
    bool modGesture = false, dropHighlight = false;
};
