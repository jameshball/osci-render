#include "ModulationSourceComponent.h"
#include "../effects/EffectComponent.h"
#include "../../LookAndFeel.h"
#include "../../audio/modulation/ModulationTypes.h"
#include "InlineEditorHelper.h"
#include <osci_render_core/midi/osci_MidiCCManager.h>

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

#if OSCI_PREMIUM
    osci::MidiCCManager* ccMgr = owner.config.midiCCManager;
    juce::String ccId;
    if (ccMgr != nullptr
        && owner.config.buildModDepthCustomId
        && owner.config.buildModDepthSetter) {
        ccId = owner.config.buildModDepthCustomId(sourceIndex, paramId);
        auto assignment = ccMgr->getCustomAssignment(ccId);
        bool learning = ccMgr->isLearningCustom(ccId);

        menu.addSeparator();
        if (learning) {
            menu.addItem(4, "Cancel MIDI CC Learn", true, true);
        } else {
            juce::String learnText = "Learn MIDI CC Assignment";
            if (assignment.isValid())
                learnText += " (CC " + juce::String(assignment.cc)
                             + " Ch " + juce::String(assignment.channel) + ")";
            menu.addItem(4, learnText);
        }
        if (assignment.isValid()) {
            menu.addItem(5, "Remove MIDI CC Assignment (CC "
                           + juce::String(assignment.cc)
                           + " Ch " + juce::String(assignment.channel) + ")");
        }
    }
#endif

    juce::Component::SafePointer<DepthIndicator> safeThis(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
        [safeThis
#if OSCI_PREMIUM
         , ccMgr, ccId
#endif
        ](int result) {
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
#if OSCI_PREMIUM
            else if (result == 4 && ccMgr != nullptr && ccId.isNotEmpty()) {
                if (ccMgr->isLearningCustom(ccId)) {
                    ccMgr->stopLearning();
                } else {
                    auto setter = safeThis->owner.config.buildModDepthSetter(
                        safeThis->sourceIndex, safeThis->paramId);
                    if (setter)
                        ccMgr->startLearningCustom(ccId, std::move(setter));
                }
            } else if (result == 5 && ccMgr != nullptr && ccId.isNotEmpty()) {
                ccMgr->removeCustomAssignment(ccId);
            }
#endif
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
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void ModulationSourceComponent::ModTabHandle::paint(juce::Graphics& g) {
    bool active = (sourceIndex == owner.activeSourceIndex);
    bool ownerCollapsed = owner.collapsed;
    auto colour = owner.config.getSourceColour(sourceIndex);
    auto bounds = getLocalBounds().toFloat();
    constexpr float radius = 4.0f;

    juce::Path tabShape;
    if (ownerCollapsed) {
        // Collapsed: all corners rounded (floating pill look)
        tabShape.addRoundedRectangle(bounds.getX(), bounds.getY(),
                                      bounds.getWidth(), bounds.getHeight(),
                                      radius, radius,
                                      true, true, true, true);
        // All tabs look "active" when collapsed
        g.setColour(Colours::darker());
        g.fillPath(tabShape);
    } else {
        tabShape.addRoundedRectangle(bounds.getX(), bounds.getY(),
                                      bounds.getWidth(), bounds.getHeight(),
                                      radius, radius,
                                      true, true, false, false);
        if (active) {
            g.setColour(Colours::darker());
            g.fillPath(tabShape);
        } else {
            g.setColour(Colours::darker().darker(0.5f));
            g.fillPath(tabShape);

            juce::ColourGradient shadow(juce::Colours::black.withAlpha(0.2f), bounds.getCentreX(), bounds.getBottom(),
                                         juce::Colours::transparentBlack, bounds.getCentreX(), bounds.getBottom() - 12.0f, false);
            g.setGradientFill(shadow);
            g.fillPath(tabShape);
        }
    }

    // Label - top aligned
    constexpr int trackWidth = 8;
    constexpr float trackPad = 2.0f;
    constexpr float trackBottomExtra = 3.0f;
    float trackX = trackPad;
    float trackY = trackPad;
    float trackH = (float)getHeight() - trackPad * 2.0f - trackBottomExtra;
    bool hasAttachment = !depthIndicators.isEmpty();

    {
        int labelLeft = (int)(trackX + trackWidth + 2.0f);
        auto labelBounds = getLocalBounds();
        labelBounds.setLeft(labelLeft);
        labelBounds = labelBounds.withHeight(18).translated(0, 1);
        g.setColour((active || ownerCollapsed) ? juce::Colours::white : juce::Colours::white.withAlpha(0.35f));
        g.setFont(juce::Font(13.0f));
        g.drawText(label, labelBounds, juce::Justification::centred);
    }

    // Value track — pill dimensions define the track geometry
    constexpr float pillGap = 1.0f;       // uniform gap between pill and track
    float pillW = (float)trackWidth - pillGap * 2.0f;
    float pillR = pillW * 0.5f;
    float trackR = pillR + pillGap;       // track corner radius hugs the pill

    auto trackRect = juce::Rectangle<float>(trackX, trackY, (float)trackWidth, trackH);
    g.setColour(juce::Colours::black.withAlpha(0.32f));
    g.fillRoundedRectangle(trackRect, trackR);
    g.setColour(juce::Colours::white.withAlpha(0.05f));
    g.drawRoundedRectangle(trackRect, trackR, 0.5f);

    if (hasAttachment && sourceActive) {
        // Vital-style animation: pill extends between current and smoothed values
        float pillX = trackX + pillGap;

        // Inset the usable range so the pill circle has pillGap clearance at extremes
        float usableH = trackH - pillW - pillGap * 2.0f;
        float usableTop = trackY + pillR + pillGap;

        // Map values to y positions (0 = bottom, 1 = top)
        float currentY = usableTop + usableH * (1.0f - sourceValue);
        float smoothY  = usableTop + usableH * (1.0f - smoothedValue);

        float top    = juce::jmin(currentY, smoothY) - pillR;
        float bottom = juce::jmax(currentY, smoothY) + pillR;

        juce::Rectangle<float> pill(pillX, top, pillW, bottom - top);
        g.setColour(colour.withAlpha(0.9f));
        g.fillRoundedRectangle(pill, pillR);
    } else if (hoverProgress > 0.001f) {
        int trackW = (int)(trackX + trackWidth + 2.0f);
        // Icon is centred below the label text (1px top pad + 12px font height)
        constexpr int titleBottom = 13;
        auto emptyArea = getLocalBounds().withTrimmedLeft(trackW).withTrimmedTop(titleBottom).reduced(2);

        if (auto svg = juce::Drawable::createFromImageData(BinaryData::drag_svg, BinaryData::drag_svgSize)) {
            float iconSize = juce::jmin((float)emptyArea.getWidth(), (float)emptyArea.getHeight()) * 0.6f;
            iconSize = juce::jmax(iconSize, 14.0f);
            auto iconBounds = emptyArea.toFloat().withSizeKeepingCentre(iconSize, iconSize);
            float baseAlpha = active ? 0.2f : 0.15f;
            svg->drawWithin(g, iconBounds, juce::RectanglePlacement::centred, baseAlpha * hoverProgress);
        }
    }
}

void ModulationSourceComponent::ModTabHandle::mouseDown(const juce::MouseEvent&) {
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

void ModulationSourceComponent::ModTabHandle::mouseUp(const juce::MouseEvent& e) {
    if (isDragging) {
        isDragging = false;

        // If this was a click (not a drag), select tab and uncollapse
        if (e.getDistanceFromDragStart() <= 4) {
            requestActivation();
            if (owner.collapsed && owner.onUncollapseRequested)
                owner.onUncollapseRequested();
        } else {
            if (owner.onDragActiveChanged)
                owner.onDragActiveChanged(false);
        }
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

    int trackW = 12;
    int labelW = 0;
    // If no depth indicators we don't need to reserve label space separately
    // With indicators, reserve space for the label at top
    int labelH = 16;
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
    tabList.setOrientation(VerticalTabListComponent::Horizontal);
    tabList.setTabGap(kTabGap);
    tabList.setMinTabSize(kMinTabWidth);
    for (int i = 0; i < config.sourceCount; ++i) {
        auto tab = std::make_unique<ModTabHandle>(config.getLabel(i), i, *this);
        tabHandles.push_back(tab.get());
        tabList.addTab(std::move(tab));
    }
    tabList.setActiveTabIndexSilent(0);
    tabList.onTabChanged = [this](int index) { switchToSource(index); };

    tabViewport.setViewedComponent(&tabList, false);
    tabViewport.setScrollBarsShown(false, false, false, true);
    tabViewport.setColour(scrollFadeOverlayBackgroundColourId,
                          findColour(juce::ResizableWindow::backgroundColourId));
    tabViewport.setSidesEnabled(false, false, true, true);
    tabViewport.setFadeWidth(20);
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

void ModulationSourceComponent::setCollapsed(bool c) {
    if (collapsed == c) return;
    collapsed = c;
    resized();
    for (auto* th : tabHandles)
        th->repaint();
    repaint();
}

void ModulationSourceComponent::timerCallback() {
    if (config.getAssignments)
        refreshAllDepthIndicators();

    if (config.getCurrentValue) {
        // Vital-style smooth decay: smoothing factor = 15 * dt (frame-rate independent)
        float dt = (float)getTimerInterval() / 1000.0f;
        float decay = juce::jlimit(0.0f, 1.0f, 15.0f * dt);
        for (int i = 0; i < (int)tabHandles.size(); ++i) {
            float current = config.getCurrentValue(i);
            tabHandles[i]->setSourceValue(current);
            tabHandles[i]->updateSmoothedValue(current, decay);
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
        auto tabArea = bounds.removeFromTop(kTabHeight);
        tabViewport.setBounds(tabArea);
        tabViewport.setVisible(true);

        int requiredW = tabList.getRequiredSize();
        int contentW = juce::jmax(tabArea.getWidth(), requiredW);
        tabList.setBounds(0, 0, contentW, tabArea.getHeight());
    } else {
        tabViewport.setVisible(false);
    }

    if (collapsed) {
        contentBounds = {};
        outlineBounds = {};
        return;
    }

    bounds.reduce(kContentInset, kContentInset);
    contentBounds = bounds;
    outlineBounds = bounds; // default; subclass may override with narrower graph bounds
}

void ModulationSourceComponent::paint(juce::Graphics& g) {
    if (collapsed) return; // No panel background in collapsed mode

    const bool hasTabs = config.sourceCount > 1 || config.alwaysShowTabs;
    const float tabOffset = hasTabs ? (float)kTabHeight : 0.0f;
    auto panelBounds = getLocalBounds().toFloat().withTrimmedTop(tabOffset);
    float r = OscirenderLookAndFeel::RECT_RADIUS;
    juce::Path panelPath;
    // When there are no tabs, round all four corners; otherwise only the bottom ones
    panelPath.addRoundedRectangle(panelBounds.getX(), panelBounds.getY(),
                                   panelBounds.getWidth(), panelBounds.getHeight(),
                                   r, r, !hasTabs, !hasTabs, true, true);
    g.setColour(Colours::darker());
    g.fillPath(panelPath);
}

void ModulationSourceComponent::paintOverChildren(juce::Graphics& g) {
    if (collapsed) return; // No overlays in collapsed mode

    const bool hasTabs = config.sourceCount > 1 || config.alwaysShowTabs;
    const float tabOffset = hasTabs ? (float)kTabHeight : 0.0f;
    auto panelBounds = getLocalBounds().toFloat().withTrimmedTop(tabOffset);
    juce::Path panelPath;
    panelPath.addRoundedRectangle(panelBounds.getX(), panelBounds.getY(),
                                   panelBounds.getWidth(), panelBounds.getHeight(),
                                   OscirenderLookAndFeel::RECT_RADIUS, OscirenderLookAndFeel::RECT_RADIUS,
                                   !hasTabs, !hasTabs, true, true);

    if (!hasTabs) {
        // No tabs — just draw the source outline
        if (config.getSourceColour && !outlineBounds.isEmpty()) {
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

    // Shadow along the bottom edge of tabs, excluding the active tab
    juce::RectangleList<int> clipRegion;
    clipRegion.add(0, kTabHeight - kSeamShadowHeight, getWidth(), kSeamShadowHeight);
    clipRegion.subtract(activeTabBounds);

    g.saveState();
    g.reduceClipRegion(clipRegion);
    panelEdgeShadow.render(g, panelPath);
    g.restoreState();

    // Source-coloured rounded border around the graph area
    if (config.getSourceColour && !outlineBounds.isEmpty()) {
        auto colour = config.getSourceColour(activeSourceIndex);
        auto graphBounds = outlineBounds.toFloat();
        g.setColour(colour.withAlpha(0.25f));
        g.drawRoundedRectangle(graphBounds, 6.0f, 1.0f);
    }
}

void ModulationSourceComponent::switchToSource(int index) {
    if (index < 0 || index >= config.sourceCount) return;

    bool changed = (index != activeSourceIndex);
    activeSourceIndex = index;
    tabList.setActiveTabIndexSilent(index);
    if (config.setActiveTab)
        config.setActiveTab(index);

    if (changed)
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
