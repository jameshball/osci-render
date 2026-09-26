#pragma once

#include "../MotionProcessor.h"
#include "../model/PropertyTarget.h"

// Contextual controls for the selected property's procedural motion. Keyframes
// continue to author the base value; modulation is evaluated afterward.
class MotionModulationPanel : public juce::Component {
public:
    explicit MotionModulationPanel(MotionProcessor& owner) : processor(owner) {
        setName("Property modulation");
        setWantsKeyboardFocus(true);
        enabled.setButtonText("Enabled");
        enabled.setName("Enable modulation");
        enabled.setTitle("Enable modulation");
        enabled.setColour(juce::TextButton::buttonColourId, osci::Colours::dark());
        waveform.setName("Modulation waveform");
        waveform.addItemList({ "Sine", "Triangle", "Saw", "Square", "Smooth random", "Random steps" }, 1);
        sync.setName("Modulation clock");
        sync.addItemList({ "Hz", "Beats" }, 1);
        mode.setName("Modulation mode");
        mode.addItemList({ "Add", "Multiply" }, 1);
        prepareSlider(rate, "Modulation rate", 0.001, 1000, 0.001);
        rate.setSkewFactorFromMidPoint(1);
        prepareSlider(amount, "Modulation amount", -10, 10, 0.001);
        prepareSlider(phase, "Modulation phase", 0, 360, 0.1);
        rateLabel.setText("Rate", juce::dontSendNotification);
        amountLabel.setText("Amount", juce::dontSendNotification);
        phaseLabel.setText("Phase", juce::dontSendNotification);
        seed.setEditable(false, true);
        seed.setName("Modulation seed");
        seed.setTooltip("A fixed seed keeps random motion identical during playback, scrubbing and export");
        seed.setColour(juce::Label::backgroundColourId, osci::Colours::veryDark());
        seed.setJustificationType(juce::Justification::centred);
        newSeed.setButtonText("New seed");
        enabled.onClick = [this] { edit([this](auto& value) { value.enabled = enabled.getToggleState(); }); };
        waveform.onChange = [this] { edit([this](auto& value) { value.waveform = static_cast<motion::ModulationWaveform>(waveform.getSelectedId() - 1); }); };
        sync.onChange = [this] { edit([this](auto& value) { value.tempoSync = sync.getSelectedId() == 2; }); };
        mode.onChange = [this] { edit([this](auto& value) { value.mode = static_cast<motion::ModulationMode>(mode.getSelectedId() - 1); }); };
        rate.onValueChange = [this] { edit([this](auto& value) { if (value.tempoSync) { value.beatsPerCycle = rate.getValue(); } else { value.rateHz = rate.getValue(); } }, true); };
        amount.onValueChange = [this] { edit([this](auto& value) { value.amount = amount.getValue(); }, true); };
        phase.onValueChange = [this] { edit([this](auto& value) { value.phase = phase.getValue() / 360.0; }, true); };
        newSeed.onClick = [this] { edit([](auto& value) { ++value.seed; }); };
        seed.onTextChange = [this] {
            if (updating) { return; }
            const auto text = seed.getText().trim();
            const auto value = text.getLargeIntValue();
            if (text.isEmpty() || text.containsAnyOf(".-+") || text.containsOnly("0123456789") == false || value < 0 || value > 0xffffffffLL) { refresh(); return; }
            edit([value](auto& settings) { settings.seed = static_cast<std::uint32_t>(value); });
        };
        for (auto* component : std::initializer_list<juce::Component*> { &enabled, &waveform, &sync, &mode, &rate, &amount, &phase, &rateLabel, &amountLabel, &phaseLabel, &seed, &newSeed }) {
            addAndMakeVisible(component);
        }
        refresh();
    }
    ~MotionModulationPanel() override { cancelGesture(); }

    void setTarget(motion::Id id, std::string property) {
        if (id != targetId || property != propertyName) {
            cancelGesture();
            targetId = id;
            propertyName = std::move(property);
        }
        refresh();
    }
    void refresh() {
        if (gesture.has_value() && processor.document.revision() != gesture->revision) { gesture.reset(); cancelled = true; }
        const auto* curve = motion::findPropertyCurve(processor.document.project(), targetId, propertyName);
        const auto settings = curve != nullptr ? curve->modulation : motion::Modulation();
        updating = true;
        enabled.setEnabled(curve != nullptr);
        enabled.setToggleState(settings.enabled, juce::dontSendNotification);
        waveform.setSelectedId(static_cast<int>(settings.waveform) + 1, juce::dontSendNotification);
        sync.setSelectedId(settings.tempoSync ? 2 : 1, juce::dontSendNotification);
        mode.setSelectedId(static_cast<int>(settings.mode) + 1, juce::dontSendNotification);
        for (auto* component : std::initializer_list<juce::Component*> { &waveform, &sync, &mode, &rate, &amount, &phase, &seed, &newSeed }) { component->setEnabled(curve != nullptr); }
        if (!gesture.has_value()) {
            rate.setRange(settings.tempoSync ? 0.0625 : 0.001, settings.tempoSync ? 64 : 1000, settings.tempoSync ? 0.0625 : 0.001);
            rate.setSkewFactorFromMidPoint(settings.tempoSync ? 4 : 1);
            rate.setValue(settings.tempoSync ? settings.beatsPerCycle : settings.rateHz, juce::dontSendNotification);
            double extent = propertyName.starts_with("rotation.") ? 360 : (propertyName == "fov" ? 90 : 10);
            if (propertyName == "red" || propertyName == "green" || propertyName == "blue" || propertyName == "strength") { extent = 1; }
            extent = std::max(extent, std::abs(settings.amount));
            amount.setRange(-extent, extent, 0.001);
            amount.setValue(settings.amount, juce::dontSendNotification);
            phase.setValue(settings.phase * 360.0, juce::dontSendNotification);
        }
        if (!seed.isBeingEdited()) { seed.setText(juce::String(settings.seed), juce::dontSendNotification); }
        const auto noise = settings.waveform == motion::ModulationWaveform::noiseHold || settings.waveform == motion::ModulationWaveform::noiseSmooth;
        seed.setVisible(noise);
        newSeed.setVisible(noise);
        rate.setTooltip(settings.tempoSync ? "Beats per cycle; follows project tempo" : "Cycles per second in the property's time domain");
        phase.setTooltip("Phase offset in degrees");
        amount.setTooltip(settings.mode == motion::ModulationMode::multiply ? "Multiply the keyed value by 1 + amount × waveform" : "Add amount × waveform to the keyed value");
        updating = false;
        repaint();
    }
    void resized() override {
        auto bounds = getLocalBounds().reduced(7, 3);
        auto header = bounds.removeFromTop(27);
        enabled.setBounds(header.removeFromRight(87));
        bounds.removeFromTop(3);
        auto shape = bounds.removeFromTop(25);
        mode.setBounds(shape.removeFromRight(88));
        shape.removeFromRight(3);
        waveform.setBounds(shape);
        bounds.removeFromTop(3);
        auto timing = bounds.removeFromTop(27);
        sync.setBounds(timing.removeFromRight(63));
        timing.removeFromRight(3);
        rateLabel.setBounds(timing.removeFromLeft(40));
        rate.setBounds(timing);
        bounds.removeFromTop(3);
        auto depth = bounds.removeFromTop(27);
        amountLabel.setBounds(depth.removeFromLeft(54));
        amount.setBounds(depth);
        bounds.removeFromTop(3);
        auto offset = bounds.removeFromTop(27);
        phaseLabel.setBounds(offset.removeFromLeft(54));
        phase.setBounds(offset);
        bounds.removeFromTop(3);
        auto noise = bounds.removeFromTop(25);
        newSeed.setBounds(noise.removeFromRight(88));
        noise.removeFromRight(3);
        seed.setBounds(noise);
    }
    void paint(juce::Graphics& g) override {
        g.setColour(osci::Colours::dark());
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 5);
        osci::PanelHeader::paintBackground(g, getLocalBounds().removeFromTop(30).toFloat(), osci::Colours::veryDark());
        g.setColour(osci::Colours::text());
        g.setFont(14);
        g.drawText("Modulation", 10, 0, getWidth() - 107, 30, juce::Justification::centredLeft);
    }
    bool keyPressed(const juce::KeyPress& key) override {
        if (key == juce::KeyPress::escapeKey && gesture.has_value()) { cancelGesture(); cancelled = true; refresh(); return true; }
        return false;
    }
private:
    void prepareSlider(juce::Slider& slider, const juce::String& name, double minimum, double maximum, double step) {
        slider.setName(name);
        slider.setWantsKeyboardFocus(true);
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 62, 24);
        slider.setRange(minimum, maximum, step);
        slider.onDragStart = [this, &slider] {
            slider.grabKeyboardFocus();
            cancelGesture();
            cancelled = false;
            gesture = Gesture { processor.document.project(), processor.document.revision(), targetId, propertyName, false };
        };
        slider.onDragEnd = [this] {
            if (gesture.has_value()) {
                auto state = std::move(*gesture);
                gesture.reset();
                if (state.changed && state.revision == processor.document.revision()) { processor.document.commit("Change modulation", std::move(state.before)); }
            }
            cancelled = false;
            refresh();
        };
    }
    void edit(std::function<void(motion::Modulation&)> update, bool fromSlider = false) {
        if (updating || (cancelled && fromSlider)) { return; }
        if (gesture.has_value() && (gesture->target != targetId || gesture->property != propertyName)) { cancelGesture(); return; }
        const auto* curve = motion::findPropertyCurve(processor.document.project(), targetId, propertyName);
        if (curve == nullptr) { return; }
        auto settings = curve->modulation;
        update(settings);
        if (!settings.valid() || settings == curve->modulation) { return; }
        if (gesture.has_value() && gesture->revision != processor.document.revision()) { gesture.reset(); cancelled = true; return; }
        const auto id = targetId;
        const auto property = propertyName;
        const auto change = [id, property, settings](motion::Project& project) {
            auto* curve = motion::findPropertyCurve(project, id, property);
            if (curve != nullptr) { curve->modulation = settings; }
        };
        if (gesture.has_value()) {
            auto project = processor.document.project();
            change(project);
            processor.document.preview(std::move(project));
            gesture->revision = processor.document.revision();
            const auto* original = motion::findPropertyCurve(gesture->before, id, property);
            gesture->changed = original != nullptr && !(original->modulation == settings);
        } else {
            processor.document.edit("Change modulation", change);
        }
        refresh();
    }
    void cancelGesture() {
        if (!gesture.has_value()) { return; }
        auto state = std::move(*gesture);
        gesture.reset();
        cancelled = true;
        if (state.changed && state.revision == processor.document.revision()) { processor.document.preview(std::move(state.before)); }
    }
    struct Gesture { motion::Project before; std::uint64_t revision; motion::Id target; std::string property; bool changed; };
    MotionProcessor& processor;
    motion::Id targetId = 0;
    std::string propertyName;
    std::optional<Gesture> gesture;
    bool updating = false, cancelled = false;
    juce::ToggleButton enabled;
    juce::ComboBox waveform, sync, mode;
    juce::Slider rate, amount, phase;
    juce::Label rateLabel, amountLabel, phaseLabel, seed;
    juce::TextButton newSeed;
};
