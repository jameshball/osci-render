#include "FractalComponent.h"
#include "../PluginEditor.h"
#include "../LookAndFeel.h"

// ---- RulesPanel paint: darker rounded background ----

void FractalComponent::RulesPanel::paint(juce::Graphics& g) {
    g.setColour(Colours::darker().darker(0.3f));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), OscirenderLookAndFeel::RECT_RADIUS);
}

// ---- FractalComponent ----

FractalComponent::FractalComponent(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& editor)
    : audioProcessor(p), pluginEditor(editor) {
    addAndMakeVisible(group);

    addAndMakeVisible(axiomLabel);
    addAndMakeVisible(axiomEditor);
    axiomEditor.setMultiLine(false);
    axiomEditor.onTextChange = [this]() {
        if (!updatingFromParser) updateFileFromUI();
    };

    addAndMakeVisible(angleLabel);
    addAndMakeVisible(angleEditor);
    angleEditor.setMultiLine(false);
    angleEditor.setInputRestrictions(10, "0123456789.-");
    angleEditor.onTextChange = [this]() {
        if (!updatingFromParser) updateFileFromUI();
    };

    addAndMakeVisible(depthSlider);

    addAndMakeVisible(rulesLabel);
    addAndMakeVisible(rulesViewport);
    rulesViewport.setViewedComponent(&rulesContent, false);
    rulesViewport.setScrollBarsShown(true, false);

    rulesContent.addAndMakeVisible(addRuleButton);
    addRuleButton.onClick = [this]() {
        addRuleRow("", "");
        updateFileFromUI();
        layoutRulesContent();
    };
}

FractalComponent::~FractalComponent() {
    stopTimer();
}

void FractalComponent::setParser(std::shared_ptr<FractalParser> parser, int fileIndex) {
    currentParser = parser;
    currentFileIndex = fileIndex;
    rebuildRulesFromParser();
}

void FractalComponent::rebuildRulesFromParser() {
    updatingFromParser = true;

    if (currentParser != nullptr) {
        axiomEditor.setText(currentParser->getAxiom(), false);
        angleEditor.setText(juce::String(currentParser->getBaseAngle()), false);

        ruleRows.clear();
        for (const auto& rule : currentParser->getRules()) {
            addRuleRow(rule.variable, rule.replacement);
        }
    } else {
        axiomEditor.clear();
        angleEditor.clear();
        ruleRows.clear();
    }

    updatingFromParser = false;
    layoutRulesContent();
}

void FractalComponent::addRuleRow(const juce::String& variable, const juce::String& replacement) {
    auto* row = ruleRows.add(new RuleRow());
    rulesContent.addAndMakeVisible(row);

    row->variableEditor.setText(variable, false);
    row->replacementEditor.setText(replacement, false);

    row->variableEditor.onTextChange = [this]() {
        if (!updatingFromParser) updateFileFromUI();
    };
    row->replacementEditor.onTextChange = [this]() {
        if (!updatingFromParser) updateFileFromUI();
    };

    int rowIndex = ruleRows.size() - 1;
    row->removeButton.onClick = [this, rowIndex]() {
        removeRuleRow(rowIndex);
    };
}

void FractalComponent::removeRuleRow(int index) {
    if (index >= 0 && index < ruleRows.size()) {
        ruleRows.remove(index);
        for (int i = 0; i < ruleRows.size(); ++i) {
            int idx = i;
            ruleRows[i]->removeButton.onClick = [this, idx]() {
                removeRuleRow(idx);
            };
        }
        updateFileFromUI();
        layoutRulesContent();
    }
}

void FractalComponent::layoutRulesContent() {
    const int rowHeight = 28;
    const int spacing = 4;
    const int pad = 8;

    int totalHeight = pad;
    for (int i = 0; i < ruleRows.size(); ++i) {
        totalHeight += rowHeight + spacing;
    }
    totalHeight += rowHeight + pad; // addRuleButton

    int contentWidth = rulesViewport.getWidth() - rulesViewport.getScrollBarThickness();
    if (contentWidth < 10) contentWidth = rulesViewport.getWidth();

    rulesContent.setSize(contentWidth, totalHeight);

    int y = pad;
    for (auto* row : ruleRows) {
        row->setBounds(pad, y, contentWidth - 2 * pad, rowHeight);
        y += rowHeight + spacing;
    }
    addRuleButton.setBounds(pad, y, 120, rowHeight);
}

void FractalComponent::updateFileFromUI() {
    if (currentParser == nullptr || currentFileIndex < 0)
        return;

    auto* obj = new juce::DynamicObject();
    obj->setProperty("axiom", axiomEditor.getText());
    obj->setProperty("angle", angleEditor.getText().getDoubleValue());

    juce::Array<juce::var> rulesArray;
    for (auto* row : ruleRows) {
        auto varText = row->variableEditor.getText().trim();
        auto repText = row->replacementEditor.getText().trim();
        if (varText.isNotEmpty()) {
            auto* ruleObj = new juce::DynamicObject();
            ruleObj->setProperty("variable", varText);
            ruleObj->setProperty("replacement", repText);
            rulesArray.add(juce::var(ruleObj));
        }
    }
    obj->setProperty("rules", rulesArray);

    juce::String json = juce::JSON::toString(juce::var(obj), false);

    auto block = std::make_shared<juce::MemoryBlock>();
    block->append(json.toRawUTF8(), json.getNumBytesAsUTF8());

    juce::SpinLock::ScopedLockType lock1(audioProcessor.parsersLock);
    juce::SpinLock::ScopedLockType lock2(audioProcessor.effectsLock);
    audioProcessor.updateFileBlock(currentFileIndex, block);
}

void FractalComponent::timerCallback() {}

void FractalComponent::paint(juce::Graphics& g) {}

void FractalComponent::resized() {
    auto area = getLocalBounds().withTrimmedTop(20).reduced(20);
    group.setBounds(getLocalBounds());

    const int rowHeight = 20;
    const int sliderHeight = 30;
    const int labelWidth = 125; // 5px pad + 120px label inside EffectComponent

    // Axiom row — indented to align with slider track
    auto axiomRow = area.removeFromTop(rowHeight);
    axiomLabel.setBounds(axiomRow.removeFromLeft(labelWidth));
    axiomEditor.setBounds(axiomRow);
    area.removeFromTop(5);

    // Base angle row — indented to align with slider track
    auto angleRow = area.removeFromTop(rowHeight);
    angleLabel.setBounds(angleRow.removeFromLeft(labelWidth));
    angleEditor.setBounds(angleRow);
    area.removeFromTop(5);

    // Effect sliders (same height as FrameSettingsComponent: 30px each, no extra spacing)
    depthSlider.setBounds(area.removeFromTop(sliderHeight).withLeft(area.getX() - 5));
    area.removeFromTop(5);

    // Rules label
    auto rulesRow = area.removeFromTop(rowHeight);
    rulesLabel.setBounds(rulesRow);
    area.removeFromTop(2);

    // Rules viewport fills remaining space
    rulesViewport.setBounds(area);
    rulesViewport.setFadeVisible(true);

    layoutRulesContent();
}
