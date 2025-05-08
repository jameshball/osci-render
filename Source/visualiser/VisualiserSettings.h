#pragma once

#define VERSION_HINT 2

#include <JuceHeader.h>
#include "../components/EffectComponent.h"
#include "../components/SvgButton.h"
#include "../LookAndFeel.h"
#include "../components/SwitchButton.h"
#include "../audio/SmoothEffect.h"
#include "../audio/StereoEffect.h"

enum class ScreenOverlay : int {
    INVALID = -1,
    Empty = 1,
    Graticule = 2,
    Smudged = 3,
    SmudgedGraticule = 4,
#if OSCI_PREMIUM
    Real = 5,
    VectorDisplay = 6,
    MAX = 6,
#else
    MAX = 4,
#endif
};

class ScreenOverlayParameter : public osci::IntParameter {
public:
    ScreenOverlayParameter(juce::String name, juce::String id, int versionHint, ScreenOverlay value) : osci::IntParameter(name, id, versionHint, (int) value, 1, (int)ScreenOverlay::MAX) {}

    juce::String getText(float value, int maximumStringLength = 100) const override {
        switch ((ScreenOverlay)(int)getUnnormalisedValue(value)) {
            case ScreenOverlay::Empty:
                return "Empty";
            case ScreenOverlay::Graticule:
                return "Graticule";
            case ScreenOverlay::Smudged:
                return "Smudged";
            case ScreenOverlay::SmudgedGraticule:
                return "Smudged Graticule";
#if OSCI_PREMIUM
            case ScreenOverlay::Real:
                return "Real Oscilloscope";
            case ScreenOverlay::VectorDisplay:
                return "Vector Display";
#endif
            default:
                return "Unknown";
        }
    }

    float getValueForText(const juce::String& text) const override {
        int unnormalisedValue;
        if (text == "Empty") {
            unnormalisedValue = (int)ScreenOverlay::Empty;
        } else if (text == "Graticule") {
            unnormalisedValue = (int)ScreenOverlay::Graticule;
        } else if (text == "Smudged") {
            unnormalisedValue = (int)ScreenOverlay::Smudged;
        } else if (text == "Smudged Graticule") {
            unnormalisedValue = (int)ScreenOverlay::SmudgedGraticule;
#if OSCI_PREMIUM
        } else if (text == "Real Oscilloscope") {
            unnormalisedValue = (int)ScreenOverlay::Real;
        } else if (text == "Vector Display") {
            unnormalisedValue = (int)ScreenOverlay::VectorDisplay;
#endif
        } else {
            unnormalisedValue = (int)ScreenOverlay::Empty;
        }
        return getNormalisedValue(unnormalisedValue);
    }

    void save(juce::XmlElement* xml) {
        xml->setAttribute("screenOverlay", getText(getValue()));
    }

    void load(juce::XmlElement* xml) {
        setValueNotifyingHost(getValueForText(xml->getStringAttribute("screenOverlay")));
    }
    
#if OSCI_PREMIUM
    bool isRealisticDisplay() {
        ScreenOverlay type = (ScreenOverlay)(int)getValueUnnormalised();
        return type == ScreenOverlay::Real || type == ScreenOverlay::VectorDisplay;
    }
#endif
};

class VisualiserParameters {
public:

    double getIntensity() {
        return intensityEffect->getActualValue() / 100;
    }
    
    double getPersistence() {
        return persistenceEffect->getActualValue() - 1.33;
    }
    
    double getHue() {
        return hueEffect->getActualValue();
    }
    
    double getLineSaturation() {
        return lineSaturationEffect->getActualValue();
    }

#if OSCI_PREMIUM
    double getScreenSaturation() {
        return screenSaturationEffect->getActualValue();
    }
    
    double getScreenHue() {
        return screenHueEffect->getActualValue();
    }
    
    double getAfterglow() {
        return afterglowEffect->getActualValue();
    }
    
    double getOverexposure() {
        return overexposureEffect->getActualValue();
    }

    bool isFlippedVertical() {
        return flipVertical->getBoolValue();
    }

    bool isFlippedHorizontal() {
        return flipHorizontal->getBoolValue();
    }
    
    bool isGoniometer() {
        return goniometer->getBoolValue();
    }
    
    bool getShutterSync() {
        return shutterSync->getBoolValue();
    }
#endif
    
    double getFocus() {
        return focusEffect->getActualValue() / 100;
    }
    
    double getNoise() {
        return noiseEffect->getActualValue() / 5;
    }
    
    double getGlow() {
        return glowEffect->getActualValue() * 3;
    }
    
    double getAmbient() {
        return ambientEffect->getActualValue();
    }
    
    ScreenOverlay getScreenOverlay() {
        return (ScreenOverlay)screenOverlay->getValueUnnormalised();
    }
    
    bool getUpsamplingEnabled() {
        return upsamplingEnabled->getBoolValue();
    }
    
    bool isSweepEnabled() {
        return sweepEnabled->getBoolValue();
    }
    
    double getSweepSeconds() {
        return sweepMsEffect->getActualValue() / 1000.0;
    }
    
    double getTriggerValue() {
        return triggerValueEffect->getActualValue();
    }

    ScreenOverlayParameter* screenOverlay = new ScreenOverlayParameter("Screen Overlay", "screenOverlay", VERSION_HINT, ScreenOverlay::SmudgedGraticule);
    osci::BooleanParameter* upsamplingEnabled = new osci::BooleanParameter("Upsample Audio", "upsamplingEnabled", VERSION_HINT, true, "Upsamples the audio before visualising it to make it appear more realistic, at the expense of performance.");
    osci::BooleanParameter* sweepEnabled = new osci::BooleanParameter("Sweep", "sweepEnabled", VERSION_HINT, false, "Plots the audio signal over time, sweeping from left to right");
    osci::BooleanParameter* visualiserFullScreen = new osci::BooleanParameter("Visualiser Fullscreen", "visualiserFullScreen", VERSION_HINT, false, "Makes the software visualiser fullscreen.");

#if OSCI_PREMIUM
    osci::BooleanParameter* flipVertical = new osci::BooleanParameter("Flip Vertical", "flipVertical", VERSION_HINT, false, "Flips the visualiser vertically.");
    osci::BooleanParameter* flipHorizontal = new osci::BooleanParameter("Flip Horizontal", "flipHorizontal", VERSION_HINT, false, "Flips the visualiser horizontally.");
    osci::BooleanParameter* goniometer = new osci::BooleanParameter("Goniometer", "goniometer", VERSION_HINT, false, "Rotates the visualiser to replicate a goniometer display to show the phase relationship between two channels.");
    osci::BooleanParameter* shutterSync = new osci::BooleanParameter("Shutter Sync", "shutterSync", VERSION_HINT, false, "Controls whether the camera's shutter speed is in sync with framerate. This makes the brightness of a single frame constant. This can be beneficial when the drawing frequency and frame rate are in sync.");

    std::shared_ptr<osci::Effect> screenSaturationEffect = std::make_shared<osci::Effect>(
        new osci::EffectParameter(
            "Screen Saturation",
            "Controls how saturated the colours are on the oscilloscope screen.",
            "screenSaturation",
            VERSION_HINT, 1.0, 0.0, 5.0
        )
    );
    std::shared_ptr<osci::Effect> screenHueEffect = std::make_shared<osci::Effect>(
        new osci::EffectParameter(
            "Screen Hue",
            "Controls the hue shift of the oscilloscope screen.",
            "screenHue",
            VERSION_HINT, 0, 0, 359, 1
        )
    );
    std::shared_ptr<osci::Effect> afterglowEffect = std::make_shared<osci::Effect>(
        new osci::EffectParameter(
            "Afterglow",
            "Controls how quickly the image disappears after glowing brightly. Closely related to persistence.",
            "afterglow",
            VERSION_HINT, 1.0, 0.0, 10.0
        )
    );
    std::shared_ptr<osci::Effect> overexposureEffect = std::make_shared<osci::Effect>(
        new osci::EffectParameter(
            "Overexposure",
            "Controls at which point the line becomes overexposed and clips, turning white.",
            "overexposure",
            VERSION_HINT, 0.5, 0.0, 1.0
        )
    );
    std::shared_ptr<StereoEffect> stereoEffectApplication = std::make_shared<StereoEffect>();
    std::shared_ptr<osci::Effect> stereoEffect = std::make_shared<osci::Effect>(
        stereoEffectApplication,
        new osci::EffectParameter(
            "Stereo",
            "Turns mono audio that is uninteresting to visualise into stereo audio that is interesting to visualise.",
            "stereo",
            VERSION_HINT, 0.0, 0.0, 1.0
        )
    );
    std::shared_ptr<osci::Effect> scaleEffect = std::make_shared<osci::Effect>(
        [this](int index, osci::Point input, const std::vector<std::atomic<double>>& values, double sampleRate) {
            input.scale(values[0].load(), values[1].load(), 1.0);
            return input;
        }, std::vector<osci::EffectParameter*>{
        new osci::EffectParameter(
            "X Scale",
            "Controls the horizontal scale of the oscilloscope display.",
            "xScale",
            VERSION_HINT, 1.0, -3.0, 3.0
        ),
            new osci::EffectParameter(
                "Y Scale",
                "Controls the vertical scale of the oscilloscope display.",
                "yScale",
                VERSION_HINT, 1.0, -3.0, 3.0
            ),
    });
    std::shared_ptr<osci::Effect> offsetEffect = std::make_shared<osci::Effect>(
        [this](int index, osci::Point input, const std::vector<std::atomic<double>>& values, double sampleRate) {
            input.translate(values[0].load(), values[1].load(), 0.0);
            return input;
        }, std::vector<osci::EffectParameter*>{
        new osci::EffectParameter(
            "X Offset",
            "Controls the horizontal position offset of the oscilloscope display.",
            "xOffset",
            VERSION_HINT, 0.0, -1.0, 1.0
        ),
        new osci::EffectParameter(
            "Y Offset",
            "Controls the vertical position offset of the oscilloscope display.",
            "yOffset",
            VERSION_HINT, 0.0, -1.0, 1.0
        ),
    });
#endif

    std::shared_ptr<osci::Effect> persistenceEffect = std::make_shared<osci::Effect>(
        new osci::EffectParameter(
            "Persistence",
            "Controls how long the light glows for on the oscilloscope display.",
            "persistence",
            VERSION_HINT, 0.5, 0, 6.0
        )
    );
    std::shared_ptr<osci::Effect> hueEffect = std::make_shared<osci::Effect>(
        new osci::EffectParameter(
            "Line Hue",
            "Controls the hue of the beam of the oscilloscope.",
            "hue",
            VERSION_HINT, 125, 0, 359, 1
        )
    );
    std::shared_ptr<osci::Effect> intensityEffect = std::make_shared<osci::Effect>(
        new osci::EffectParameter(
            "Line Intensity",
            "Controls how bright the electron beam of the oscilloscope is.",
            "intensity",
            VERSION_HINT, 5.0, 0.0, 10.0
        )
    );
    std::shared_ptr<osci::Effect> lineSaturationEffect = std::make_shared<osci::Effect>(
        new osci::EffectParameter(
            "Line Saturation",
            "Controls how saturated the colours are on the oscilloscope lines.",
            "lineSaturation",
            VERSION_HINT, 1.0, 0.0, 5.0
        )
    );
    std::shared_ptr<osci::Effect> focusEffect = std::make_shared<osci::Effect>(
        new osci::EffectParameter(
            "Focus",
            "Controls how focused the electron beam of the oscilloscope is.",
            "focus",
            VERSION_HINT, 1.0, 0.3, 10.0
        )
    );
    std::shared_ptr<osci::Effect> noiseEffect = std::make_shared<osci::Effect>(
        new osci::EffectParameter(
            "Noise",
            "Controls how much noise/grain is added to the oscilloscope display.",
            "noise",
            VERSION_HINT, 0.0, 0.0, 1.0
        )
    );
    std::shared_ptr<osci::Effect> glowEffect = std::make_shared<osci::Effect>(
        new osci::EffectParameter(
            "Glow",
            "Controls how much the light glows on the oscilloscope display.",
            "glow",
            VERSION_HINT, 0.3, 0.0, 1.0
        )
    );
    std::shared_ptr<osci::Effect> ambientEffect = std::make_shared<osci::Effect>(
        new osci::EffectParameter(
            "Ambient Light",
            "Controls how much ambient light is added to the oscilloscope display.",
            "ambient",
            VERSION_HINT, 0.7, 0.0, 5.0
        )
    );
    std::shared_ptr<osci::Effect> smoothEffect = std::make_shared<osci::Effect>(
        std::make_shared<SmoothEffect>(),
        new osci::EffectParameter(
            "Smoothing",
            "This works as a low-pass frequency filter, effectively reducing the sample rate of the audio being visualised.",
            "visualiserSmoothing",
            VERSION_HINT, 0, 0.0, 1.0
        )
    );
    std::shared_ptr<osci::Effect> sweepMsEffect = std::make_shared<osci::Effect>(
        new osci::EffectParameter(
            "Sweep (ms)",
            "The number of milliseconds it takes for the oscilloscope to sweep from left to right.",
            "sweepMs",
            VERSION_HINT, 10.0, 0.0, 1000.0
        )
    );
    std::shared_ptr<osci::Effect> triggerValueEffect = std::make_shared<osci::Effect>(
        new osci::EffectParameter(
            "Trigger Value",
            "The trigger value sets the signal level that starts waveform capture to display a stable waveform.",
            "triggerValue",
            VERSION_HINT, 0.0, -1.0, 1.0
        )
    );
    
    std::vector<std::shared_ptr<osci::Effect>> effects = {
        persistenceEffect,
        hueEffect,
        intensityEffect,
        lineSaturationEffect,
        focusEffect,
        noiseEffect,
        glowEffect,
        ambientEffect,
        sweepMsEffect,
        triggerValueEffect,
#if OSCI_PREMIUM
        afterglowEffect,
        screenSaturationEffect,
        screenHueEffect,
        overexposureEffect,
#endif
    };
    std::vector<std::shared_ptr<osci::Effect>> audioEffects = {
        smoothEffect,
#if OSCI_PREMIUM
        stereoEffect,
        scaleEffect,
        offsetEffect,
#endif
    };
    std::vector<osci::BooleanParameter*> booleans = {
        upsamplingEnabled,
        visualiserFullScreen,
        sweepEnabled,
#if OSCI_PREMIUM
        flipVertical,
        flipHorizontal,
        goniometer,
        shutterSync,
#endif
    };
    std::vector<osci::IntParameter*> integers = {
        screenOverlay,
    };
};

class GroupedSettings : public juce::GroupComponent {
public:
    GroupedSettings(std::vector<std::shared_ptr<EffectComponent>> effects, juce::String label) : effects(effects), juce::GroupComponent(label, label) {
        for (auto effect : effects) {
            addAndMakeVisible(effect.get());
            effect->setSliderOnValueChange();
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

private:
    GroupedSettings lineColour{
        std::vector<std::shared_ptr<EffectComponent>>{
            std::make_shared<EffectComponent>(*parameters.hueEffect),
            std::make_shared<EffectComponent>(*parameters.lineSaturationEffect),
            std::make_shared<EffectComponent>(*parameters.intensityEffect),
        },
        "Line Colour"
    };
    
#if OSCI_PREMIUM
    GroupedSettings screenColour{
        std::vector<std::shared_ptr<EffectComponent>>{
            std::make_shared<EffectComponent>(*parameters.screenHueEffect),
            std::make_shared<EffectComponent>(*parameters.screenSaturationEffect),
            std::make_shared<EffectComponent>(*parameters.ambientEffect),
        },
        "Screen Colour"
    };
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
        "Light Effects"
    };
    
    GroupedSettings videoEffects{
        std::vector<std::shared_ptr<EffectComponent>>{
            std::make_shared<EffectComponent>(*parameters.noiseEffect),
        },
        "Video Effects"
    };
    
    GroupedSettings lineEffects{
        std::vector<std::shared_ptr<EffectComponent>>{
            std::make_shared<EffectComponent>(*parameters.smoothEffect),
#if OSCI_PREMIUM
            std::make_shared<EffectComponent>(*parameters.stereoEffect),
#endif
        },
        "Line Effects"
    };
    
    EffectComponent sweepMs{*parameters.sweepMsEffect};
    EffectComponent triggerValue{*parameters.triggerValueEffect};
    
    juce::Label screenOverlayLabel{"Screen Overlay", "Screen Overlay"};
    juce::ComboBox screenOverlay;
    
    jux::SwitchButton upsamplingToggle{parameters.upsamplingEnabled};
    jux::SwitchButton sweepToggle{parameters.sweepEnabled};

#if OSCI_PREMIUM
    GroupedSettings positionSize{
        std::vector<std::shared_ptr<EffectComponent>>{
            std::make_shared<EffectComponent>(*parameters.scaleEffect, 0),
            std::make_shared<EffectComponent>(*parameters.scaleEffect, 1),
            std::make_shared<EffectComponent>(*parameters.offsetEffect, 0),
            std::make_shared<EffectComponent>(*parameters.offsetEffect, 1),
        },
        "Line Position & Scale"
    };

    jux::SwitchButton flipVerticalToggle{parameters.flipVertical};
    jux::SwitchButton flipHorizontalToggle{parameters.flipHorizontal};
    jux::SwitchButton goniometerToggle{parameters.goniometer};
    jux::SwitchButton shutterSyncToggle{parameters.shutterSync};
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
    SettingsWindow(juce::String name, juce::Component& component, int windowWidth, int windowHeight, int componentWidth, int componentHeight) : juce::DialogWindow(name, Colours::darker, true, true), component(component) {
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

private:
    juce::Viewport viewport;
    juce::Component& component;
};
