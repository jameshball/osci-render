#include "ModulationSourceComponent.h"
#include "../effects/EffectComponent.h"
#include "../../LookAndFeel.h"
#include "../../audio/modulation/ModulationTypes.h"
#include "InlineEditorHelper.h"

// ============================================================================
// DepthIndicator – small arc knob for a single source→param connection
// ============================================================================

ModulationSourceComponent::DepthIndicator::DepthIndicator(
        ModulationSourceComponent& o, int idx, juce::String pid, float d, bool bp)
    : owner(o), sourceIndex(idx), paramId(pid), depth(d), bipolar(bp) {
    setSize(14, 14);
}

void ModulationSourceComponent::DepthIndicator::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat().reduced(1.5f);
    float cx = bounds.getCentreX();
    float cy = bounds.getCentreY();
    float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;

    auto colour = owner.config.getSourceColour(sourceIndex);

    g.setColour(Colours::veryDark());
    g.fillEllipse(bounds);

    g.setColour(colour.withAlpha(0.2f));
    g.drawEllipse(bounds, 1.0f);

    float p = getAnimationProgress();
    float arcThickness = getWidth() * juce::jmap(p, 0.0f, 1.0f, 0.20f, 0.25f);
    float arcRadius = radius - arcThickness * 0.5f;
    constexpr float topAngle = 0.0f;

    if (std::abs(depth) > 0.005f) {
        float sweep = depth * juce::MathConstants<float>::twoPi;
        sweep = juce::jlimit(-6.27f, 6.27f, sweep);
        juce::Path arc;
        arc.addCentredArc(cx, cy, arcRadius, arcRadius, 0.0f,
                          topAngle, topAngle + sweep, true);
        float alpha = juce::jmap(p, 0.0f, 1.0f, 0.8f, 1.0f);
        g.setColour(colour.withAlpha(alpha));
        g.strokePath(arc, juce::PathStrokeType(arcThickness,
                     juce::PathStrokeType::curved, juce::PathStrokeType::butt));
    }
}

void ModulationSourceComponent::DepthIndicator::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu()) {
        showRightClickMenu();
        return;
    }
    dragging = true;
    dragStartDepth = depth;
    showValuePopup();
}

void ModulationSourceComponent::DepthIndicator::mouseDrag(const juce::MouseEvent& e) {
    if (!dragging) return;
    float dy = (float)(e.getPosition().y - e.getMouseDownPosition().y);
    float delta = -dy / 80.0f;
    float newDepth = dragStartDepth + delta;
    depth = juce::jlimit(-1.0f, 1.0f, newDepth);
    owner.config.addAssignment({ sourceIndex, paramId, depth, bipolar });
    EffectComponent::modRangeDepth = depth;
    showValuePopup();
    repaint();
    owner.repaint();
    if (auto* topLevel = owner.getTopLevelComponent(); topLevel != nullptr && topLevel != &owner)
        topLevel->repaint();
}

void ModulationSourceComponent::DepthIndicator::mouseUp(const juce::MouseEvent&) {
    dragging = false;
    hideValuePopup();
}

void ModulationSourceComponent::DepthIndicator::mouseEnter(const juce::MouseEvent& e) {
    HoverAnimationMixin::mouseEnter(e);
    hovering = true;
    EffectComponent::highlightedParamId = paramId;
    EffectComponent::modRangeParamId = paramId;
    EffectComponent::modRangeDepth = depth;
    EffectComponent::modRangeBipolar = bipolar;
    showValuePopup();
    owner.repaint();
    if (auto* topLevel = owner.getTopLevelComponent(); topLevel != nullptr && topLevel != &owner)
        topLevel->repaint();
}

void ModulationSourceComponent::DepthIndicator::mouseExit(const juce::MouseEvent& e) {
    HoverAnimationMixin::mouseExit(e);
    hovering = false;
    EffectComponent::highlightedParamId = juce::String();
    EffectComponent::modRangeParamId = juce::String();
    if (!dragging)
        hideValuePopup();
    owner.repaint();
    if (auto* topLevel = owner.getTopLevelComponent(); topLevel != nullptr && topLevel != &owner)
        topLevel->repaint();
}

void ModulationSourceComponent::DepthIndicator::mouseDoubleClick(const juce::MouseEvent&) {
    // Clear highlight state immediately so the effect list stops highlighting
    // without waiting for a mouse-move to trigger a repaint.
    hovering = false;
    EffectComponent::highlightedParamId = juce::String();
    EffectComponent::modRangeParamId = juce::String();
    hideValuePopup();
    if (auto* topLevel = owner.getTopLevelComponent(); topLevel != nullptr && topLevel != &owner)
        topLevel->repaint();
    owner.repaint();
    removeAndRefresh();
}

void ModulationSourceComponent::DepthIndicator::removeAndRefresh() {
    owner.config.removeAssignment(sourceIndex, paramId);

    juce::Component::SafePointer<ModulationSourceComponent> safeOwner(&owner);
    juce::MessageManager::callAsync([safeOwner]() {
        if (safeOwner == nullptr) return;
        safeOwner->refreshAllDepthIndicators();
        safeOwner->repaint();
    });
}

void ModulationSourceComponent::DepthIndicator::showRightClickMenu() {
    juce::PopupMenu menu;
    menu.addItem(1, "Remove");
    menu.addItem(2, bipolar ? "Make Unipolar" : "Make Bipolar");
    menu.addSeparator();
    menu.addItem(3, "Enter Value");

    juce::Component::SafePointer<DepthIndicator> safeThis(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
        [safeThis](int result) {
            if (safeThis == nullptr) return;
            if (result == 1) {
                safeThis->removeAndRefresh();
            } else if (result == 2) {
                safeThis->bipolar = !safeThis->bipolar;
                safeThis->owner.config.addAssignment(
                    { safeThis->sourceIndex, safeThis->paramId, safeThis->depth, safeThis->bipolar });
                safeThis->repaint();
            } else if (result == 3) {
                safeThis->startTextEdit();
            }
        });
}

void ModulationSourceComponent::DepthIndicator::startTextEdit() {
    if (inlineEditor) return;
    auto commitFn = [this](const juce::String& text) {
        float val = text.getFloatValue() / 100.0f;
        depth = juce::jlimit(-1.0f, 1.0f, val);
        owner.config.addAssignment({ sourceIndex, paramId, depth, bipolar });
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
        owner.config.getSourceColour(sourceIndex), 11.0f);
    addAndMakeVisible(inlineEditor.get());
    inlineEditor->grabKeyboardFocus();
}

void ModulationSourceComponent::DepthIndicator::showValuePopup() {
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

    juce::String paramName = owner.config.getParamDisplayName
        ? owner.config.getParamDisplayName(paramId)
        : paramId;

    int pct = (int)(depth * 100.0f);
    valuePopup->setText(paramName + " " + juce::String(pct) + "%", juce::dontSendNotification);

    int popupW = valuePopup->getFont().getStringWidth(valuePopup->getText()) + 14;
    int popupH = 20;
    auto screenPos = localPointToGlobal(juce::Point<int>(getWidth() / 2, 0));
    valuePopup->setBounds(screenPos.x - popupW / 2, screenPos.y - popupH - 4, popupW, popupH);
    valuePopup->setVisible(true);
}

void ModulationSourceComponent::DepthIndicator::hideValuePopup() {
    valuePopup.reset();
}

// ============================================================================
// ModTabHandle – tab label + value bar + drag source + depth indicators
// ============================================================================

ModulationSourceComponent::ModTabHandle::ModTabHandle(
        const juce::String& l, int idx, ModulationSourceComponent& o)
    : label(l), sourceIndex(idx), owner(o) {
    setMouseCursor(juce::MouseCursor::DraggingHandCursor);
}

void ModulationSourceComponent::ModTabHandle::paint(juce::Graphics& g) {
    bool active = isActiveTab();
    auto colour = owner.config.getSourceColour(sourceIndex);
    auto bounds = getLocalBounds().toFloat();
    constexpr float radius = 4.0f;

    juce::Path tabShape;
    tabShape.addRoundedRectangle(bounds.getX(), bounds.getY(),
                                  bounds.getWidth(), bounds.getHeight(),
                                  radius, radius,
                                  true, false, true, false);

    if (active) {
        g.setColour(Colours::darker());
        g.fillPath(tabShape);
    } else {
        g.setColour(Colours::darker().darker(0.5f));
        g.fillPath(tabShape);

        juce::ColourGradient shadow(juce::Colours::black.withAlpha(0.2f), bounds.getRight(), bounds.getCentreY(),
                                     juce::Colours::transparentBlack, bounds.getRight() - 12.0f, bounds.getCentreY(), false);
        g.setGradientFill(shadow);
        g.fillPath(tabShape);
    }

    constexpr int trackWidth = 8;
    constexpr float trackPad = 2.0f;
    float trackX = trackPad;
    float trackY = trackPad;
    float trackH = (float)getHeight() - trackPad * 2.0f;
    bool hasAttachment = !depthIndicators.isEmpty();

    // Label
    {
        int labelLeft = (int)(trackX + trackWidth + 2.0f);
        auto labelBounds = getLocalBounds().removeFromTop(20);
        labelBounds.setLeft(labelLeft);
        g.setColour(active ? juce::Colours::white : juce::Colours::white.withAlpha(0.35f));
        g.setFont(juce::Font(13.0f));
        g.drawText(label, labelBounds, juce::Justification::centred);
    }

    // Value track
    auto trackRect = juce::Rectangle<float>(trackX, trackY, (float)trackWidth, trackH);
    g.setColour(juce::Colours::black.withAlpha(0.32f));
    g.fillRoundedRectangle(trackRect, (float)trackWidth * 0.5f);
    g.setColour(juce::Colours::white.withAlpha(0.05f));
    g.drawRoundedRectangle(trackRect, (float)trackWidth * 0.5f, 0.5f);

    if (hasAttachment && sourceActive) {
        constexpr float pillBaseH = 5.0f;
        constexpr float pillMaxStretch = 14.0f;
        float stretch = juce::jlimit(0.0f, pillMaxStretch, std::abs(sourceDelta) * 80.0f);
        float pillW = (float)trackWidth - 2.0f;
        float pillH = pillBaseH + stretch;

        float pillArea = trackH - pillBaseH;
        float pillCentreY = trackY + pillArea * (1.0f - sourceValue) + pillBaseH * 0.5f;
        float pillX = trackX + 1.0f;

        juce::Rectangle<float> pill(pillX, pillCentreY - pillH * 0.5f, pillW, pillH);
        g.setColour(colour.withAlpha(0.9f));
        g.fillRoundedRectangle(pill, pillW * 0.5f);
    } else if (hoverProgress > 0.001f) {
        int labelH = 20;
        int trackW = (int)(trackX + trackWidth + 2.0f);
        auto emptyArea = getLocalBounds().withTrimmedTop(labelH).withTrimmedLeft(trackW).reduced(2);

        if (auto svg = juce::Drawable::createFromImageData(BinaryData::drag_svg, BinaryData::drag_svgSize)) {
            float iconSize = juce::jmin((float)emptyArea.getWidth(), (float)emptyArea.getHeight()) * 0.45f;
            iconSize = juce::jmax(iconSize, 10.0f);
            auto iconBounds = emptyArea.toFloat().withSizeKeepingCentre(iconSize, iconSize)
                                  .translated(0.0f, -3.0f);
            float baseAlpha = active ? 0.2f : 0.15f;
            svg->drawWithin(g, iconBounds, juce::RectanglePlacement::centred, baseAlpha * hoverProgress);
        }
    }
}

void ModulationSourceComponent::ModTabHandle::mouseDown(const juce::MouseEvent&) {
    requestActivation();
    isDragging = true;
    repaint();
}

void ModulationSourceComponent::ModTabHandle::mouseDrag(const juce::MouseEvent& e) {
    if (!isDragging) return;

    if (e.getDistanceFromDragStart() > 4) {
        juce::String desc = ModDrag::make(owner.config.dragPrefix, sourceIndex);

        juce::Image dragImage(juce::Image::ARGB, 50, 20, true);
        {
            juce::Graphics g(dragImage);
            auto colour = owner.config.getSourceColour(sourceIndex);
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
        isDragging = false;
    }
}

void ModulationSourceComponent::ModTabHandle::mouseUp(const juce::MouseEvent&) {
    if (isDragging) {
        isDragging = false;
        if (owner.onDragActiveChanged)
            owner.onDragActiveChanged(false);
    }
}

void ModulationSourceComponent::ModTabHandle::mouseEnter(const juce::MouseEvent&) {
    isHovering = true;
    float startP = hoverProgress;
    hoverAnim = juce::ValueAnimatorBuilder{}
        .withOnStartReturningValueChangedCallback([this, startP] {
            return [this, startP](float p) {
                hoverProgress = startP + p * (1.0f - startP);
                repaint();
            };
        })
        .withDurationMs(150.0)
        .build();
    hoverAnim->start();
    hoverAnimUpdater.addAnimator(*hoverAnim);
}

void ModulationSourceComponent::ModTabHandle::mouseExit(const juce::MouseEvent&) {
    isHovering = false;
    float startP = hoverProgress;
    hoverAnim = juce::ValueAnimatorBuilder{}
        .withOnStartReturningValueChangedCallback([this, startP] {
            return [this, startP](float p) {
                hoverProgress = startP * (1.0f - p);
                repaint();
            };
        })
        .withDurationMs(150.0)
        .build();
    hoverAnim->start();
    hoverAnimUpdater.addAnimator(*hoverAnim);
}

void ModulationSourceComponent::ModTabHandle::resized() {
    int n = depthIndicators.size();
    if (n == 0) return;

    int labelH = 16;
    int trackW = 12;
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

void ModulationSourceComponent::ModTabHandle::refreshDepthIndicators(
        const std::vector<ModAssignment>& assignments) {
    std::vector<ModAssignment> myAssignments;
    for (const auto& a : assignments) {
        if (a.sourceIndex == sourceIndex)
            myAssignments.push_back(a);
    }

    bool anyInteracting = false;
    for (auto* ind : depthIndicators) {
        if (ind->dragging || ind->hovering) {
            anyInteracting = true;
            break;
        }
    }

    if (anyInteracting) {
        for (auto* ind : depthIndicators) {
            for (const auto& a : myAssignments) {
                if (a.paramId == ind->paramId) {
                    if (!ind->dragging) {
                        ind->depth = a.depth;
                        ind->bipolar = a.bipolar;
                    }
                    break;
                }
            }
        }
        return;
    }

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

    depthIndicators.clear();
    for (const auto& a : myAssignments) {
        auto* indicator = new DepthIndicator(owner, sourceIndex, a.paramId, a.depth, a.bipolar);
        addAndMakeVisible(indicator);
        depthIndicators.add(indicator);
    }
    resized();
    repaint();
}

// ============================================================================
// ModulationSourceComponent
// ============================================================================

ModulationSourceComponent::ModulationSourceComponent(const ModulationSourceConfig& cfg)
    : config(cfg) {
    tabList.setTabGap(kTabGap);
    tabList.setMinTabHeight(kMinTabHeight);
    for (int i = 0; i < config.sourceCount; ++i) {
        auto tab = std::make_unique<ModTabHandle>(config.getLabel(i), i, *this);
        tabHandles.push_back(tab.get());
        tabList.addTab(std::move(tab));
    }
    tabList.setActiveTabIndexSilent(0);
    tabList.onTabChanged = [this](int index) { switchToSource(index); };

    tabViewport.setViewedComponent(&tabList, false);
    tabViewport.setScrollBarsShown(false, false, true, false);
    tabViewport.setColour(scrollFadeOverlayBackgroundColourId,
                          findColour(juce::ResizableWindow::backgroundColourId));
    tabViewport.setSidesEnabled(true, true);
    tabViewport.setFadeHeight(20);
    addAndMakeVisible(tabViewport);

    startTimerHz(60);

    if (config.broadcaster)
        config.broadcaster->addChangeListener(this);
}

ModulationSourceComponent::~ModulationSourceComponent() {
    stopTimer();
    if (config.broadcaster)
        config.broadcaster->removeChangeListener(this);
}

void ModulationSourceComponent::timerCallback() {
    if (config.getAssignments)
        refreshAllDepthIndicators();

    if (config.getCurrentValue) {
        for (int i = 0; i < (int)tabHandles.size(); ++i) {
            tabHandles[i]->setSourceValue(config.getCurrentValue(i));
            if (config.isSourceActive)
                tabHandles[i]->setSourceActive(config.isSourceActive(i));
        }
    }
}

void ModulationSourceComponent::changeListenerCallback(juce::ChangeBroadcaster* source) {
    if (source == config.broadcaster)
        syncFromProcessorState();
}

void ModulationSourceComponent::resized() {
    auto bounds = getLocalBounds();

    const bool showTabs = config.sourceCount > 1 || config.alwaysShowTabs;
    if (showTabs) {
        auto tabArea = bounds.removeFromLeft(kTabWidth);
        tabViewport.setBounds(tabArea);
        tabViewport.setVisible(true);

        int requiredH = tabList.getRequiredHeight();
        int contentH = juce::jmax(tabArea.getHeight(), requiredH);
        tabList.setBounds(0, 0, tabArea.getWidth(), contentH);
    } else {
        tabViewport.setVisible(false);
    }

    bounds.reduce(kContentInset, kContentInset);
    contentBounds = bounds;
    outlineBounds = bounds; // default; subclass may override with narrower graph bounds
}

void ModulationSourceComponent::paint(juce::Graphics& g) {
    const bool hasTabs = config.sourceCount > 1 || config.alwaysShowTabs;
    const float tabOffset = hasTabs ? (float)kTabWidth : 0.0f;
    auto panelBounds = getLocalBounds().toFloat().withTrimmedLeft(tabOffset);
    float r = OscirenderLookAndFeel::RECT_RADIUS;
    juce::Path panelPath;
    // When there are no tabs (free version), round all four corners; otherwise only the right ones
    panelPath.addRoundedRectangle(panelBounds.getX(), panelBounds.getY(),
                                   panelBounds.getWidth(), panelBounds.getHeight(),
                                   r, r, !hasTabs, true, !hasTabs, true);
    g.setColour(Colours::darker());
    g.fillPath(panelPath);
}

void ModulationSourceComponent::paintOverChildren(juce::Graphics& g) {
    const bool hasTabs = config.sourceCount > 1 || config.alwaysShowTabs;
    const float tabOffset = hasTabs ? (float)kTabWidth : 0.0f;
    auto panelBounds = getLocalBounds().toFloat().withTrimmedLeft(tabOffset);
    juce::Path panelPath;
    panelPath.addRoundedRectangle(panelBounds.getX(), panelBounds.getY(),
                                   panelBounds.getWidth(), panelBounds.getHeight(),
                                   OscirenderLookAndFeel::RECT_RADIUS, OscirenderLookAndFeel::RECT_RADIUS,
                                   !hasTabs, true, !hasTabs, true);

    if (!hasTabs) {
        // No tabs — just draw the source outline
        if (config.getSourceColour) {
            auto colour = config.getSourceColour(activeSourceIndex);
            auto graphBounds = outlineBounds.toFloat();
            g.setColour(colour.withAlpha(0.25f));
            g.drawRoundedRectangle(graphBounds, 6.0f, 1.0f);
        }
        return;
    }

    if (tabList.getNumTabs() == 0 || activeSourceIndex < 0 || activeSourceIndex >= tabList.getNumTabs())
        return;
    auto* activeTab = tabList.getTab(activeSourceIndex);
    auto activeTabBounds = getLocalArea(&tabList, activeTab->getBounds());
    juce::RectangleList<int> clipRegion;
    clipRegion.add(kTabWidth - kSeamShadowWidth, 0, kSeamShadowWidth, getHeight());
    clipRegion.subtract(activeTabBounds);

    g.saveState();
    g.reduceClipRegion(clipRegion);
    panelEdgeShadow.render(g, panelPath);
    g.restoreState();

    // Source-coloured rounded border around the graph area
    if (config.getSourceColour) {
        auto colour = config.getSourceColour(activeSourceIndex);
        auto graphBounds = outlineBounds.toFloat();
        g.setColour(colour.withAlpha(0.25f));
        g.drawRoundedRectangle(graphBounds, 6.0f, 1.0f);
    }
}

void ModulationSourceComponent::switchToSource(int index) {
    if (index < 0 || index >= config.sourceCount) return;
    if (index == activeSourceIndex) return;

    activeSourceIndex = index;
    if (config.setActiveTab)
        config.setActiveTab(index);

    onActiveSourceChanged(index);

    for (auto* th : tabHandles)
        th->repaint();
    repaint();
}

void ModulationSourceComponent::syncFromProcessorState() {
    if (config.getActiveTab) {
        activeSourceIndex = juce::jlimit(0, config.sourceCount - 1, config.getActiveTab());
        tabList.setActiveTabIndexSilent(activeSourceIndex);
    }
    refreshAllDepthIndicators();
    repaint();
}

void ModulationSourceComponent::refreshAllDepthIndicators() {
    if (!config.getAssignments) return;
    auto assignments = config.getAssignments();
    for (auto* th : tabHandles)
        th->refreshDepthIndicators(assignments);
}
