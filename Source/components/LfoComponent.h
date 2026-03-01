#pragma once

#include <JuceHeader.h>
#include "NodeGraphComponent.h"
#include "../audio/LfoState.h"

class OscirenderAudioProcessor;

// The full LFO panel: vertical tab/drag-handles (LFO 1-4) with per-connection
// depth indicators, a waveform graph editor, preset selector, and rate knob.
// Each LFO tab is itself a drag source for creating new parameter connections.
class LfoComponent : public juce::Component, public juce::Timer, public juce::ChangeListener {
public:
    LfoComponent(OscirenderAudioProcessor& processor);
    ~LfoComponent() override;

    void resized() override;
    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void timerCallback() override;
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

    // Get the current active LFO index (0-3).
    int getActiveLfoIndex() const { return activeLfoIndex; }

    // Set nodes for the active LFO (called when loading state).
    void setLfoWaveform(int lfoIndex, const LfoWaveform& waveform);
    LfoWaveform getLfoWaveform(int lfoIndex) const;

    // Set/get preset for an LFO.
    void setLfoPreset(int lfoIndex, LfoPreset preset);
    LfoPreset getLfoPreset(int lfoIndex) const;

    // Callback invoked when a drag starts/ends from an LFO tab.
    // Use this to highlight drop targets externally.
    std::function<void(bool isDragging)> onDragActiveChanged;

    // LFO colour per index (for visual distinction).
    static juce::Colour getLfoColour(int lfoIndex);

    // Force-refresh all depth indicators from processor assignments.
    void refreshAllDepthIndicators();

    // Sync UI state from processor after a state load (presets, waveforms, active tab).
    void syncFromProcessorState();

private:
    OscirenderAudioProcessor& audioProcessor;

    // Per-LFO state
    struct LfoData {
        LfoWaveform waveform;
        LfoWaveform customWaveform;  // preserved when switching away from Custom
        LfoPreset preset = LfoPreset::Triangle;
    };
    std::array<LfoData, NUM_LFOS> lfoData;

    int activeLfoIndex = 0;

    // --- Child Components ---

    // Vital-style arc knob for one LFO-to-parameter connection.
    class DepthIndicator : public juce::Component {
    public:
        DepthIndicator(LfoComponent& owner, int lfoIndex, juce::String paramId, float depth, bool bipolar);
        void paint(juce::Graphics& g) override;
        void mouseDown(const juce::MouseEvent& e) override;
        void mouseDrag(const juce::MouseEvent& e) override;
        void mouseUp(const juce::MouseEvent& e) override;
        void mouseEnter(const juce::MouseEvent& e) override;
        void mouseExit(const juce::MouseEvent& e) override;
        void mouseDoubleClick(const juce::MouseEvent& e) override;

        void showRightClickMenu();
        void showValuePopup();
        void hideValuePopup();
        void startTextEdit();

        LfoComponent& owner;
        int lfoIndex;
        juce::String paramId;
        float depth; // 0..1 unipolar, -1..1 bipolar
        bool bipolar = false;
        bool hovering = false;
        bool dragging = false;
        float dragStartDepth = 0.0f;

        std::unique_ptr<juce::Label> valuePopup;
        std::unique_ptr<juce::TextEditor> inlineEditor;
    };

    // Tab/drag-handle for a single LFO. The label text IS the drag source,
    // and depth indicators for connections are laid out inside.
    class LfoTabHandle : public juce::Component {
    public:
        LfoTabHandle(const juce::String& label, int index, LfoComponent& owner);
        void paint(juce::Graphics& g) override;
        void mouseDown(const juce::MouseEvent& e) override;
        void mouseDrag(const juce::MouseEvent& e) override;
        void mouseUp(const juce::MouseEvent& e) override;
        void resized() override;

        // Rebuild depth indicators from current assignments.
        void refreshDepthIndicators();

        // Update the LFO value indicator (called from timer)
        void setLfoValue(float value01) {
            float delta = value01 - lfoValue;
            lfoDelta = delta;
            lfoValue = value01;
            repaint();
        }

        juce::String label;
        int lfoIndex;
        LfoComponent& owner;
        bool isDragging = false;
        float lfoValue = 0.0f;  // current LFO output 0..1
        float lfoDelta = 0.0f;  // change since last update

        juce::OwnedArray<DepthIndicator> depthIndicators;
    };
    juce::OwnedArray<LfoTabHandle> tabHandles;

    // The waveform graph
    NodeGraphComponent graph;

    // Vital-style preset selector bar
    class PresetSelector : public juce::Component {
    public:
        PresetSelector();
        void paint(juce::Graphics& g) override;
        void mouseDown(const juce::MouseEvent& e) override;
        void resized() override;

        void setPresetName(const juce::String& name);
        std::function<void()> onPrev;
        std::function<void()> onNext;
    private:
        juce::String presetName;
        juce::Rectangle<int> leftArrowArea, rightArrowArea;
    };
    PresetSelector presetSelector;
    juce::Slider rateSlider;
    juce::Label rateLabel;

    // --- Internal methods ---
    void switchToLfo(int index);
    void syncGraphToActiveLfo();
    void syncActiveLfoFromGraph();
    void cyclePreset(int direction);
    void applyPreset(LfoPreset preset);
    void updatePresetLabel();
    void applyLfoConstraints(int nodeIndex, double& time, double& value);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LfoComponent)
};
