#include "LfoComponent.h"
#include "../effects/EffectComponent.h"
#include "../../PluginProcessor.h"
#include "../../CommonPluginEditor.h"
#include "../../LookAndFeel.h"

// ============================================================================
// PresetSelector – Vital-style dark bar with arrows
// ============================================================================

LfoComponent::PresetSelector::PresetSelector() {
    setRepaintsOnMouseActivity(true);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void LfoComponent::PresetSelector::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();

    g.setColour(Colours::evenDarker());
    g.fillRoundedRectangle(bounds, Colours::kPillRadius);

    // Left chevron
    {
        auto area = leftArrowArea.toFloat();
        float cx = area.getCentreX(), cy = area.getCentreY();
        float hw = 3.0f, hh = 4.5f;
        juce::Path chevron;
        chevron.startNewSubPath(cx + hw, cy - hh);
        chevron.lineTo(cx - hw, cy);
        chevron.lineTo(cx + hw, cy + hh);
        g.setColour(juce::Colours::white.withAlpha(0.7f));
        g.strokePath(chevron, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));
    }

    // Right chevron
    {
        auto area = rightArrowArea.toFloat();
        float cx = area.getCentreX(), cy = area.getCentreY();
        float hw = 3.0f, hh = 4.5f;
        juce::Path chevron;
        chevron.startNewSubPath(cx - hw, cy - hh);
        chevron.lineTo(cx + hw, cy);
        chevron.lineTo(cx - hw, cy + hh);
        g.setColour(juce::Colours::white.withAlpha(0.7f));
        g.strokePath(chevron, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));
    }

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
    else if (onNameClick)
        onNameClick();
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
// PaintShapePreview – mini waveform + click to open menu
// ============================================================================

LfoComponent::PaintShapePreview::PaintShapePreview() {
    setRepaintsOnMouseActivity(true);
}

void LfoComponent::PaintShapePreview::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat().reduced(2.0f);
    bool hovering = isMouseOver(true);

    // Draw the shape waveform
    juce::Path path;
    float x0 = bounds.getX() + 2.0f;
    float x1 = bounds.getRight() - 2.0f;
    float yBottom = bounds.getBottom() - 2.0f;
    float yTop = bounds.getY() + 2.0f;
    float width = x1 - x0;
    float height = yBottom - yTop;

    // Always start at zero
    path.startNewSubPath(x0, yBottom);

    int steps = 48;
    for (int i = 0; i <= steps; ++i) {
        float t = (float)i / (float)steps;
        float val = NodeGraphComponent::evaluatePaintShape(shape, t);
        float x = x0 + t * width;
        float y = yBottom - val * height;
        path.lineTo(x, y);
    }

    // Always end at zero
    path.lineTo(x1, yBottom);

    juce::Colour lineColour = paintEnabled ? accent : juce::Colours::white.withAlpha(0.5f);
    g.setColour(lineColour.withAlpha(hovering ? 1.0f : (paintEnabled ? 0.8f : 0.5f)));
    g.strokePath(path, juce::PathStrokeType(2.0f));
}

void LfoComponent::PaintShapePreview::mouseDown(const juce::MouseEvent&) {
    if (onClick) onClick();
}

// ============================================================================
// SmoothToggle – S-curve / straight line toggle icon
// ============================================================================

LfoComponent::SmoothToggle::SmoothToggle() {
    setRepaintsOnMouseActivity(true);
}

void LfoComponent::SmoothToggle::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat().reduced(3.0f);
    bool hovering = isMouseOver(true);

    if (hovering) {
        g.setColour(juce::Colours::white.withAlpha(0.08f));
        g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 3.0f);
    }

    juce::Path path;
    float x0 = bounds.getX();
    float x1 = bounds.getRight();
    float yTop = bounds.getY();
    float yBot = bounds.getBottom();

    if (smooth) {
        // S-shaped smooth curve
        path.startNewSubPath(x0, yBot);
        path.cubicTo(x0 + (x1 - x0) * 0.5f, yBot,
                     x0 + (x1 - x0) * 0.5f, yTop,
                     x1, yTop);
    } else {
        // Straight diagonal line
        path.startNewSubPath(x0, yBot);
        path.lineTo(x1, yTop);
    }

    float alpha = hovering ? 1.0f : 0.65f;
    g.setColour(smooth ? accent.withAlpha(alpha) : juce::Colours::white.withAlpha(alpha * 0.7f));
    g.strokePath(path, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved));
}

void LfoComponent::SmoothToggle::mouseDown(const juce::MouseEvent&) {
    smooth = !smooth;
    repaint();
    if (onToggle) onToggle(smooth);
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
    cfg.getCurrentValue = [&proc](int i) { return proc.lfoParameters.getCurrentValue(i); };
    cfg.isSourceActive = [&proc](int i) { return proc.lfoParameters.isActive(i); };
    cfg.getAssignments = [&proc]() { return proc.lfoParameters.getAssignments(); };
    cfg.addAssignment = [&proc](const ModAssignment& a) { proc.lfoParameters.addAssignment(a); };
    cfg.removeAssignment = [&proc](int idx, const juce::String& pid) { proc.lfoParameters.removeAssignment(idx, pid); };
    cfg.getParamDisplayName = [&proc](const juce::String& pid) -> juce::String {
        return proc.getParamDisplayName(pid);
    };
    cfg.broadcaster = &proc.broadcaster;
    cfg.getActiveTab = [&proc]() { return proc.lfoParameters.activeTab; };
    cfg.setActiveTab = [&proc](int i) { proc.lfoParameters.activeTab = i; };
#if OSCI_PREMIUM
    cfg.typeId = "lfo";
    cfg.midiCCManager = &proc.midiCCManager;
    cfg.buildModDepthCustomId = [](int idx, const juce::String& pid) {
        return OscirenderAudioProcessor::modDepthCustomId("lfo", idx, pid);
    };
    cfg.buildModDepthSetter = [&proc](int idx, const juce::String& pid) {
        return proc.buildModDepthSetter("lfo", idx, pid);
    };
#endif
    return cfg;
}

static ModulationRateConfig buildLfoRateConfig(OscirenderAudioProcessor& proc) {
    ModulationRateConfig cfg;
    cfg.getRateParam = [&proc](int i) -> osci::FloatParameter* { return proc.lfoParameters.rate[i]; };
    cfg.getRateMode = [&proc](int i) { return proc.lfoParameters.getRateMode(i); };
    cfg.setRateMode = [&proc](int i, LfoRateMode m) { proc.lfoParameters.setRateMode(i, m); };
    cfg.getTempoDivision = [&proc](int i) { return proc.lfoParameters.getTempoDivision(i); };
    cfg.setTempoDivision = [&proc](int i, int d) { proc.lfoParameters.setTempoDivision(i, d); };
    cfg.getCurrentBpm = [&proc]() { return proc.currentBpm.load(std::memory_order_relaxed); };
    cfg.maxIndex = NUM_LFOS;
    return cfg;
}

static ModulationModeConfig buildLfoModeConfig(OscirenderAudioProcessor& proc) {
    ModulationModeConfig cfg;
    cfg.modes = getAllLfoModePairs();
    cfg.getMode = [&proc](int i) { return static_cast<int>(proc.lfoParameters.getMode(i)); };
    cfg.setMode = [&proc](int i, int m) { proc.lfoParameters.setMode(i, static_cast<LfoMode>(m)); };
    cfg.labelText = "MODE";
    cfg.maxIndex = NUM_LFOS;
    return cfg;
}

LfoComponent::LfoComponent(OscirenderAudioProcessor& processor)
    : ModulationSourceComponent(buildLfoConfig(processor)),
      audioProcessor(processor),
      paintToggle("paint", juce::String(BinaryData::brush_svg, BinaryData::brush_svgSize),
                  juce::Colours::white.withAlpha(0.5f), getLfoColour(0)),
      copyButton("copy", juce::String(BinaryData::copy_svg, BinaryData::copy_svgSize),
                 juce::Colours::white.withAlpha(0.5f), juce::Colours::white.withAlpha(0.5f)),
      pasteButton("paste", juce::String(BinaryData::paste_svg, BinaryData::paste_svgSize),
                  juce::Colours::white.withAlpha(0.5f), juce::Colours::white.withAlpha(0.5f)),
      presetManager(processor.applicationFolder.getChildFile("LFO Presets")),
      presetBrowser(presetManager, this),
      rateControl(buildLfoRateConfig(processor), 0),
      modeControl(buildLfoModeConfig(processor), 0) {
    // Initialize all LFOs with the global default preset (or Triangle if none set)
    LfoPreset defaultPreset = LfoPreset::Triangle;
    LfoWaveform defaultWaveform;
    bool defaultIsFile = false;
    juce::String defaultUserName;

    juce::String defaultFileStr = audioProcessor.getGlobalStringValue("defaultLfoPresetFile");
    juce::String defaultFactoryStr = audioProcessor.getGlobalStringValue("defaultLfoPreset");

    if (defaultFileStr.isNotEmpty()) {
        juce::File file(defaultFileStr);
        if (file.existsAsFile()) {
            LfoWaveform waveform;
            juce::String name;
            if (presetManager.loadPreset(file, waveform, name)) {
                defaultWaveform = waveform;
                defaultUserName = name;
                defaultIsFile = true;
            }
        }
    }
    if (!defaultIsFile && defaultFactoryStr.isNotEmpty()) {
        auto parsed = stringToLfoPreset(defaultFactoryStr);
        if (parsed.has_value())
            defaultPreset = *parsed;
    }

    for (int i = 0; i < NUM_LFOS; ++i) {
        lfoData[i].preset = defaultPreset;
        if (defaultIsFile) {
            lfoData[i].waveform = defaultWaveform;
            lfoData[i].isCustom = true;
            lfoData[i].userPresetName = defaultUserName;
        } else {
            lfoData[i].waveform = createLfoPreset(defaultPreset);
        }
        lfoData[i].factoryWaveform = createLfoPreset(defaultPreset);
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
    graph.setSmoothMode(true);
    graph.setUndoManager(&audioProcessor.getUndoManager());

    graph.constrainNode = [this](int nodeIndex, double& time, double& value) {
        applyLfoConstraints(nodeIndex, time, value);
    };
    graph.hasCurveHandle = [](int) { return true; };
    graph.onNodesChanged = [this]() { if (!isSyncingGraph) syncActiveLfoFromGraph(); };
    addAndMakeVisible(graph);

    // Preset selector
    presetSelector.setTooltip("LFO waveform preset - click to browse, arrows to cycle");
    presetSelector.onPrev = [this]() { cyclePreset(-1); };
    presetSelector.onNext = [this]() { cyclePreset(1); };
    presetSelector.onNameClick = [this]() { showPresetBrowser(); };
    addAndMakeVisible(presetSelector);

    // Paint brush toggle
    paintToggle.setClickingTogglesState(true);
    paintToggle.setTooltip("Paint mode - draw waveform shapes with the brush");
    paintToggle.onClick = [this]() {
        bool on = paintToggle.getToggleState();
        graph.setPaintMode(on);
        shapePreview.setEnabled(on);
        resized();
    };
    addAndMakeVisible(paintToggle);

    // Paint shape preview (always visible, greyed out when paint mode is off)
    shapePreview.setShape(NodeGraphComponent::PaintShape::Step);
    shapePreview.setTooltip("Paint shape - click to change");
    shapePreview.setAccentColour(getLfoColour(0));
    shapePreview.onClick = [this]() { showPaintShapeMenu(); };
    shapePreview.setEnabled(false);
    addAndMakeVisible(shapePreview);

    // Smooth/straight toggle
    smoothToggle.setAccentColour(getLfoColour(0));
    smoothToggle.setTooltip("Toggle smooth / straight line interpolation");
    smoothToggle.onToggle = [this](bool smooth) {
        graph.setSmoothMode(smooth);
        syncActiveLfoFromGraph();
    };
    addAndMakeVisible(smoothToggle);

    // Copy/paste buttons
    copyButton.setTooltip("Copy LFO waveform to clipboard as JSON");
    copyButton.onClick = [this]() { copyWaveformToClipboard(); };
    addAndMakeVisible(copyButton);

    pasteButton.setTooltip("Paste LFO waveform from clipboard JSON");
    pasteButton.onClick = [this]() { pasteWaveformFromClipboard(); };
    addAndMakeVisible(pasteButton);

    // Rate control
    rateControl.setSourceIndex(getActiveSourceIndex());
    addAndMakeVisible(rateControl);

    // Mode control
    modeControl.setSourceIndex(getActiveSourceIndex());
    addAndMakeVisible(modeControl);

    // Phase slider
    phaseSlider.setTextValueSuffix(" phase");
    phaseSlider.onValueChange = [this]() {
        int idx = getActiveSourceIndex();
        audioProcessor.lfoParameters.setPhaseOffset(idx, (float)phaseSlider.getValue());
    };
    addAndMakeVisible(phaseSlider);

    // Smooth amount knob (0-16 seconds, default 0.005, skew midpoint 1.0)
    smoothKnob.bindToParam(audioProcessor.lfoParameters.smoothAmount[0], 1.0, 3);
    smoothKnob.getKnob().setTextValueSuffix(" secs");
    smoothKnob.setAccentColour(getLfoColour(0));
    smoothKnob.wireModulation(audioProcessor);
    addAndMakeVisible(smoothKnob);

    // Delay knob (0-4 seconds, default 0)
    delayKnob.bindToParam(audioProcessor.lfoParameters.delayAmount[0], 0.5, 3);
    delayKnob.getKnob().setTextValueSuffix(" secs");
    delayKnob.setAccentColour(getLfoColour(0));
    delayKnob.wireModulation(audioProcessor);
    addAndMakeVisible(delayKnob);

    // Register as listener on all LFO parameters so undo/redo triggers a UI sync
    for (int i = 0; i < NUM_LFOS; ++i) {
        paramSync.track(audioProcessor.lfoParameters.rate[i]);
        paramSync.track(audioProcessor.lfoParameters.mode[i]);
        paramSync.track(audioProcessor.lfoParameters.phaseOffset[i]);
        paramSync.track(audioProcessor.lfoParameters.smoothAmount[i]);
        paramSync.track(audioProcessor.lfoParameters.delayAmount[i]);
        paramSync.track(audioProcessor.lfoParameters.rateMode[i]);
        paramSync.track(audioProcessor.lfoParameters.tempoDivision[i]);
    }

    // Restore state
    syncFromProcessorState();
}

LfoComponent::~LfoComponent() {
}

void LfoComponent::timerCallback() {
    ModulationSourceComponent::timerCallback();

    int idx = getActiveSourceIndex();
    bool active = audioProcessor.lfoParameters.isActive(idx);

    if (active) {
        // Reset the flow trail when transitioning from inactive to active, or when
        // the LFO was retriggered (new note played while already running) so the
        // old trail doesn't linger at the previous position
        if (!wasLfoActive || audioProcessor.lfoParameters.consumeRetriggered(idx))
            graph.resetFlowTrail();

        double phase = (double)audioProcessor.lfoParameters.getCurrentPhase(idx);
        graph.setFlowMarkerDomainPositions(&phase, 1);
    } else {
        graph.clearFlowMarkers();
    }

    wasLfoActive = active;
}

void LfoComponent::paint(juce::Graphics& g) {
    ModulationSourceComponent::paint(g);

    // Dark rounded background behind paint + preview controls
    if (!paintControlsBg.isEmpty()) {
        g.setColour(Colours::darkerer());
        g.fillRoundedRectangle(paintControlsBg.toFloat(), Colours::kPillRadius);
    }
}

void LfoComponent::resized() {
    ModulationSourceComponent::resized();
    if (isCollapsed()) return;

    auto bounds = getContentBounds();

    auto topBar = bounds.removeFromTop(kTopBarHeight);
    bounds.removeFromTop(kTopBarGap);

    // Top bar layout: [paintToggle][shapePreview] (dark bg) [smoothToggle][gap][presetSelector]
    auto paintControlsLeft = topBar.getX();
    paintToggle.setBounds(topBar.removeFromLeft(kIconSize));

    shapePreview.setBounds(topBar.removeFromLeft(kShapePreviewWidth));
    auto paintControlsRight = topBar.getX();

    // Store background rect for the paint + preview group only
    paintControlsBg = juce::Rectangle<int>(paintControlsLeft, topBar.getY(),
                                            paintControlsRight - paintControlsLeft,
                                            topBar.getHeight());

    topBar.removeFromLeft(6);
    smoothToggle.setBounds(topBar.removeFromLeft(kIconSize));
    topBar.removeFromLeft(6);

    // Copy/paste buttons on the right
    auto pasteArea = topBar.removeFromRight(kIconSize);
    topBar.removeFromRight(2);
    auto copyArea = topBar.removeFromRight(kIconSize);
    topBar.removeFromRight(4);
    copyButton.setBounds(copyArea);
    pasteButton.setBounds(pasteArea);

    presetSelector.setBounds(topBar);

    // Save the full area below the top bar for the preset browser overlay
    presetBrowserBounds = bounds;

    // Bottom row: mode + rate controls
    auto bottomRow = bounds.removeFromBottom(kRateHeight);
    bounds.removeFromBottom(kRateGap);

    // Phase slider above the bottom row
    auto phaseRow = bounds.removeFromBottom(kPhaseHeight);
    bounds.removeFromBottom(kPhaseGap);
    phaseSlider.setBounds(phaseRow);

    // Layout mode and rate side by side, then smooth + delay knobs
    float quarter = (float)(bottomRow.getWidth() - kRateGap * 3) / 4.0f;
    float modeW = juce::jmin((float)kMaxModeWidth, quarter);
    float rateW = juce::jmin((float)kMaxRateWidth, quarter);
    float knobW = juce::jmin((float)kMaxKnobWidth, quarter);
    float totalW = modeW + rateW + knobW * 2.0f + (float)kRateGap * 3.0f;
    int leftPad = juce::roundToInt(((float)bottomRow.getWidth() - totalW) * 0.5f);
    if (leftPad > 0) bottomRow.removeFromLeft(leftPad);

    int startX = bottomRow.getX();
    int y = bottomRow.getY();
    int h = bottomRow.getHeight();
    float cum = 0.0f;
    int gapsSoFar = 0;

    auto nextLfoCol = [&](float w) -> juce::Rectangle<int> {
        int left = startX + juce::roundToInt(cum) + gapsSoFar;
        cum += w;
        int right = startX + juce::roundToInt(cum) + gapsSoFar;
        gapsSoFar += kRateGap;
        return { left, y, right - left, h };
    };

    modeControl.setBounds(nextLfoCol(modeW));
    rateControl.setBounds(nextLfoCol(rateW));
    smoothKnob.setBounds(nextLfoCol(knobW));
    delayKnob.setBounds(nextLfoCol(knobW));

    graph.setBounds(bounds);
    setOutlineBounds(presetBrowserVisible ? juce::Rectangle<int>{} : graph.getBounds());

    if (presetBrowserVisible)
        presetBrowser.setBounds(presetBrowserBounds);
}

void LfoComponent::onActiveSourceChanged(int index) {
    syncGraphToActiveLfo();
    updatePresetLabel();
    graph.resetFlowTrail();
    rateControl.setSourceIndex(index);
    rateControl.syncFromProcessor();
    modeControl.setSourceIndex(index);
    modeControl.syncFromProcessor();
    phaseSlider.setValue(audioProcessor.lfoParameters.getPhaseOffset(index), juce::dontSendNotification);

    auto colour = getLfoColour(index);
    phaseSlider.setAccentColour(colour);
    shapePreview.setAccentColour(colour);
    paintToggle.setOnColour(colour);
    smoothToggle.setAccentColour(colour);
    smoothKnob.setAccentColour(colour);
    smoothKnob.rebindParam(audioProcessor.lfoParameters.smoothAmount[index]);
    delayKnob.setAccentColour(colour);
    delayKnob.rebindParam(audioProcessor.lfoParameters.delayAmount[index]);
}

void LfoComponent::syncGraphToActiveLfo() {
    int idx = getActiveSourceIndex();
    auto& waveform = lfoData[idx].waveform;
    juce::ScopedValueSetter<bool> guard(isSyncingGraph, true);
    graph.setSmoothMode(waveform.smooth);
    graph.setNodes(waveform.nodes);
    smoothToggle.setSmooth(waveform.smooth);

    auto colour = getLfoColour(idx);
    graph.setColour(NodeGraphComponent::lineColourId, colour);
    graph.setColour(NodeGraphComponent::fillColourId, colour.withAlpha(0.15f));
    graph.setColour(NodeGraphComponent::nodeColourId, colour);
}

void LfoComponent::syncActiveLfoFromGraph() {
    int idx = getActiveSourceIndex();
    lfoData[idx].waveform.nodes = graph.getNodes();
    lfoData[idx].waveform.smooth = graph.isSmoothMode();

    // Only mark as custom if the waveform actually differs from the current preset
    if (!lfoData[idx].isCustom) {
        if (lfoData[idx].waveform != lfoData[idx].factoryWaveform) {
            lfoData[idx].isCustom = true;
            lfoData[idx].userPresetName.clear();
        }
    }
    updatePresetLabel();

    audioProcessor.lfoParameters.waveformChanged(idx, lfoData[idx].waveform);
    audioProcessor.lfoParameters.setPreset(idx, lfoData[idx].preset);
    audioProcessor.lfoParameters.setIsCustom(idx, lfoData[idx].isCustom);
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
    lfoData[lfoIndex].isCustom = false;
    auto waveformBefore = audioProcessor.lfoParameters.getWaveform(lfoIndex);
    lfoData[lfoIndex].waveform = createLfoPreset(preset);
    lfoData[lfoIndex].factoryWaveform = lfoData[lfoIndex].waveform;
    audioProcessor.lfoParameters.waveformChanged(lfoIndex, lfoData[lfoIndex].waveform);
    if (lfoIndex == getActiveSourceIndex()) {
        auto nodesBefore = graph.getNodes();
        syncGraphToActiveLfo();
        recordLfoUndoableChangeGuarded(nodesBefore, waveformBefore, lfoIndex);
        updatePresetLabel();
    }
}

LfoPreset LfoComponent::getLfoPreset(int lfoIndex) const {
    if (lfoIndex < 0 || lfoIndex >= NUM_LFOS) return LfoPreset::Triangle;
    return lfoData[lfoIndex].preset;
}

void LfoComponent::cyclePreset(int direction) {
    auto& registry = getLfoPresetRegistry();
    if (registry.empty()) return;

    int idx = getActiveSourceIndex();

    int currentIdx = -1;
    for (int i = 0; i < (int)registry.size(); ++i) {
        if (registry[i].preset == lfoData[idx].preset) {
            currentIdx = i;
            break;
        }
    }
    if (currentIdx < 0) currentIdx = (direction > 0) ? -1 : 0;

    int newIdx = (currentIdx + direction + (int)registry.size()) % (int)registry.size();
    applyPreset(registry[newIdx].preset);
}

void LfoComponent::applyPreset(LfoPreset preset) {
    int idx = getActiveSourceIndex();
    if (lfoData[idx].isCustom)
        lfoData[idx].customWaveform = lfoData[idx].waveform;

    lfoData[idx].preset = preset;
    lfoData[idx].isCustom = false;
    lfoData[idx].userPresetName.clear();

    lfoData[idx].waveform = createLfoPreset(preset);
    lfoData[idx].factoryWaveform = lfoData[idx].waveform;

    // Disable paint mode when switching to a preset
    if (paintToggle.getToggleState()) {
        paintToggle.setToggleState(false, juce::dontSendNotification);
        graph.setPaintMode(false);
        shapePreview.setEnabled(false);
    }

    auto waveformBefore = audioProcessor.lfoParameters.getWaveform(idx);
    audioProcessor.lfoParameters.waveformChanged(idx, lfoData[idx].waveform);
    auto nodesBefore = graph.getNodes();
    syncGraphToActiveLfo();

    recordLfoUndoableChangeGuarded(nodesBefore, waveformBefore, idx);

    updatePresetLabel();
    audioProcessor.lfoParameters.setPreset(idx, preset);
    audioProcessor.lfoParameters.setIsCustom(idx, false);
}

void LfoComponent::updatePresetLabel() {
    int idx = getActiveSourceIndex();
    if (lfoData[idx].userPresetName.isNotEmpty())
        presetSelector.setPresetName(lfoData[idx].userPresetName);
    else if (lfoData[idx].isCustom)
        presetSelector.setPresetName("Custom");
    else
        presetSelector.setPresetName(lfoPresetToString(lfoData[idx].preset));
}

void LfoComponent::syncFromProcessorState() {
    for (int i = 0; i < NUM_LFOS; ++i) {
        lfoData[i].waveform = audioProcessor.lfoParameters.getWaveform(i);
        lfoData[i].preset = audioProcessor.lfoParameters.getPreset(i);
        lfoData[i].factoryWaveform = createLfoPreset(lfoData[i].preset);
        lfoData[i].isCustom = (lfoData[i].waveform != lfoData[i].factoryWaveform);
    }

    ModulationSourceComponent::syncFromProcessorState();

    syncGraphToActiveLfo();
    updatePresetLabel();

    rateControl.setSourceIndex(getActiveSourceIndex());
    rateControl.syncFromProcessor();
    modeControl.setSourceIndex(getActiveSourceIndex());
    modeControl.syncFromProcessor();

    auto colour = getLfoColour(getActiveSourceIndex());
    phaseSlider.setValue(audioProcessor.lfoParameters.getPhaseOffset(getActiveSourceIndex()), juce::dontSendNotification);
    phaseSlider.setAccentColour(colour);
    shapePreview.setAccentColour(colour);
    smoothKnob.setAccentColour(colour);
    smoothKnob.rebindParam(audioProcessor.lfoParameters.smoothAmount[getActiveSourceIndex()]);
    delayKnob.setAccentColour(colour);
    delayKnob.rebindParam(audioProcessor.lfoParameters.delayAmount[getActiveSourceIndex()]);
}

void LfoComponent::recordLfoUndoableChange(const std::vector<GraphNode>& nodesBefore,
                                            const LfoWaveform& waveformBefore, int lfoIndex) {
    // Record the UI-level graph node change (no-op after editor destroy/recreate)
    graph.recordUndoableChange(nodesBefore);

    // Also record the processor-side waveform change so undo works even if
    // the editor has been destroyed and reopened.
    auto& um = audioProcessor.getUndoManager();
    auto waveformAfter = audioProcessor.lfoParameters.getWaveform(lfoIndex);
    if (waveformBefore != waveformAfter) {
        um.perform(new LfoWaveformChangeAction(
            audioProcessor.lfoParameters.waveforms,
            audioProcessor.lfoParameters.waveformLock,
            lfoIndex, waveformBefore, waveformAfter));
    }
}

void LfoComponent::recordLfoUndoableChangeGuarded(const std::vector<GraphNode>& nodesBefore,
                                                   const LfoWaveform& waveformBefore, int lfoIndex) {
    juce::ScopedValueSetter<bool> guard(isSyncingGraph, true);
    recordLfoUndoableChange(nodesBefore, waveformBefore, lfoIndex);
}

void LfoComponent::applyLfoConstraints(int nodeIndex, double& time, double& value) {
    auto graphNodes = graph.getNodes();
    int numNodes = (int)graphNodes.size();

    if (nodeIndex == 0)
        time = 0.0;
    if (nodeIndex == numNodes - 1)
        time = 1.0;
    if (nodeIndex == numNodes - 1 && numNodes > 1)
        graph.setFirstNodeValue(value);
    if (nodeIndex == 0 && numNodes > 1)
        graph.setLastNodeValue(value);

    time = juce::jlimit(0.0, 1.0, time);
    value = juce::jlimit(0.0, 1.0, value);
}

void LfoComponent::showPaintShapeMenu() {
    juce::PopupMenu menu;
    using PS = NodeGraphComponent::PaintShape;
    auto current = graph.getPaintShape();

    auto addItem = [&](const juce::String& name, PS shape) {
        menu.addItem(name, true, shape == current, [this, shape]() {
            graph.setPaintShape(shape);
            shapePreview.setShape(shape);
            // Bump shape requires smooth interpolation
            if (shape == PS::Bump) {
                graph.setSmoothMode(true);
                smoothToggle.setSmooth(true);
            }
        });
    };

    addItem("Step", PS::Step);
    addItem("Half", PS::Half);
    addItem("Down", PS::Down);
    addItem("Up", PS::Up);
    addItem("Tri", PS::Tri);
    addItem("Bump", PS::Bump);

    menu.showMenuAsync(juce::PopupMenu::Options()
        .withTargetComponent(&shapePreview)
        .withMinimumWidth(80));
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
            if (audioProcessor.lfoParameters.getMode(i) != LfoMode::Free)
                audioProcessor.lfoParameters.setMode(i, LfoMode::Free);
        }
    }
}

void LfoComponent::copyWaveformToClipboard() {
    int idx = getActiveSourceIndex();
    auto& waveform = lfoData[idx].waveform;
    auto name = lfoData[idx].userPresetName.isNotEmpty()
        ? lfoData[idx].userPresetName
        : lfoPresetToString(lfoData[idx].preset);
    auto json = LfoPresetManager::waveformToVitalJson(waveform, name);
    juce::SystemClipboard::copyTextToClipboard(juce::JSON::toString(json));
}

void LfoComponent::pasteWaveformFromClipboard() {
    auto text = juce::SystemClipboard::getTextFromClipboard().trim();
    if (text.isEmpty()) return;

    auto parsed = juce::JSON::parse(text);
    LfoWaveform waveform;
    juce::String name;
    if (!LfoPresetManager::vitalJsonToWaveform(parsed, waveform, name)) return;

    int idx = getActiveSourceIndex();
    lfoData[idx].waveform = std::move(waveform);
    lfoData[idx].isCustom = true;
    lfoData[idx].userPresetName.clear();
    auto waveformBefore = audioProcessor.lfoParameters.getWaveform(idx);
    audioProcessor.lfoParameters.waveformChanged(idx, lfoData[idx].waveform);
    audioProcessor.lfoParameters.setIsCustom(idx, true);

    auto nodesBefore = graph.getNodes();
    syncGraphToActiveLfo();
    recordLfoUndoableChangeGuarded(nodesBefore, waveformBefore, idx);
    updatePresetLabel();
}

// ============================================================================
// Preset Browser & Management
// ============================================================================

void LfoComponent::showPresetBrowser() {
    if (presetBrowserVisible) {
        dismissPresetBrowser();
        return;
    }

    presetBrowser.onDismissRequested = [this]() { dismissPresetBrowser(); };
    addAndMakeVisible(presetBrowser);
    presetBrowserVisible = true;

    // Position inline browser over the graph and controls area
    graph.setVisible(false);
    phaseSlider.setVisible(false);
    modeControl.setVisible(false);
    rateControl.setVisible(false);
    smoothKnob.setVisible(false);
    delayKnob.setVisible(false);
    setOutlineBounds({});
    repaint();
    presetBrowser.setBounds(presetBrowserBounds);

    int idx = getActiveSourceIndex();
    presetBrowser.show(lfoData[idx].preset, lfoData[idx].userPresetName,
                        getDefaultFactoryName(), getDefaultFilePath());
}

void LfoComponent::dismissPresetBrowser() {
    if (presetBrowserVisible) {
        graph.setVisible(true);
        phaseSlider.setVisible(true);
        modeControl.setVisible(true);
        rateControl.setVisible(true);
        smoothKnob.setVisible(true);
        delayKnob.setVisible(true);
        setOutlineBounds(graph.getBounds());
        removeChildComponent(&presetBrowser);
        presetBrowserVisible = false;
        repaint();
    }
}

void LfoComponent::presetBrowserFactorySelected(LfoPreset preset) {
    applyPreset(preset);
    dismissPresetBrowser();
}

void LfoComponent::presetBrowserUserSelected(const juce::File& file) {
    loadUserPreset(file);
    dismissPresetBrowser();
}

void LfoComponent::presetBrowserUserDeleted(const juce::File& file) {
    juce::String deletedName;
    {
        LfoWaveform dummy;
        presetManager.loadPreset(file, dummy, deletedName);
    }
    presetManager.deletePreset(file);

    // If the deleted preset was active, clear it
    int idx = getActiveSourceIndex();
    if (lfoData[idx].userPresetName == deletedName) {
        lfoData[idx].userPresetName.clear();
        updatePresetLabel();
    }

    // Refresh the overlay
    if (presetBrowserVisible)
        presetBrowser.refresh(lfoData[idx].preset, lfoData[idx].userPresetName,
                              getDefaultFactoryName(), getDefaultFilePath());
}

void LfoComponent::presetBrowserSaveRequested(const juce::String& name) {
    int idx = getActiveSourceIndex();
    presetManager.savePreset(name, lfoData[idx].waveform);
    lfoData[idx].userPresetName = name;
    lfoData[idx].isCustom = true;
    updatePresetLabel();

    // Refresh the overlay to show the new preset
    refreshPresetBrowserIfVisible();
}

void LfoComponent::loadUserPreset(const juce::File& file) {
    LfoWaveform waveform;
    juce::String name;

    if (!presetManager.loadPreset(file, waveform, name))
        return;

    int idx = getActiveSourceIndex();
    lfoData[idx].waveform = waveform;
    lfoData[idx].isCustom = true;
    lfoData[idx].userPresetName = name;

    auto waveformBefore = audioProcessor.lfoParameters.getWaveform(idx);
    audioProcessor.lfoParameters.waveformChanged(idx, lfoData[idx].waveform);
    audioProcessor.lfoParameters.setIsCustom(idx, true);

    auto nodesBefore = graph.getNodes();
    syncGraphToActiveLfo();
    recordLfoUndoableChangeGuarded(nodesBefore, waveformBefore, idx);
    updatePresetLabel();
}

void LfoComponent::presetBrowserSetDefaultFactory(LfoPreset preset) {
    audioProcessor.setGlobalValue("defaultLfoPreset", lfoPresetToString(preset));
    audioProcessor.removeGlobalValue("defaultLfoPresetFile");
    audioProcessor.saveGlobalSettings();
    refreshPresetBrowserIfVisible();
}

void LfoComponent::presetBrowserSetDefaultFile(const juce::File& file) {
    audioProcessor.setGlobalValue("defaultLfoPresetFile", file.getFullPathName());
    audioProcessor.removeGlobalValue("defaultLfoPreset");
    audioProcessor.saveGlobalSettings();
    refreshPresetBrowserIfVisible();
}

void LfoComponent::presetBrowserClearDefault() {
    audioProcessor.removeGlobalValue("defaultLfoPreset");
    audioProcessor.removeGlobalValue("defaultLfoPresetFile");
    audioProcessor.saveGlobalSettings();
    refreshPresetBrowserIfVisible();
}

juce::String LfoComponent::getDefaultFactoryName() const {
    return audioProcessor.getGlobalStringValue("defaultLfoPreset");
}

juce::String LfoComponent::getDefaultFilePath() const {
    return audioProcessor.getGlobalStringValue("defaultLfoPresetFile");
}

void LfoComponent::refreshPresetBrowserIfVisible() {
    if (presetBrowserVisible) {
        int idx = getActiveSourceIndex();
        presetBrowser.refresh(lfoData[idx].preset, lfoData[idx].userPresetName,
                              getDefaultFactoryName(), getDefaultFilePath());
    }
}
