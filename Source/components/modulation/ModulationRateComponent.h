#pragma once

#include <JuceHeader.h>
#include "../../audio/LfoState.h"
#include "../ValuePopupHelper.h"

class OscirenderAudioProcessor;

// Configuration for a generic modulation rate control.
// All processor interaction happens through these callbacks.
struct ModulationRateConfig {
    // Get/set the rate parameter for a given source index
    std::function<osci::FloatParameter*(int)> getRateParam;

    // Get/set the rate mode (Hz, Tempo, etc.)
    std::function<LfoRateMode(int)> getRateMode;
    std::function<void(int, LfoRateMode)> setRateMode;

    // Get/set the tempo division index
    std::function<int(int)> getTempoDivision;
    std::function<void(int, int)> setTempoDivision;

    // Get the current BPM
    std::function<double()> getCurrentBpm;

    // Max valid source index (for clamping)
    int maxIndex = 1;
};

// Vital-inspired multi-mode rate control.
// Supports: Hz (seconds), Tempo, Tempo Dotted, Tempo Triplets.
// Mode is chosen via a popup triggered by clicking the mode icon on the right.
// Generic version — works for LFO, Random, or any rate-based modulator.
class ModulationRateComponent : public juce::Component {
public:
    ModulationRateComponent(const ModulationRateConfig& config, int sourceIndex);
    ~ModulationRateComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseEnter(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

    // Switch which source this control targets.
    void setSourceIndex(int index);
    int getSourceIndex() const { return sourceIndex; }

    // Get/set the current rate mode.
    LfoRateMode getRateMode() const { return rateMode; }
    void setRateMode(LfoRateMode mode);

    // Get/set the current tempo division index (for tempo modes).
    int getTempoDivisionIndex() const { return tempoDivisionIndex; }
    void setTempoDivisionIndex(int index);

    // Sync the display from the processor's current parameter value.
    void syncFromProcessor();

    // Get the effective rate in Hz (accounting for mode and BPM).
    double getEffectiveRateHz() const;

    // Get the current BPM.
    double getCurrentBpm() const;

    // Display string for the current value.
    juce::String getDisplayText() const;

    // Label text ("FREQUENCY" for Hz, "TEMPO" for tempo modes).
    juce::String getLabelText() const;

private:
    ModulationRateConfig config;
    int sourceIndex = 0;

    LfoRateMode rateMode = LfoRateMode::Seconds;
    int tempoDivisionIndex = 8; // Default: 1/4

    // Drag state
    float dragStartValue = 0.0f;
    int dragStartDivision = 0;
    int dragStartY = 0;
    bool isDragging = false;

    // Inline text editor for direct entry
    std::unique_ptr<juce::TextEditor> inlineEditor;

    ValuePopupHelper valuePopup;

    // Hit areas
    juce::Rectangle<int> valueArea;
    juce::Rectangle<int> modeButtonArea;
    juce::Rectangle<int> labelArea;

    void showModePopup();
    void showInlineEditor();
    void commitInlineEditor();

    // Push the current Hz rate to the processor parameter.
    void pushRateToProcessor(float hz);

    // Draw the mode icon into the given area.
    void drawModeIcon(juce::Graphics& g, juce::Rectangle<float> area) const;

    // Individual icon drawing helpers
    static void drawHzIcon(juce::Graphics& g, juce::Rectangle<float> area);
    static void drawNoteIcon(juce::Graphics& g, juce::Rectangle<float> area);
    static void drawDottedNoteIcon(juce::Graphics& g, juce::Rectangle<float> area);
    static void drawTripletNoteIcon(juce::Graphics& g, juce::Rectangle<float> area);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModulationRateComponent)
};
