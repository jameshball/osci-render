#pragma once

#include <JuceHeader.h>
#include "NodeGraphComponent.h"
#include "ModulationRateComponent.h"
#include "ModulationModeComponent.h"
#include "../../audio/LfoState.h"
#include "ModulationSourceComponent.h"
#include "../PhaseSliderComponent.h"
#include "../SvgButton.h"

class OscirenderAudioProcessor;

// LFO panel: subclass of ModulationSourceComponent that adds a waveform
// graph editor, preset selector, and rate knob.
class LfoComponent : public ModulationSourceComponent {
public:
    LfoComponent(OscirenderAudioProcessor& processor);

    void resized() override;
    void paint(juce::Graphics& g) override;
    void timerCallback() override;

    int getActiveLfoIndex() const { return getActiveSourceIndex(); }

    void setLfoWaveform(int lfoIndex, const LfoWaveform& waveform);
    LfoWaveform getLfoWaveform(int lfoIndex) const;
    void setLfoPreset(int lfoIndex, LfoPreset preset);
    LfoPreset getLfoPreset(int lfoIndex) const;

    static juce::Colour getLfoColour(int lfoIndex);

    // Restrict LFO features when MIDI is disabled (only Free mode available)
    void setMidiEnabled(bool enabled);

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

    class PresetSelector : public juce::Component, public juce::SettableTooltipClient {
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

    // Miniature preview that paints a single paint-shape waveform.
    class PaintShapePreview : public juce::Component, public juce::SettableTooltipClient {
    public:
        PaintShapePreview();
        void paint(juce::Graphics& g) override;
        void mouseDown(const juce::MouseEvent& e) override;
        void setShape(NodeGraphComponent::PaintShape s) { shape = s; repaint(); }
        NodeGraphComponent::PaintShape getShape() const { return shape; }
        void setAccentColour(juce::Colour c) { accent = c; repaint(); }
        void setEnabled(bool enabled) { paintEnabled = enabled; repaint(); }
        std::function<void()> onClick;
    private:
        NodeGraphComponent::PaintShape shape = NodeGraphComponent::PaintShape::Step;
        juce::Colour accent { juce::Colours::transparentBlack };
        bool paintEnabled = false;
    };

    PresetSelector presetSelector;
    SvgButton paintToggle;

    // Custom S-curve toggle for bezier/straight interpolation mode.
    class BezierToggle : public juce::Component, public juce::SettableTooltipClient {
    public:
        BezierToggle();
        void paint(juce::Graphics& g) override;
        void mouseDown(const juce::MouseEvent& e) override;
        void setBezier(bool b) { bezier = b; repaint(); }
        bool isBezier() const { return bezier; }
        void setAccentColour(juce::Colour c) { accent = c; repaint(); }
        std::function<void(bool)> onToggle;
    private:
        bool bezier = true;
        juce::Colour accent { juce::Colours::transparentBlack };
    };

    BezierToggle bezierToggle;
    PaintShapePreview shapePreview;
    SvgButton copyButton;
    SvgButton pasteButton;
    ModulationRateComponent rateControl;
    ModulationModeComponent modeControl;
    PhaseSliderComponent phaseSlider;

    // --- Layout constants ---
    static constexpr int kTopBarHeight   = 20;
    static constexpr int kTopBarGap      = 4;
    static constexpr int kPhaseHeight    = 14;
    static constexpr int kPhaseGap       = 4;
    static constexpr int kRateHeight     = 40;
    static constexpr int kRateGap        = 4;
    static constexpr int kMaxPresetWidth = 180;
    static constexpr int kMaxRateWidth   = 130;
    static constexpr int kMaxModeWidth   = 130;
    static constexpr int kIconSize       = 20;
    static constexpr int kShapePreviewWidth = 38;

    bool wasLfoActive = false; // Tracks previous active state for flow trail reset
    juce::Rectangle<int> paintControlsBg; // Dark background behind paint controls
    bool isSyncingGraph = false; // Guard against re-entrant onNodesChanged during programmatic sync

    // --- Internal methods ---
    void syncGraphToActiveLfo();
    void syncActiveLfoFromGraph();
    void cyclePreset(int direction);
    void applyPreset(LfoPreset preset);
    void updatePresetLabel();
    void applyLfoConstraints(int nodeIndex, double& time, double& value);
    void showPaintShapeMenu();
    void copyWaveformToClipboard();
    void pasteWaveformFromClipboard();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LfoComponent)
};
