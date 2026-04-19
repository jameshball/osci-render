#pragma once

#if OSCI_PREMIUM

#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "../parser/fractal/FractalParser.h"
#include "KnobContainerComponent.h"
#include "ScrollFadeViewport.h"

class OscirenderAudioProcessorEditor;

class FractalComponent : public juce::Component, private juce::Timer {
public:
    FractalComponent(OscirenderAudioProcessor&, OscirenderAudioProcessorEditor&);
    ~FractalComponent() override;

    void resized() override;
    void paint(juce::Graphics&) override;

    // Called when the active file changes
    void setParser(std::shared_ptr<FractalParser> parser, int fileIndex);

private:
    void timerCallback() override;
    void updateFileFromUI();

    OscirenderAudioProcessor& audioProcessor;
    OscirenderAudioProcessorEditor& pluginEditor;
    std::shared_ptr<FractalParser> currentParser;
    int currentFileIndex = -1;

    // UI Components
    juce::GroupComponent group{{}, "L-System Fractal"};

    juce::Label axiomLabel{{}, "Axiom"};
    juce::TextEditor axiomEditor;

    juce::Label angleLabel{{}, juce::String(juce::CharPointer_UTF8("Angle (\xC2\xB0)"))};
    juce::TextEditor angleEditor;
    KnobContainerComponent depthKnob{"DEPTH"};

    // Rules section — scrollable panel with dark background
    struct RulesPanel : public juce::Component {
        void paint(juce::Graphics& g) override;
    };

    struct RuleRow : public juce::Component {
        juce::TextEditor variableEditor;
        juce::Label arrowLabel{{}, juce::String (juce::CharPointer_UTF8 ("\xE2\x86\x92"))};
        juce::TextEditor replacementEditor;
        juce::TextButton removeButton{"X"};

        RuleRow() {
            addAndMakeVisible(variableEditor);
            addAndMakeVisible(arrowLabel);
            addAndMakeVisible(replacementEditor);
            addAndMakeVisible(removeButton);

            variableEditor.setJustification(juce::Justification::centred);
            variableEditor.setInputRestrictions(1);
            arrowLabel.setJustificationType(juce::Justification::centred);
        }

        void resized() override {
            auto area = getLocalBounds();
            variableEditor.setBounds(area.removeFromLeft(30));
            area.removeFromLeft(2);
            arrowLabel.setBounds(area.removeFromLeft(20));
            area.removeFromLeft(2);
            removeButton.setBounds(area.removeFromRight(24));
            area.removeFromRight(2);
            replacementEditor.setBounds(area);
        }
    };

    juce::Label rulesLabel{{}, "Rules"};
    ScrollFadeViewport rulesViewport;
    RulesPanel rulesContent;
    juce::OwnedArray<RuleRow> ruleRows;
    juce::TextButton addRuleButton{"+ Add Rule"};

    void addRuleRow(const juce::String& variable = "", const juce::String& replacement = "");
    void removeRuleRow(int index);
    void rebuildRulesFromParser();
    void layoutRulesContent();

    bool updatingFromParser = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FractalComponent)
};

#endif // OSCI_PREMIUM
