#pragma once

#include <JuceHeader.h>
#include "NodeGraphComponent.h"
#include "LfoRateComponent.h"
#include "../../audio/LfoState.h"
#include "ModulationSourceComponent.h"

class OscirenderAudioProcessor;

// LFO panel: subclass of ModulationSourceComponent that adds a waveform
// graph editor, preset selector, and rate knob.
class LfoComponent : public ModulationSourceComponent {
public:
    LfoComponent(OscirenderAudioProcessor& processor);

    void resized() override;

    int getActiveLfoIndex() const { return getActiveSourceIndex(); }

    void setLfoWaveform(int lfoIndex, const LfoWaveform& waveform);
    LfoWaveform getLfoWaveform(int lfoIndex) const;
    void setLfoPreset(int lfoIndex, LfoPreset preset);
    LfoPreset getLfoPreset(int lfoIndex) const;

    static juce::Colour getLfoColour(int lfoIndex);

    void syncFromProcessorState() override;

protected:
    void onActiveSourceChanged(int newIndex) override;

private:
    OscirenderAudioProcessor& audioProcessor;

    // Per-LFO state
    struct LfoData {
        LfoWaveform waveform;
        LfoWaveform customWaveform;
        LfoPreset preset = LfoPreset::Triangle;
    };
    std::array<LfoData, NUM_LFOS> lfoData;

    // --- Child Components ---
    NodeGraphComponent graph;

    class PresetSelector : public juce::Component {
    public:
        PresetSelector();
        void paint(juce::Graphics& g) override;
        void mouseDown(const juce::MouseEvent& e) override;
        void mouseMove(const juce::MouseEvent& e) override;
        void resized() override;
        void setPresetName(const juce::String& name);
        std::function<void()> onPrev;
        std::function<void()> onNext;
    private:
        juce::String presetName;
        juce::Rectangle<int> leftArrowArea, rightArrowArea;
    };
    PresetSelector presetSelector;
    LfoRateComponent rateControl;

    // --- Layout constants ---
    static constexpr int kTopBarHeight   = 22;
    static constexpr int kTopBarGap      = 4;
    static constexpr int kRateHeight     = 40;
    static constexpr int kRateGap        = 4;
    static constexpr int kMaxPresetWidth = 180;
    static constexpr int kMaxRateWidth   = 130;

    // --- Internal methods ---
    void syncGraphToActiveLfo();
    void syncActiveLfoFromGraph();
    void cyclePreset(int direction);
    void applyPreset(LfoPreset preset);
    void updatePresetLabel();
    void applyLfoConstraints(int nodeIndex, double& time, double& value);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LfoComponent)
};
