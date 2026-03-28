#pragma once

#include <JuceHeader.h>
#include "NodeGraphComponent.h"
#include "ModulationRateComponent.h"
#include "ModulationModeComponent.h"
#include "LfoPresetBrowserOverlay.h"
#include "../../audio/modulation/LfoState.h"
#include "../../audio/modulation/LfoPresetManager.h"
#include "ModulationSourceComponent.h"
#include "../PhaseSliderComponent.h"
#include "../SvgButton.h"
#include "../KnobContainerComponent.h"

class OscirenderAudioProcessor;

// LFO panel: subclass of ModulationSourceComponent that adds a waveform
// graph editor, preset selector, and rate knob.
class LfoComponent : public ModulationSourceComponent,
                     public LfoPresetBrowserOverlay::Listener {
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

    // LfoPresetBrowserOverlay::Listener
    void presetBrowserFactorySelected(LfoPreset preset) override;
    void presetBrowserUserSelected(const juce::File& file) override;
    void presetBrowserUserDeleted(const juce::File& file) override;
    void presetBrowserSaveRequested(const juce::String& name) override;
    void presetBrowserImportRequested() override;

protected:
    void onActiveSourceChanged(int newIndex) override;

private:
    OscirenderAudioProcessor& audioProcessor;

    // Per-LFO state
    struct LfoData {
        LfoWaveform waveform;
        LfoWaveform customWaveform;
        LfoPreset preset = LfoPreset::Triangle;
        juce::String userPresetName; // Non-empty when a user preset is loaded
    };
    std::array<LfoData, NUM_LFOS> lfoData;

    // --- Child Components ---
    NodeGraphComponent graph;

    class PresetSelector : public juce::Component, public juce::SettableTooltipClient {
    public:
        PresetSelector();
        void paint(juce::Graphics& g) override;
        void mouseDown(const juce::MouseEvent& e) override;
        void resized() override;
        void setPresetName(const juce::String& name);
        std::function<void()> onPrev;
        std::function<void()> onNext;
        std::function<void()> onNameClick;
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

    // Custom S-curve toggle for smooth/straight interpolation mode.
    class SmoothToggle : public juce::Component, public juce::SettableTooltipClient {
    public:
        SmoothToggle();
        void paint(juce::Graphics& g) override;
        void mouseDown(const juce::MouseEvent& e) override;
        void setSmooth(bool b) { smooth = b; repaint(); }
        bool isSmooth() const { return smooth; }
        void setAccentColour(juce::Colour c) { accent = c; repaint(); }
        std::function<void(bool)> onToggle;
    private:
        bool smooth = true;
        juce::Colour accent { juce::Colours::transparentBlack };
    };

    SmoothToggle smoothToggle;
    PaintShapePreview shapePreview;
    SvgButton copyButton;
    SvgButton pasteButton;
    LfoPresetManager presetManager;
    std::unique_ptr<juce::FileChooser> fileChooser;
    juce::Component::SafePointer<LfoPresetBrowserOverlay> presetBrowserOverlay;
    ModulationRateComponent rateControl;
    ModulationModeComponent modeControl;
    PhaseSliderComponent phaseSlider;
    KnobContainerComponent smoothKnob { "SMOOTH" };
    KnobContainerComponent delayKnob { "DELAY" };

    // --- Layout constants ---
    static constexpr int kTopBarHeight   = 20;
    static constexpr int kTopBarGap      = 4;
    static constexpr int kPhaseHeight    = 8;
    static constexpr int kPhaseGap       = 4;
    static constexpr int kRateHeight     = 48;
    static constexpr int kRateGap        = 4;
    static constexpr int kMaxPresetWidth = 180;
    static constexpr int kMaxRateWidth   = 130;
    static constexpr int kMaxModeWidth   = 130;
    static constexpr int kMaxKnobWidth   = 120;
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
    void configureKnob(KnobContainerComponent& container, double maxVal, double skewCentre,
                        double defaultVal, const juce::String& suffix,
                        std::function<void(float)> onChange);
    void showPaintShapeMenu();
    void showPresetBrowser();
    void dismissPresetBrowser();
    void importVitalLfo();
    void loadUserPreset(const juce::File& file);
    void copyWaveformToClipboard();
    void pasteWaveformFromClipboard();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LfoComponent)
};
