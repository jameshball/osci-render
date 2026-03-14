#include "LfoComponent.h"
#include "../EffectComponent.h"
#include "../../PluginProcessor.h"
#include "../../LookAndFeel.h"

// ============================================================================
// PresetSelector – Vital-style dark bar with arrows
// ============================================================================

LfoComponent::PresetSelector::PresetSelector() {
    setRepaintsOnMouseActivity(true);
}

void LfoComponent::PresetSelector::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();

    g.setColour(Colours::veryDark);
    g.fillRoundedRectangle(bounds, 4.0f);

    bool hovering = isMouseOver(true);
    g.setColour(juce::Colours::white.withAlpha(hovering ? 0.12f : 0.06f));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 4.0f, 1.0f);

    g.setColour(juce::Colours::white.withAlpha(0.7f));
    auto arrowFont = juce::Font(14.0f, juce::Font::bold);
    g.setFont(arrowFont);
    g.drawText("<", leftArrowArea.toFloat(), juce::Justification::centred);
    g.drawText(">", rightArrowArea.toFloat(), juce::Justification::centred);

    g.setColour(juce::Colours::white.withAlpha(0.9f));
    g.setFont(juce::Font(12.0f, juce::Font::bold));
    auto textArea = bounds.toNearestInt();
    textArea.removeFromLeft(leftArrowArea.getWidth());
    textArea.removeFromRight(rightArrowArea.getWidth());
    g.drawText(presetName, textArea.toFloat(), juce::Justification::centred);
}

void LfoComponent::PresetSelector::mouseDown(const juce::MouseEvent& e) {
    if (leftArrowArea.contains(e.getPosition()) && onPrev)
        onPrev();
    else if (rightArrowArea.contains(e.getPosition()) && onNext)
        onNext();
}

void LfoComponent::PresetSelector::mouseMove(const juce::MouseEvent& e) {
    if (leftArrowArea.contains(e.getPosition()) || rightArrowArea.contains(e.getPosition()))
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    else
        setMouseCursor(juce::MouseCursor::NormalCursor);
}

void LfoComponent::PresetSelector::resized() {
    auto bounds = getLocalBounds();
    leftArrowArea = bounds.removeFromLeft(28);
    rightArrowArea = bounds.removeFromRight(28);
}

void LfoComponent::PresetSelector::setPresetName(const juce::String& name) {
    presetName = name;
    repaint();
}

// ============================================================================
// LfoComponent
// ============================================================================

juce::Colour LfoComponent::getLfoColour(int lfoIndex) {
    static const juce::Colour colours[] = {
        juce::Colour(0xFF00E5FF), // 0 Cyan
        juce::Colour(0xFFFF79C6), // 1 Pink
        juce::Colour(0xFF50FA7B), // 2 Green
        juce::Colour(0xFFFFB86C), // 3 Orange
        juce::Colour(0xFFBD93F9), // 4 Purple
        juce::Colour(0xFFF1FA8C), // 5 Yellow
        juce::Colour(0xFFFF5555), // 6 Red
        juce::Colour(0xFFFFFFFF), // 7 White
    };
    return colours[juce::jlimit(0, (int)std::size(colours) - 1, lfoIndex)];
}

static ModulationSourceConfig buildLfoConfig(OscirenderAudioProcessor& proc) {
    ModulationSourceConfig cfg;
    cfg.sourceCount = NUM_LFOS;
    cfg.dragPrefix = "LFO";
    cfg.getLabel = [](int i) { return "LFO " + juce::String(i + 1); };
    cfg.getSourceColour = &LfoComponent::getLfoColour;
    cfg.getCurrentValue = [&proc](int i) { return proc.getLfoCurrentValue(i); };
    cfg.isSourceActive = [&proc](int i) { return proc.isLfoActive(i); };
    cfg.getAssignments = [&proc]() { return proc.getLfoAssignments(); };
    cfg.addAssignment = [&proc](const ModAssignment& a) { proc.addLfoAssignment(a); };
    cfg.removeAssignment = [&proc](int idx, const juce::String& pid) { proc.removeLfoAssignment(idx, pid); };
    cfg.getParamDisplayName = [&proc](const juce::String& pid) -> juce::String {
        return proc.getParamDisplayName(pid);
    };
    cfg.broadcaster = &proc.broadcaster;
    cfg.getActiveTab = [&proc]() { return proc.activeLfoTab; };
    cfg.setActiveTab = [&proc](int i) { proc.activeLfoTab = i; };
    return cfg;
}

static ModulationRateConfig buildLfoRateConfig(OscirenderAudioProcessor& proc) {
    ModulationRateConfig cfg;
    cfg.getRateParam = [&proc](int i) -> osci::FloatParameter* { return proc.lfoRate[i]; };
    cfg.getRateMode = [&proc](int i) { return proc.getLfoRateMode(i); };
    cfg.setRateMode = [&proc](int i, LfoRateMode m) { proc.setLfoRateMode(i, m); };
    cfg.getTempoDivision = [&proc](int i) { return proc.getLfoTempoDivision(i); };
    cfg.setTempoDivision = [&proc](int i, int d) { proc.setLfoTempoDivision(i, d); };
    cfg.getCurrentBpm = [&proc]() { return proc.currentBpm.load(std::memory_order_relaxed); };
    cfg.maxIndex = NUM_LFOS;
    return cfg;
}

static ModulationModeConfig buildLfoModeConfig(OscirenderAudioProcessor& proc) {
    ModulationModeConfig cfg;
    cfg.modes = getAllLfoModePairs();
    cfg.getMode = [&proc](int i) { return static_cast<int>(proc.getLfoMode(i)); };
    cfg.setMode = [&proc](int i, int m) { proc.setLfoMode(i, static_cast<LfoMode>(m)); };
    cfg.labelText = "MODE";
    cfg.maxIndex = NUM_LFOS;
    return cfg;
}

LfoComponent::LfoComponent(OscirenderAudioProcessor& processor)
    : ModulationSourceComponent(buildLfoConfig(processor)),
      audioProcessor(processor),
      rateControl(buildLfoRateConfig(processor), 0),
      modeControl(buildLfoModeConfig(processor), 0) {
    // Initialize all LFOs with default triangle preset
    for (int i = 0; i < NUM_LFOS; ++i) {
        lfoData[i].preset = LfoPreset::Triangle;
        lfoData[i].waveform = createLfoPreset(LfoPreset::Triangle);
    }

    // Setup graph
    graph.setDomainRange(0.0, 1.0);
    graph.setValueRange(0.0, 1.0);
    graph.setNodeLimits(2, 32);
    graph.setGridDisplay(true, false);
    graph.setAllowNodeAddRemove(true);
    graph.setHoverThreshold(20.0f);
    graph.setCornerRadius(6.0f);
    graph.setFlowTrailWrapping(true, 0.0, 1.0);

    graph.constrainNode = [this](int nodeIndex, double& time, double& value) {
        applyLfoConstraints(nodeIndex, time, value);
    };
    graph.hasCurveHandle = [](int) { return true; };
    graph.onNodesChanged = [this]() { syncActiveLfoFromGraph(); };
    addAndMakeVisible(graph);

    // Preset selector
    presetSelector.onPrev = [this]() { cyclePreset(-1); };
    presetSelector.onNext = [this]() { cyclePreset(1); };
    addAndMakeVisible(presetSelector);

    // Rate control
    rateControl.setSourceIndex(getActiveSourceIndex());
    addAndMakeVisible(rateControl);

    // Mode control
    modeControl.setSourceIndex(getActiveSourceIndex());
    addAndMakeVisible(modeControl);

    // Phase slider
    phaseSlider.setDefaultValue(0.0f);
    phaseSlider.onValueChanged = [this](float val) {
        int idx = getActiveSourceIndex();
        audioProcessor.setLfoPhaseOffset(idx, val);
    };
    addAndMakeVisible(phaseSlider);

    // Restore state
    syncFromProcessorState();
}

void LfoComponent::timerCallback() {
    ModulationSourceComponent::timerCallback();

    int idx = getActiveSourceIndex();
    bool active = audioProcessor.isLfoActive(idx);

    if (active) {
        // Reset the flow trail when transitioning from inactive to active, or when
        // the LFO was retriggered (new note played while already running) so the
        // old trail doesn't linger at the previous position
        if (!wasLfoActive || audioProcessor.consumeLfoRetriggered(idx))
            graph.resetFlowTrail();

        double phase = (double)audioProcessor.getLfoCurrentPhase(idx);
        graph.setFlowMarkerDomainPositions(&phase, 1);
    } else {
        graph.clearFlowMarkers();
    }

    wasLfoActive = active;
}

void LfoComponent::resized() {
    ModulationSourceComponent::resized();

    auto bounds = getContentBounds();

    auto topBar = bounds.removeFromTop(kTopBarHeight);
    bounds.removeFromTop(kTopBarGap);
    int presetW = juce::jmin(kMaxPresetWidth, topBar.getWidth() / 3);
    presetSelector.setBounds(topBar.removeFromRight(presetW));

    // Bottom row: mode + rate controls
    auto bottomRow = bounds.removeFromBottom(kRateHeight);
    bounds.removeFromBottom(kRateGap);

    // Phase slider above the bottom row
    auto phaseRow = bounds.removeFromBottom(kPhaseHeight);
    bounds.removeFromBottom(kPhaseGap);
    phaseSlider.setBounds(phaseRow);

    // Layout mode and rate side by side
    int modeW = juce::jmin(kMaxModeWidth, bottomRow.getWidth() / 3);
    int rateW = juce::jmin(kMaxRateWidth, bottomRow.getWidth() / 3);
    modeControl.setBounds(bottomRow.removeFromLeft(modeW));
    bottomRow.removeFromLeft(kRateGap);
    rateControl.setBounds(bottomRow.removeFromLeft(rateW));

    graph.setBounds(bounds);
    setOutlineBounds(graph.getBounds());
}

void LfoComponent::onActiveSourceChanged(int index) {
    syncGraphToActiveLfo();
    updatePresetLabel();
    graph.resetFlowTrail();
    rateControl.setSourceIndex(index);
    rateControl.syncFromProcessor();
    modeControl.setSourceIndex(index);
    modeControl.syncFromProcessor();
    phaseSlider.setValue(audioProcessor.getLfoPhaseOffset(index));
    phaseSlider.setAccentColour(getLfoColour(index));
}

void LfoComponent::syncGraphToActiveLfo() {
    int idx = getActiveSourceIndex();
    auto& waveform = lfoData[idx].waveform;
    graph.setNodes(waveform.nodes);

    auto colour = getLfoColour(idx);
    graph.setColour(NodeGraphComponent::lineColourId, colour);
    graph.setColour(NodeGraphComponent::fillColourId, colour.withAlpha(0.15f));
    graph.setColour(NodeGraphComponent::nodeColourId, colour);
}

void LfoComponent::syncActiveLfoFromGraph() {
    int idx = getActiveSourceIndex();
    lfoData[idx].waveform.nodes = graph.getNodes();
    lfoData[idx].preset = LfoPreset::Custom;
    updatePresetLabel();

    audioProcessor.lfoWaveformChanged(idx, lfoData[idx].waveform);
    audioProcessor.lfoPresets[idx] = LfoPreset::Custom;
}

void LfoComponent::setLfoWaveform(int lfoIndex, const LfoWaveform& waveform) {
    if (lfoIndex < 0 || lfoIndex >= NUM_LFOS) return;
    lfoData[lfoIndex].waveform = waveform;
    if (lfoIndex == getActiveSourceIndex())
        syncGraphToActiveLfo();
}

LfoWaveform LfoComponent::getLfoWaveform(int lfoIndex) const {
    if (lfoIndex < 0 || lfoIndex >= NUM_LFOS) return {};
    return lfoData[lfoIndex].waveform;
}

void LfoComponent::setLfoPreset(int lfoIndex, LfoPreset preset) {
    if (lfoIndex < 0 || lfoIndex >= NUM_LFOS) return;
    lfoData[lfoIndex].preset = preset;
    lfoData[lfoIndex].waveform = createLfoPreset(preset);
    if (lfoIndex == getActiveSourceIndex()) {
        syncGraphToActiveLfo();
        updatePresetLabel();
    }
    audioProcessor.lfoWaveformChanged(lfoIndex, lfoData[lfoIndex].waveform);
}

LfoPreset LfoComponent::getLfoPreset(int lfoIndex) const {
    if (lfoIndex < 0 || lfoIndex >= NUM_LFOS) return LfoPreset::Custom;
    return lfoData[lfoIndex].preset;
}

void LfoComponent::cyclePreset(int direction) {
    static const std::array<LfoPreset, 6> presets = {
        LfoPreset::Sine, LfoPreset::Triangle, LfoPreset::Sawtooth, LfoPreset::ReverseSawtooth, LfoPreset::Square, LfoPreset::Custom
    };

    int idx = getActiveSourceIndex();
    auto current = lfoData[idx].preset;
    int currentIdx = -1;
    for (int i = 0; i < (int)presets.size(); ++i) {
        if (presets[i] == current) { currentIdx = i; break; }
    }
    if (currentIdx < 0) currentIdx = (direction > 0) ? -1 : 0;
    int newIdx = (currentIdx + direction + (int)presets.size()) % (int)presets.size();
    applyPreset(presets[newIdx]);
}

void LfoComponent::applyPreset(LfoPreset preset) {
    int idx = getActiveSourceIndex();
    if (lfoData[idx].preset == LfoPreset::Custom && preset != LfoPreset::Custom)
        lfoData[idx].customWaveform = lfoData[idx].waveform;

    lfoData[idx].preset = preset;

    if (preset == LfoPreset::Custom && !lfoData[idx].customWaveform.nodes.empty())
        lfoData[idx].waveform = lfoData[idx].customWaveform;
    else
        lfoData[idx].waveform = createLfoPreset(preset);

    syncGraphToActiveLfo();
    updatePresetLabel();
    audioProcessor.lfoWaveformChanged(idx, lfoData[idx].waveform);
    audioProcessor.lfoPresets[idx] = preset;
}

void LfoComponent::updatePresetLabel() {
    presetSelector.setPresetName(lfoPresetToString(lfoData[getActiveSourceIndex()].preset));
}

void LfoComponent::syncFromProcessorState() {
    for (int i = 0; i < NUM_LFOS; ++i) {
        lfoData[i].waveform = audioProcessor.getLfoWaveform(i);
        lfoData[i].preset = audioProcessor.lfoPresets[i];
    }

    ModulationSourceComponent::syncFromProcessorState();

    syncGraphToActiveLfo();
    updatePresetLabel();

    rateControl.setSourceIndex(getActiveSourceIndex());
    rateControl.syncFromProcessor();
    modeControl.setSourceIndex(getActiveSourceIndex());
    modeControl.syncFromProcessor();
    phaseSlider.setValue(audioProcessor.getLfoPhaseOffset(getActiveSourceIndex()));
    phaseSlider.setAccentColour(getLfoColour(getActiveSourceIndex()));
}

void LfoComponent::applyLfoConstraints(int nodeIndex, double& time, double& value) {
    auto graphNodes = graph.getNodes();
    int numNodes = (int)graphNodes.size();

    if (nodeIndex == 0)
        time = 0.0;
    if (nodeIndex == numNodes - 1)
        time = 1.0;
    if (nodeIndex == numNodes - 1 && numNodes > 1)
        value = graphNodes[0].value;
    if (nodeIndex == 0 && numNodes > 1)
        graph.setLastNodeValue(value);

    time = juce::jlimit(0.0, 1.0, time);
    value = juce::jlimit(0.0, 1.0, value);
}

void LfoComponent::setMidiEnabled(bool enabled) {
    if (enabled) {
        modeControl.setModes(getAllLfoModePairs());
    } else {
        // Only Free mode when MIDI is off
        modeControl.setModes({
            { static_cast<int>(LfoMode::Free), "Free" },
        });
        // Force Free mode on ALL LFO sources, not just the visible one
        for (int i = 0; i < NUM_LFOS; ++i) {
            if (audioProcessor.getLfoMode(i) != LfoMode::Free)
                audioProcessor.setLfoMode(i, LfoMode::Free);
        }
    }
}
