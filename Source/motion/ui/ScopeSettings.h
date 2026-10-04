#pragma once

#include "MotionStyle.h"

#include "../MotionProcessor.h"
#include "ScrubField.h"

// The Scope popover's layout: two columns of sections, each a heading over
// caption and field rows.
namespace motion::scope {
inline constexpr int captionWidth = 84, fieldWidth = 116, columnGap = 20, row = 28, rowGap = 6, heading = 26;
inline constexpr int columnWidth = captionWidth + fieldWidth;
inline void sectionTitle(juce::Label& label, const juce::String& text) {
    label.setText(text, juce::dontSendNotification);
    label.setFont(motion::style::title());
    label.setColour(juce::Label::textColourId, motion::style::text());
    label.setBorderSize({});
    label.setJustificationType(juce::Justification::centredLeft);
}
inline void formRow(juce::Rectangle<int>& area, juce::Label& caption, juce::Component& field) {
    auto line = area.removeFromTop(row);
    caption.setBounds(line.removeFromLeft(captionWidth));
    field.setBounds(line.removeFromLeft(fieldWidth));
    area.removeFromTop(rowGap);
}
}

// Scope timing calibration for the project: dwell, travel and settle, with
// presets for common displays. Every edit is one undo step; drags preview
// live and commit once when released.
class MotionScopeProfilePanel final : public juce::Component, private juce::ChangeListener {
public:
    explicit MotionScopeProfilePanel(MotionProcessor& owner) : processor(owner) {
        setName("Scope timing");
        motion::scope::sectionTitle(title, "Timing");
        addAndMakeVisible(title);
        presets.setName("Scope presets");
        presets.setTitle("Scope presets");
        presets.setButtonText("Presets");
        presets.setTooltip("Apply timing suited to a kind of display");
        presets.onClick = [this] { showPresets(); };
        addAndMakeVisible(presets);
        for (std::size_t index = 0; index < fields.size(); ++index) {
            auto& field = fields[index];
            const auto& spec = specs[index];
            const auto label = juce::String(spec.label.data(), spec.label.size());
            field.caption.setText(label, juce::dontSendNotification);
            motion::style::dialog::caption(field.caption);
            field.editor.setSpec(spec);
            field.editor.setName("Scope " + label.toLowerCase());
            field.editor.setTitle("Scope " + label.toLowerCase());
            field.editor.setComponentID("motion.scope." + juce::String(spec.id.data(), spec.id.size()));
            const auto lower = label.toLowerCase();
            field.editor.setTooltip(lower.contains("dwell") ? "How long the beam stays dark at both ends of every jump"
                : lower.contains("travel") ? "Dark move time per unit of screen distance"
                : lower.contains("settle") ? "Wait after each jump before drawing, for slow scopes and galvos" : label);
            field.editor.onBegin = [this] { beginGesture(); };
            field.editor.onChange = [this, index](double value) { previewValue(index, value); };
            field.editor.onEnd = [this] { endGesture(); };
            field.editor.onCancel = [this] { refresh(); };
            field.editor.onCommit = [this, index](double value) { commitValue(index, value); };
            addAndMakeVisible(field.caption);
            addAndMakeVisible(field.editor);
        }
        processor.document.addChangeListener(this);
        refresh();
    }
    ~MotionScopeProfilePanel() override { processor.document.removeChangeListener(this); }

    static int preferredHeight() { return motion::scope::heading + static_cast<int>(specs.size()) * (motion::scope::row + motion::scope::rowGap) - motion::scope::rowGap; }

    void refresh() {
        if (gesture.has_value() && processor.document.revision() != gesture->revision) { gesture.reset(); }
        const auto& scope = processor.document.project().scope;
        fields[0].editor.setValue(scope.dwellMicros);
        fields[1].editor.setValue(scope.travelMicrosPerUnit);
        fields[2].editor.setValue(scope.settleMicros);
    }

    void resized() override {
        auto area = getLocalBounds();
        auto header = area.removeFromTop(motion::scope::heading);
        // Presets ends where the fields do.
        presets.setBounds(motion::scope::columnWidth - 72, header.getCentreY() - 11, 72, 22);
        title.setBounds(header.withWidth(motion::scope::captionWidth));
        for (auto& field : fields) { motion::scope::formRow(area, field.caption, field.editor); }
    }

private:
    static constexpr std::array<motion::PropertySpec, 3> specs {{
        {"dwell", "Dwell", "Scope", "", 0, motion::ScopeProfile::maximumDwellMicros, 12, 1, 1, " µs"},
        {"travel", "Travel", "Scope", "", 0, motion::ScopeProfile::maximumTravelMicrosPerUnit, 30, 1, 1, " µs/unit"},
        {"settle", "Settle", "Scope", "", 0, motion::ScopeProfile::maximumSettleMicros, 0, 1, 1, " µs"},
    }};
    struct Field {
        juce::Label caption;
        MotionScrubField editor;
    };
    struct Gesture {
        motion::Project before;
        std::uint64_t revision;
    };

    void changeListenerCallback(juce::ChangeBroadcaster*) override { refresh(); }
    static void apply(motion::ScopeProfile& scope, std::size_t index, double value) {
        const auto clamped = specs[index].clamp(value);
        if (index == 0) {
            scope.dwellMicros = clamped;
        } else if (index == 1) {
            scope.travelMicrosPerUnit = clamped;
        } else {
            scope.settleMicros = clamped;
        }
    }
    void beginGesture() {
        gesture = Gesture{processor.document.project(), processor.document.revision()};
        changed = false;
    }
    void previewValue(std::size_t index, double value) {
        if (!gesture.has_value()) { return; }
        auto updated = gesture->before;
        apply(updated.scope, index, value);
        processor.document.preview(std::move(updated));
        gesture->revision = processor.document.revision();
        changed = true;
    }
    void endGesture() {
        if (gesture.has_value() && changed && processor.document.revision() == gesture->revision) {
            processor.document.commit("Change scope timing", std::move(gesture->before));
        }
        gesture.reset();
        changed = false;
        refresh();
    }
    void commitValue(std::size_t index, double value) {
        auto scope = processor.document.project().scope;
        apply(scope, index, value);
        if (scope == processor.document.project().scope) { refresh(); return; }
        const auto control = "scope." + juce::String(specs[index].id.data(), specs[index].id.size());
        processor.document.editCoalesced("Change scope timing", control, [scope](motion::Project& project) { project.scope = scope; });
    }
    void showPresets() {
        juce::PopupMenu menu;
        const auto& current = processor.document.project().scope;
        for (std::size_t index = 0; index < motion::scopeProfilePresets.size(); ++index) {
            const auto& preset = motion::scopeProfilePresets[index];
            menu.addItem(static_cast<int>(index + 1), juce::String(preset.name.data(), preset.name.size()), true, preset.profile == current);
        }
        juce::Component::SafePointer<MotionScopeProfilePanel> safe(this);
        menu.setLookAndFeel(&getLookAndFeel());
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&presets), [safe](int result) {
            if (safe == nullptr || result <= 0 || result > static_cast<int>(motion::scopeProfilePresets.size())) { return; }
            safe->applyPreset(motion::scopeProfilePresets[static_cast<std::size_t>(result - 1)]);
        });
    }
    void applyPreset(const motion::ScopeProfilePreset& preset) {
        if (preset.profile == processor.document.project().scope) { return; }
        const auto profile = preset.profile;
        processor.document.edit("Apply " + juce::String(preset.name.data(), preset.name.size()) + " scope timing", [profile](motion::Project& project) { project.scope = profile; });
    }

    MotionProcessor& processor;
    juce::Label title;
    juce::TextButton presets;
    std::array<Field, 3> fields;
    std::optional<Gesture> gesture;
    bool changed = false;
};

// The Scope's settings in one popover: the beam and display settings Motion
// uses, edited live (each drag or typed value one undo step), and the
// project's scope timing. osci-render's per-parameter LFOs and microphone
// input have no place here; Motion's modulators drive the picture instead.
class MotionScopeSettings final : public juce::Component, private juce::AudioProcessorParameter::Listener, private juce::AsyncUpdater {
public:
    explicit MotionScopeSettings(MotionProcessor& owner) : processor(owner), timing(owner) {
        setName("Scope settings");
        setTitle("Scope settings");
        auto& parameters = processor.visualiserParameters;
        motion::scope::sectionTitle(beamTitle, "Beam");
        motion::scope::sectionTitle(displayTitle, "Display");
        motion::scope::sectionTitle(qualityTitle, "Quality");
        for (auto* label : {&beamTitle, &displayTitle, &qualityTitle}) { addAndMakeVisible(label); }
        bind(intensity, *parameters.intensityEffect, "Intensity", 2);
        bind(focus, *parameters.focusEffect, "Focus", 2);
        bind(persistence, *parameters.persistenceEffect, "Persistence", 2);
        bind(afterglow, *parameters.afterglowEffect, "Afterglow", 2);
        bind(glow, *parameters.glowEffect, "Glow", 2);
        bind(hue, *parameters.hueEffect, "Hue", 0);
        bind(saturation, *parameters.lineSaturationEffect, "Saturation", 2);
        bind(ambient, *parameters.ambientEffect, "Ambient", 2);
        bind(smoothing, *parameters.smoothEffect, "Smoothing", 2);
        overlayCaption.setText("Overlay", juce::dontSendNotification);
        motion::style::dialog::caption(overlayCaption);
        addAndMakeVisible(overlayCaption);
        overlay.setName("Scope overlay");
        overlay.setTitle("Scope overlay");
        overlay.setTooltip("Graticule or screen drawn over the beam");
        auto* screen = parameters.screenOverlay;
        for (int id = 1; id <= screen->max.load(); ++id) { overlay.addItem(screen->getText(screen->getNormalisedValue(static_cast<float>(id))), id); }
        overlay.onChange = [this] {
            processor.lastUndoParamId = {};
            processor.visualiserParameters.screenOverlay->setUnnormalisedValueNotifyingHost(static_cast<float>(overlay.getSelectedId()));
        };
        addAndMakeVisible(overlay);
        upsampleCaption.setText("Upsample", juce::dontSendNotification);
        motion::style::dialog::caption(upsampleCaption);
        addAndMakeVisible(upsampleCaption);
        upsample.setName("Upsample Audio");
        upsample.setTitle("Upsample Audio");
        upsample.setTooltip(parameters.upsamplingEnabled->getDescription());
        upsample.onClick = [this] {
            processor.lastUndoParamId = {};
            processor.visualiserParameters.upsamplingEnabled->setBoolValueNotifyingHost(upsample.getToggleState());
        };
        addAndMakeVisible(upsample);
        addAndMakeVisible(timing);
        for (auto* parameter : std::initializer_list<juce::AudioProcessorParameter*> {screen, parameters.upsamplingEnabled}) { parameter->addListener(this); }
        refresh();
        const auto rows = [](int count) { return count * (motion::scope::row + motion::scope::rowGap) - motion::scope::rowGap; };
        // Left: Beam (5) and Quality (2); right: Display (4) and Timing (3).
        const auto height = motion::scope::heading * 2 + rows(5) + rows(2) + sectionGap;
        setSize(motion::style::dialog::margin * 2 + motion::scope::columnWidth * 2 + motion::scope::columnGap, motion::style::dialog::margin * 2 + height);
    }
    ~MotionScopeSettings() override {
        auto& parameters = processor.visualiserParameters;
        for (auto* field : fields()) { field->parameter->removeListener(this); }
        parameters.screenOverlay->removeListener(this);
        parameters.upsamplingEnabled->removeListener(this);
        cancelPendingUpdate();
    }

    void resized() override {
        auto area = getLocalBounds().reduced(motion::style::dialog::margin);
        auto left = area.removeFromLeft(motion::scope::columnWidth);
        area.removeFromLeft(motion::scope::columnGap);
        auto right = area;
        beamTitle.setBounds(left.removeFromTop(motion::scope::heading));
        for (auto* field : {&intensity, &focus, &persistence, &afterglow, &glow}) { motion::scope::formRow(left, field->caption, field->editor); }
        left.removeFromTop(sectionGap - motion::scope::rowGap);
        qualityTitle.setBounds(left.removeFromTop(motion::scope::heading));
        motion::scope::formRow(left, smoothing.caption, smoothing.editor);
        auto line = left.removeFromTop(motion::scope::row);
        upsampleCaption.setBounds(line.removeFromLeft(motion::scope::captionWidth));
        upsample.setBounds(line.removeFromLeft(motion::scope::row));
        displayTitle.setBounds(right.removeFromTop(motion::scope::heading));
        motion::scope::formRow(right, overlayCaption, overlay);
        for (auto* field : {&hue, &saturation, &ambient}) { motion::scope::formRow(right, field->caption, field->editor); }
        right.removeFromTop(sectionGap - motion::scope::rowGap);
        timing.setBounds(right.removeFromTop(MotionScopeProfilePanel::preferredHeight()));
    }

private:
    static constexpr int sectionGap = 14;
    struct Field {
        juce::Label caption;
        MotionScrubField editor;
        osci::EffectParameter* parameter = nullptr;
        std::string id;
    };
    std::array<Field*, 9> fields() { return {&intensity, &focus, &persistence, &afterglow, &glow, &hue, &saturation, &ambient, &smoothing}; }

    void bind(Field& field, osci::Effect& effect, const juce::String& label, int decimals) {
        auto* parameter = effect.parameters[0];
        field.parameter = parameter;
        field.id = parameter->paramID.toStdString();
        const auto low = static_cast<double>(parameter->min.load());
        const auto high = static_cast<double>(parameter->max.load());
        // About 300 px of drag covers the range.
        field.editor.setSpec({field.id, {}, "Scope", "", low, high, static_cast<double>(parameter->defaultValue.load()), (high - low) / 300.0, decimals, ""});
        field.editor.setName("Scope " + label.toLowerCase());
        field.editor.setTitle("Scope " + label.toLowerCase());
        field.editor.setTooltip(parameter->description);
        field.caption.setText(label, juce::dontSendNotification);
        motion::style::dialog::caption(field.caption);
        // Each drag or typed value is its own undo step.
        field.editor.onBegin = [this, parameter] {
            processor.lastUndoParamId = {};
            parameter->beginChangeGesture();
        };
        field.editor.onChange = [parameter](double value) { parameter->setUnnormalisedValueNotifyingHost(static_cast<float>(value)); };
        field.editor.onEnd = [parameter] { parameter->endChangeGesture(); };
        field.editor.onCommit = [this, parameter](double value) {
            processor.lastUndoParamId = {};
            parameter->setUnnormalisedValueNotifyingHost(static_cast<float>(value));
        };
        field.editor.onCancel = [this] { refresh(); };
        parameter->addListener(this);
        addAndMakeVisible(field.caption);
        addAndMakeVisible(field.editor);
    }
    void refresh() {
        for (auto* field : fields()) { field->editor.setValue(field->parameter->getValueUnnormalised()); }
        auto& parameters = processor.visualiserParameters;
        // The realistic screens need a square canvas.
        const auto square = VisualiserGeometry::isSquare(processor.recordingParameters.getCanvasSize());
        overlay.setItemEnabled(static_cast<int>(ScreenOverlay::Real), square);
        overlay.setItemEnabled(static_cast<int>(ScreenOverlay::VectorDisplay), square);
        overlay.setSelectedId(static_cast<int>(parameters.screenOverlay->getValueUnnormalised()), juce::dontSendNotification);
        upsample.setToggleState(parameters.upsamplingEnabled->getBoolValue(), juce::dontSendNotification);
    }
    void parameterValueChanged(int, float) override { triggerAsyncUpdate(); }
    void parameterGestureChanged(int, bool) override {}
    void handleAsyncUpdate() override { refresh(); }

    MotionProcessor& processor;
    juce::Label beamTitle, displayTitle, qualityTitle, overlayCaption, upsampleCaption;
    Field intensity, focus, persistence, afterglow, glow, hue, saturation, ambient, smoothing;
    juce::ComboBox overlay;
    juce::ToggleButton upsample;
    MotionScopeProfilePanel timing;
};
