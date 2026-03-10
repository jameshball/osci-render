#include "EffectComponent.h"

#include "../LookAndFeel.h"
#include "../PluginProcessor.h"
#include "modulation/EnvelopeComponent.h"
#include "modulation/LfoComponent.h"
#include "modulation/ModulationHelper.h"

std::atomic<bool> EffectComponent::modAnyDragActive{false};
juce::String EffectComponent::highlightedParamId;
juce::String EffectComponent::modRangeParamId;
std::atomic<float> EffectComponent::modRangeDepth{0.0f};
std::atomic<bool> EffectComponent::modRangeBipolar{false};

EffectComponent::EffectComponent(osci::Effect& effect, int index) : effect(effect), index(index) {
    addAndMakeVisible(slider);
    addChildComponent(lfoSlider);
    addAndMakeVisible(label);
    addAndMakeVisible(settingsButton);

    // LFO ComboBox is only relevant when per-param LFO sub-params exist (beginner mode).
    // In advanced mode, lfo/lfoRate are nullptr so we skip the combobox entirely.
    lfoEnabled = effect.parameters[index]->lfo != nullptr && effect.parameters[index]->lfoRate != nullptr;
    if (lfoEnabled) {
        addAndMakeVisible(lfo);
    }

    sidechainEnabled = effect.parameters[index]->sidechain != nullptr;
    if (sidechainEnabled) {
        sidechainButton = std::make_unique<SvgButton>(effect.parameters[index]->name, BinaryData::microphone_svg, juce::Colours::white, juce::Colours::red, effect.parameters[index]->sidechain);
        sidechainButton->setTooltip("When enabled, the volume of the input audio controls the value of the slider, acting like a sidechain effect.");
        addAndMakeVisible(*sidechainButton);
    }

    if (effect.linked != nullptr && index == 0) {
        linkButton.setTooltip("When enabled, parameters are linked and changes to one parameter will affect all parameters.");
        linkButton.onClick = [this]() {
            if (this->effect.linked != nullptr) {
                this->effect.linked->setBoolValueNotifyingHost(!this->effect.linked->getBoolValue());
            }
        };
        addAndMakeVisible(linkButton);
    }

    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, TEXT_BOX_WIDTH, slider.getTextBoxHeight());
    slider.setScrollWheelEnabled(false);
    if (effect.parameters[index]->step == 1.0) {
        slider.setNumDecimalPlacesToDisplay(0);
    } else {
        slider.setNumDecimalPlacesToDisplay(4);
    }

    lfoSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    lfoSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, TEXT_BOX_WIDTH, lfoSlider.getTextBoxHeight());
    lfoSlider.setTextValueSuffix("Hz");
    lfoSlider.setColour(sliderThumbOutlineColourId, juce::Colour(0xff00ff00));
    lfoSlider.setNumDecimalPlacesToDisplay(3);
    lfoSlider.setScrollWheelEnabled(false);

    // Add this component as a listener for both sliders to handle gesture notifications
    slider.addListener(this);
    lfoSlider.addListener(this);

    label.setFont(juce::Font(14.0f));

    if (lfoEnabled) {
        lfo.addItem("Static", static_cast<int>(osci::LfoType::Static));
        lfo.addItem("Sine", static_cast<int>(osci::LfoType::Sine));
        lfo.addItem("Square", static_cast<int>(osci::LfoType::Square));
        lfo.addItem("Seesaw", static_cast<int>(osci::LfoType::Seesaw));
        lfo.addItem("Triangle", static_cast<int>(osci::LfoType::Triangle));
        lfo.addItem("Sawtooth", static_cast<int>(osci::LfoType::Sawtooth));
        lfo.addItem("Reverse Sawtooth", static_cast<int>(osci::LfoType::ReverseSawtooth));
        lfo.addItem("Noise", static_cast<int>(osci::LfoType::Noise));
    }

    settingsButton.setTooltip("Click to change the slider settings, including range.");

    settingsButton.onClick = [this] {
        auto settings = std::make_unique<EffectSettingsComponent>(this);
        settings->setLookAndFeel(&getLookAndFeel());
        // Smaller popup in advanced mode (no LFO start/end sliders)
        int height = (this->effect.parameters[this->index]->lfoStartPercent != nullptr) ? 290 : 170;
        settings->setSize(200, height);
        auto& myBox = juce::CallOutBox::launchAsynchronously(std::move(settings), settingsButton.getScreenBounds(), nullptr);
    };

    effect.addListener(index, this);
    setupComponent();
}

EffectComponent::EffectComponent(osci::Effect& effect) : EffectComponent(effect, 0) {}

void EffectComponent::setSliderValueIfChanged(osci::FloatParameter* parameter, juce::Slider& slider) {
    juce::String newSliderValue = juce::String(parameter->getValueUnnormalised(), 3);
    juce::String oldSliderValue = juce::String((float)slider.getValue(), 3);

    // only set the slider value if the parameter value is different so that we prefer the more
    // precise slider value.
    if (newSliderValue != oldSliderValue) {
        slider.setValue(parameter->getValueUnnormalised(), juce::dontSendNotification);
    }
}

void EffectComponent::setupComponent() {
    osci::EffectParameter* parameter = effect.parameters[index];

    setEnabled(effect.enabled == nullptr || effect.enabled->getBoolValue());

    if (effect.linked != nullptr && index == 0) {
        linkButton.setToggleState(effect.linked->getBoolValue(), juce::dontSendNotification);

        if (effect.linked->getBoolValue()) {
            for (int i = 1; i < effect.parameters.size(); i++) {
                effect.setValue(i, effect.parameters[0]->getValueUnnormalised());
            }
        }
    }

    if (updateToggleState != nullptr) {
        updateToggleState();
    }

    setTooltip(parameter->description);
    label.setText(parameter->name, juce::dontSendNotification);
    label.setInterceptsMouseClicks(false, false);

    slider.setRange(parameter->min, parameter->max, parameter->step);
    setSliderValueIfChanged(parameter, slider);
    slider.setDoubleClickReturnValue(true, parameter->defaultValue);

    // Set the new slider value change handler
    slider.onValueChange = [this] {
        // Update the effect parameter with this slider's value
        effect.setValue(index, slider.getValue());

        if (effect.linked != nullptr && effect.linked->getBoolValue()) {
            for (int i = 0; i < effect.parameters.size(); i++) {
                if (i != index) {
                    effect.setValue(i, slider.getValue());
                }
            }
        }
    };

    lfoEnabled = parameter->lfo != nullptr && parameter->lfoRate != nullptr;
    if (lfoEnabled) {
        lfo.setSelectedId(parameter->lfo->getValueUnnormalised(), juce::dontSendNotification);

        lfo.onChange = [this]() {
            if (lfo.getSelectedId() != 0) {
                effect.parameters[index]->lfo->setUnnormalisedValueNotifyingHost(lfo.getSelectedId());

                if (lfo.getSelectedId() == static_cast<int>(osci::LfoType::Static)) {
                    lfoSlider.setVisible(false);
                    slider.setVisible(true);
                } else {
                    lfoSlider.setVisible(true);
                    slider.setVisible(false);
                }
            }
        };

        lfoSlider.setRange(parameter->lfoRate->min, parameter->lfoRate->max, parameter->lfoRate->step);
        setSliderValueIfChanged(parameter->lfoRate, lfoSlider);
        lfoSlider.setValue(parameter->lfoRate->getValueUnnormalised(), juce::dontSendNotification);
        lfoSlider.setSkewFactorFromMidPoint(parameter->lfoRate->min + 0.1 * (parameter->lfoRate->max - parameter->lfoRate->min));
        lfoSlider.setDoubleClickReturnValue(true, 1.0);

        if (lfo.getSelectedId() == static_cast<int>(osci::LfoType::Static)) {
            lfoSlider.setVisible(false);
            slider.setVisible(true);
        } else {
            lfoSlider.setVisible(true);
            slider.setVisible(false);
        }

        lfoSlider.onValueChange = [this]() {
            effect.parameters[index]->lfoRate->setUnnormalisedValueNotifyingHost(lfoSlider.getValue());
        };
    }

    if (sidechainEnabled) {
        sidechainButton->onClick = [this] {
            effect.parameters[index]->sidechain->setBoolValueNotifyingHost(!effect.parameters[index]->sidechain->getBoolValue());
        };
    }

    if (sidechainEnabled && effect.parameters[index]->sidechain->getBoolValue()) {
        slider.setEnabled(false);
        slider.setColour(sliderThumbOutlineColourId, juce::Colour(0xffff0000));
        slider.setTooltip("Sidechain effect applied - click the microphone icon to disable this.");
    } else {
        slider.setEnabled(true);
        slider.setColour(sliderThumbOutlineColourId, findColour(sliderThumbOutlineColourId));
        slider.setTooltip("");
    }
}

EffectComponent::~EffectComponent() {
    if (modBroadcaster)
        modBroadcaster->removeListener(this);
    slider.removeListener(this);
    lfoSlider.removeListener(this);
    effect.removeListener(index, this);
}

void EffectComponent::resized() {
    auto bounds = getLocalBounds();
    auto componentBounds = bounds.removeFromRight(25);

    if (effect.linked != nullptr) {
        linkButton.setBounds(componentBounds);
    } else if (component != nullptr) {
        component->setBounds(componentBounds);
    }

    if (sidechainEnabled) {
        sidechainButton->setBounds(bounds.removeFromRight(20));
    }

    if (settingsButton.isVisible()) {
        settingsButton.setBounds(bounds.removeFromRight(20));
    }

    bool drawingSmall = bounds.getWidth() < 3.5 * TEXT_WIDTH;

    if (lfoEnabled) {
        lfo.setBounds(bounds.removeFromRight(drawingSmall ? 70 : 100).reduced(0, 5));
    }

    bounds.removeFromRight(2);

    bounds.removeFromLeft(5);

    label.setBounds(bounds.removeFromLeft(drawingSmall ? SMALL_TEXT_WIDTH : TEXT_WIDTH));
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, drawingSmall ? SMALL_TEXT_BOX_WIDTH : TEXT_BOX_WIDTH, slider.getTextBoxHeight());
    lfoSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, drawingSmall ? SMALL_TEXT_BOX_WIDTH : TEXT_BOX_WIDTH, lfoSlider.getTextBoxHeight());

    slider.setBounds(bounds);
    lfoSlider.setBounds(bounds);
}

void EffectComponent::paint(juce::Graphics& g) {
    g.setColour(findColour(effectComponentBackgroundColourId, true));
    g.fillRect(getLocalBounds());
}

void EffectComponent::paintOverChildren(juce::Graphics& g) {
    if (modDropHighlight) {
        auto bounds = getLocalBounds().toFloat().reduced(1.0f);
        g.setColour(juce::Colour(0xFF00FF00).withAlpha(0.18f));
        g.fillRoundedRectangle(bounds, 4.0f);
        g.setColour(juce::Colour(0xFF00FF00).withAlpha(0.8f));
        g.drawRoundedRectangle(bounds, 4.0f, 2.0f);
    } else if (modAnyDragActive.load(std::memory_order_relaxed)) {
        auto bounds = getLocalBounds().toFloat().reduced(1.0f);
        g.setColour(juce::Colour(0xFF00FF00).withAlpha(0.06f));
        g.fillRoundedRectangle(bounds, 4.0f);
        g.setColour(juce::Colour(0xFF00FF00).withAlpha(0.25f));
        g.drawRoundedRectangle(bounds, 4.0f, 1.0f);
    } else if (highlightedParamId.isNotEmpty() && effect.parameters[index]->paramID == highlightedParamId) {
        auto colour = juce::Colour(0xFF00FF00);

        // Draw a highlight border around this component to show it's the modulation target.
        {
            auto highlightBounds = getLocalBounds().toFloat().reduced(1.0f);
            g.setColour(colour.withAlpha(0.18f));
            g.fillRoundedRectangle(highlightBounds, 4.0f);
            g.setColour(colour.withAlpha(0.8f));
            g.drawRoundedRectangle(highlightBounds, 4.0f, 2.0f);
        }

        // Draw the modulation range on the slider track.
        if (modRangeParamId == highlightedParamId && modRangeDepth.load() > 0.0f) {
            auto currentVal = slider.getValue();
            auto sliderMin = slider.getMinimum();
            auto sliderMax = slider.getMaximum();
            auto range = sliderMax - sliderMin;

            double rangeStart, rangeEnd;
            if (modRangeBipolar.load()) {
                double halfSpan = modRangeDepth.load() * range * 0.5;
                rangeStart = currentVal - halfSpan;
                rangeEnd = currentVal + halfSpan;
            } else {
                rangeStart = currentVal;
                rangeEnd = currentVal + modRangeDepth.load() * range;
            }

            // Clamp to slider range
            rangeStart = juce::jlimit(sliderMin, sliderMax, rangeStart);
            rangeEnd = juce::jlimit(sliderMin, sliderMax, rangeEnd);

            // Convert to pixel positions using slider's coordinate mapping
            auto startPx = (float)slider.getPositionOfValue(rangeStart);
            auto endPx = (float)slider.getPositionOfValue(rangeEnd);

            if (startPx > endPx)
                std::swap(startPx, endPx);

            // Get slider bounds in our coordinate space
            auto sliderBounds = slider.getBounds().toFloat();
            auto trackY = sliderBounds.getCentreY() - 3.0f;
            auto trackHeight = 6.0f;

            g.setColour(colour.withAlpha(0.35f));
            g.fillRoundedRectangle(startPx + sliderBounds.getX(), trackY, endPx - startPx, trackHeight, 2.0f);
        }
    }
}

void EffectComponent::parameterValueChanged(int parameterIndex, float newValue) {
    triggerAsyncUpdate();
}

void EffectComponent::parameterGestureChanged(int parameterIndex, bool gestureIsStarting) {}

// Slider::Listener callbacks for MIDI learn support
void EffectComponent::sliderValueChanged(juce::Slider* sliderThatChanged) {
    // Value changes are handled by the slider's onValueChange lambda
    // This is intentionally empty as the lambda provides more flexibility
}

void EffectComponent::sliderDragStarted(juce::Slider* sliderThatChanged) {
    // Notify the host that a gesture (parameter change) is starting
    // This is critical for MIDI learn functionality in DAWs like Logic and Ableton
    if (sliderThatChanged == &slider) {
        beginGestureIfPossible (effect.parameters[index]);
        
        // Also begin gesture for linked parameters
        if (effect.linked != nullptr && effect.linked->getBoolValue()) {
            for (int i = 0; i < effect.parameters.size(); i++) {
                if (i != index) {
                    beginGestureIfPossible (effect.parameters[i]);
                }
            }
        }
    } else if (sliderThatChanged == &lfoSlider && effect.parameters[index]->lfoRate != nullptr) {
        beginGestureIfPossible (effect.parameters[index]->lfoRate);
    }
}

void EffectComponent::sliderDragEnded(juce::Slider* sliderThatChanged) {
    // Notify the host that a gesture (parameter change) has ended
    // This is critical for MIDI learn functionality in DAWs like Logic and Ableton
    if (sliderThatChanged == &slider) {
        endGestureIfPossible (effect.parameters[index]);
        
        // Also end gesture for linked parameters
        if (effect.linked != nullptr && effect.linked->getBoolValue()) {
            for (int i = 0; i < effect.parameters.size(); i++) {
                if (i != index) {
                    endGestureIfPossible (effect.parameters[i]);
                }
            }
        }
    } else if (sliderThatChanged == &lfoSlider && effect.parameters[index]->lfoRate != nullptr) {
        endGestureIfPossible (effect.parameters[index]->lfoRate);
    }
}

void EffectComponent::handleAsyncUpdate() {
    setupComponent();
    if (auto* parent = getParentComponent()) {
        parent->repaint();
    }
}

void EffectComponent::updateLfoModulation() {
    juce::String paramId = effect.parameters[index]->paramID;
    auto& props = slider.getProperties();
    bool needsRepaint = false;

    // LFO modulation
    if (queryLfoModulation) {
        auto info = queryLfoModulation(paramId, slider);
        if (info.active) {
            props.set("lfo_active", true);
            props.set("lfo_mod_pos", info.modulatedPos);
            props.set("lfo_colour", (juce::int64)info.colour.getARGB());
            needsRepaint = true;
        } else if ((bool)props.getWithDefault("lfo_active", false)) {
            props.set("lfo_active", false);
            needsRepaint = true;
        }
    }

    // Envelope modulation
    if (queryEnvModulation) {
        auto info = queryEnvModulation(paramId, slider);
        if (info.active) {
            props.set("env_active", true);
            props.set("env_mod_pos", info.modulatedPos);
            props.set("env_colour", (juce::int64)info.colour.getARGB());
            needsRepaint = true;
        } else if ((bool)props.getWithDefault("env_active", false)) {
            props.set("env_active", false);
            needsRepaint = true;
        }
    }

    if (needsRepaint)
        slider.repaint();
}

void EffectComponent::setRangeEnabled(bool enabled) {
    settingsButton.setVisible(enabled);
}

EffectComponent::LfoModInfo EffectComponent::computeLfoModulation(
        OscirenderAudioProcessor& processor,
        const juce::String& paramId,
        juce::Slider& sl) {
    auto h = ModulationHelper::compute(
        processor.getLfoAssignments(), paramId, sl,
        [&](int i) { return processor.getLfoCurrentValue(i); },
        &LfoComponent::getLfoColour);
    return { h.active, h.modulatedPos, h.colour };
}

EffectComponent::LfoModInfo EffectComponent::computeEnvModulation(
        OscirenderAudioProcessor& processor,
        const juce::String& paramId,
        juce::Slider& sl) {
    auto h = ModulationHelper::compute(
        processor.getEnvAssignments(), paramId, sl,
        [&](int i) { return processor.getEnvCurrentValue(i); },
        &EnvelopeComponent::getEnvColour);
    return { h.active, h.modulatedPos, h.colour };
}

void EffectComponent::setComponent(std::shared_ptr<juce::Component> component) {
    this->component = component;
    addAndMakeVisible(component.get());
}

// === DragAndDropTarget for LFO assignment ===

bool EffectComponent::isInterestedInDragSource(const SourceDetails& dragSourceDetails) {
    auto desc = dragSourceDetails.description.toString();
    return desc.startsWith("LFO:") || desc.startsWith("ENV:");
}

void EffectComponent::itemDragEnter(const SourceDetails&) {
    modDropHighlight = true;
    repaint();
}

void EffectComponent::itemDragExit(const SourceDetails&) {
    modDropHighlight = false;
    repaint();
}

void EffectComponent::itemDropped(const SourceDetails& dragSourceDetails) {
    modDropHighlight = false;
    modAnyDragActive.store(false, std::memory_order_relaxed);
    repaint();

    juce::String desc = dragSourceDetails.description.toString();
    juce::String paramId = effect.parameters[index]->paramID;
    if (desc.startsWith("LFO:")) {
        int lfoIndex = desc.fromFirstOccurrenceOf("LFO:", false, false).getIntValue();
        if (onLfoDropped)
            onLfoDropped(lfoIndex, paramId);
    } else if (desc.startsWith("ENV:")) {
        int envIndex = desc.fromFirstOccurrenceOf("ENV:", false, false).getIntValue();
        if (onEnvDropped)
            onEnvDropped(envIndex, paramId);
    }
}

void EffectComponent::wireModulation(OscirenderAudioProcessor& processor) {
    onLfoDropped = [&processor](int lfoIndex, const juce::String& paramId) {
        LfoAssignment assignment;
        assignment.sourceIndex = lfoIndex;
        assignment.paramId = paramId;
        assignment.depth = 0.5f;
        processor.addLfoAssignment(assignment);
    };
    queryLfoModulation = [&processor](const juce::String& paramId, juce::Slider& sl) -> LfoModInfo {
        return computeLfoModulation(processor, paramId, sl);
    };
    onEnvDropped = [&processor](int envIndex, const juce::String& paramId) {
        EnvAssignment assignment;
        assignment.sourceIndex = envIndex;
        assignment.paramId = paramId;
        assignment.depth = 0.5f;
        processor.addEnvAssignment(assignment);
    };
    queryEnvModulation = [&processor](const juce::String& paramId, juce::Slider& sl) -> LfoModInfo {
        return computeEnvModulation(processor, paramId, sl);
    };
    modBroadcaster = &processor.modulationUpdateBroadcaster;
    modBroadcaster->addListener(this, [this]() {
        updateLfoModulation();

        // Disable the per-parameter LFO dropdown when a global modulation
        // source (LFO panel or envelope) is wired to this parameter.
        if (lfoEnabled) {
            bool hasGlobalMod = (bool)slider.getProperties().getWithDefault("lfo_active", false)
                             || (bool)slider.getProperties().getWithDefault("env_active", false);
            lfo.setEnabled(!hasGlobalMod);
        }

        if (modDropHighlight
            || modAnyDragActive.load(std::memory_order_relaxed)
            || (highlightedParamId.isNotEmpty() && effect.parameters[index]->paramID == highlightedParamId))
            repaint();
    });
}
