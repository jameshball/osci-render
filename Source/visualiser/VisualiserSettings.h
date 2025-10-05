#pragma once

#define VERSION_HINT 2

#include <JuceHeader.h>

#include "../LookAndFeel.h"
#include "../audio/SmoothEffect.h"
#include "../audio/StereoEffect.h"
#include "../components/EffectComponent.h"
#include "../components/SvgButton.h"
#include "../components/SwitchButton.h"
#include "VisualiserParameters.h"

class GroupedSettings : public juce::GroupComponent {
public:
    GroupedSettings(std::vector<std::shared_ptr<EffectComponent>> effects, juce::String label) : effects(effects), juce::GroupComponent(label, label) {
        for (auto effect : effects) {
            addAndMakeVisible(effect.get());
        }

        setColour(groupComponentBackgroundColourId, Colours::veryDark.withMultipliedBrightness(3.0));
    }

    void resized() override {
        auto area = getLocalBounds();
        area.removeFromTop(35);
        double rowHeight = 30;

        for (auto effect : effects) {
            effect->setBounds(area.removeFromTop(rowHeight));
        }
    }

    int getHeight() {
        return 40 + effects.size() * 30;
    }

private:
    std::vector<std::shared_ptr<EffectComponent>> effects;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GroupedSettings)
};

class VisualiserSettings : public juce::Component, public juce::AudioProcessorParameter::Listener {
public:
    VisualiserSettings(VisualiserParameters&, int numChannels = 2);
    ~VisualiserSettings();

    void paint(juce::Graphics& g) override;
    void resized() override;
    void parameterValueChanged(int parameterIndex, float newValue) override;
    void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override;

    VisualiserParameters& parameters;
    int numChannels;
    std::function<void()> onUpgradeRequested;

private:
    GroupedSettings lineColour{
        std::vector<std::shared_ptr<EffectComponent>>{
            std::make_shared<EffectComponent>(*parameters.lineRedEffect),
            std::make_shared<EffectComponent>(*parameters.lineGreenEffect),
            std::make_shared<EffectComponent>(*parameters.lineBlueEffect),
            std::make_shared<EffectComponent>(*parameters.lineSaturationEffect),
            std::make_shared<EffectComponent>(*parameters.intensityEffect),
        },
        "Line Colour"};

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

    juce::Label screenOverlayLabel{"Screen Overlay", "Screen Overlay"};
    juce::ComboBox screenOverlay;

    jux::SwitchButton upsamplingToggle{parameters.upsamplingEnabled};
    jux::SwitchButton sweepToggle{parameters.sweepEnabled};

#if OSCI_PREMIUM
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

    jux::SwitchButton flipVerticalToggle{parameters.flipVertical};
    jux::SwitchButton flipHorizontalToggle{parameters.flipHorizontal};
    jux::SwitchButton goniometerToggle{parameters.goniometer};
    jux::SwitchButton shutterSyncToggle{parameters.shutterSync};
#endif

#if !OSCI_PREMIUM
    juce::TextButton upgradeButton { "Unlock Premium Features" };
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VisualiserSettings)
};

class ScrollableComponent : public juce::Component {
public:
    ScrollableComponent(juce::Component& component) : component(component) {
        addAndMakeVisible(viewport);
        viewport.setViewedComponent(&component, false);
        viewport.setScrollBarsShown(true, false, true, false);
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(Colours::darker);
    }

    void resized() override {
        viewport.setBounds(getLocalBounds());
    }

private:
    juce::Viewport viewport;
    juce::Component& component;
};

class SettingsWindow : public juce::DialogWindow {
public:
    SettingsWindow(juce::String name, juce::Component& component, int windowWidth, int windowHeight, int componentWidth, int componentHeight) : juce::DialogWindow(name, Colours::darker, true, true), component(component), componentHeight(componentHeight) {
        setContentComponent(&viewport);
        centreWithSize(windowWidth, windowHeight);
        setResizeLimits(windowWidth, windowHeight, componentWidth, componentHeight);
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
        // Update the component width to match the viewport width while maintaining its height
        component.setSize(viewport.getWidth(), componentHeight);
    }

private:
    juce::Viewport viewport;
    juce::Component& component;
    int componentHeight;
};
