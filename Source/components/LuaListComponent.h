#pragma once
#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "../audio/Effect.h"
#include "EffectComponent.h"

class LuaListComponent : public juce::Component
{
public:
    LuaListComponent(OscirenderAudioProcessor& p, Effect& effect);
    ~LuaListComponent();

    void resized() override;

protected:
    std::shared_ptr<EffectComponent> effectComponent;
private:
    
    class RenameEffectComponent : public juce::Component {
    public:
        RenameEffectComponent(EffectComponent* parent) {
            addAndMakeVisible(staticName);
            addAndMakeVisible(aliasLabel);
            addAndMakeVisible(alias);
            addAndMakeVisible(variableLabel);
            addAndMakeVisible(variableName);
            
            staticName.setJustificationType(juce::Justification::centred);
            aliasLabel.setJustificationType(juce::Justification::right);
            variableLabel.setJustificationType(juce::Justification::right);
            
            aliasLabel.setText("Alias", juce::dontSendNotification);
            variableLabel.setText("Variable Name", juce::dontSendNotification);
            
            juce::Font font = juce::Font(13.0f);
            staticName.setFont(font);
            aliasLabel.setFont(font);
            variableLabel.setFont(font);
            
            staticName.setText(parent->effect.getName(), juce::dontSendNotification);
            alias.setText(parent->effect.parameters[parent->index]->alias, juce::dontSendNotification);
            // variableName.setText(parent->effect.parameters[parent->index]->, juce::dontSendNotification);
            
            alias.onTextChange = [parent, this] {
                parent->effect.parameters[parent->index]->alias = alias.getText();
                parent->label.setText(alias.getText(), juce::dontSendNotification);
            };
        }
        
        void resized() override {
            auto bounds = getLocalBounds();
            staticName.setBounds(bounds.removeFromTop(30));
            auto aliasRow = bounds.removeFromTop(30);
            aliasLabel.setBounds(aliasRow.removeFromLeft(90));
            alias.setBounds(aliasRow);
            bounds.removeFromTop(10);
            auto variableRow = bounds.removeFromTop(30);
            variableLabel.setBounds(variableRow.removeFromLeft(90));
            variableName.setBounds(variableRow);
        }
        
    private:
        juce::Label staticName;
        juce::Label aliasLabel;
        juce::TextEditor alias;
        juce::Label variableLabel;
        juce::TextEditor variableName;
    };
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LuaListComponent)
};

class LuaListBoxModel : public juce::ListBoxModel
{
public:
    LuaListBoxModel(juce::ListBox& lb, OscirenderAudioProcessor& p) : listBox(lb), audioProcessor(p) {}

    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    juce::Component* refreshComponentForRow(int sliderNum, bool isRowSelected, juce::Component *existingComponentToUpdate) override;

private:
    int numSliders = 26;
    juce::ListBox& listBox;
    OscirenderAudioProcessor& audioProcessor;
};
