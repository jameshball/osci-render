#include "ModulationRateComponent.h"
#include "../../LookAndFeel.h"
#include "InlineEditorHelper.h"
#include "../DarkBarPainter.h"

ModulationRateComponent::ModulationRateComponent(const ModulationRateConfig& cfg, int index)
    : config(cfg), sourceIndex(index)
{
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    setRepaintsOnMouseActivity(true);
}

ModulationRateComponent::~ModulationRateComponent() {}

// ============================================================================
// Icon drawing helpers
// ============================================================================

static void drawStemmedNote(juce::Graphics& g, float headCx, float headCy,
                            float headRx, float headRy, float stemHeight, float stemW) {
    g.fillEllipse(headCx - headRx, headCy - headRy, headRx * 2.0f, headRy * 2.0f);
    float stemX = headCx + headRx - stemW * 0.5f - 0.3f;
    g.fillRect(stemX, headCy - stemHeight, stemW, stemHeight);
}

void ModulationRateComponent::drawHzIcon(juce::Graphics& g, juce::Rectangle<float> area) {
    g.setFont(juce::Font(11.0f, juce::Font::bold));
    g.drawText("Hz", area, juce::Justification::centred);
}

void ModulationRateComponent::drawNoteIcon(juce::Graphics& g, juce::Rectangle<float> area) {
    float cx = area.getCentreX();
    float cy = area.getCentreY();
    float s = juce::jmin(area.getWidth(), area.getHeight()) * 0.42f;
    float headRx = s * 0.32f;
    float headRy = s * 0.24f;
    float headCy = cy + s * 0.4f;
    drawStemmedNote(g, cx, headCy, headRx, headRy, s * 1.1f, 1.2f);
}

void ModulationRateComponent::drawDottedNoteIcon(juce::Graphics& g, juce::Rectangle<float> area) {
    drawNoteIcon(g, area);
    float cx = area.getCentreX();
    float cy = area.getCentreY();
    float s = juce::jmin(area.getWidth(), area.getHeight()) * 0.42f;
    float dotR = s * 0.13f;
    float dotX = cx + s * 0.55f;
    float dotY = cy + s * 0.35f;
    g.fillEllipse(dotX - dotR, dotY - dotR, dotR * 2.0f, dotR * 2.0f);
}

void ModulationRateComponent::drawTripletNoteIcon(juce::Graphics& g, juce::Rectangle<float> area) {
    float s = juce::jmin(area.getWidth(), area.getHeight()) * 0.42f;
    float cy = area.getCentreY();
    float headRx = s * 0.22f;
    float headRy = s * 0.17f;
    float stemH = s * 0.8f;
    float stemW = 1.0f;
    float spacing = s * 0.55f;
    float baseCx = area.getCentreX();
    float headCy = cy + s * 0.45f;
    float xs[3] = { baseCx - spacing, baseCx, baseCx + spacing };
    for (int i = 0; i < 3; ++i)
        drawStemmedNote(g, xs[i], headCy, headRx, headRy, stemH, stemW);
    float beamY = headCy - stemH;
    float beamH = 1.5f;
    float beamLeft = xs[0] + headRx - stemW * 0.5f;
    float beamRight = xs[2] + headRx + stemW * 0.5f;
    g.fillRect(beamLeft, beamY, beamRight - beamLeft, beamH);
    float labelY = beamY - 8.0f;
    g.setFont(juce::Font(7.5f, juce::Font::bold));
    g.drawText("3", juce::Rectangle<float>(beamLeft, labelY, beamRight - beamLeft, 8.0f),
               juce::Justification::centred);
}

void ModulationRateComponent::drawModeIcon(juce::Graphics& g, juce::Rectangle<float> area) const {
    switch (rateMode) {
        case LfoRateMode::Seconds:       drawHzIcon(g, area);          break;
        case LfoRateMode::Tempo:         drawNoteIcon(g, area);        break;
        case LfoRateMode::TempoDotted:   drawDottedNoteIcon(g, area);  break;
        case LfoRateMode::TempoTriplets: drawTripletNoteIcon(g, area); break;
    }
}

double ModulationRateComponent::getCurrentBpm() const {
    return config.getCurrentBpm();
}

double ModulationRateComponent::getEffectiveRateHz() const {
    if (rateMode == LfoRateMode::Seconds) {
        auto* param = config.getRateParam(sourceIndex);
        if (param != nullptr)
            return (double)param->getValueUnnormalised();
        return 1.0;
    }
    auto& divisions = getTempoDivisions();
    int idx = juce::jlimit(0, (int)divisions.size() - 1, tempoDivisionIndex);
    return divisions[idx].toHz(getCurrentBpm(), rateMode);
}

juce::String ModulationRateComponent::getDisplayText() const {
    if (rateMode == LfoRateMode::Seconds) {
        float hz = 1.0f;
        auto* param = config.getRateParam(sourceIndex);
        if (param != nullptr)
            hz = param->getValueUnnormalised();
        if (hz >= 10.0f)
            return juce::String(hz, 1) + " Hz";
        return juce::String(hz, 2) + " Hz";
    }
    auto& divisions = getTempoDivisions();
    int idx = juce::jlimit(0, (int)divisions.size() - 1, tempoDivisionIndex);
    return divisions[idx].toString();
}

juce::String ModulationRateComponent::getLabelText() const {
    return rateMode == LfoRateMode::Seconds ? "FREQUENCY" : "TEMPO";
}

// ============================================================================
// State
// ============================================================================

void ModulationRateComponent::setSourceIndex(int index) {
    sourceIndex = juce::jlimit(0, config.maxIndex - 1, index);
    repaint();
}

void ModulationRateComponent::setRateMode(LfoRateMode mode) {
    if (rateMode == mode) return;
    rateMode = mode;
    config.setRateMode(sourceIndex, mode);
    if (rateMode != LfoRateMode::Seconds) {
        float hz = (float)getEffectiveRateHz();
        pushRateToProcessor(hz);
    }
    repaint();
    if (auto* p = getParentComponent()) p->repaint();
}

void ModulationRateComponent::setTempoDivisionIndex(int index) {
    auto& divisions = getTempoDivisions();
    tempoDivisionIndex = juce::jlimit(0, (int)divisions.size() - 1, index);
    config.setTempoDivision(sourceIndex, tempoDivisionIndex);
    if (rateMode != LfoRateMode::Seconds) {
        float hz = (float)getEffectiveRateHz();
        pushRateToProcessor(hz);
    }
    repaint();
}

void ModulationRateComponent::syncFromProcessor() {
    rateMode = config.getRateMode(sourceIndex);
    tempoDivisionIndex = config.getTempoDivision(sourceIndex);
    repaint();
}

void ModulationRateComponent::pushRateToProcessor(float hz) {
    auto* param = config.getRateParam(sourceIndex);
    if (param != nullptr)
        param->setUnnormalisedValueNotifyingHost(juce::jmax(0.0001f, hz));
}

// ============================================================================
// Paint
// ============================================================================

void ModulationRateComponent::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();
    bool hovering = isMouseOver(true);

    DarkBarPainter::paintBackground(g, bounds, labelArea, getLabelText(), hovering);

    g.setColour(juce::Colours::white.withAlpha(0.9f));
    g.setFont(juce::Font(14.0f, juce::Font::bold));
    auto textBounds = valueArea.toFloat();
    g.drawText(getDisplayText(), textBounds, juce::Justification::centred);

    auto iconBounds = modeButtonArea.toFloat();
    g.setColour(juce::Colours::white.withAlpha(0.15f));
    g.drawVerticalLine((int)iconBounds.getX(), iconBounds.getY() + 3.0f, iconBounds.getBottom() - 3.0f);

    bool iconHover = modeButtonArea.contains(getMouseXYRelative());
    g.setColour(iconHover ? juce::Colours::white.withAlpha(0.7f) : Colours::grey);
    drawModeIcon(g, iconBounds.reduced(2.0f, 2.0f));
}

// ============================================================================
// Layout
// ============================================================================

void ModulationRateComponent::resized() {
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

void ModulationRateComponent::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu() || modeButtonArea.contains(e.getPosition())) {
        showModePopup();
        return;
    }

    dragStartY = e.getPosition().y;
    isDragging = true;

    if (rateMode == LfoRateMode::Seconds) {
        auto* param = config.getRateParam(sourceIndex);
        if (param != nullptr)
            dragStartValue = param->getValueUnnormalised();
        else
            dragStartValue = 1.0f;
    } else {
        dragStartDivision = tempoDivisionIndex;
    }
}

void ModulationRateComponent::mouseDrag(const juce::MouseEvent& e) {
    if (!isDragging) return;

    if (rateMode == LfoRateMode::Seconds) {
        float dy = (float)(dragStartY - e.getPosition().y);
        float logNewVal = std::log(dragStartValue) + dy * 0.02f;
        float newVal = juce::jlimit(0.01f, 100.0f, std::exp(logNewVal));
        pushRateToProcessor(newVal);
        repaint();
    } else {
        float dy = (float)(dragStartY - e.getPosition().y);
        int steps = (int)(dy / 12.0f);
        auto& divisions = getTempoDivisions();
        int newIdx = juce::jlimit(0, (int)divisions.size() - 1, dragStartDivision + steps);
        if (newIdx != tempoDivisionIndex)
            setTempoDivisionIndex(newIdx);
    }

    valuePopup.show(*this, getDisplayText());
}

void ModulationRateComponent::mouseUp(const juce::MouseEvent&) {
    isDragging = false;
    valuePopup.hide();
}

void ModulationRateComponent::mouseDoubleClick(const juce::MouseEvent& e) {
    if (modeButtonArea.contains(e.getPosition())) return;

    if (rateMode == LfoRateMode::Seconds) {
        showInlineEditor();
    }
}

// ============================================================================
// Mode popup
// ============================================================================

void ModulationRateComponent::showModePopup() {
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

void ModulationRateComponent::showInlineEditor() {
    float hz = 1.0f;
    auto* param = config.getRateParam(sourceIndex);
    if (param != nullptr)
        hz = param->getValueUnnormalised();

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

void ModulationRateComponent::commitInlineEditor() {
    auto editor = std::move(inlineEditor);
    if (!editor) return;
    float val = editor->getText().getFloatValue();
    val = juce::jlimit(0.01f, 100.0f, val);
    pushRateToProcessor(val);
    repaint();
}

void ModulationRateComponent::mouseEnter(const juce::MouseEvent&) {
    valuePopup.show(*this, getDisplayText());
}

void ModulationRateComponent::mouseExit(const juce::MouseEvent&) {
    if (!isDragging)
        valuePopup.hide();
}
