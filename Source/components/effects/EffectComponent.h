#pragma once
#include <JuceHeader.h>

#include <osci_gui/osci_gui.h>
#include "../ModulationState.h"

#ifndef SOSCI
class OscirenderAudioProcessor;
#endif

class ModulationUpdateBroadcaster;

class EffectComponent : public juce::Component, public juce::AudioProcessorParameter::Listener, juce::AsyncUpdater, public juce::SettableTooltipClient, private juce::Slider::Listener, public juce::DragAndDropTarget, public juce::ChangeListener {
public:
    EffectComponent(osci::Effect& effect, int index);
    EffectComponent(osci::Effect& effect);
    ~EffectComponent();

    // Backwards-compatible aliases — these now delegate to ModulationState.
    static inline std::atomic<bool>& modAnyDragActive = ModulationState::anyDragActive;
    static inline juce::String&      highlightedParamId = ModulationState::highlightedParamId;
    static inline juce::String&      modRangeParamId = ModulationState::rangeParamId;
    static inline std::atomic<float>& modRangeDepth = ModulationState::rangeDepth;
    static inline std::atomic<bool>&  modRangeBipolar = ModulationState::rangeBipolar;

    void resized() override;
    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void parameterValueChanged(int parameterIndex, float newValue) override;
    void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override;
    void handleAsyncUpdate() override;
    
    // Slider::Listener callbacks for MIDI learn support
    void sliderValueChanged(juce::Slider* slider) override;
    void sliderDragStarted(juce::Slider* slider) override;
    void sliderDragEnded(juce::Slider* slider) override;

    void setRangeEnabled(bool enabled);

    void setComponent(std::shared_ptr<juce::Component> component);

    // DragAndDropTarget overrides for LFO assignment
    bool isInterestedInDragSource(const SourceDetails& dragSourceDetails) override;
    void itemDragEnter(const SourceDetails& dragSourceDetails) override;
    void itemDragExit(const SourceDetails& dragSourceDetails) override;
    void itemDropped(const SourceDetails& dragSourceDetails) override;

    // Modulation display info returned by query callbacks.
    struct ModInfo {
        bool active = false;           // true if any source is assigned to this param
        float modulatedPos = 0.0f;     // pixel position of the current modulated value
        juce::Colour colour;           // blended colour from all assigned sources
    };

    // Describes one modulation source type (LFO, Envelope, Random, etc.)
    // All source types are handled identically via these bindings.
    struct ModBinding {
        juce::String dragPrefix;       // "LFO", "ENV", "RNG" — matched in "MOD:LFO:0" drag descriptions
        juce::String propPrefix;       // "lfo", "env", "rng" — used as slider property key prefix
        std::function<void(int, const juce::String&)> onDropped;  // add assignment callback
        std::function<ModInfo(const juce::String& paramId, juce::Slider& slider)> query;  // query callback
    };

    // All registered modulation bindings — populated by wireModulation().
    std::vector<ModBinding> modBindings;

    // Update all modulation displays (call from a parent timer, not per-instance).
    void updateModulationDisplay();

    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

    // Show a right-click context menu with Reset, Set Value, MIDI CC, and settings options.
    void showContextMenu(juce::Point<int> screenPos);

    void mouseDown(const juce::MouseEvent& event) override;
    void mouseEnter(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;

#ifndef SOSCI
    // Wire standard modulation callbacks (LFO + envelope drop, query) to the processor.
    // Call this after construction to enable modulation for this slider.
    void wireModulation(OscirenderAudioProcessor& processor);
#endif

    juce::Slider slider;
    juce::Slider lfoSlider;
    osci::Effect& effect;
    int index = 0;
    juce::ComboBox lfo;

    std::function<void()> updateToggleState;

private:
    const int TEXT_BOX_WIDTH = 70;
    const int SMALL_TEXT_BOX_WIDTH = 50;

    const int TEXT_WIDTH = 120;
    const int SMALL_TEXT_WIDTH = 90;

    static void beginGestureIfPossible (juce::AudioProcessorParameter* parameter)
    {
        if (parameter != nullptr && parameter->getParameterIndex() >= 0)
            parameter->beginChangeGesture();
    }

    static void endGestureIfPossible (juce::AudioProcessorParameter* parameter)
    {
        if (parameter != nullptr && parameter->getParameterIndex() >= 0)
            parameter->endChangeGesture();
    }

    void setSliderValueIfChanged(osci::FloatParameter* parameter, juce::Slider& slider);
    void setupComponent();
    void wireMidiCC(osci::MidiCCManager& manager);
    void showValueEditor();
    void updateLabelAppearance();
    bool lfoEnabled = true;
    bool sidechainEnabled = true;
    bool modDropHighlight = false;
    std::shared_ptr<juce::Component> component;

    std::unique_ptr<osci::SvgButton> sidechainButton;

    osci::SvgButton linkButton = osci::SvgButton(effect.parameters[index]->name + " Link",
        BinaryData::link_svg,
        juce::Colours::white,
        juce::Colours::red,
        nullptr,
        BinaryData::link_svg
    );

    juce::Label label;

    ModulationUpdateBroadcaster* modBroadcaster = nullptr;
    osci::MidiCCManager* midiCCManager = nullptr;
    bool labelHovered = false;
    bool rangeEnabled = true;
    bool* undoGroupingFlag = nullptr;
    juce::String* lastChangedParamIdPtr = nullptr;
    std::shared_ptr<juce::TextEditor> valueEditor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EffectComponent)
};
