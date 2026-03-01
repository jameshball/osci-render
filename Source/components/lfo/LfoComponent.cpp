#include "LfoComponent.h"
#include "../EffectComponent.h"
#include "../../PluginProcessor.h"
#include "../../LookAndFeel.h"
#include "InlineEditorHelper.h"

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
    depth = juce::jlimit(-1.0f, 1.0f, newDepth);
    owner.audioProcessor.addLfoAssignment({ lfoIndex, paramId, depth, bipolar });
    // Keep the slider range overlay in sync during drag
    EffectComponent::lfoRangeDepth = depth;
    showValuePopup();
    repaint();
    owner.repaint();
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
    owner.repaint();
}

void LfoComponent::DepthIndicator::mouseExit(const juce::MouseEvent&) {
    hovering = false;
    EffectComponent::highlightedParamId = juce::String();
    EffectComponent::lfoRangeParamId = juce::String();
    if (!dragging)
        hideValuePopup();
    repaint();
    owner.repaint();
}

void LfoComponent::DepthIndicator::mouseDoubleClick(const juce::MouseEvent&) {
    removeAndRefresh();
}

void LfoComponent::DepthIndicator::removeAndRefresh() {
    owner.audioProcessor.removeLfoAssignment(lfoIndex, paramId);

    // Async to avoid mutating component hierarchy mid-callback
    juce::Component::SafePointer<LfoComponent> safeOwner(&owner);
    juce::MessageManager::callAsync([safeOwner]() {
        if (safeOwner == nullptr)
            return;
        safeOwner->refreshAllDepthIndicators(safeOwner->audioProcessor.getLfoAssignments());
        safeOwner->repaint();
    });
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
                removeAndRefresh();
            } else if (result == 2) {
                bipolar = !bipolar;
                owner.audioProcessor.addLfoAssignment({ lfoIndex, paramId, depth, bipolar });
                repaint();
            } else if (result == 3) {
                startTextEdit();
            }
        });
}

void LfoComponent::DepthIndicator::startTextEdit() {
    if (inlineEditor) return;
    auto commitFn = [this](const juce::String& text) {
        float val = text.getFloatValue() / 100.0f;
        depth = juce::jlimit(-1.0f, 1.0f, val);
        owner.audioProcessor.addLfoAssignment({ lfoIndex, paramId, depth, bipolar });
        inlineEditor.reset();
        repaint();
    };
    auto cancelFn = [this]() {
        inlineEditor.reset();
        repaint();
    };
    inlineEditor = InlineEditorHelper::create(
        juce::String((int)(depth * 100.0f)),
        getLocalBounds().expanded(8, 2),
        { commitFn, cancelFn },
        Dracula::background, Dracula::foreground,
        LfoComponent::getLfoColour(lfoIndex), 11.0f);
    addAndMakeVisible(inlineEditor.get());
    inlineEditor->grabKeyboardFocus();
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
    : label(l), lfoIndex(index), owner(o) {
    setMouseCursor(juce::MouseCursor::DraggingHandCursor);
}

void LfoComponent::LfoTabHandle::paint(juce::Graphics& g) {
    bool active = (lfoIndex == owner.activeLfoIndex);
    auto colour = LfoComponent::getLfoColour(lfoIndex);
    auto bounds = getLocalBounds().toFloat();
    constexpr float radius = 4.0f;

    // Tab shape: rounded on left side only (flat right edge connects to panel)
    juce::Path tabShape;
    tabShape.addRoundedRectangle(bounds.getX(), bounds.getY(),
                                  bounds.getWidth(), bounds.getHeight(),
                                  radius, radius,
                                  true,  false,   // top-left, top-right
                                  true,  false);   // bottom-left, bottom-right

    if (active) {
        g.setColour(Colours::darker);
        g.fillPath(tabShape);
    } else {
        g.setColour(Colours::darker.darker(0.5f));
        g.fillPath(tabShape);

        // Subtle inner shadow (darker at the right edge, toward the panel)
        juce::ColourGradient shadow(juce::Colours::black.withAlpha(0.2f), bounds.getRight(), bounds.getCentreY(),
                                     juce::Colours::transparentBlack, bounds.getRight() - 12.0f, bounds.getCentreY(), false);
        g.setGradientFill(shadow);
        g.fillPath(tabShape);
    }

    // Separate LFO value track on the left — full height of tab
    constexpr int trackWidth = 8;
    constexpr float trackPad = 2.0f;
    float trackX = trackPad;
    float trackY = trackPad;
    float trackH = (float)getHeight() - trackPad * 2.0f;
    bool hasAttachment = !depthIndicators.isEmpty();

    // Label centred in the remaining area (to the right of the track)
    {
        int labelLeft = (int)(trackX + trackWidth + 2.0f);
        auto labelBounds = getLocalBounds().removeFromTop(20);
        labelBounds.setLeft(labelLeft);
        g.setColour(active ? juce::Colours::white : juce::Colours::white.withAlpha(0.35f));
        g.setFont(juce::Font(13.0f));
        g.drawText(label, labelBounds, juce::Justification::centred);
    }

    // Always draw the value track (cutout-style rail)
    auto trackRect = juce::Rectangle<float>(trackX, trackY, (float)trackWidth, trackH);
    g.setColour(juce::Colours::black.withAlpha(0.32f));
    g.fillRoundedRectangle(trackRect, (float)trackWidth * 0.5f);
    g.setColour(juce::Colours::white.withAlpha(0.05f));
    g.drawRoundedRectangle(trackRect, (float)trackWidth * 0.5f, 0.5f);

    if (hasAttachment) {
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
    } else if (isHovering) {
        // No connections — draw the drag hint icon on hover only
        int labelH = 20;
        int trackW = (int)(trackX + trackWidth + 2.0f);
        auto emptyArea = getLocalBounds().withTrimmedTop(labelH).withTrimmedLeft(trackW).reduced(2);

        if (auto svg = juce::Drawable::createFromImageData(BinaryData::drag_svg, BinaryData::drag_svgSize)) {
            float iconSize = juce::jmin((float)emptyArea.getWidth(), (float)emptyArea.getHeight()) * 0.45f;
            iconSize = juce::jmax(iconSize, 10.0f);
            auto iconBounds = emptyArea.toFloat().withSizeKeepingCentre(iconSize, iconSize)
                                  .translated(0.0f, -3.0f);
            svg->drawWithin(g, iconBounds, juce::RectanglePlacement::centred, active ? 0.2f : 0.15f);
        }
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

void LfoComponent::LfoTabHandle::mouseEnter(const juce::MouseEvent&) {
    isHovering = true;
    repaint();
}

void LfoComponent::LfoTabHandle::mouseExit(const juce::MouseEvent&) {
    isHovering = false;
    repaint();
}

void LfoComponent::LfoTabHandle::resized() {
    // Lay out depth indicators in a grid below the label, to the right of the track
    int n = depthIndicators.size();
    if (n == 0) return;

    int labelH = 16;
    int trackW = 12; // track (8px) + gap (4px)
    int availW = getWidth() - trackW - 1;
    int availH = getHeight() - labelH - 1;

    int maxSize = juce::jmin(28, juce::jmax(14, juce::jmin(availW, availH) / juce::jmax(1, n)));
    int minSize = 8;
    int spacing = 2;

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

void LfoComponent::LfoTabHandle::refreshDepthIndicators(const std::vector<LfoAssignment>& assignments) {

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

LfoComponent::PresetSelector::PresetSelector() {
    setRepaintsOnMouseActivity(true);
}

void LfoComponent::PresetSelector::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();

    // Dark rounded background (matches LfoRateComponent)
    g.setColour(Colours::veryDark);
    g.fillRoundedRectangle(bounds, 4.0f);

    // Subtle border
    bool hovering = isMouseOver(true);
    g.setColour(juce::Colours::white.withAlpha(hovering ? 0.12f : 0.06f));
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
    : audioProcessor(processor), rateControl(processor, 0) {
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

    // Rate control (Vital-style multi-mode)
    rateControl.setLfoIndex(activeLfoIndex);
    addAndMakeVisible(rateControl);

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
    refreshAllDepthIndicators(audioProcessor.getLfoAssignments());
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

    // Tab handles on the left — flush with component edges, small gap between tabs
    auto tabArea = bounds.removeFromLeft(kTabWidth);
    int totalGaps = kTabGap * (NUM_LFOS - 1);
    int availH = tabArea.getHeight() - totalGaps;
    int tabH = availH / NUM_LFOS;
    int extra = availH - tabH * NUM_LFOS;
    for (int i = 0; i < tabHandles.size(); i++) {
        int h = tabH + (i < extra ? 1 : 0);
        tabHandles[i]->setBounds(tabArea.removeFromTop(h));
        if (i < tabHandles.size() - 1)
            tabArea.removeFromTop(kTabGap);
    }

    // Inset the main content area uniformly
    bounds.reduce(kContentInset, kContentInset);

    // Top bar: preset selector right-aligned above the graph (no label)
    auto topBar = bounds.removeFromTop(kTopBarHeight);
    bounds.removeFromTop(kTopBarGap);
    int presetW = juce::jmin(kMaxPresetWidth, topBar.getWidth() / 3);
    presetSelector.setBounds(topBar.removeFromRight(presetW));

    // Bottom bar: rate control below the graph
    auto rateRow = bounds.removeFromBottom(kRateHeight);
    bounds.removeFromBottom(kRateGap);
    int rateW = juce::jmin(kMaxRateWidth, rateRow.getWidth() / 3);
    rateControl.setBounds(rateRow.removeFromLeft(rateW));

    // Graph gets the remaining space
    graph.setBounds(bounds);
}

void LfoComponent::paint(juce::Graphics& g) {
    // Panel background — only the main content area (right of tabs)
    // Rounded on the right side only (flat left edge where tabs connect)
    auto panelBounds = getLocalBounds().toFloat().withTrimmedLeft((float)kTabWidth);
    float r = OscirenderLookAndFeel::RECT_RADIUS;
    juce::Path panelPath;
    panelPath.addRoundedRectangle(panelBounds.getX(), panelBounds.getY(),
                                   panelBounds.getWidth(), panelBounds.getHeight(),
                                   r, r,
                                   false, true,   // top-left, top-right
                                   false, true);   // bottom-left, bottom-right
    g.setColour(Colours::darker);
    g.fillPath(panelPath);
}

void LfoComponent::paintOverChildren(juce::Graphics& g) {
    // Drop shadow from panel edge onto inactive tabs using melatonin blur.
    // Build a path for the panel shape.
    auto panelBounds = getLocalBounds().toFloat().withTrimmedLeft((float)kTabWidth);
    juce::Path panelPath;
    panelPath.addRoundedRectangle(panelBounds.getX(), panelBounds.getY(),
                                   panelBounds.getWidth(), panelBounds.getHeight(),
                                   OscirenderLookAndFeel::RECT_RADIUS, OscirenderLookAndFeel::RECT_RADIUS,
                                   false, true, false, true);

    // Clip to a narrow strip on the tab side of the seam only,
    // and exclude the active tab so it stays connected to the panel.
    if (tabHandles.isEmpty() || activeLfoIndex < 0 || activeLfoIndex >= tabHandles.size())
        return;
    auto activeTabBounds = tabHandles[activeLfoIndex]->getBounds();
    juce::RectangleList<int> clipRegion;
    clipRegion.add(kTabWidth - kSeamShadowWidth, 0, kSeamShadowWidth, getHeight());
    clipRegion.subtract(activeTabBounds);

    g.saveState();
    g.reduceClipRegion(clipRegion);
    panelEdgeShadow.render(g, panelPath);
    g.restoreState();

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

    // Update rate control to match this LFO's rate mode and value
    rateControl.setLfoIndex(index);
    rateControl.syncFromProcessor();

    for (auto* tab : tabHandles)
        tab->repaint();
    repaint();
}

void LfoComponent::syncGraphToActiveLfo() {
    auto& waveform = lfoData[activeLfoIndex].waveform;
    graph.setNodes(waveform.nodes);

    // Update graph line colour to match LFO
    auto colour = getLfoColour(activeLfoIndex);
    graph.setColour(NodeGraphComponent::lineColourId, colour);
    graph.setColour(NodeGraphComponent::fillColourId, colour.withAlpha(0.15f));
    graph.setColour(NodeGraphComponent::nodeColourId, colour);
}

void LfoComponent::syncActiveLfoFromGraph() {
    lfoData[activeLfoIndex].waveform.nodes = graph.getNodes();

    // Mark as custom if nodes were manually edited
    lfoData[activeLfoIndex].preset = LfoPreset::Custom;
    updatePresetLabel();

    // Notify processor
    audioProcessor.lfoWaveformChanged(activeLfoIndex, lfoData[activeLfoIndex].waveform);
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

    rateControl.setLfoIndex(activeLfoIndex);
    rateControl.syncFromProcessor();

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

void LfoComponent::refreshAllDepthIndicators(const std::vector<LfoAssignment>& assignments) {
    for (auto* tab : tabHandles) {
        tab->refreshDepthIndicators(assignments);
    }
}
