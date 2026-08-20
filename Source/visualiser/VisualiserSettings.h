#pragma once

#include <JuceHeader.h>

#include "../LookAndFeel.h"
#include "../components/effects/EffectComponent.h"
#include <osci_gui/osci_gui.h>
#include <osci_gui/visualiser/osci_VisualiserParameters.h>

#ifndef SOSCI
class OscirenderAudioProcessor;
#endif
class RecordingParameters;

class SettingsSection : public juce::GroupComponent {
public:
    explicit SettingsSection(const juce::String& label) : juce::GroupComponent(label, label) {
        const auto background = osci::Colours::darker().overlaidWith(juce::Colours::transparentBlack.withAlpha(0.2f));
        setColour(osci::groupComponentBackgroundColourId, background);
        setColour(osci::effectComponentBackgroundColourId, juce::Colours::transparentBlack);
    }

    static constexpr int headerHeight = 35;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsSection)
};

class GroupedSettings : public SettingsSection {
public:
    GroupedSettings(std::vector<std::shared_ptr<EffectComponent>> effects, juce::String label)
        : SettingsSection(label), effects(std::move(effects)) {
        for (auto effect : this->effects) {
            addAndMakeVisible(effect.get());
        }
    }

#ifndef SOSCI
    void wireModulation(OscirenderAudioProcessor& processor) {
        for (auto& effect : effects)
            effect->wireModulation(processor);
    }
#endif

    void resized() override {
        auto area = getLocalBounds();
        area.removeFromTop(headerHeight);
        for (auto effect : effects) {
            effect->setBounds(area.removeFromTop(rowHeight));
        }
    }

    int getPreferredHeight() const {
        return headerHeight + static_cast<int>(effects.size()) * rowHeight + bottomPadding;
    }

    std::shared_ptr<EffectComponent> getEffect(int index) const {
        return effects[static_cast<size_t>(index)];
    }

private:
    static constexpr int rowHeight = 30;
    static constexpr int bottomPadding = 5;
    std::vector<std::shared_ptr<EffectComponent>> effects;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GroupedSettings)
};

class VisualiserSettings : public juce::Component, public juce::AudioProcessorParameter::Listener {
public:
    VisualiserSettings(VisualiserParameters&, int numChannels, RecordingParameters& recordingParameters);
    ~VisualiserSettings();

#ifndef SOSCI
    void wireModulation(OscirenderAudioProcessor& processor);
#endif

    void paint(juce::Graphics& g) override;
    void resized() override;
    int getPreferredHeight(int width);
    void setSizeToFitWidth(int width);
    void fitToViewport(juce::Viewport& viewport);
    void parameterValueChanged(int parameterIndex, float newValue) override;
    void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override;

    VisualiserParameters& parameters;
    RecordingParameters& recordingParameters;
    int numChannels;
    std::function<void()> onUpgradeRequested;

private:
    struct LayoutItem {
        juce::Component* component;
        int height;
    };

    void updateScreenOverlayItemsEnabled();
    void updateTriggerControls();
    juce::Colour getCurrentLineColour() const;
    std::vector<LayoutItem> getLayoutItems(int sectionWidth);
    int getDisplayOptionsHeight(int width) const;
    int getSweepSettingsHeight(int width) const;
    void layoutDisplayOptions();
    void layoutSweepSettings();

    SettingsSection displayOptions{"Display Options"};
    SettingsSection sweepSettings{""};
    juce::Label sweepTitle{"Sweep & Trigger", "Sweep & Trigger"};
    osci::GridComponent optionToggleGrid{juce::FlexBox::JustifyContent::center};

    GroupedSettings lineColour{
        std::vector<std::shared_ptr<EffectComponent>>{
            std::make_shared<EffectComponent>(*parameters.hueEffect),
            std::make_shared<EffectComponent>(*parameters.lineSaturationEffect),
            std::make_shared<EffectComponent>(*parameters.intensityEffect),
        },
        "Line Colour"};
    std::shared_ptr<osci::ColourSwatchComponent> lineColourSwatch;

#if OSCI_PREMIUM
    GroupedSettings screenColour{
        std::vector<std::shared_ptr<EffectComponent>>{
            std::make_shared<EffectComponent>(*parameters.screenHueEffect),
            std::make_shared<EffectComponent>(*parameters.screenSaturationEffect),
            std::make_shared<EffectComponent>(*parameters.ambientEffect),
        },
        "Screen Colour"};
#endif

    GroupedSettings lightEffects{
        std::vector<std::shared_ptr<EffectComponent>>{
            std::make_shared<EffectComponent>(*parameters.persistenceEffect),
            std::make_shared<EffectComponent>(*parameters.focusEffect),
            std::make_shared<EffectComponent>(*parameters.glowEffect),
#if OSCI_PREMIUM
            std::make_shared<EffectComponent>(*parameters.afterglowEffect),
            std::make_shared<EffectComponent>(*parameters.overexposureEffect),
#else
            std::make_shared<EffectComponent>(*parameters.ambientEffect),
#endif
        },
        "Light Effects"};

    GroupedSettings videoEffects{
        std::vector<std::shared_ptr<EffectComponent>>{
            std::make_shared<EffectComponent>(*parameters.noiseEffect),
        },
        "Video Effects"};

    GroupedSettings lineEffects{
        std::vector<std::shared_ptr<EffectComponent>>{
            std::make_shared<EffectComponent>(*parameters.smoothEffect),
#if OSCI_PREMIUM
            std::make_shared<EffectComponent>(*parameters.stereoEffect),
#endif
        },
        "Line Effects"};

    EffectComponent sweepMs{*parameters.sweepMsEffect};
    EffectComponent triggerValue{*parameters.triggerValueEffect};
    juce::Label triggerSourceLabel{"Trigger Source", "Trigger Source"};
    juce::ComboBox triggerSourceBox;
    juce::Label triggerSlopeLabel{"Trigger Slope", "Trigger Slope"};
    juce::ComboBox triggerSlopeBox;

    juce::Label screenOverlayLabel{"Screen Overlay", "Screen Overlay"};
    juce::ComboBox screenOverlay;

#if OSCI_GUI_ENABLE_CHOWDSP_RESAMPLING
    ToggleLabelComponent upsamplingToggle{parameters.upsamplingEnabled, {}, false};
#endif
    jux::SwitchButton sweepToggle{parameters.sweepEnabled, true, false};

#if OSCI_PREMIUM
    ToggleLabelComponent transparentBackgroundToggle{parameters.transparentBackground, {}, false};
    GroupedSettings scale{
        std::vector<std::shared_ptr<EffectComponent>>{
            std::make_shared<EffectComponent>(*parameters.scaleEffect, 0),
            std::make_shared<EffectComponent>(*parameters.scaleEffect, 1),
        },
        "Image Scale"};

    GroupedSettings position{
        std::vector<std::shared_ptr<EffectComponent>>{
            std::make_shared<EffectComponent>(*parameters.offsetEffect, 0),
            std::make_shared<EffectComponent>(*parameters.offsetEffect, 1),
        },
        "Image Position"};

    ToggleLabelComponent flipVerticalToggle{parameters.flipVertical, {}, false};
    ToggleLabelComponent flipHorizontalToggle{parameters.flipHorizontal, {}, false};
    ToggleLabelComponent goniometerToggle{parameters.goniometer, {}, false};
    ToggleLabelComponent shutterSyncToggle{parameters.shutterSync, {}, false};
#endif

#if !OSCI_PREMIUM
    juce::TextButton upgradeButton { "Unlock Premium Features" };
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VisualiserSettings)
};

class ScrollableComponent : public juce::Component {
public:
    ScrollableComponent(VisualiserSettings& component) : component(component) {
        addAndMakeVisible(viewport);
        viewport.setViewedComponent(&component, false);
        viewport.setScrollBarsShown(true, false, true, false);
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(osci::Colours::darker());
    }

    void resized() override {
        viewport.setBounds(getLocalBounds());
        component.fitToViewport(viewport);
    }

private:
    juce::Viewport viewport;
    VisualiserSettings& component;
};

class SettingsWindow : public juce::DialogWindow {
public:
    SettingsWindow(juce::String name, VisualiserSettings& component, int windowWidth, int windowHeight, int maximumWindowWidth) : juce::DialogWindow(name, osci::Colours::darker(), true, true), component(component) {
        setContentNonOwned(&viewport, false);
        centreWithSize(windowWidth, windowHeight);
        setResizeLimits(windowWidth, windowHeight, maximumWindowWidth, juce::jmax(windowHeight, component.getPreferredHeight(windowWidth)));
        setResizable(true, false);
        viewport.setColour(juce::ScrollBar::trackColourId, juce::Colours::white);
        viewport.setViewedComponent(&component, false);
        viewport.setScrollBarsShown(true, false, true, false);
        setAlwaysOnTop(true);
    }

    void closeButtonPressed() override {
        setVisible(false);
    }

    void resized() override {
        DialogWindow::resized();
        component.fitToViewport(viewport);
    }

private:
    juce::Viewport viewport;
    VisualiserSettings& component;
};
