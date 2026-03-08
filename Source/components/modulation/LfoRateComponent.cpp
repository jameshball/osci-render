#include "LfoRateComponent.h"
#include "../../PluginProcessor.h"
#include "../../LookAndFeel.h"
#include "InlineEditorHelper.h"

LfoRateComponent::LfoRateComponent(OscirenderAudioProcessor& processor, int index)
    : audioProcessor(processor), lfoIndex(index)
{
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    setRepaintsOnMouseActivity(true);
}

LfoRateComponent::~LfoRateComponent() {}

// ============================================================================
// Display helpers
// ============================================================================

// ============================================================================
// Icon drawing helpers
// ============================================================================

// Draw a single filled note head + stem (crotchet / quarter note style).
// headCx/headCy = centre of the head ellipse. stemHeight = upward stem length.
static void drawStemmedNote(juce::Graphics& g, float headCx, float headCy,
                            float headRx, float headRy, float stemHeight, float stemW) {
    // Head
    g.fillEllipse(headCx - headRx, headCy - headRy, headRx * 2.0f, headRy * 2.0f);
    // Stem (right edge of head, going up)
    float stemX = headCx + headRx - stemW * 0.5f - 0.3f;
    g.fillRect(stemX, headCy - stemHeight, stemW, stemHeight);
}

void LfoRateComponent::drawHzIcon(juce::Graphics& g, juce::Rectangle<float> area) {
    g.setFont(juce::Font(11.0f, juce::Font::bold));
    g.drawText("Hz", area, juce::Justification::centred);
}

void LfoRateComponent::drawNoteIcon(juce::Graphics& g, juce::Rectangle<float> area) {
    // Simple quarter note: small head + stem, no flag
    float cx = area.getCentreX();
    float cy = area.getCentreY();
    float s = juce::jmin(area.getWidth(), area.getHeight()) * 0.42f;

    float headRx = s * 0.32f;
    float headRy = s * 0.24f;
    float headCy = cy + s * 0.4f;

    drawStemmedNote(g, cx, headCy, headRx, headRy, s * 1.1f, 1.2f);
}

void LfoRateComponent::drawDottedNoteIcon(juce::Graphics& g, juce::Rectangle<float> area) {
    // Same-sized quarter note (drawn in full area) + augmentation dot
    drawNoteIcon(g, area);

    float cx = area.getCentreX();
    float cy = area.getCentreY();
    float s = juce::jmin(area.getWidth(), area.getHeight()) * 0.42f;
    float dotR = s * 0.13f;
    // Dot to the right and slightly above the head centre
    float dotX = cx + s * 0.55f;
    float dotY = cy + s * 0.35f;
    g.fillEllipse(dotX - dotR, dotY - dotR, dotR * 2.0f, dotR * 2.0f);
}

void LfoRateComponent::drawTripletNoteIcon(juce::Graphics& g, juce::Rectangle<float> area) {
    // Three beamed notes with "3" above the beam
    float s = juce::jmin(area.getWidth(), area.getHeight()) * 0.42f;
    float cy = area.getCentreY();

    float headRx = s * 0.22f;
    float headRy = s * 0.17f;
    float stemH = s * 0.8f;
    float stemW = 1.0f;

    // Three evenly spaced note positions
    float spacing = s * 0.55f;
    float baseCx = area.getCentreX();
    float headCy = cy + s * 0.45f;
    float xs[3] = { baseCx - spacing, baseCx, baseCx + spacing };

    // Draw three stemmed notes
    for (int i = 0; i < 3; ++i)
        drawStemmedNote(g, xs[i], headCy, headRx, headRy, stemH, stemW);

    // Beam connecting the tops of the stems
    float beamY = headCy - stemH;
    float beamH = 1.5f;
    float beamLeft = xs[0] + headRx - stemW * 0.5f;
    float beamRight = xs[2] + headRx + stemW * 0.5f;
    g.fillRect(beamLeft, beamY, beamRight - beamLeft, beamH);

    // "3" above the beam
    float labelY = beamY - 8.0f;
    g.setFont(juce::Font(7.5f, juce::Font::bold));
    g.drawText("3", juce::Rectangle<float>(beamLeft, labelY, beamRight - beamLeft, 8.0f),
               juce::Justification::centred);
}

void LfoRateComponent::drawModeIcon(juce::Graphics& g, juce::Rectangle<float> area) const {
    switch (rateMode) {
        case LfoRateMode::Seconds:       drawHzIcon(g, area);          break;
        case LfoRateMode::Tempo:         drawNoteIcon(g, area);        break;
        case LfoRateMode::TempoDotted:   drawDottedNoteIcon(g, area);  break;
        case LfoRateMode::TempoTriplets: drawTripletNoteIcon(g, area); break;
    }
}

double LfoRateComponent::getCurrentBpm() const {
    return audioProcessor.currentBpm.load(std::memory_order_relaxed);
}

double LfoRateComponent::getEffectiveRateHz() const {
    if (rateMode == LfoRateMode::Seconds) {
        if (audioProcessor.lfoRate[lfoIndex] != nullptr)
            return (double)audioProcessor.lfoRate[lfoIndex]->getValueUnnormalised();
        return 1.0;
    }
    // Tempo modes
    auto& divisions = getTempoDivisions();
    int idx = juce::jlimit(0, (int)divisions.size() - 1, tempoDivisionIndex);
    return divisions[idx].toHz(getCurrentBpm(), rateMode);
}

juce::String LfoRateComponent::getDisplayText() const {
    if (rateMode == LfoRateMode::Seconds) {
        float hz = 1.0f;
        if (audioProcessor.lfoRate[lfoIndex] != nullptr)
            hz = audioProcessor.lfoRate[lfoIndex]->getValueUnnormalised();
        if (hz >= 10.0f)
            return juce::String(hz, 1) + " Hz";
        return juce::String(hz, 2) + " Hz";
    }
    // Tempo modes — show the division name
    auto& divisions = getTempoDivisions();
    int idx = juce::jlimit(0, (int)divisions.size() - 1, tempoDivisionIndex);
    return divisions[idx].toString();
}

juce::String LfoRateComponent::getLabelText() const {
    return rateMode == LfoRateMode::Seconds ? "FREQUENCY" : "TEMPO";
}

// ============================================================================
// State
// ============================================================================

void LfoRateComponent::setLfoIndex(int index) {
    lfoIndex = juce::jlimit(0, NUM_LFOS - 1, index);
    repaint();
}

void LfoRateComponent::setRateMode(LfoRateMode mode) {
    if (rateMode == mode) return;
    rateMode = mode;
    audioProcessor.setLfoRateMode(lfoIndex, mode);
    // When switching to a tempo mode, push the computed Hz to the processor
    if (rateMode != LfoRateMode::Seconds) {
        float hz = (float)getEffectiveRateHz();
        pushRateToProcessor(hz);
    }
    // Repaint self and parent so the label text updates immediately
    repaint();
    if (auto* p = getParentComponent()) p->repaint();
}

void LfoRateComponent::setTempoDivisionIndex(int index) {
    auto& divisions = getTempoDivisions();
    tempoDivisionIndex = juce::jlimit(0, (int)divisions.size() - 1, index);
    audioProcessor.setLfoTempoDivision(lfoIndex, tempoDivisionIndex);
    if (rateMode != LfoRateMode::Seconds) {
        float hz = (float)getEffectiveRateHz();
        pushRateToProcessor(hz);
    }
    repaint();
}

void LfoRateComponent::syncFromProcessor() {
    rateMode = audioProcessor.getLfoRateMode(lfoIndex);
    tempoDivisionIndex = audioProcessor.getLfoTempoDivision(lfoIndex);
    repaint();
}

void LfoRateComponent::pushRateToProcessor(float hz) {
    if (audioProcessor.lfoRate[lfoIndex] != nullptr)
        audioProcessor.lfoRate[lfoIndex]->setUnnormalisedValueNotifyingHost(juce::jmax(0.0001f, hz));
}

// ============================================================================
// Paint — Vital-inspired dark rounded bar with value + mode icon
// ============================================================================

void LfoRateComponent::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();
    bool hovering = isMouseOver(true);
    float r = 4.0f;

    // Background
    g.setColour(Colours::veryDark);
    g.fillRoundedRectangle(bounds, r);

    // Label strip at bottom — lighter shade
    if (!labelArea.isEmpty()) {
        auto labelBounds = labelArea.toFloat();
        // Clip to bottom rounded corners
        juce::Path labelPath;
        labelPath.addRoundedRectangle(bounds.getX(), labelBounds.getY(),
                                       bounds.getWidth(), labelBounds.getHeight(),
                                       r, r, false, false, true, true);
        g.saveState();
        g.reduceClipRegion(labelPath);
        g.setColour(juce::Colour(0xFF222222));
        g.fillRect(labelBounds);
        g.restoreState();

        // Horizontal divider between value and label
        g.setColour(juce::Colours::white.withAlpha(0.10f));
        g.drawHorizontalLine((int)labelBounds.getY(), bounds.getX() + 1.0f, bounds.getRight() - 1.0f);

        // Label text
        g.setColour(juce::Colours::white.withAlpha(0.55f));
        g.setFont(juce::Font(9.0f, juce::Font::bold));
        g.drawText(getLabelText(), labelBounds, juce::Justification::centred);
    }

    // Border
    g.setColour(juce::Colours::white.withAlpha(hovering ? 0.12f : 0.06f));
    g.drawRoundedRectangle(bounds.reduced(0.5f), r, 1.0f);

    // Value text (centre of value area)
    g.setColour(juce::Colours::white.withAlpha(0.9f));
    g.setFont(juce::Font(14.0f, juce::Font::bold));
    auto textBounds = valueArea.toFloat();
    g.drawText(getDisplayText(), textBounds, juce::Justification::centred);

    // Mode icon button (right portion) — separator + icon
    auto iconBounds = modeButtonArea.toFloat();

    // Separator line — brighter
    g.setColour(juce::Colours::white.withAlpha(0.15f));
    g.drawVerticalLine((int)iconBounds.getX(), iconBounds.getY() + 3.0f, iconBounds.getBottom() - 3.0f);

    // Mode icon — brighter colors
    bool iconHover = modeButtonArea.contains(getMouseXYRelative());
    g.setColour(iconHover ? juce::Colours::white.withAlpha(0.7f) : Colours::grey);
    drawModeIcon(g, iconBounds.reduced(2.0f, 2.0f));
}

// ============================================================================
// Layout
// ============================================================================

void LfoRateComponent::resized() {
    auto bounds = getLocalBounds();
    static constexpr int labelH = 14;
    labelArea = bounds.removeFromBottom(labelH);
    int iconW = 26;
    modeButtonArea = bounds.removeFromRight(iconW);
    valueArea = bounds;
}

// ============================================================================
// Mouse interaction
// ============================================================================

void LfoRateComponent::mouseDown(const juce::MouseEvent& e) {
    // Right-click or mode icon click → show mode popup
    if (e.mods.isPopupMenu() || modeButtonArea.contains(e.getPosition())) {
        showModePopup();
        return;
    }

    dragStartY = e.getPosition().y;
    isDragging = true;

    if (rateMode == LfoRateMode::Seconds) {
        if (audioProcessor.lfoRate[lfoIndex] != nullptr)
            dragStartValue = audioProcessor.lfoRate[lfoIndex]->getValueUnnormalised();
        else
            dragStartValue = 1.0f;
    } else {
        dragStartDivision = tempoDivisionIndex;
    }
}

void LfoRateComponent::mouseDrag(const juce::MouseEvent& e) {
    if (!isDragging) return;

    if (rateMode == LfoRateMode::Seconds) {
        float dy = (float)(dragStartY - e.getPosition().y);
        // Logarithmic drag: work in log space for consistent feel across 0.01–100 Hz
        float logNewVal = std::log(dragStartValue) + dy * 0.02f;
        float newVal = juce::jlimit(0.01f, 100.0f, std::exp(logNewVal));
        pushRateToProcessor(newVal);
        repaint();
    } else {
        // Tempo modes: drag up = higher index (faster division), drag down = lower
        float dy = (float)(dragStartY - e.getPosition().y);
        int steps = (int)(dy / 12.0f); // 12 px per division step
        auto& divisions = getTempoDivisions();
        int newIdx = juce::jlimit(0, (int)divisions.size() - 1, dragStartDivision + steps);
        if (newIdx != tempoDivisionIndex)
            setTempoDivisionIndex(newIdx);
    }
}

void LfoRateComponent::mouseUp(const juce::MouseEvent&) {
    isDragging = false;
}

void LfoRateComponent::mouseDoubleClick(const juce::MouseEvent& e) {
    if (modeButtonArea.contains(e.getPosition())) return;

    if (rateMode == LfoRateMode::Seconds) {
        showInlineEditor();
    }
}

// ============================================================================
// Mode popup
// ============================================================================

void LfoRateComponent::showModePopup() {
    juce::PopupMenu menu;
    menu.addItem(1, "Seconds (Hz)",    true, rateMode == LfoRateMode::Seconds);
    menu.addItem(2, "Tempo",           true, rateMode == LfoRateMode::Tempo);
    menu.addItem(3, "Tempo Dotted",    true, rateMode == LfoRateMode::TempoDotted);
    menu.addItem(4, "Tempo Triplets",  true, rateMode == LfoRateMode::TempoTriplets);

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
        [this](int result) {
            if (result == 0) return;
            switch (result) {
                case 1: setRateMode(LfoRateMode::Seconds);       break;
                case 2: setRateMode(LfoRateMode::Tempo);         break;
                case 3: setRateMode(LfoRateMode::TempoDotted);   break;
                case 4: setRateMode(LfoRateMode::TempoTriplets); break;
            }
        });
}

// ============================================================================
// Inline text editor for Hz mode
// ============================================================================

void LfoRateComponent::showInlineEditor() {
    float hz = 1.0f;
    if (audioProcessor.lfoRate[lfoIndex] != nullptr)
        hz = audioProcessor.lfoRate[lfoIndex]->getValueUnnormalised();

    auto commitFn = [this](const juce::String& text) {
        float val = text.getFloatValue();
        val = juce::jlimit(0.01f, 100.0f, val);
        inlineEditor.reset();
        pushRateToProcessor(val);
        repaint();
    };
    auto cancelFn = [this]() {
        inlineEditor.reset();
        repaint();
    };
    inlineEditor = InlineEditorHelper::create(
        juce::String(hz, 2), valueArea, { commitFn, cancelFn },
        Colours::veryDark, juce::Colours::white,
        juce::Colours::white.withAlpha(0.3f), 12.0f);
    inlineEditor->setJustification(juce::Justification::centredLeft);
    addAndMakeVisible(inlineEditor.get());
    inlineEditor->grabKeyboardFocus();
}

void LfoRateComponent::commitInlineEditor() {
    auto editor = std::move(inlineEditor);
    if (!editor) return;
    float val = editor->getText().getFloatValue();
    val = juce::jlimit(0.01f, 100.0f, val);
    pushRateToProcessor(val);
    repaint();
}
