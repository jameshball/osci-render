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
// BezierToggle – S-curve / straight line toggle icon
// ============================================================================

LfoComponent::BezierToggle::BezierToggle() {
    setRepaintsOnMouseActivity(true);
}

void LfoComponent::BezierToggle::paint(juce::Graphics& g) {
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

    if (bezier) {
        // S-shaped bezier curve
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
    g.setColour(bezier ? accent.withAlpha(alpha) : juce::Colours::white.withAlpha(alpha * 0.7f));
    g.strokePath(path, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved));
}

void LfoComponent::BezierToggle::mouseDown(const juce::MouseEvent&) {
    bezier = !bezier;
    repaint();
    if (onToggle) onToggle(bezier);
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
      paintToggle("paint", juce::String(BinaryData::brush_svg, BinaryData::brush_svgSize),
                  juce::Colours::white.withAlpha(0.5f), getLfoColour(0)),
      copyButton("copy", juce::String(BinaryData::copy_svg, BinaryData::copy_svgSize),
                 juce::Colours::white.withAlpha(0.5f), juce::Colours::white.withAlpha(0.5f)),
      pasteButton("paste", juce::String(BinaryData::paste_svg, BinaryData::paste_svgSize),
                  juce::Colours::white.withAlpha(0.5f), juce::Colours::white.withAlpha(0.5f)),
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
    graph.setBezierMode(true);

    graph.constrainNode = [this](int nodeIndex, double& time, double& value) {
        applyLfoConstraints(nodeIndex, time, value);
    };
    graph.hasCurveHandle = [](int) { return true; };
    graph.onNodesChanged = [this]() { if (!isSyncingGraph) syncActiveLfoFromGraph(); };
    addAndMakeVisible(graph);

    // Preset selector
    presetSelector.setTooltip("LFO waveform preset - click arrows to cycle");
    presetSelector.onPrev = [this]() { cyclePreset(-1); };
    presetSelector.onNext = [this]() { cyclePreset(1); };
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

    // Bezier/straight toggle
    bezierToggle.setAccentColour(getLfoColour(0));
    bezierToggle.setTooltip("Toggle bezier / straight line interpolation");
    bezierToggle.onToggle = [this](bool bezier) {
        graph.setBezierMode(bezier);
        syncActiveLfoFromGraph();
    };
    addAndMakeVisible(bezierToggle);

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

void LfoComponent::paint(juce::Graphics& g) {
    ModulationSourceComponent::paint(g);

    // Dark rounded background behind paint + preview controls
    if (!paintControlsBg.isEmpty()) {
        g.setColour(Colours::veryDark.brighter(0.15f));
        g.fillRoundedRectangle(paintControlsBg.toFloat(), 4.0f);
    }
}

void LfoComponent::resized() {
    ModulationSourceComponent::resized();

    auto bounds = getContentBounds();

    auto topBar = bounds.removeFromTop(kTopBarHeight);
    bounds.removeFromTop(kTopBarGap);

    // Top bar layout: [paintToggle][shapePreview] (dark bg) [bezierToggle][gap][presetSelector]
    auto paintControlsLeft = topBar.getX();
    paintToggle.setBounds(topBar.removeFromLeft(kIconSize));

    shapePreview.setBounds(topBar.removeFromLeft(kShapePreviewWidth));
    auto paintControlsRight = topBar.getX();

    // Store background rect for the paint + preview group only
    paintControlsBg = juce::Rectangle<int>(paintControlsLeft, topBar.getY(),
                                            paintControlsRight - paintControlsLeft,
                                            topBar.getHeight());

    topBar.removeFromLeft(6);
    bezierToggle.setBounds(topBar.removeFromLeft(kIconSize));
    topBar.removeFromLeft(6);

    // Copy/paste buttons on the right
    auto pasteArea = topBar.removeFromRight(kIconSize);
    topBar.removeFromRight(2);
    auto copyArea = topBar.removeFromRight(kIconSize);
    topBar.removeFromRight(4);
    copyButton.setBounds(copyArea);
    pasteButton.setBounds(pasteArea);

    presetSelector.setBounds(topBar);

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

    auto colour = getLfoColour(index);
    phaseSlider.setAccentColour(colour);
    shapePreview.setAccentColour(colour);
    paintToggle.setOnColour(colour);
    bezierToggle.setAccentColour(colour);
}

void LfoComponent::syncGraphToActiveLfo() {
    int idx = getActiveSourceIndex();
    auto& waveform = lfoData[idx].waveform;
    juce::ScopedValueSetter<bool> guard(isSyncingGraph, true);
    graph.setNodes(waveform.nodes);
    graph.setBezierMode(waveform.useBezier);
    bezierToggle.setBezier(waveform.useBezier);

    auto colour = getLfoColour(idx);
    graph.setColour(NodeGraphComponent::lineColourId, colour);
    graph.setColour(NodeGraphComponent::fillColourId, colour.withAlpha(0.15f));
    graph.setColour(NodeGraphComponent::nodeColourId, colour);
}

void LfoComponent::syncActiveLfoFromGraph() {
    int idx = getActiveSourceIndex();
    lfoData[idx].waveform.nodes = graph.getNodes();
    lfoData[idx].waveform.useBezier = graph.isBezierMode();
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
    auto& registry = getLfoPresetRegistry();

    int idx = getActiveSourceIndex();
    auto current = lfoData[idx].preset;
    int currentIdx = -1;
    for (int i = 0; i < (int)registry.size(); ++i) {
        if (registry[i].preset == current) { currentIdx = i; break; }
    }
    if (currentIdx < 0) currentIdx = (direction > 0) ? -1 : 0;
    int newIdx = (currentIdx + direction + (int)registry.size()) % (int)registry.size();
    applyPreset(registry[newIdx].preset);
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

    // Disable paint mode when switching to a preset
    if (paintToggle.getToggleState()) {
        paintToggle.setToggleState(false, juce::dontSendNotification);
        graph.setPaintMode(false);
        shapePreview.setEnabled(false);
    }

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

    auto colour = getLfoColour(getActiveSourceIndex());
    phaseSlider.setValue(audioProcessor.getLfoPhaseOffset(getActiveSourceIndex()));
    phaseSlider.setAccentColour(colour);
    shapePreview.setAccentColour(colour);
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

void LfoComponent::showPaintShapeMenu() {
    juce::PopupMenu menu;
    using PS = NodeGraphComponent::PaintShape;
    auto current = graph.getPaintShape();

    auto addItem = [&](const juce::String& name, PS shape) {
        menu.addItem(name, true, shape == current, [this, shape]() {
            graph.setPaintShape(shape);
            shapePreview.setShape(shape);
            // Bump shape requires bezier interpolation (nodes at 0, curves create shape)
            if (shape == PS::Bump) {
                graph.setBezierMode(true);
                bezierToggle.setBezier(true);
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
            if (audioProcessor.getLfoMode(i) != LfoMode::Free)
                audioProcessor.setLfoMode(i, LfoMode::Free);
        }
    }
}

void LfoComponent::copyWaveformToClipboard() {
    int idx = getActiveSourceIndex();
    auto& waveform = lfoData[idx].waveform;

    auto* root = new juce::DynamicObject();
    root->setProperty("bezier", waveform.useBezier);

    juce::Array<juce::var> nodesArray;
    for (auto& n : waveform.nodes) {
        auto* nodeObj = new juce::DynamicObject();
        nodeObj->setProperty("t", n.time);
        nodeObj->setProperty("v", n.value);
        nodeObj->setProperty("c", (double)n.curve);
        nodesArray.add(juce::var(nodeObj));
    }
    root->setProperty("nodes", nodesArray);

    juce::SystemClipboard::copyTextToClipboard(juce::JSON::toString(juce::var(root)));
}

void LfoComponent::pasteWaveformFromClipboard() {
    auto text = juce::SystemClipboard::getTextFromClipboard().trim();
    if (text.isEmpty()) return;

    auto parsed = juce::JSON::parse(text);
    if (!parsed.isObject()) return;

    auto* obj = parsed.getDynamicObject();
    if (obj == nullptr) return;

    auto nodesVar = obj->getProperty("nodes");
    if (!nodesVar.isArray()) return;

    auto* nodesArray = nodesVar.getArray();
    if (nodesArray == nullptr || nodesArray->size() < 2) return;

    std::vector<LfoNode> nodes;
    for (auto& item : *nodesArray) {
        if (!item.isObject()) return;
        auto* nodeObj = item.getDynamicObject();
        if (nodeObj == nullptr) return;

        LfoNode node;
        node.time = (double)nodeObj->getProperty("t");
        node.value = (double)nodeObj->getProperty("v");
        node.curve = (float)(double)nodeObj->getProperty("c");

        if (std::isnan(node.time) || std::isinf(node.time) ||
            std::isnan(node.value) || std::isinf(node.value) ||
            std::isnan(node.curve) || std::isinf(node.curve))
            return;

        node.time = juce::jlimit(0.0, 1.0, node.time);
        node.value = juce::jlimit(0.0, 1.0, node.value);
        node.curve = juce::jlimit(-osci_audio::kMaxBezierCurve, osci_audio::kMaxBezierCurve, node.curve);
        nodes.push_back(node);
    }

    if (nodes.size() < 2) return;

    std::sort(nodes.begin(), nodes.end(),
              [](const LfoNode& a, const LfoNode& b) { return a.time < b.time; });

    bool bezier = obj->getProperty("bezier");

    int idx = getActiveSourceIndex();
    lfoData[idx].waveform.nodes = std::move(nodes);
    lfoData[idx].waveform.useBezier = bezier;
    lfoData[idx].preset = LfoPreset::Custom;
    audioProcessor.lfoWaveformChanged(idx, lfoData[idx].waveform);
    audioProcessor.lfoPresets[idx] = LfoPreset::Custom;

    syncGraphToActiveLfo();
    updatePresetLabel();
}
