#include "EffectsComponent.h"
#include "audio/BitCrushEffect.h"
#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "LookAndFeel.h"

EffectsComponent::EffectsComponent(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& editor) 
    : juce::GroupComponent("", "Audio Effects"), 
      audioProcessor(p), 
      itemData(p, editor),
      listBoxModel(listBox, itemData)
{
	setText("Audio Effects");

#if !defined(MIDI_ALWAYS_ENABLED) || (MIDI_ALWAYS_ENABLED == 0)
    addAndMakeVisible(frequency);
#endif

    frequency.slider.setSkewFactorFromMidPoint(500.0);
    frequency.slider.setTextValueSuffix("Hz");
    frequency.slider.setValue(audioProcessor.frequencyEffect->getValue(), juce::dontSendNotification);

    frequency.slider.onValueChange = [this] {
        audioProcessor.frequencyEffect->parameters[0]->setUnnormalisedValueNotifyingHost(frequency.slider.getValue());
    };

    /*addBtn.setButtonText("Add Item...");
    addBtn.onClick = [this]()
    {
        itemData.data.push_back(juce::String("Item " + juce::String(1 + itemData.getNumItems())));
        listBox.updateContent();
    };
    addAndMakeVisible(addBtn);*/

    addAndMakeVisible(randomiseButton);

	randomiseButton.setTooltip("Randomise all effect parameter values, randomise which effects are enabled, and randomise their order.");

	randomiseButton.onClick = [this] {
		itemData.randomise();
		listBox.updateContent();
	};

    {
        juce::MessageManagerLock lock;
        audioProcessor.broadcaster.addChangeListener(this);
    }

    listBox.setModel(&listBoxModel);
    addAndMakeVisible(listBox);

    // Add preset buttons and set up callbacks
    addAndMakeVisible(loadPresetButton);
    addAndMakeVisible(savePresetButton);
    loadPresetButton.onClick = [this] { loadPresetClicked(); };
    savePresetButton.onClick = [this] { savePresetClicked(); };

    setTextLabelPosition(juce::Justification::centred);
}

EffectsComponent::~EffectsComponent() {
    juce::MessageManagerLock lock;
    audioProcessor.broadcaster.removeChangeListener(this);
}

void EffectsComponent::resized() {
    auto area = getLocalBounds();
    auto titleBar = area.removeFromTop(30);
    titleBar.removeFromLeft(100);

    // Position preset buttons near the top
    // auto topArea = area.removeFromTop(25);
    // Position the preset buttons below the title bar on the right
    savePresetButton.setBounds(titleBar.removeFromRight(50));
    loadPresetButton.setBounds(titleBar.removeFromRight(50 + 5)); // Add 5px spacing
    
	randomiseButton.setBounds(titleBar.removeFromLeft(20));
    area = area.reduced(20);
    frequency.setBounds(area.removeFromTop(30));

    area.removeFromTop(6);
    listBox.setBounds(area);
}

void EffectsComponent::changeListenerCallback(juce::ChangeBroadcaster* source) {
    itemData.resetData();
    listBox.updateContent();
}

// Add implementations for button handlers
void EffectsComponent::loadPresetClicked()
{
    presetChooser = std::make_unique<juce::FileChooser>("Load OsciPreset",
                                                      juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
                                                      "*.ospreset");
    auto chooserFlags = juce::FileBrowserComponent::openMode |
                       juce::FileBrowserComponent::canSelectFiles;

    presetChooser->launchAsync(chooserFlags, [this](const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        if (file != juce::File{})
            audioProcessor.loadPreset(file);
    });
}

void EffectsComponent::savePresetClicked()
{
    presetChooser = std::make_unique<juce::FileChooser>("Save OsciPreset",
                                                     juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
                                                     "*.ospreset");
    auto chooserFlags = juce::FileBrowserComponent::saveMode |
                       juce::FileBrowserComponent::canSelectFiles;

    presetChooser->launchAsync(chooserFlags, [this](const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        if (file != juce::File{})
        {
            // Ensure the file has the correct extension
            if (!file.hasFileExtension(".ospreset"))
                file = file.withFileExtension(".ospreset");
                
            audioProcessor.savePreset(file);
        }
    });
}
