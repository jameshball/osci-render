#include "LfoComponent.h"
#include "EffectComponent.h"
#include "../PluginProcessor.h"
#include "../LookAndFeel.h"

// ============================================================================
// DepthIndicator – small arc knob for a single LFO→param connection
// ============================================================================

LfoComponent::DepthIndicator::DepthIndicator(LfoComponent& o, int idx, juce::String pid, float d, bool bp)
    : owner(o), lfoIndex(idx), paramId(pid), depth(d), bipolar(bp) {
    setSize(14, 14);
}

void LfoComponent::DepthIndicator::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat().reduced(1.5f);
    float cx = bounds.getCentreX();
    float cy = bounds.getCentreY();
    float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;

    auto colour = LfoComponent::getLfoColour(lfoIndex);

    // Dark filled centre
    g.setColour(Colours::veryDark);
    g.fillEllipse(bounds);

    // Subtle coloured ring outline
    g.setColour(colour.withAlpha(0.2f));
    g.drawEllipse(bounds, 1.0f);

    // Arc from 12 o'clock
    float arcThickness = hovering ? 3.0f : 2.0f;
    float arcRadius = radius - arcThickness * 0.5f;
    constexpr float topAngle = 0.0f;

    if (std::abs(depth) > 0.005f) {
        float sweep;
        if (bipolar) {
            sweep = depth * juce::MathConstants<float>::twoPi;
        } else {
            sweep = depth * juce::MathConstants<float>::twoPi;
        }
        sweep = juce::jlimit(-6.27f, 6.27f, sweep);
        juce::Path arc;
        arc.addCentredArc(cx, cy, arcRadius, arcRadius, 0.0f,
                          topAngle, topAngle + sweep, true);
        g.setColour(colour.withAlpha(hovering ? 1.0f : 0.8f));
        g.strokePath(arc, juce::PathStrokeType(arcThickness,
                     juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

void LfoComponent::DepthIndicator::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu()) {
        showRightClickMenu();
        return;
    }
    dragging = true;
    dragStartDepth = depth;
    showValuePopup();
}

void LfoComponent::DepthIndicator::mouseDrag(const juce::MouseEvent& e) {
    if (!dragging) return;
    float dy = (float)(e.getPosition().y - e.getMouseDownPosition().y);
    float delta = -dy / 80.0f;
    float newDepth = dragStartDepth + delta;
    if (bipolar)
        depth = juce::jlimit(-1.0f, 1.0f, newDepth);
    else
        depth = juce::jlimit(0.0f, 1.0f, newDepth);
    owner.audioProcessor.addLfoAssignment({ lfoIndex, paramId, depth, bipolar });
    // Keep the slider range overlay in sync during drag
    EffectComponent::lfoRangeDepth = depth;
    showValuePopup();
    repaint();
    if (auto* topLevel = getTopLevelComponent())
        topLevel->repaint();
}

void LfoComponent::DepthIndicator::mouseUp(const juce::MouseEvent&) {
    dragging = false;
    hideValuePopup();
}

void LfoComponent::DepthIndicator::mouseEnter(const juce::MouseEvent&) {
    hovering = true;
    EffectComponent::highlightedParamId = paramId;
    EffectComponent::lfoRangeParamId = paramId;
    EffectComponent::lfoRangeDepth = depth;
    EffectComponent::lfoRangeBipolar = bipolar;
    showValuePopup();
    repaint();
    if (auto* topLevel = getTopLevelComponent())
        topLevel->repaint();
}

void LfoComponent::DepthIndicator::mouseExit(const juce::MouseEvent&) {
    hovering = false;
    EffectComponent::highlightedParamId = juce::String();
    EffectComponent::lfoRangeParamId = juce::String();
    if (!dragging)
        hideValuePopup();
    repaint();
    if (auto* topLevel = getTopLevelComponent())
        topLevel->repaint();
}

void LfoComponent::DepthIndicator::mouseDoubleClick(const juce::MouseEvent&) {
    // Double-click removes the connection
    owner.audioProcessor.removeLfoAssignment(lfoIndex, paramId);
    // The next timer tick will rebuild indicators
}

void LfoComponent::DepthIndicator::showRightClickMenu() {
    juce::PopupMenu menu;
    menu.addItem(1, "Remove");
    menu.addItem(2, bipolar ? "Make Unipolar" : "Make Bipolar");
    menu.addSeparator();
    menu.addItem(3, "Enter Value");

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
        [this](int result) {
            if (result == 1) {
                owner.audioProcessor.removeLfoAssignment(lfoIndex, paramId);
            } else if (result == 2) {
                bipolar = !bipolar;
                if (!bipolar && depth < 0.0f)
                    depth = -depth;
                owner.audioProcessor.addLfoAssignment({ lfoIndex, paramId, depth, bipolar });
                repaint();
            } else if (result == 3) {
                startTextEdit();
            }
        });
}

void LfoComponent::DepthIndicator::startTextEdit() {
    if (inlineEditor) return;
    inlineEditor = std::make_unique<juce::TextEditor>();
    inlineEditor->setFont(juce::Font(11.0f, juce::Font::bold));
    inlineEditor->setText(juce::String((int)(depth * 100.0f)), false);
    inlineEditor->setJustification(juce::Justification::centred);
    inlineEditor->setColour(juce::TextEditor::backgroundColourId, Dracula::background);
    inlineEditor->setColour(juce::TextEditor::textColourId, Dracula::foreground);
    inlineEditor->setColour(juce::TextEditor::outlineColourId, LfoComponent::getLfoColour(lfoIndex));
    inlineEditor->setColour(juce::TextEditor::focusedOutlineColourId, LfoComponent::getLfoColour(lfoIndex));
    inlineEditor->setBounds(getLocalBounds().expanded(8, 2));
    addAndMakeVisible(inlineEditor.get());
    inlineEditor->selectAll();
    inlineEditor->grabKeyboardFocus();

    inlineEditor->onReturnKey = [this]() {
        float val = inlineEditor->getText().getFloatValue() / 100.0f;
        if (bipolar)
            depth = juce::jlimit(-1.0f, 1.0f, val);
        else
            depth = juce::jlimit(0.0f, 1.0f, val);
        owner.audioProcessor.addLfoAssignment({ lfoIndex, paramId, depth, bipolar });
        inlineEditor.reset();
        repaint();
    };
    inlineEditor->onEscapeKey = [this]() {
        inlineEditor.reset();
        repaint();
    };
    inlineEditor->onFocusLost = [this]() {
        inlineEditor.reset();
        repaint();
    };
}

void LfoComponent::DepthIndicator::showValuePopup() {
    if (!valuePopup) {
        valuePopup = std::make_unique<juce::Label>();
        valuePopup->setFont(juce::Font(11.0f, juce::Font::bold));
        valuePopup->setColour(juce::Label::backgroundColourId, Dracula::background);
        valuePopup->setColour(juce::Label::textColourId, Dracula::foreground);
        valuePopup->setJustificationType(juce::Justification::centred);
        valuePopup->setInterceptsMouseClicks(false, false);
        valuePopup->addToDesktop(juce::ComponentPeer::windowIsTemporary | juce::ComponentPeer::windowIgnoresKeyPresses);
        valuePopup->setAlwaysOnTop(true);
    }

    // Find parameter name
    juce::String paramName = paramId;
    for (auto& effect : owner.audioProcessor.toggleableEffects) {
        for (auto* p : effect->parameters) {
            if (p->paramID == paramId) {
                paramName = p->name;
                break;
            }
        }
    }

    int pct = (int)(depth * 100.0f);
    valuePopup->setText(paramName + " " + juce::String(pct) + "%", juce::dontSendNotification);

    // Position above this component in screen coordinates
    int popupW = valuePopup->getFont().getStringWidth(valuePopup->getText()) + 14;
    int popupH = 20;
    auto screenPos = localPointToGlobal(juce::Point<int>(getWidth() / 2, 0));
    valuePopup->setBounds(screenPos.x - popupW / 2, screenPos.y - popupH - 4, popupW, popupH);
    valuePopup->setVisible(true);
}

void LfoComponent::DepthIndicator::hideValuePopup() {
    valuePopup.reset();
}

// ============================================================================
// LfoTabHandle – tab label that is also a drag source
// ============================================================================

LfoComponent::LfoTabHandle::LfoTabHandle(const juce::String& l, int index, LfoComponent& o)
    : label(l), lfoIndex(index), owner(o) {}

void LfoComponent::LfoTabHandle::paint(juce::Graphics& g) {
    bool active = (lfoIndex == owner.activeLfoIndex);
    auto colour = LfoComponent::getLfoColour(lfoIndex);

    if (active) {
        g.setColour(colour.withAlpha(0.12f));
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 3.0f);
        g.setColour(colour.withAlpha(0.5f));
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 3.0f, 1.0f);
    } else {
        g.setColour(juce::Colours::white.withAlpha(0.03f));
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 3.0f);
    }

    // Separate LFO value track on the left — full height of tab
    constexpr int trackWidth = 8;
    constexpr float trackPad = 2.0f;
    float trackX = trackPad;
    float trackY = trackPad;
    float trackH = (float)getHeight() - trackPad * 2.0f;
    bool hasTrack = !depthIndicators.isEmpty();

    // Label centred in the remaining area (to the right of the track)
    {
        int labelLeft = hasTrack ? (int)(trackX + trackWidth + 2.0f) : 0;
        auto labelBounds = getLocalBounds().removeFromTop(16);
        labelBounds.setLeft(labelLeft);
        g.setColour(active ? colour : juce::Colours::white.withAlpha(0.5f));
        g.setFont(juce::Font(10.0f, juce::Font::bold));
        g.drawText(label, labelBounds, juce::Justification::centred);
    }

    if (hasTrack) {

        // Draw the track background (rounded rect rail)
        auto trackRect = juce::Rectangle<float>(trackX, trackY, (float)trackWidth, trackH);
        g.setColour(colour.withAlpha(0.08f));
        g.fillRoundedRectangle(trackRect, (float)trackWidth * 0.5f);
        g.setColour(colour.withAlpha(0.2f));
        g.drawRoundedRectangle(trackRect, (float)trackWidth * 0.5f, 0.5f);

        // Draw the pill marker inside the track
        constexpr float pillBaseH = 5.0f;
        constexpr float pillMaxStretch = 14.0f;
        float stretch = juce::jlimit(0.0f, pillMaxStretch, std::abs(lfoDelta) * 80.0f);
        float pillW = (float)trackWidth - 2.0f;
        float pillH = pillBaseH + stretch;

        float pillArea = trackH - pillBaseH;
        float pillCentreY = trackY + pillArea * (1.0f - lfoValue) + pillBaseH * 0.5f;
        float pillX = trackX + 1.0f;

        juce::Rectangle<float> pill(pillX, pillCentreY - pillH * 0.5f, pillW, pillH);
        g.setColour(colour.withAlpha(0.9f));
        g.fillRoundedRectangle(pill, pillW * 0.5f);
    }
}

void LfoComponent::LfoTabHandle::mouseDown(const juce::MouseEvent& e) {
    // Left click = switch to this LFO
    owner.switchToLfo(lfoIndex);

    // Start drag for creating new connections
    isDragging = true;
    repaint();
}

void LfoComponent::LfoTabHandle::mouseDrag(const juce::MouseEvent& e) {
    if (!isDragging) return;

    // Start actual DnD once the mouse has moved enough
    if (e.getDistanceFromDragStart() > 4) {
        juce::String desc = "LFO:" + juce::String(lfoIndex);

        juce::Image dragImage(juce::Image::ARGB, 50, 20, true);
        {
            juce::Graphics g(dragImage);
            auto colour = LfoComponent::getLfoColour(lfoIndex);
            g.setColour(colour.withAlpha(0.7f));
            g.fillRoundedRectangle(0.0f, 0.0f, 50.0f, 20.0f, 4.0f);
            g.setColour(juce::Colours::white);
            g.setFont(11.0f);
            g.drawText(label, 0, 0, 50, 20, juce::Justification::centred);
        }

        if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this)) {
            container->startDragging(desc, this, juce::ScaledImage(dragImage), true);
            if (owner.onDragActiveChanged)
                owner.onDragActiveChanged(true);
        }
        isDragging = false; // DnD container takes over
    }
}

void LfoComponent::LfoTabHandle::mouseUp(const juce::MouseEvent&) {
    if (isDragging) {
        isDragging = false;
        if (owner.onDragActiveChanged)
            owner.onDragActiveChanged(false);
    }
}

void LfoComponent::LfoTabHandle::resized() {
    // Lay out depth indicators in a grid below the label, to the right of the track
    int n = depthIndicators.size();
    if (n == 0) return;

    int labelH = 16;
    int trackW = 12; // track (8px) + gap (4px)
    int availW = getWidth() - trackW - 1;
    int availH = getHeight() - labelH - 1;

    int maxSize = 14;
    int minSize = 8;
    int spacing = 1;

    int cols = juce::jmax(1, (availW + spacing) / (maxSize + spacing));
    int rows = (n + cols - 1) / cols;
    int indicatorSize = maxSize;

    while (rows * (indicatorSize + spacing) - spacing > availH && indicatorSize > minSize) {
        indicatorSize--;
        cols = juce::jmax(1, (availW + spacing) / (indicatorSize + spacing));
        rows = (n + cols - 1) / cols;
    }

    int gridW = cols * indicatorSize + (cols - 1) * spacing;
    int gridH = rows * indicatorSize + (rows - 1) * spacing;
    int startX = trackW + (availW - gridW) / 2;
    int startY = labelH + (availH - gridH) / 2;

    for (int i = 0; i < n; ++i) {
        int col = i % cols;
        int row = i / cols;
        int x = startX + col * (indicatorSize + spacing);
        int y = startY + row * (indicatorSize + spacing);
        depthIndicators[i]->setBounds(x, y, indicatorSize, indicatorSize);
    }
}

void LfoComponent::LfoTabHandle::refreshDepthIndicators() {
    auto assignments = owner.audioProcessor.getLfoAssignments();

    // Build list of assignments for this LFO
    std::vector<LfoAssignment> myAssignments;
    for (const auto& a : assignments) {
        if (a.lfoIndex == lfoIndex)
            myAssignments.push_back(a);
    }

    // Check if any indicator is being interacted with — skip full rebuild if so
    bool anyInteracting = false;
    for (auto* ind : depthIndicators) {
        if (ind->dragging || ind->hovering) {
            anyInteracting = true;
            break;
        }
    }

    if (anyInteracting) {
        // Just update depth values in-place without destroying components
        for (auto* ind : depthIndicators) {
            for (const auto& a : myAssignments) {
                if (a.paramId == ind->paramId) {
                    if (!ind->dragging) { // don't overwrite while user is dragging
                        ind->depth = a.depth;
                        ind->bipolar = a.bipolar;
                    }
                    break;
                }
            }
        }
        return;
    }

    // Check if the set of paramIds has changed
    bool sameSet = ((int)myAssignments.size() == depthIndicators.size());
    if (sameSet) {
        for (int i = 0; i < (int)myAssignments.size(); ++i) {
            if (depthIndicators[i]->paramId != myAssignments[i].paramId) {
                sameSet = false;
                break;
            }
        }
    }

    if (sameSet) {
        // Just update depths in-place
        for (int i = 0; i < (int)myAssignments.size(); ++i) {
            if (depthIndicators[i]->depth != myAssignments[i].depth
                || depthIndicators[i]->bipolar != myAssignments[i].bipolar) {
                depthIndicators[i]->depth = myAssignments[i].depth;
                depthIndicators[i]->bipolar = myAssignments[i].bipolar;
                depthIndicators[i]->repaint();
            }
        }
        return;
    }

    // Assignments changed — full rebuild
    depthIndicators.clear();
    for (const auto& a : myAssignments) {
        auto* indicator = new DepthIndicator(owner, lfoIndex, a.paramId, a.depth, a.bipolar);
        addAndMakeVisible(indicator);
        depthIndicators.add(indicator);
    }
    resized();
    repaint();
}

// ============================================================================
// PresetSelector – Vital-style dark bar with arrows
// ============================================================================

LfoComponent::PresetSelector::PresetSelector() {}

void LfoComponent::PresetSelector::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();

    // Dark rounded background
    g.setColour(juce::Colour(0xFF222222));
    g.fillRoundedRectangle(bounds, 4.0f);

    // Subtle border
    g.setColour(juce::Colours::white.withAlpha(0.06f));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 4.0f, 1.0f);

    // Left arrow
    g.setColour(juce::Colours::white.withAlpha(0.7f));
    auto arrowFont = juce::Font(14.0f, juce::Font::bold);
    g.setFont(arrowFont);
    g.drawText("<", leftArrowArea.toFloat(), juce::Justification::centred);

    // Right arrow
    g.drawText(">", rightArrowArea.toFloat(), juce::Justification::centred);

    // Preset name centered
    g.setColour(juce::Colours::white.withAlpha(0.9f));
    g.setFont(juce::Font(13.0f, juce::Font::bold));
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
    switch (lfoIndex) {
        case 0: return juce::Colour(0xFF00E5FF); // Cyan
        case 1: return juce::Colour(0xFFFF79C6); // Pink
        case 2: return juce::Colour(0xFF50FA7B); // Green
        case 3: return juce::Colour(0xFFFFB86C); // Orange
        default: return juce::Colour(0xFF00E5FF);
    }
}

LfoComponent::LfoComponent(OscirenderAudioProcessor& processor)
    : audioProcessor(processor) {
    // Initialize all LFOs with default triangle preset
    for (int i = 0; i < NUM_LFOS; ++i) {
        lfoData[i].preset = LfoPreset::Triangle;
        lfoData[i].waveform = createLfoPreset(LfoPreset::Triangle);
    }

    // Create tab/drag handles
    for (int i = 0; i < NUM_LFOS; ++i) {
        auto* tab = new LfoTabHandle("LFO " + juce::String(i + 1), i, *this);
        addAndMakeVisible(tab);
        tabHandles.add(tab);
    }

    // Setup graph
    graph.setDomainRange(0.0, 1.0);
    graph.setValueRange(0.0, 1.0);
    graph.setNodeLimits(2, 32);
    graph.setGridDisplay(true, false);
    graph.setAllowNodeAddRemove(true);
    graph.setHoverThreshold(20.0f);
    graph.setCornerRadius(6.0f);

    graph.constrainNode = [this](int nodeIndex, double& time, double& value) {
        applyLfoConstraints(nodeIndex, time, value);
    };

    graph.hasCurveHandle = [](int) { return true; };

    graph.onNodesChanged = [this]() {
        syncActiveLfoFromGraph();
    };

    addAndMakeVisible(graph);

    // Preset selector (Vital-style)
    presetSelector.onPrev = [this]() { cyclePreset(-1); };
    presetSelector.onNext = [this]() { cyclePreset(1); };
    addAndMakeVisible(presetSelector);

    // Rate slider
    rateSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    rateSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 16);
    rateSlider.setRange(0.01, 100.0, 0.01);
    rateSlider.setValue(1.0);
    rateSlider.setSkewFactorFromMidPoint(5.0);
    rateSlider.setTextValueSuffix(" Hz");
    addAndMakeVisible(rateSlider);

    rateLabel.setText("RATE", juce::dontSendNotification);
    rateLabel.setJustificationType(juce::Justification::centred);
    rateLabel.setFont(juce::Font(10.0f, juce::Font::bold));
    rateLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.6f));
    addAndMakeVisible(rateLabel);

    // Wire rate slider to the processor parameter
    rateSlider.onValueChange = [this]() {
        if (audioProcessor.lfoRate[activeLfoIndex] != nullptr)
            audioProcessor.lfoRate[activeLfoIndex]->setUnnormalisedValueNotifyingHost((float)rateSlider.getValue());
    };

    // Timer for refreshing depth indicators and LFO value bars
    startTimerHz(60);

    // Listen for state reloads from the processor
    audioProcessor.broadcaster.addChangeListener(this);

    // Restore any previously loaded state (active tab, presets, waveforms)
    syncFromProcessorState();
}

LfoComponent::~LfoComponent() {
    stopTimer();
    audioProcessor.broadcaster.removeChangeListener(this);
}

void LfoComponent::timerCallback() {
    refreshAllDepthIndicators();
    // Update LFO value bars
    for (int i = 0; i < NUM_LFOS; ++i) {
        float val = audioProcessor.getLfoCurrentValue(i);
        tabHandles[i]->setLfoValue(val);
    }
}

void LfoComponent::changeListenerCallback(juce::ChangeBroadcaster* source) {
    if (source == &audioProcessor.broadcaster) {
        syncFromProcessorState();
    }
}

void LfoComponent::resized() {
    auto bounds = getLocalBounds();

    // Tab handles on the left
    static constexpr int tabWidth = 75;
    auto tabArea = bounds.removeFromLeft(tabWidth);
    int tabHeight = tabArea.getHeight() / NUM_LFOS;
    for (auto* tab : tabHandles) {
        tab->setBounds(tabArea.removeFromTop(tabHeight).reduced(2, 2));
    }

    // Top bar: preset selector + rate knob above the graph
    static constexpr int topBarHeight = 32;
    auto topBar = bounds.removeFromTop(topBarHeight);
    auto presetArea = topBar.removeFromLeft(juce::jmin(200, topBar.getWidth() / 2)).reduced(2, 4);
    presetSelector.setBounds(presetArea);

    auto rateArea = topBar.removeFromLeft(80);
    rateLabel.setBounds(rateArea.removeFromBottom(14));
    rateSlider.setBounds(rateArea.reduced(4, 2));

    // Graph gets the remaining space
    graph.setBounds(bounds.reduced(4));
}

void LfoComponent::paint(juce::Graphics& g) {
    auto colour = getLfoColour(activeLfoIndex);

    // Panel background – slightly lighter than the graph for depth
    g.setColour(juce::Colour(0xFF1A1A1A));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), OscirenderLookAndFeel::RECT_RADIUS);

    // Subtle LFO-coloured hairline border
    g.setColour(colour.withAlpha(0.15f));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), OscirenderLookAndFeel::RECT_RADIUS, 1.0f);
}

void LfoComponent::paintOverChildren(juce::Graphics& g) {
    // Draw an LFO-coloured rounded border around the graph area
    auto colour = getLfoColour(activeLfoIndex);
    auto graphBounds = graph.getBounds().toFloat();
    g.setColour(colour.withAlpha(0.25f));
    g.drawRoundedRectangle(graphBounds, 6.0f, 1.0f);
}

void LfoComponent::switchToLfo(int index) {
    if (index < 0 || index >= NUM_LFOS) return;
    if (index == activeLfoIndex) return;

    activeLfoIndex = index;
    audioProcessor.activeLfoTab = index;
    syncGraphToActiveLfo();
    updatePresetLabel();

    // Update rate slider to match this LFO's rate
    if (audioProcessor.lfoRate[index] != nullptr)
        rateSlider.setValue(audioProcessor.lfoRate[index]->getValueUnnormalised(), juce::dontSendNotification);

    for (auto* tab : tabHandles)
        tab->repaint();
    repaint();
}

void LfoComponent::syncGraphToActiveLfo() {
    auto& waveform = lfoData[activeLfoIndex].waveform;
    std::vector<GraphNode> graphNodes;
    graphNodes.reserve(waveform.nodes.size());
    for (const auto& n : waveform.nodes) {
        graphNodes.push_back({ n.time, n.value, n.curve });
    }
    graph.setNodes(graphNodes);

    // Update graph line colour to match LFO
    auto colour = getLfoColour(activeLfoIndex);
    graph.setColour(NodeGraphComponent::lineColourId, colour);
    graph.setColour(NodeGraphComponent::fillColourId, colour.withAlpha(0.15f));
    graph.setColour(NodeGraphComponent::nodeColourId, colour);
}

void LfoComponent::syncActiveLfoFromGraph() {
    auto graphNodes = graph.getNodes();
    auto& waveform = lfoData[activeLfoIndex].waveform;
    waveform.nodes.clear();
    waveform.nodes.reserve(graphNodes.size());
    for (const auto& gn : graphNodes) {
        waveform.nodes.push_back({ gn.time, gn.value, gn.curve });
    }

    // Mark as custom if nodes were manually edited
    lfoData[activeLfoIndex].preset = LfoPreset::Custom;
    updatePresetLabel();

    // Notify processor
    audioProcessor.lfoWaveformChanged(activeLfoIndex, waveform);
    audioProcessor.lfoPresets[activeLfoIndex] = LfoPreset::Custom;
}

void LfoComponent::setLfoWaveform(int lfoIndex, const LfoWaveform& waveform) {
    if (lfoIndex < 0 || lfoIndex >= NUM_LFOS) return;
    lfoData[lfoIndex].waveform = waveform;
    if (lfoIndex == activeLfoIndex) {
        syncGraphToActiveLfo();
    }
}

LfoWaveform LfoComponent::getLfoWaveform(int lfoIndex) const {
    if (lfoIndex < 0 || lfoIndex >= NUM_LFOS) return {};
    return lfoData[lfoIndex].waveform;
}

void LfoComponent::setLfoPreset(int lfoIndex, LfoPreset preset) {
    if (lfoIndex < 0 || lfoIndex >= NUM_LFOS) return;
    lfoData[lfoIndex].preset = preset;
    lfoData[lfoIndex].waveform = createLfoPreset(preset);
    if (lfoIndex == activeLfoIndex) {
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
    static const std::array<LfoPreset, 5> presets = {
        LfoPreset::Sine, LfoPreset::Triangle, LfoPreset::Sawtooth, LfoPreset::Square, LfoPreset::Custom
    };

    auto current = lfoData[activeLfoIndex].preset;
    int currentIdx = -1;
    for (int i = 0; i < (int)presets.size(); ++i) {
        if (presets[i] == current) { currentIdx = i; break; }
    }

    if (currentIdx < 0) currentIdx = (direction > 0) ? -1 : 0;

    int newIdx = (currentIdx + direction + (int)presets.size()) % (int)presets.size();
    applyPreset(presets[newIdx]);
}

void LfoComponent::applyPreset(LfoPreset preset) {
    // Save current waveform as the custom waveform if leaving Custom
    if (lfoData[activeLfoIndex].preset == LfoPreset::Custom && preset != LfoPreset::Custom) {
        lfoData[activeLfoIndex].customWaveform = lfoData[activeLfoIndex].waveform;
    }

    lfoData[activeLfoIndex].preset = preset;

    if (preset == LfoPreset::Custom && !lfoData[activeLfoIndex].customWaveform.nodes.empty()) {
        lfoData[activeLfoIndex].waveform = lfoData[activeLfoIndex].customWaveform;
    } else {
        lfoData[activeLfoIndex].waveform = createLfoPreset(preset);
    }

    syncGraphToActiveLfo();
    updatePresetLabel();
    audioProcessor.lfoWaveformChanged(activeLfoIndex, lfoData[activeLfoIndex].waveform);
    audioProcessor.lfoPresets[activeLfoIndex] = preset;
}

void LfoComponent::updatePresetLabel() {
    presetSelector.setPresetName(lfoPresetToString(lfoData[activeLfoIndex].preset));
}

void LfoComponent::syncFromProcessorState() {
    // Sync waveforms and presets from processor after a state load
    for (int i = 0; i < NUM_LFOS; ++i) {
        lfoData[i].waveform = audioProcessor.getLfoWaveform(i);
        lfoData[i].preset = audioProcessor.lfoPresets[i];
    }
    activeLfoIndex = juce::jlimit(0, NUM_LFOS - 1, audioProcessor.activeLfoTab);
    syncGraphToActiveLfo();
    updatePresetLabel();

    if (audioProcessor.lfoRate[activeLfoIndex] != nullptr)
        rateSlider.setValue(audioProcessor.lfoRate[activeLfoIndex]->getValueUnnormalised(), juce::dontSendNotification);

    for (auto* tab : tabHandles)
        tab->repaint();
    repaint();
}

void LfoComponent::applyLfoConstraints(int nodeIndex, double& time, double& value) {
    auto graphNodes = graph.getNodes();
    int numNodes = (int)graphNodes.size();

    // First node locked at time = 0
    if (nodeIndex == 0) {
        time = 0.0;
    }

    // Last node locked at time = 1
    if (nodeIndex == numNodes - 1) {
        time = 1.0;
    }

    // Last node mirrors first node's value for seamless looping
    if (nodeIndex == numNodes - 1 && numNodes > 1) {
        value = graphNodes[0].value;
    }

    // When first node moves, update last node to match for seamless looping
    if (nodeIndex == 0 && numNodes > 1) {
        graph.setLastNodeValue(value);
    }

    // Clamp
    time = juce::jlimit(0.0, 1.0, time);
    value = juce::jlimit(0.0, 1.0, value);
}

void LfoComponent::refreshAllDepthIndicators() {
    for (auto* tab : tabHandles) {
        tab->refreshDepthIndicators();
    }
}
